/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a caller may change about a process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* What ProcessAffinityUpdateMode carries */
#define PSP_AFFINITY_UPDATE_ENABLE      0x1
#define PSP_AFFINITY_UPDATE_PERMANENT   0x2

/*
 * The lengths each class may be set with, checked before the process is
 * looked up. A class that is only ever read is refused once the buffer has
 * been checked.
 */
static const PSP_INFORMATION_CLASS PspSetProcessClasses[MaxProcessInfoClass] =
{
    [ProcessBasicInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessQuotaLimits] = PSP_INFO_EXACT_OR(sizeof(QUOTA_LIMITS), sizeof(QUOTA_LIMITS_EX)),
    [ProcessIoCounters] = PSP_INFO_WRONG_DIRECTION,
    [ProcessVmCounters] = PSP_INFO_WRONG_DIRECTION,
    [ProcessTimes] = PSP_INFO_WRONG_DIRECTION,
    [ProcessBasePriority] = PSP_INFO_EXACT(sizeof(KPRIORITY)),
    [ProcessRaisePriority] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessDebugPort] = PSP_INFO_WRONG_DIRECTION,
    [ProcessExceptionPort] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ProcessAccessToken] = PSP_INFO_EXACT(sizeof(PROCESS_ACCESS_TOKEN)),
    /* TODO: x86 keeps a local descriptor table and I/O port handlers that these classes change */
    [ProcessLdtInformation] = PSP_INFO_NOT_IMPLEMENTED,
    [ProcessLdtSize] = PSP_INFO_NOT_IMPLEMENTED,
    [ProcessDefaultHardErrorMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessIoPortHandlers] = PSP_INFO_NOT_IMPLEMENTED,
    [ProcessPooledUsageAndLimits] = PSP_INFO_WRONG_DIRECTION,
    [ProcessEnableAlignmentFaultFixup] = PSP_INFO_EXACT_ALIGNED(sizeof(BOOLEAN), sizeof(UCHAR)),
    [ProcessPriorityClass] = PSP_INFO_EXACT_ALIGNED(sizeof(PROCESS_PRIORITY_CLASS), sizeof(UCHAR)),
    [ProcessWx86Information] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessHandleCount] = PSP_INFO_WRONG_DIRECTION,
    [ProcessAffinityMask] = PSP_INFO_EXACT(sizeof(KAFFINITY)),
    [ProcessPriorityBoost] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessDeviceMap] = PSP_INFO_EXACT(RTL_FIELD_SIZE(PROCESS_DEVICEMAP_INFORMATION, Set)),
    [ProcessSessionInformation] = PSP_INFO_EXACT(sizeof(PROCESS_SESSION_INFORMATION)),
    [ProcessForegroundInformation] = PSP_INFO_EXACT_ALIGNED(sizeof(PROCESS_FOREGROUND_BACKGROUND),
                                                            sizeof(UCHAR)),
    [ProcessWow64Information] = PSP_INFO_WRONG_DIRECTION,
    [ProcessImageFileName] = PSP_INFO_WRONG_DIRECTION,
    [ProcessLUIDDeviceMapsEnabled] = PSP_INFO_WRONG_DIRECTION,
    [ProcessBreakOnTermination] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessDebugObjectHandle] = PSP_INFO_WRONG_DIRECTION,
    [ProcessDebugFlags] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessIoPriority] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessExecuteFlags] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessTlsInformation] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessCookie] = PSP_INFO_WRONG_DIRECTION,
    [ProcessImageInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessCycleTime] = PSP_INFO_WRONG_DIRECTION,
    [ProcessPagePriority] = PSP_INFO_EXACT(sizeof(PAGE_PRIORITY_INFORMATION)),
    [ProcessImageFileNameWin32] = PSP_INFO_WRONG_DIRECTION,
    [ProcessAffinityUpdateMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessMemoryAllocationMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessGroupInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessTokenVirtualizationEnabled] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessMitigationPolicy] = PSP_INFO_EXACT(sizeof(ULONG) * 2),
    [ProcessHandleCheckingMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessHandleTable] = PSP_INFO_WRONG_DIRECTION,
    [ProcessCheckStackExtentsMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessCommandLineInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessTelemetryIdInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessSubsystemInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessEnergyValues] = PSP_INFO_WRONG_DIRECTION,
    [ProcessPowerThrottlingState] = PSP_INFO_EXACT(sizeof(PROCESS_POWER_THROTTLING_STATE)),
    [ProcessWakeInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessManageWritesToExecutableMemory] = PSP_INFO_EXACT(sizeof(MANAGE_WRITES_TO_EXECUTABLE_MEMORY)),
};

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads a fixed-size value the caller is setting. Every set class goes
 * through this, so a wrong length is refused in one place and a caller never
 * has its own memory read twice.
 */
static
NTSTATUS
PspCaptureSetInformationAligned(
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Expected) PVOID Captured,
    _In_ ULONG Expected,
    _In_ ULONG Alignment)
{
    ASSERT((Alignment == sizeof(UCHAR)) || (Alignment == sizeof(ULONG)));

    if (Length != Expected) return STATUS_INFO_LENGTH_MISMATCH;

    if (PreviousMode == KernelMode)
    {
        RtlCopyMemory(Captured, Information, Expected);
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForRead(Information, Expected, Alignment);
        RtlCopyMemory(Captured, Information, Expected);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PspCaptureSetInformation(
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Expected) PVOID Captured,
    _In_ ULONG Expected)
{
    return PspCaptureSetInformationAligned(Information, Length, PreviousMode,
                                           Captured, Expected, sizeof(ULONG));
}

/*
 * Raises every thread of the process by a boost. The walk takes a reference
 * on each thread as it goes, so a thread that leaves while the walk runs
 * does not take the walk with it.
 */
static
NTSTATUS
PspRaiseProcessThreads(
    _In_ PEPROCESS Process,
    _In_ KPRIORITY Increment)
{
    PETHREAD Thread;

    PAGED_CODE();

    Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread != NULL)
    {
        KeBoostPriorityThread(&Thread->Tcb, Increment);
        Thread = PsGetNextProcessThread(Process, Thread);
    }

    return STATUS_SUCCESS;
}

/*
 * Clears one storage slot in every thread of the process, which is what the
 * loader asks for when a module that owned a slot goes away: a slot that
 * still held the module's data would be read after the module was gone.
 */
static
NTSTATUS
PspFlushProcessTlsSlot(
    _In_ PEPROCESS Process,
    _In_ ULONG Index)
{
    PETHREAD Thread;
    KAPC_STATE ApcState;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Index >= TLS_MINIMUM_AVAILABLE) return STATUS_INVALID_PARAMETER;

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread != NULL)
    {
        PTEB Teb = Thread->Tcb.Teb;

        if (Teb != NULL)
        {
            _SEH2_TRY
            {
                Teb->TlsSlots[Index] = NULL;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }

        Thread = PsGetNextProcessThread(Process, Thread);
    }

    KeUnstackDetachProcess(&ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);

    return Status;
}

/*
 * Reserves a thread stack in the process and tells the caller where it is.
 * A caller that is about to make a thread in another process asks for this
 * rather than reserving the memory itself, so the stack is laid out the way
 * the kernel lays one out.
 */
static
NTSTATUS
PspSetThreadStackAllocation(
    _In_ PEPROCESS Process,
    _Inout_ PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PROCESS_STACK_ALLOCATION_INFORMATION_EX Extended;
    PROCESS_STACK_ALLOCATION_INFORMATION *Target;
    SIZE_T ReserveSize;
    PVOID StackBase;
    HANDLE Handle;
    NTSTATUS Status;
    BOOLEAN IsExtended;

    PAGED_CODE();

    IsExtended = (Length == sizeof(PROCESS_STACK_ALLOCATION_INFORMATION_EX));

    if (!IsExtended &&
        Length != sizeof(PROCESS_STACK_ALLOCATION_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(&Extended, sizeof(Extended));

    Status = PspCaptureSetInformation(Information,
                                      Length,
                                      PreviousMode,
                                      IsExtended ? (PVOID)&Extended
                                                 : (PVOID)&Extended.AllocInfo,
                                      Length);
    if (!NT_SUCCESS(Status)) return Status;

    if (Extended.AllocInfo.ReserveSize == 0) return STATUS_INVALID_PARAMETER;

    Status = ObOpenObjectByPointer(Process,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   PROCESS_VM_OPERATION,
                                   PsProcessType,
                                   KernelMode,
                                   &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    /* The stack is only reserved: whoever builds the thread on it commits what it needs */
    StackBase = NULL;
    ReserveSize = Extended.AllocInfo.ReserveSize;
    Status = ZwAllocateVirtualMemory(Handle,
                                     &StackBase,
                                     Extended.AllocInfo.ZeroBits,
                                     &ReserveSize,
                                     MEM_RESERVE,
                                     PAGE_READWRITE);
    ZwClose(Handle);

    if (!NT_SUCCESS(Status)) return Status;

    Extended.AllocInfo.StackBase = StackBase;

    Target = IsExtended
                 ? &((PPROCESS_STACK_ALLOCATION_INFORMATION_EX)Information)
                        ->AllocInfo
                 : (PPROCESS_STACK_ALLOCATION_INFORMATION)Information;

    if (PreviousMode == KernelMode)
    {
        Target->StackBase = Extended.AllocInfo.StackBase;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        Target->StackBase = Extended.AllocInfo.StackBase;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * Moves the bounds of the process's working set, which is how a caller asks
 * the memory manager to keep more or less of the process resident.
 */
static
NTSTATUS
PspSetWorkingSetControl(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    QUOTA_LIMITS Limits;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspCaptureSetInformation(Information,
                                      Length,
                                      PreviousMode,
                                      &Limits,
                                      sizeof(Limits));
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * The memory manager works on the process it is called in, so the caller
     * is put into the target process for the length of the call.
     */
    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    Status = MmAdjustWorkingSetSize(Limits.MinimumWorkingSetSize,
                                    Limits.MaximumWorkingSetSize,
                                    0,
                                    FALSE);

    KeUnstackDetachProcess(&ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);

    return Status;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtSetInformationProcess(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationLength) PVOID ProcessInformation,
    _In_ ULONG ProcessInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Access = PROCESS_SET_INFORMATION;
    PROCESS_POWER_THROTTLING_STATE Throttling;
    PROCESS_DEVICEMAP_INFORMATION DeviceMap;
    PROCESS_SESSION_INFORMATION Session;
    PROCESS_PRIORITY_CLASS PriorityClass;
    PROCESS_FOREGROUND_BACKGROUND Foreground;
    MANAGE_WRITES_TO_EXECUTABLE_MEMORY Writes;
    PROCESS_ACCESS_TOKEN AccessToken;
    PS_PROTECTION Protection;
    ULONG_PTR Pointer;
    KPRIORITY Priority;
    PEPROCESS Process;
    NTSTATUS Status;
    BOOLEAN Boolean;
    ULONG Value;
    ULONG Pair[2];
    HANDLE Handle;

    PAGED_CODE();

    if (ProcessInformationClass >= MaxProcessInfoClass)
        return STATUS_INVALID_INFO_CLASS;

    /* The class exists only where the processor lets writes to code be managed */
    if ((ProcessInformationClass == ProcessManageWritesToExecutableMemory) &&
        !PspArchManagesExecutableWrites())
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    Status = PspCheckInformationClass(&PspSetProcessClasses[ProcessInformationClass],
                                      ProcessInformation,
                                      ProcessInformationLength,
                                      PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Which right the change needs depends on what is being changed: the
     * accounting classes are a different right from the behaviour ones, and
     * giving a process a token is both.
     */
    if (ProcessInformationClass == ProcessAccessToken)
        Access = PROCESS_SET_INFORMATION | PROCESS_SET_QUOTA;
    else if (ProcessInformationClass == ProcessQuotaLimits ||
             ProcessInformationClass == ProcessWorkingSetWatch ||
             ProcessInformationClass == ProcessWorkingSetWatchEx ||
             ProcessInformationClass == ProcessWorkingSetControl)
    {
        Access = PROCESS_SET_QUOTA;
    }
    else if (ProcessInformationClass == ProcessHighGraphicsPriorityInformation ||
             ProcessInformationClass == ProcessPowerThrottlingState)
    {
        Access = PROCESS_SET_LIMITED_INFORMATION;
    }

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       Access,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    switch (ProcessInformationClass)
    {
        case ProcessQuotaLimits:
            Status = PspSetQuotaLimits(Process,
                                       0,
                                       ProcessInformation,
                                       ProcessInformationLength,
                                       PreviousMode);
            break;

        case ProcessBasePriority:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Priority,
                                              sizeof(Priority));
            if (!NT_SUCCESS(Status)) break;

            /*
             * Raising a process into the real-time range needs the privilege
             * for it; lowering it needs nothing, because a process may
             * always give up time it was going to get.
             */
            if (Priority >= LOW_REALTIME_PRIORITY &&
                !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                        PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            if (Priority > HIGH_PRIORITY || Priority <= LOW_PRIORITY)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeSetPriorityAndQuantumProcess(&Process->Pcb, Priority, 0);
            Status = STATUS_SUCCESS;
            break;

        case ProcessRaisePriority:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            /* A boost past the top of the range is the boost to the top of it */
            if (Value > HIGH_PRIORITY) Value = HIGH_PRIORITY;

            Status = PspRaiseProcessThreads(Process, (KPRIORITY)Value);
            break;

        case ProcessPriorityClass:
            Status = PspCaptureSetInformationAligned(ProcessInformation,
                                                     ProcessInformationLength,
                                                     PreviousMode,
                                                     &PriorityClass,
                                                     sizeof(PriorityClass),
                                                     sizeof(UCHAR));
            if (!NT_SUCCESS(Status)) break;

            Status = PspSetProcessPriorityClass(Process,
                                                PriorityClass.PriorityClass);

            /*
             * The record also says whether the process is in the foreground,
             * which changes how long its threads run inside that class.
             */
            if (NT_SUCCESS(Status))
            {
                PsSetProcessPriorityByClass(Process,
                                            PriorityClass.Foreground
                                                ? PsProcessPriorityForeground
                                                : PsProcessPriorityBackground);
            }
            break;

        case ProcessForegroundInformation:
            Status = PspCaptureSetInformationAligned(ProcessInformation,
                                                     ProcessInformationLength,
                                                     PreviousMode,
                                                     &Foreground,
                                                     sizeof(Foreground),
                                                     sizeof(UCHAR));
            if (!NT_SUCCESS(Status)) break;

            PsSetProcessPriorityByClass(Process,
                                        Foreground.Foreground
                                            ? PsProcessPriorityForeground
                                            : PsProcessPriorityBackground);
            Status = STATUS_SUCCESS;
            break;

        case ProcessAffinityMask:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Pointer,
                                              sizeof(Pointer));
            if (!NT_SUCCESS(Status)) break;

            /* Nothing may be asked for outside what the machine has */
            if (Pointer == 0 ||
                (Pointer & ~(ULONG_PTR)KeActiveProcessors) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* A process whose affinity was fixed keeps the one it has */
            if (Process->AffinityPermanent)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            if (!ExAcquireRundownProtection(&Process->RundownProtect))
            {
                Status = STATUS_PROCESS_IS_TERMINATING;
                break;
            }

            KeSetAffinityProcess(&Process->Pcb, (KAFFINITY)Pointer);
            ExReleaseRundownProtection(&Process->RundownProtect);
            Status = STATUS_SUCCESS;
            break;

        case ProcessAffinityUpdateMode:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if ((Value & ~(PSP_AFFINITY_UPDATE_ENABLE |
                           PSP_AFFINITY_UPDATE_PERMANENT)) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * Fixing the affinity cannot be undone: a process that could
             * unfix it would not be fixed at all.
             */
            if (Process->AffinityPermanent &&
                (Value & PSP_AFFINITY_UPDATE_PERMANENT) == 0)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            Process->AffinityUpdateEnable =
                ((Value & PSP_AFFINITY_UPDATE_ENABLE) != 0) ? 1 : 0;
            Process->AffinityPermanent =
                ((Value & PSP_AFFINITY_UPDATE_PERMANENT) != 0) ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessMemoryAllocationMode:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value > 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Process->VmTopDown = (Value != 0) ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessPriorityBoost:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            KeSetDisableBoostProcess(&Process->Pcb, (BOOLEAN)(Value == 0));
            Status = STATUS_SUCCESS;
            break;

        case ProcessDefaultHardErrorMode:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            InterlockedExchange((PLONG)&Process->DefaultHardErrorProcessing,
                                (LONG)Value);

            /*
             * The process also says here whether it wants to be told about
             * misaligned accesses, which the scheduler has to know.
             */
            KeSetAutoAlignmentProcess(
                &Process->Pcb,
                (BOOLEAN)((Value & SEM_NOALIGNMENTFAULTEXCEPT) == 0));
            Status = STATUS_SUCCESS;
            break;

        case ProcessEnableAlignmentFaultFixup:
            Status = PspCaptureSetInformationAligned(ProcessInformation,
                                                     ProcessInformationLength,
                                                     PreviousMode,
                                                     &Boolean,
                                                     sizeof(Boolean),
                                                     sizeof(UCHAR));
            if (!NT_SUCCESS(Status)) break;

            KeSetAutoAlignmentProcess(&Process->Pcb, Boolean);
            Status = STATUS_SUCCESS;
            break;

        case ProcessCheckStackExtentsMode:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            KeSetCheckStackExtentsProcess(&Process->Pcb,
                                          (BOOLEAN)(Value != 0));
            Status = STATUS_SUCCESS;
            break;

        case ProcessAccessToken:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &AccessToken,
                                              sizeof(AccessToken));
            if (!NT_SUCCESS(Status)) break;

            /* The record names the token by handle, and no handle names none */
            if (AccessToken.Token == NULL)
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }

            /*
             * A token may be given to a process only while it is still
             * starting, before anything in it has run with the old one.
             */
            if (Process->PrimaryTokenFrozen)
            {
                Status = STATUS_TOKEN_ALREADY_IN_USE;
                break;
            }

            Status = PspSetPrimaryToken(Process,
                                        AccessToken.Token,
                                        NULL,
                                        FALSE);
            break;

        case ProcessTokenVirtualizationEnabled:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            Status = PspSetTokenVirtualization(Process, (BOOLEAN)(Value != 0));
            break;

        case ProcessExceptionPort:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Handle,
                                              sizeof(Handle));
            if (!NT_SUCCESS(Status)) break;

            /* A process keeps one exception port for its whole life */
            if (Process->ExceptionPortData != NULL)
            {
                Status = STATUS_PORT_ALREADY_SET;
                break;
            }

            {
                PVOID Port;

                Status = ObReferenceObjectByHandle(Handle,
                                                   0,
                                                   LpcPortObjectType,
                                                   PreviousMode,
                                                   &Port,
                                                   NULL);
                if (!NT_SUCCESS(Status)) break;

                if (InterlockedCompareExchangePointer(
                        &Process->ExceptionPortData,
                        Port,
                        NULL) != NULL)
                {
                    ObDereferenceObject(Port);
                    Status = STATUS_PORT_ALREADY_SET;
                }
            }
            break;

        case ProcessBreakOnTermination:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->BreakOnTermination = (Value != 0) ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessDebugFlags:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if ((Value & ~1UL) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Process->NoDebugInherit = (Value == 0) ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessSessionInformation:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Session,
                                              sizeof(Session));
            if (!NT_SUCCESS(Status)) break;

            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Status = MmSetProcessSession(Process, Session.SessionId);
            break;

        case ProcessDeviceMap:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &DeviceMap,
                                              sizeof(DeviceMap.Set));
            if (!NT_SUCCESS(Status)) break;

            Status = ObSetDeviceMap(Process, DeviceMap.Set.DirectoryHandle);
            break;

        case ProcessWorkingSetWatch:
        case ProcessWorkingSetWatchEx:
            Status = PspSetWorkingSetWatch(Process, TRUE);
            break;

        case ProcessWorkingSetControl:
            Status = PspSetWorkingSetControl(Process,
                                             ProcessInformation,
                                             ProcessInformationLength,
                                             PreviousMode);
            break;

        case ProcessHandleTracing:
            Status = PspSetHandleTracing(Process,
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         PreviousMode);
            break;

        case ProcessExecuteFlags:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            /*
             * Which memory may hold code is decided for the process the
             * caller is in: another process's choice is not the caller's to
             * make, because the caller cannot know what that code is.
             */
            if (Process != PsGetCurrentProcess())
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = MmSetExecuteOptions(Value);
            break;

        case ProcessTlsInformation:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            Status = PspFlushProcessTlsSlot(Process, Value);
            break;

        case ProcessThreadStackAllocation:
            Status = PspSetThreadStackAllocation(Process,
                                                 ProcessInformation,
                                                 ProcessInformationLength,
                                                 PreviousMode);
            break;

        case ProcessInstrumentationCallback:
            Status = PspArchSetInstrumentationCallback(
                         Process,
                         ProcessInformation,
                         ProcessInformationLength,
                         PreviousMode);
            break;

        case ProcessIoPriority:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value >= MaxIoPriorityTypes)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Value > IoPriorityNormal &&
                !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                        PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->DefaultIoPriority = Value;
            Status = STATUS_SUCCESS;
            break;

        case ProcessPagePriority:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value > MEMORY_PRIORITY_FOREGROUND + 3)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = MmSetMemoryPriorityProcess(Process, (UCHAR)Value);
            if (NT_SUCCESS(Status)) Process->DefaultPagePriority = Value;
            break;

        case ProcessMitigationPolicy:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              Pair,
                                              sizeof(Pair));
            if (!NT_SUCCESS(Status)) break;

            Status = PspSetExtendedMitigationPolicy(Process,
                                                    Pair[0],
                                                    Pair[1]);
            break;

        case ProcessMemoryExhaustion:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              Pair,
                                              sizeof(Pair));
            if (!NT_SUCCESS(Status)) break;

            /*
             * A process may ask to be stopped outright when a commit it
             * needs cannot be had, rather than being handed a failure it
             * would not know how to recover from.
             */
            Process->FailFastOnCommitFail = (Pair[1] != 0) ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessFaultInformation:
            if (ProcessInformationLength != sizeof(ULONG))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            PsSetProcessFaultInformation(Process, &Value);
            Status = STATUS_SUCCESS;
            break;

        case ProcessPowerThrottlingState:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Throttling,
                                              sizeof(Throttling));
            if (!NT_SUCCESS(Status)) break;

            if (Throttling.Version != 1 ||
                (Throttling.ControlMask &
                 ~(ULONG)PSP_PROCESS_POWER_THROTTLING_VALID_FLAGS) != 0 ||
                (Throttling.StateMask & ~Throttling.ControlMask) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            {
                PPO_PROCESS_ENERGY_CONTEXT Energy;

                Energy = PspGetEnergyContext(Process);
                if (Energy == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                InterlockedExchange(&Energy->PowerThrottlingControlMask,
                                    (LONG)Throttling.ControlMask);
                InterlockedExchange(&Energy->PowerThrottlingStateMask,
                                    (LONG)Throttling.StateMask);
                Status = STATUS_SUCCESS;
            }
            break;

        case ProcessHighGraphicsPriorityInformation:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Boolean,
                                              sizeof(Boolean));
            if (!NT_SUCCESS(Status)) break;

            /*
             * Being told that a process's drawing matters more than another
             * one's is a system decision, not a process's own.
             */
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->HighGraphicsPriority = Boolean ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessWin32kSyscallFilterInformation:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->Win32KFilterSet = Value;
            Status = STATUS_SUCCESS;
            break;

        case ProcessDefaultCpuSetsInformation:
        case ProcessAllowedCpuSetsInformation:
            Status = PspSetProcessCpuSets(
                         Process,
                         (BOOLEAN)(ProcessInformationClass ==
                                   ProcessAllowedCpuSetsInformation),
                         ProcessInformation,
                         ProcessInformationLength,
                         PreviousMode);
            break;

        case ProcessDisableSystemAllowedCpuSets:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Boolean,
                                              sizeof(Boolean));
            if (!NT_SUCCESS(Status)) break;

            Process->DisableSystemAllowedCpuSet = Boolean ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessManageWritesToExecutableMemory:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Writes,
                                              sizeof(Writes));
            if (!NT_SUCCESS(Status)) break;

            if (Writes.Version != 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* FIXME: the memory manager raises no exception on a write to executable memory; the request is only recorded */
            Process->ManageExecutableMemoryWrites =
                Writes.ProcessEnableWriteExceptions ? 1 : 0;
            Status = STATUS_SUCCESS;
            break;

        case ProcessSubsystemProcess:
            /*
             * Only something trusted may claim a process for a subsystem of
             * its own, because the claim takes the process out of the
             * native subsystem's hands.
             */
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->SubsystemProcess = TRUE;
            Status = STATUS_SUCCESS;
            break;

        case ProcessInPrivate:
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->InPrivate = TRUE;
            Status = STATUS_SUCCESS;
            break;

        case ProcessProtectionInformation:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Protection,
                                              sizeof(Protection));
            if (!NT_SUCCESS(Status)) break;

            if (Protection.Type >= PsProtectedTypeMax ||
                Protection.Signer >= PsProtectedSignerMax)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * Protection is given once, as the process starts: a process
             * that could raise its own protection later would be protected
             * from the very checks that were made against it before.
             */
            if (Process->Protection.Level != 0)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Process->Protection = Protection;
            Status = STATUS_SUCCESS;
            break;

        case ProcessRevokeFileHandles:
            /* TODO: close the process's handles to the named directory */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ProcessDynamicFunctionTableInformation:
            /* TODO: add or remove an unwind table in the target process */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ProcessUserModeIOPL:
            /* TODO: x86 lets a process do port I/O from user mode; this needs an architecture hook */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ProcessWx86Information:
            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            if (Value != 0)
                PspSetProcessFlag(Process, PSF_VDM_ALLOWED_BIT);
            else
                PspClearProcessFlag(Process, PSF_VDM_ALLOWED_BIT);

            Status = STATUS_SUCCESS;
            break;

        case ProcessHandleCheckingMode:
        {
            PHANDLE_TABLE Table;

            Status = PspCaptureSetInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              PreviousMode,
                                              &Value,
                                              sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            Table = ObReferenceProcessHandleTable(Process);
            if (Table == NULL)
            {
                Status = STATUS_PROCESS_IS_TERMINATING;
                break;
            }

            /* A bad handle then raises in the process instead of only failing the call */
            if (Value != 0)
                InterlockedOr((PLONG)&Table->Flags, OB_HANDLE_EXCEPTIONS_ENABLED);
            else
                InterlockedAnd((PLONG)&Table->Flags, ~OB_HANDLE_EXCEPTIONS_ENABLED);

            ObDereferenceProcessHandleTable(Process);
            Status = STATUS_SUCCESS;
            break;
        }

        /*
         * The classes below are read and never written.
         */
        case ProcessBasicInformation:
        case ProcessIoCounters:
        case ProcessVmCounters:
        case ProcessTimes:
        case ProcessDebugPort:
        case ProcessDebugObjectHandle:
        case ProcessHandleCount:
        case ProcessHandleTable:
        case ProcessWow64Information:
        case ProcessImageFileName:
        case ProcessImageFileNameWin32:
        case ProcessCommandLineInformation:
        case ProcessLUIDDeviceMapsEnabled:
        case ProcessCookie:
        case ProcessImageInformation:
        case ProcessCycleTime:
        case ProcessPooledUsageAndLimits:
        case ProcessGroupInformation:
        case ProcessSubsystemInformation:
        case ProcessTelemetryIdInformation:
        case ProcessEnergyValues:
        case ProcessWakeInformation:
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        default:
            /*
             * TODO: ProcessConsoleHostProcess, ProcessWindowInformation,
             * ProcessHandleInformation, ProcessKeepAliveCount,
             * ProcessCommitReleaseInformation, ProcessJobMemoryInformation,
             * ProcessChildProcessInformation, ProcessIumChallengeResponse,
             * ProcessRaiseUMExceptionOnInvalidHandleClose and
             * ProcessEnergyTrackingState: neither the published headers nor
             * the public symbols give a shape or a field for these.
             */
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Process);
    return Status;
}
