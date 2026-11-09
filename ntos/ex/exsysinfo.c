/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the system will say about itself, and what may be told to it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

FAST_MUTEX ExpEnvironmentLock;

ERESOURCE ExpFirmwareTableResource;
LIST_ENTRY ExpFirmwareTableProviderListHead;

/* PRIVATE DATA ***************************************************************/

#define EXP_SYSINFO_TAG 'ofnI'

/* Where the machine's own description is kept once the firmware has been read */
#define EXP_SMBIOS_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\mssmbios\\Data"

#define EXP_SMBIOS_VALUE L"SMBiosData"

/*
 * The vendor a variable named without one belongs to, which is the firmware's
 * own, and the attributes such a variable is written with: kept across a
 * restart and readable both before and after the system has started.
 */
static const GUID ExpVendorGuid =
    { 0x8BE4DF61, 0x93CA, 0x11D2,
      { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };

#define EXP_FIRMWARE_ATTRIBUTES 0x00000007

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Checks a caller's output buffer once, so every class below can be written as
 * though the buffer were the kernel's own.
 */
static
NTSTATUS
ExpProbeSystemInfoBuffer(
    _In_opt_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_opt_ PULONG ReturnLength,
    _In_ ULONG Alignment,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (PreviousMode == KernelMode)
    {
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        if ((Buffer != NULL) && (BufferLength != 0))
        {
            ProbeForWrite(Buffer, BufferLength, Alignment);
        }

        if (ReturnLength != NULL)
        {
            ProbeForWriteUlong(ReturnLength);
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
VOID
ExpReportLength(
    _Out_opt_ PULONG ReturnLength,
    _In_ ULONG Length)
{
    if (ReturnLength == NULL)
    {
        return;
    }

    _SEH2_TRY
    {
        *ReturnLength = Length;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* The caller's own buffer went away; there is nothing left to report. */
    }
    _SEH2_END;
}

static
NTSTATUS
ExpQueryBasicInformation(
    _Out_ PSYSTEM_BASIC_INFORMATION Basic)
{
    RtlZeroMemory(Basic, sizeof(*Basic));

    Basic->Reserved = 0;
    Basic->TimerResolution = KeMaximumIncrement;
    Basic->PageSize = PAGE_SIZE;
    Basic->NumberOfPhysicalPages = MmNumberOfPhysicalPages;
    Basic->LowestPhysicalPageNumber = (ULONG)MmLowestPhysicalPage;
    Basic->HighestPhysicalPageNumber = (ULONG)MmHighestPhysicalPage;
    Basic->AllocationGranularity = MM_ALLOCATION_GRANULARITY;
    Basic->MinimumUserModeAddress = (ULONG_PTR)MM_LOWEST_USER_ADDRESS;
    Basic->MaximumUserModeAddress = (ULONG_PTR)MM_HIGHEST_USER_ADDRESS;
    Basic->ActiveProcessorsAffinityMask = KeActiveProcessors;
    Basic->NumberOfProcessors = (CCHAR)KeNumberProcessors;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpQueryProcessorInformation(
    _Out_ PSYSTEM_PROCESSOR_INFORMATION Processor)
{
    RtlZeroMemory(Processor, sizeof(*Processor));

    Processor->MaximumProcessors = 0;
    Processor->ProcessorArchitecture = (USHORT)KeProcessorArchitecture;
    Processor->ProcessorLevel = (USHORT)KeProcessorLevel;
    Processor->ProcessorRevision = (USHORT)KeProcessorRevision;
    Processor->ProcessorFeatureBits = KeFeatureBits;

    return STATUS_SUCCESS;
}

/*
 * The time of day, told three ways: when the system started, what time it is
 * now, and how long it has been up, so a caller does not have to work any of
 * them out from the others and risk them disagreeing.
 */
static
NTSTATUS
ExpQueryTimeOfDay(
    _Out_ PSYSTEM_TIMEOFDAY_INFORMATION TimeOfDay,
    _In_ ULONG Length)
{
    SYSTEM_TIMEOFDAY_INFORMATION Local;

    RtlZeroMemory(&Local, sizeof(Local));

    Local.BootTime = KeBootTime;
    KeQuerySystemTime(&Local.CurrentTime);
    Local.TimeZoneBias = ExpTimeZoneBias;
    Local.TimeZoneId = ExpTimeZoneId;
    Local.Reserved = 0;
    Local.BootTimeBias = KeBootTimeBias;
    Local.SleepTimeBias = 0;

    RtlCopyMemory(TimeOfDay, &Local, Length);

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpQueryProcessorPerformance(
    _Out_ PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Performance,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    ULONG Needed;
    ULONG Index;

    Needed = KeNumberProcessors *
             (ULONG)sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);

    *ReturnLength = Needed;

    if (Length < Needed)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        RtlZeroMemory(&Performance[Index], sizeof(Performance[Index]));

        Performance[Index].IdleTime.QuadPart =
            (LONGLONG)Prcb->IdleThread->KernelTime * KeMaximumIncrement;
        Performance[Index].KernelTime.QuadPart =
            (LONGLONG)Prcb->KernelTime * KeMaximumIncrement;
        Performance[Index].UserTime.QuadPart =
            (LONGLONG)Prcb->UserTime * KeMaximumIncrement;
        Performance[Index].DpcTime.QuadPart =
            (LONGLONG)Prcb->DpcTime * KeMaximumIncrement;
        Performance[Index].InterruptTime.QuadPart =
            (LONGLONG)Prcb->InterruptTime * KeMaximumIncrement;
        Performance[Index].InterruptCount = Prcb->InterruptCount;
    }

    return STATUS_SUCCESS;
}

/*
 * Lists what is loaded in kernel space, in the order it was loaded, which is
 * what a debugger or a tool that resolves an address needs.
 */
static
NTSTATUS
ExpQueryModuleInformationWorker(
    _Out_opt_ PRTL_PROCESS_MODULES Modules,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PLIST_ENTRY Entry;
    PLDR_DATA_TABLE_ENTRY Module;
    ULONG Count = 0;
    ULONG Needed;
    ULONG Index = 0;
    ANSI_STRING Ansi;
    NTSTATUS Status = STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    for (Entry = PsLoadedModuleList.Flink;
         Entry != &PsLoadedModuleList;
         Entry = Entry->Flink)
    {
        Count++;
    }

    Needed = FIELD_OFFSET(RTL_PROCESS_MODULES, Modules) +
             Count * (ULONG)sizeof(RTL_PROCESS_MODULE_INFORMATION);

    *ReturnLength = Needed;

    if ((Modules == NULL) || (Length < Needed))
    {
        ExReleaseResourceLite(&PsLoadedModuleResource);
        KeLeaveCriticalRegion();
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Modules->NumberOfModules = Count;

    for (Entry = PsLoadedModuleList.Flink;
         Entry != &PsLoadedModuleList;
         Entry = Entry->Flink, Index++)
    {
        PRTL_PROCESS_MODULE_INFORMATION Info = &Modules->Modules[Index];

        Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        RtlZeroMemory(Info, sizeof(*Info));

        Info->Section = 0;
        Info->MappedBase = NULL;
        Info->ImageBase = Module->DllBase;
        Info->ImageSize = Module->SizeOfImage;
        Info->Flags = Module->Flags;
        Info->LoadOrderIndex = (USHORT)Index;
        Info->InitOrderIndex = 0;
        Info->LoadCount = Module->LoadCount;

        Ansi.Buffer = Info->FullPathName;
        Ansi.Length = 0;
        Ansi.MaximumLength = (USHORT)sizeof(Info->FullPathName);

        if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&Ansi,
                                                    &Module->FullDllName,
                                                    FALSE)))
        {
            PCHAR Separator = strrchr(Info->FullPathName, '\\');

            Info->OffsetToFileName = (Separator != NULL) ?
                (USHORT)(Separator - Info->FullPathName + 1) : 0;
        }
    }

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();

    return Status;
}

NTSTATUS NTAPI ExpCaptureUnicodeString(_Out_ PUNICODE_STRING,
    _In_ PUNICODE_STRING);
VOID NTAPI ExpFreeCapturedUnicodeString(_Inout_ PUNICODE_STRING);

/* The classes served by this subsystem's other files */
NTSTATUS NTAPI ExpQueryProcessInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG, _In_ BOOLEAN, _In_ ULONG, _In_ BOOLEAN);
NTSTATUS NTAPI ExpQueryProcessIdInformation(
    _Inout_ PSYSTEM_PROCESS_ID_INFORMATION);
NTSTATUS NTAPI ExpQuerySessionProcessInformation(
    _Inout_ PSYSTEM_SESSION_PROCESS_INFORMATION, _Out_ PULONG);
NTSTATUS NTAPI ExpQueryHandleInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG, _In_ BOOLEAN);
NTSTATUS NTAPI ExpQueryObjectInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG);
NTSTATUS NTAPI ExpQueryLocksInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG);
NTSTATUS NTAPI ExpQueryLookasideInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG);
NTSTATUS NTAPI ExpQueryPerformanceInformation(
    _Out_ PSYSTEM_PERFORMANCE_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryInterruptInformation(
    _Out_ PSYSTEM_INTERRUPT_INFORMATION, _In_ ULONG, _Out_ PULONG);
NTSTATUS NTAPI ExpQueryExceptionInformation(
    _Out_ PSYSTEM_EXCEPTION_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryContextSwitchInformation(
    _Out_ PSYSTEM_CONTEXT_SWITCH_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryProcessorIdleInformation(_Out_ PVOID, _In_ ULONG,
    _Out_ PULONG);
NTSTATUS NTAPI ExpQueryProcessorCycleTimeInformation(_Out_ PVOID, _In_ ULONG,
    _Out_ PULONG, _In_ BOOLEAN);
NTSTATUS NTAPI ExpQueryTimeAdjustment(
    _Out_ PSYSTEM_QUERY_TIME_ADJUST_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryDpcBehaviour(
    _Out_ PSYSTEM_DPC_BEHAVIOR_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryPageFileInformation(_Out_opt_ PVOID, _In_ ULONG,
    _Out_ PULONG);
NTSTATUS NTAPI ExpQueryMemoryListInformation(
    _Out_ PSYSTEM_MEMORY_LIST_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryFileCacheInformation(
    _Out_ PSYSTEM_FILECACHE_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryLogicalProcessorInformation(_Out_opt_ PVOID,
    _In_ ULONG, _Out_ PULONG);
NTSTATUS NTAPI ExpQueryBootEnvironmentInformation(
    _Out_ PSYSTEM_BOOT_ENVIRONMENT_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpQueryRegistryQuota(
    _Out_ PSYSTEM_REGISTRY_QUOTA_INFORMATION, _In_ ULONG);

NTSTATUS NTAPI ExpSetTimeAdjustment(
    _In_ PSYSTEM_SET_TIME_ADJUST_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpSetDpcBehaviour(
    _In_ PSYSTEM_DPC_BEHAVIOR_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpSetRegistryQuota(
    _In_ PSYSTEM_REGISTRY_QUOTA_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpSetPrioritySeparation(_In_ PULONG, _In_ ULONG);
NTSTATUS NTAPI ExpSetTimeSlipEvent(_In_ PHANDLE, _In_ ULONG);
NTSTATUS NTAPI ExpSetObjectSecurityMode(_In_ PULONG, _In_ ULONG);
NTSTATUS NTAPI ExpExtendServiceTable(
    _In_ PSYSTEM_GDI_DRIVER_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpUnloadGdiDriver(_In_ PVOID *, _In_ ULONG);
NTSTATUS NTAPI ExpSetLoadGdiDriver(
    _Inout_ PSYSTEM_GDI_DRIVER_INFORMATION, _In_ ULONG, _In_ BOOLEAN);
NTSTATUS NTAPI ExpSetFileCacheInformation(
    _In_ PSYSTEM_FILECACHE_INFORMATION, _In_ ULONG);
NTSTATUS NTAPI ExpSetLowPriorityIo(_In_ PULONG, _In_ ULONG);

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeSystemInformation(VOID)
{
    ExInitializeFastMutex(&ExpEnvironmentLock);
    ExInitializeResourceLite(&ExpFirmwareTableResource);
    InitializeListHead(&ExpFirmwareTableProviderListHead);
}

#undef ExGetPreviousMode
KPROCESSOR_MODE
NTAPI
ExGetPreviousMode(VOID)
{
    return KeGetPreviousMode();
}

BOOLEAN
NTAPI
ExVerifySuite(
    _In_ SUITE_TYPE SuiteType)
{
    if (SuiteType == Personal)
    {
        return (ExSuiteMask & (1 << Personal)) != 0;
    }

    return (ExSuiteMask & (1 << (ULONG)SuiteType)) != 0;
}

BOOLEAN
NTAPI
ExIsProcessorFeaturePresent(
    _In_ ULONG ProcessorFeature)
{
    if (ProcessorFeature >= PROCESSOR_FEATURE_MAX)
    {
        return FALSE;
    }

    return SharedUserData->ProcessorFeatures[ProcessorFeature];
}

VOID
NTAPI
ExGetCurrentProcessorCounts(
    _Out_ PULONG IdleCount,
    _Out_ PULONG KernelAndUserCount,
    _Out_ PULONG ProcessorNumber)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    *IdleCount = Prcb->IdleThread->KernelTime;
    *KernelAndUserCount = Prcb->KernelTime + Prcb->UserTime;
    *ProcessorNumber = KeGetCurrentProcessorNumber();
}

VOID
NTAPI
ExGetCurrentProcessorCpuUsage(
    _Out_ PULONG CpuUsage)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Total;
    ULONG Idle;

    Idle = Prcb->IdleThread->KernelTime;
    Total = Prcb->KernelTime + Prcb->UserTime;

    if (Total == 0)
    {
        *CpuUsage = 0;
        return;
    }

    /* What is left once the idle thread's share is taken off the whole. */
    if (Idle >= Total)
    {
        *CpuUsage = 0;
        return;
    }

    *CpuUsage = ((Total - Idle) * 100) / Total;
}

/*
 * Pins a caller's buffer in memory and gives the kernel an address of its own
 * for it, so the buffer can be written at a level at which a fault is not
 * allowed.
 */
NTSTATUS
NTAPI
ExLockUserBuffer(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation,
    _Out_ PVOID *MappedSystemVa,
    _Out_ PMDL *OutMdl)
{
    PMDL Mdl;
    PVOID Mapped;
    NTSTATUS Status = STATUS_SUCCESS;

    *MappedSystemVa = NULL;
    *OutMdl = NULL;

    if (Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = IoAllocateMdl(BaseAddress, Length, FALSE, TRUE, NULL);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, AccessMode, Operation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Mapped = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (Mapped == NULL)
    {
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *MappedSystemVa = Mapped;
    *OutMdl = Mdl;

    return STATUS_SUCCESS;
}

VOID
NTAPI
ExUnlockUserBuffer(
    _In_opt_ PMDL Mdl)
{
    if (Mdl == NULL)
    {
        return;
    }

    MmUnlockPages(Mdl);
    IoFreeMdl(Mdl);
}

/*
 * Hands back the machine's own description exactly as the firmware gave it,
 * from where whoever read the firmware put it.
 */
NTSTATUS
NTAPI
ExpGetRawSMBiosTable(
    _Out_opt_ PVOID Buffer,
    _Out_ ULONG *OutSize,
    _In_ ULONG BufferSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(EXP_SMBIOS_KEY);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(EXP_SMBIOS_VALUE);
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    HANDLE Key;
    ULONG Length = 0;
    NTSTATUS Status;

    PAGED_CODE();

    *OutSize = 0;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&Key, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwQueryValueKey(Key,
                             &ValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &Length);
    if ((Status != STATUS_BUFFER_TOO_SMALL) &&
        (Status != STATUS_BUFFER_OVERFLOW))
    {
        ZwClose(Key);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Value = ExAllocatePoolZero(PagedPool, Length, EXP_SYSINFO_TAG);
    if (Value == NULL)
    {
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(Key,
                             &ValueName,
                             KeyValuePartialInformation,
                             Value,
                             Length,
                             &Length);

    ZwClose(Key);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Value, EXP_SYSINFO_TAG);
        return Status;
    }

    *OutSize = Value->DataLength;

    if (Buffer == NULL)
    {
        ExFreePoolWithTag(Value, EXP_SYSINFO_TAG);
        return STATUS_SUCCESS;
    }

    if (BufferSize < Value->DataLength)
    {
        ExFreePoolWithTag(Value, EXP_SYSINFO_TAG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(Buffer, Value->Data, Value->DataLength);

    ExFreePoolWithTag(Value, EXP_SYSINFO_TAG);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryModuleInformation(
    _In_ PLIST_ENTRY KernelModeList,
    _In_opt_ PLIST_ENTRY UserModeList,
    _Out_opt_ PRTL_PROCESS_MODULES Modules,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    UNREFERENCED_PARAMETER(KernelModeList);
    UNREFERENCED_PARAMETER(UserModeList);

    return ExpQueryModuleInformationWorker(Modules, Length, ReturnLength);
}

NTSTATUS
NTAPI
RtlQueryModuleInformation(
    _Inout_ PULONG BufferSize,
    _In_ ULONG EntrySize,
    _Out_opt_ PVOID Buffer)
{
    ULONG Needed = 0;
    NTSTATUS Status;

    if (EntrySize != sizeof(RTL_PROCESS_MODULE_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Status = ExpQueryModuleInformationWorker(Buffer, *BufferSize, &Needed);

    *BufferSize = Needed;

    return Status;
}

ULONG
NTAPI
NtGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentProcessorNumber();
}

NTSTATUS
NTAPI
NtQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID SystemInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG UnsafeResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG ResultLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpProbeSystemInfoBuffer(SystemInformation,
                                      Length,
                                      UnsafeResultLength,
                                      sizeof(ULONG),
                                      PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        switch (SystemInformationClass)
        {
            case SystemBasicInformation:
            case SystemEmulationBasicInformation:
            {
                ResultLength = sizeof(SYSTEM_BASIC_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ExpQueryBasicInformation(SystemInformation);
                break;
            }

            case SystemProcessorInformation:
            case SystemEmulationProcessorInformation:
            {
                ResultLength = sizeof(SYSTEM_PROCESSOR_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ExpQueryProcessorInformation(SystemInformation);
                break;
            }

            case SystemTimeOfDayInformation:
            {
                if (Length > sizeof(SYSTEM_TIMEOFDAY_INFORMATION))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                ResultLength = Length;
                Status = ExpQueryTimeOfDay(SystemInformation, Length);
                break;
            }

            case SystemProcessorPerformanceInformation:
            {
                Status = ExpQueryProcessorPerformance(SystemInformation,
                                                      Length,
                                                      &ResultLength);
                break;
            }

            case SystemModuleInformation:
            {
                Status = ExpQueryModuleInformationWorker(SystemInformation,
                                                         Length,
                                                         &ResultLength);
                break;
            }

            case SystemFlagsInformation:
            {
                ResultLength = sizeof(SYSTEM_FLAGS_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                ((PSYSTEM_FLAGS_INFORMATION)SystemInformation)->Flags =
                    NtGlobalFlag;
                break;
            }

            case SystemKernelDebuggerInformation:
            {
                PSYSTEM_KERNEL_DEBUGGER_INFORMATION Debugger =
                    SystemInformation;

                ResultLength = sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION);
                if (Length < ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Debugger->KernelDebuggerEnabled = KdDebuggerEnabled;
                Debugger->KernelDebuggerNotPresent = KdDebuggerNotPresent;
                break;
            }

            case SystemRangeStartInformation:
            {
                ResultLength = sizeof(ULONG_PTR);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PULONG_PTR)SystemInformation =
                    (ULONG_PTR)MmSystemRangeStart;
                break;
            }

            case SystemCurrentTimeZoneInformation:
            {
                ResultLength = sizeof(RTL_TIME_ZONE_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PRTL_TIME_ZONE_INFORMATION)SystemInformation =
                    ExpTimeZoneInfo;
                break;
            }

            case SystemPoolTagInformation:
            {
                Status = ExGetPoolTagInfo(SystemInformation,
                                          Length,
                                          &ResultLength);
                break;
            }

            case SystemProcessInformation:
            case SystemExtendedProcessInformation:
            case SystemFullProcessInformation:
            {
                Status = ExpQueryProcessInformation(
                             SystemInformation,
                             Length,
                             &ResultLength,
                             (BOOLEAN)(SystemInformationClass !=
                                       SystemProcessInformation),
                             0,
                             FALSE);
                break;
            }

            case SystemSessionProcessInformation:
            {
                ResultLength =
                    sizeof(SYSTEM_SESSION_PROCESS_INFORMATION);
                if (Length < ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ExpQuerySessionProcessInformation(SystemInformation,
                                                           &ResultLength);
                break;
            }

            case SystemProcessIdInformation:
            {
                ResultLength = sizeof(SYSTEM_PROCESS_ID_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ExpQueryProcessIdInformation(SystemInformation);
                break;
            }

            case SystemHandleInformation:
            case SystemExtendedHandleInformation:
            {
                Status = ExpQueryHandleInformation(
                             SystemInformation,
                             Length,
                             &ResultLength,
                             (BOOLEAN)(SystemInformationClass ==
                                       SystemExtendedHandleInformation));
                break;
            }

            case SystemObjectInformation:
            {
                Status = ExpQueryObjectInformation(SystemInformation,
                                                   Length,
                                                   &ResultLength);
                break;
            }

            case SystemLocksInformation:
            {
                Status = ExpQueryLocksInformation(SystemInformation,
                                                  Length,
                                                  &ResultLength);
                break;
            }

            case SystemLookasideInformation:
            {
                Status = ExpQueryLookasideInformation(SystemInformation,
                                                      Length,
                                                      &ResultLength);
                break;
            }

            case SystemPerformanceInformation:
            {
                ResultLength = sizeof(SYSTEM_PERFORMANCE_INFORMATION);
                Status = ExpQueryPerformanceInformation(SystemInformation,
                                                        Length);
                break;
            }

            case SystemInterruptInformation:
            {
                Status = ExpQueryInterruptInformation(SystemInformation,
                                                      Length,
                                                      &ResultLength);
                break;
            }

            case SystemExceptionInformation:
            {
                ResultLength = sizeof(SYSTEM_EXCEPTION_INFORMATION);
                Status = ExpQueryExceptionInformation(SystemInformation,
                                                      Length);
                break;
            }

            case SystemContextSwitchInformation:
            {
                ResultLength = sizeof(SYSTEM_CONTEXT_SWITCH_INFORMATION);
                Status = ExpQueryContextSwitchInformation(SystemInformation,
                                                          Length);
                break;
            }

            case SystemProcessorIdleInformation:
            {
                Status = ExpQueryProcessorIdleInformation(SystemInformation,
                                                          Length,
                                                          &ResultLength);
                break;
            }

            case SystemProcessorCycleTimeInformation:
            case SystemProcessorIdleCycleTimeInformation:
            {
                Status = ExpQueryProcessorCycleTimeInformation(
                             SystemInformation,
                             Length,
                             &ResultLength,
                             (BOOLEAN)(SystemInformationClass ==
                                       SystemProcessorIdleCycleTimeInformation));
                break;
            }

            case SystemTimeAdjustmentInformation:
            {
                ResultLength =
                    sizeof(SYSTEM_QUERY_TIME_ADJUST_INFORMATION);
                Status = ExpQueryTimeAdjustment(SystemInformation, Length);
                break;
            }

            case SystemDpcBehaviorInformation:
            {
                ResultLength = sizeof(SYSTEM_DPC_BEHAVIOR_INFORMATION);
                Status = ExpQueryDpcBehaviour(SystemInformation, Length);
                break;
            }

            case SystemPageFileInformation:
            case SystemPageFileInformationEx:
            {
                Status = ExpQueryPageFileInformation(SystemInformation,
                                                     Length,
                                                     &ResultLength);
                break;
            }

            case SystemMemoryListInformation:
            {
                ResultLength = sizeof(SYSTEM_MEMORY_LIST_INFORMATION);
                Status = ExpQueryMemoryListInformation(SystemInformation,
                                                       Length);
                break;
            }

            case SystemFileCacheInformation:
            case SystemFileCacheInformationEx:
            {
                ResultLength = sizeof(SYSTEM_FILECACHE_INFORMATION);
                Status = ExpQueryFileCacheInformation(SystemInformation,
                                                      Length);
                break;
            }

            case SystemLogicalProcessorInformation:
            {
                Status = ExpQueryLogicalProcessorInformation(
                             SystemInformation,
                             Length,
                             &ResultLength);
                break;
            }

            case SystemBootEnvironmentInformation:
            {
                ResultLength = sizeof(SYSTEM_BOOT_ENVIRONMENT_INFORMATION);
                Status = ExpQueryBootEnvironmentInformation(SystemInformation,
                                                            Length);
                break;
            }

            case SystemRegistryQuotaInformation:
            {
                ResultLength = sizeof(SYSTEM_REGISTRY_QUOTA_INFORMATION);
                Status = ExpQueryRegistryQuota(SystemInformation, Length);
                break;
            }

            case SystemPagedPoolInformation:
            case SystemNonPagedPoolInformation:
            {
                /*
                 * The whole of a pool is described entry by entry, which only a
                 * pool built to be walked can do.
                 */
                ResultLength = sizeof(SYSTEM_POOL_INFORMATION);
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            case SystemFullMemoryInformation:
            case SystemSummaryMemoryInformation:
            {
                ResultLength = sizeof(ULONG);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PULONG)SystemInformation = (ULONG)MmNumberOfPhysicalPages;
                break;
            }

            case SystemDeviceInformation:
            {
                PSYSTEM_DEVICE_INFORMATION Device = SystemInformation;

                ResultLength = sizeof(SYSTEM_DEVICE_INFORMATION);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* TODO: the I/O manager does not count its device objects by
                   kind, so the counts are reported as none. */
                RtlZeroMemory(Device, ResultLength);
                break;
            }

            case SystemCallCountInformation:
            {
                PSYSTEM_CALL_COUNT_INFORMATION Calls = SystemInformation;

                ResultLength = sizeof(SYSTEM_CALL_COUNT_INFORMATION);
                if (Length < ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* The per-service counts are only kept when asked for at
                   build time, which no shipping kernel is. */
                Calls->Length = ResultLength;
                Calls->NumberOfTables = 0;
                break;
            }

            case SystemRecommendedSharedDataAlignment:
            {
                ResultLength = sizeof(ULONG);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PULONG)SystemInformation =
                    KeGetRecommendedSharedDataAlignment();
                break;
            }

            case SystemObjectSecurityMode:
            {
                ResultLength = sizeof(ULONG);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PULONG)SystemInformation = ObpObjectSecurityMode;
                break;
            }

            case SystemPrioritySeperation:
            {
                ResultLength = sizeof(ULONG);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PULONG)SystemInformation = PsPrioritySeparation;
                break;
            }

            case SystemTimeZoneInformation:
            case SystemDynamicTimeZoneInformation:
            {
                ResultLength = sizeof(RTL_TIME_ZONE_INFORMATION);
                if (Length < ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                *(PRTL_TIME_ZONE_INFORMATION)SystemInformation =
                    ExpTimeZoneInfo;
                break;
            }

            case SystemNumaProximityNodeInformation:
            case SystemNodeDistanceInformation:
            {
                ResultLength = sizeof(ULONG);
                if (Length < ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* One node means no distance to anywhere else. */
                *(PULONG)SystemInformation = 0;
                break;
            }

            case SystemHypervisorProcessorCountInformation:
            {
                ResultLength = sizeof(ULONG);
                if (Length != ResultLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* Nothing is running underneath this kernel. */
                *(PULONG)SystemInformation = 0;
                break;
            }

            case SystemModuleInformationEx:
            {
                /*
                 * The longer form of the module list carries a per-entry offset
                 * and an image checksum that this kernel's loader does not keep.
                 */
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            default:
            {
                /*
                 * TODO: the classes that are left describe things this kernel
                 * does not have, or have no published layout to fill in.
                 */
                Status = STATUS_INVALID_INFO_CLASS;
                break;
            }
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExpReportLength(UnsafeResultLength, ResultLength);

    return Status;
}

/*
 * The same questions, asked about something the caller names rather than about
 * the system as a whole.
 */
NTSTATUS
NTAPI
NtQuerySystemInformationEx(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(Length) PVOID SystemInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if ((InputBuffer != NULL) && (InputBufferLength != 0))
            {
                ProbeForRead(InputBuffer, InputBufferLength, sizeof(ULONG));
            }
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /*
     * Only a handful of classes take an input of their own, and each of them
     * names one processor or one group. The rest are the same question as the
     * plain form asks, so they are passed straight through.
     */
    switch (SystemInformationClass)
    {
        case SystemProcessorPerformanceInformation:
        case SystemProcessorIdleInformation:
        case SystemProcessorCycleTimeInformation:
        case SystemProcessorIdleCycleTimeInformation:
        case SystemLogicalProcessorAndGroupInformation:
        {
            USHORT Group;

            if (InputBufferLength < sizeof(USHORT))
            {
                return STATUS_INVALID_PARAMETER;
            }

            _SEH2_TRY
            {
                Group = *(PUSHORT)InputBuffer;
            }
            _SEH2_EXCEPT(ExSystemExceptionFilter())
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;

            /* This kernel keeps every processor in one group. */
            if (Group != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (SystemInformationClass ==
                SystemLogicalProcessorAndGroupInformation)
            {
                SystemInformationClass = SystemLogicalProcessorInformation;
            }

            break;
        }

        default:
            break;
    }

    Status = NtQuerySystemInformation(SystemInformationClass,
                                      SystemInformation,
                                      Length,
                                      ResultLength);

    return Status;
}

NTSTATUS
NTAPI
NtSetSystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _In_reads_bytes_opt_(Length) PVOID SystemInformation,
    _In_ ULONG Length)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if ((PreviousMode != KernelMode) && (SystemInformation != NULL) &&
        (Length != 0))
    {
        _SEH2_TRY
        {
            ProbeForRead(SystemInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        switch (SystemInformationClass)
        {
            case SystemFlagsInformation:
            {
                if (Length != sizeof(SYSTEM_FLAGS_INFORMATION))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                NtGlobalFlag =
                    ((PSYSTEM_FLAGS_INFORMATION)SystemInformation)->Flags;
                break;
            }

            case SystemTimeAdjustmentInformation:
            {
                Status = ExpSetTimeAdjustment(SystemInformation, Length);
                break;
            }

            case SystemDpcBehaviorInformation:
            {
                Status = ExpSetDpcBehaviour(SystemInformation, Length);
                break;
            }

            case SystemRegistryQuotaInformation:
            {
                Status = ExpSetRegistryQuota(SystemInformation, Length);
                break;
            }

            case SystemPrioritySeperation:
            {
                Status = ExpSetPrioritySeparation(SystemInformation, Length);
                break;
            }

            case SystemTimeSlipNotification:
            {
                Status = ExpSetTimeSlipEvent(SystemInformation, Length);
                break;
            }

            case SystemObjectSecurityMode:
            {
                Status = ExpSetObjectSecurityMode(SystemInformation, Length);
                break;
            }

            case SystemExtendServiceTableInformation:
            {
                Status = ExpExtendServiceTable(SystemInformation, Length);
                break;
            }

            case SystemLoadGdiDriverInformation:
            {
                Status = ExpSetLoadGdiDriver(SystemInformation,
                                             Length,
                                             FALSE);
                break;
            }

            case SystemLoadGdiDriverInSystemSpace:
            {
                Status = ExpSetLoadGdiDriver(SystemInformation,
                                             Length,
                                             TRUE);
                break;
            }

            case SystemUnloadGdiDriverInformation:
            {
                Status = ExpUnloadGdiDriver(SystemInformation, Length);
                break;
            }

            case SystemFileCacheInformation:
            case SystemFileCacheInformationEx:
            {
                Status = ExpSetFileCacheInformation(SystemInformation,
                                                    Length);
                break;
            }

            case SystemLowPriorityIoInformation:
            {
                Status = ExpSetLowPriorityIo(SystemInformation, Length);
                break;
            }

            case SystemVerifierAddDriverInformation:
            case SystemVerifierRemoveDriverInformation:
            case SystemVerifierThunkExtend:
            case SystemVerifierInformation:
            {
                if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                /* TODO: the driver verifier is not present, so a driver cannot
                   be put under it. */
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            case SystemCrashDumpStateInformation:
            {
                if (Length != sizeof(ULONG))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                /* TODO: no crash dump is written, so there is no state of one
                   to turn on or off. */
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            case SystemSessionCreate:
            case SystemSessionDetach:
            {
                if (Length != sizeof(ULONG))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                /* TODO: a session of its own address space is not created by
                   the memory manager yet. */
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            case SystemMemoryListInformation:
            {
                if (Length != sizeof(ULONG))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                if (!SeSinglePrivilegeCheck(SeProfileSingleProcessPrivilege,
                                            PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                /* TODO: the page lists cannot be emptied on request. */
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            case SystemComPlusPackage:
            case SystemAitSamplingValue:
            case SystemRegistryAppendString:
            case SystemWatchdogTimerHandler:
            case SystemWatchdogTimerInformation:
            case SystemRegisterFirmwareTableInformationHandler:
            {
                if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                /* TODO: nothing in this kernel keeps what these classes set. */
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            default:
            {
                /*
                 * TODO: the classes that are left name things this kernel does
                 * not have, or have no published layout to read.
                 */
                Status = STATUS_INVALID_INFO_CLASS;
                break;
            }
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* ENVIRONMENT VARIABLES ******************************************************/

/*
 * The older form of the firmware variable interface. It names no vendor, so the
 * variable is looked for under the firmware's own, and it carries its value as
 * characters rather than as bytes.
 */
NTSTATUS
NTAPI
NtQuerySystemEnvironmentValue(
    _In_ PUNICODE_STRING VariableName,
    _Out_writes_bytes_(Length) PWSTR Value,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING Name;
    PVOID Buffer;
    ULONG Size = Length;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Length == 0)
    {
        return STATUS_INVALID_PARAMETER_3;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Value, Length, sizeof(WCHAR));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpCaptureUnicodeString(&Name, VariableName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Buffer = ExAllocatePoolZero(PagedPool, Length, EXP_SYSINFO_TAG);
    if (Buffer == NULL)
    {
        ExpFreeCapturedUnicodeString(&Name);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ExGetFirmwareEnvironmentVariable(&Name,
                                              (LPGUID)&ExpVendorGuid,
                                              Buffer,
                                              &Size,
                                              NULL);

    ExpFreeCapturedUnicodeString(&Name);

    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Value, Buffer, Size);

            if (ReturnLength != NULL)
            {
                *ReturnLength = Size;
            }
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else if (ReturnLength != NULL)
    {
        ExpReportLength(ReturnLength, Size);
    }

    ExFreePoolWithTag(Buffer, EXP_SYSINFO_TAG);

    return Status;
}

NTSTATUS
NTAPI
NtSetSystemEnvironmentValue(
    _In_ PUNICODE_STRING VariableName,
    _In_ PUNICODE_STRING Value)
{
    UNICODE_STRING Name;
    UNICODE_STRING Contents;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ExpCaptureUnicodeString(&Name, VariableName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpCaptureUnicodeString(&Contents, Value);
    if (!NT_SUCCESS(Status))
    {
        ExpFreeCapturedUnicodeString(&Name);
        return Status;
    }

    Status = ExSetFirmwareEnvironmentVariable(&Name,
                                              (LPGUID)&ExpVendorGuid,
                                              Contents.Buffer,
                                              Contents.Length,
                                              EXP_FIRMWARE_ATTRIBUTES);

    ExpFreeCapturedUnicodeString(&Contents);
    ExpFreeCapturedUnicodeString(&Name);

    return Status;
}

NTSTATUS
NTAPI
NtQuerySystemEnvironmentValueEx(
    _In_ PUNICODE_STRING VariableName,
    _In_ LPGUID VendorGuid,
    _Out_opt_ PVOID Value,
    _Inout_ PULONG ReturnLength,
    _Out_opt_ PULONG Attributes)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    return ExGetFirmwareEnvironmentVariable(VariableName,
                                            VendorGuid,
                                            Value,
                                            ReturnLength,
                                            Attributes);
}

NTSTATUS
NTAPI
NtSetSystemEnvironmentValueEx(
    _In_ PUNICODE_STRING VariableName,
    _In_ LPGUID VendorGuid,
    _In_reads_bytes_opt_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    return ExSetFirmwareEnvironmentVariable(VariableName,
                                            VendorGuid,
                                            Value,
                                            ValueLength,
                                            Attributes);
}

/*
 * Walks the firmware's variables. The firmware hands them over one at a time
 * through a call this kernel does not reach, so nothing can be listed; the
 * caller is told that rather than being handed an empty list it would believe.
 */
NTSTATUS
NTAPI
NtEnumerateSystemEnvironmentValuesEx(
    _In_ ULONG InformationClass,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    /* TODO: the HAL offers no way to ask for the next variable's name. */
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
