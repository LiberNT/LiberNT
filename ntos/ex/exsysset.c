/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What may be told to the system, and who is allowed to tell it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* The event the session manager is told to set when the clock has slipped */
PKEVENT ExpTimeSlipEvent = NULL;

/* PRIVATE DATA ***************************************************************/

#define EXP_SYSSET_TAG 'teSE'

extern ULONG ExpRegistryQuotaAllowed;
extern ULONG ExpRegistryQuotaUsed;

/*
 * How the kernel treats a graphics driver it is asked to load: it goes into the
 * part of the address space the window manager's own session owns, and it is
 * entered as a driver rather than called.
 */
#define EXP_GDI_DRIVER_FLAGS 0

NTSTATUS
NTAPI
ExpCaptureUnicodeString(
    _Out_ PUNICODE_STRING Captured,
    _In_ PUNICODE_STRING Source);

VOID
NTAPI
ExpFreeCapturedUnicodeString(
    _Inout_ PUNICODE_STRING Captured);

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Brings in a driver on the window manager's behalf and hands back where it
 * landed, so the window manager can call into it without a handle.
 */
static
NTSTATUS
ExpLoadGdiDriver(
    _Inout_ PSYSTEM_GDI_DRIVER_INFORMATION Information,
    _In_ BOOLEAN InSessionSpace)
{
    UNICODE_STRING Name;
    PVOID ModuleObject = NULL;
    PVOID ImageBase = NULL;
    PLDR_DATA_TABLE_ENTRY Entry;
    ULONG ExportSize = 0;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(InSessionSpace);

    Status = ExpCaptureUnicodeString(&Name, &Information->DriverName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = MmLoadSystemImage(&Name,
                               NULL,
                               NULL,
                               EXP_GDI_DRIVER_FLAGS,
                               &ModuleObject,
                               &ImageBase);

    ExpFreeCapturedUnicodeString(&Name);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Entry = ModuleObject;

    Information->ImageAddress = ImageBase;
    Information->SectionPointer = ModuleObject;
    Information->EntryPoint = Entry->EntryPoint;
    Information->ImageLength = Entry->SizeOfImage;
    Information->ExportSectionPointer =
        RtlImageDirectoryEntryToData(ImageBase,
                                     TRUE,
                                     IMAGE_DIRECTORY_ENTRY_EXPORT,
                                     &ExportSize);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes a string a caller in user mode handed in and makes a copy the kernel
 * owns, because the caller must not be able to change it afterwards.
 */
NTSTATUS
NTAPI
ExpCaptureUnicodeString(
    _Out_ PUNICODE_STRING Captured,
    _In_ PUNICODE_STRING Source)
{
    UNICODE_STRING Local;
    NTSTATUS Status = STATUS_SUCCESS;

    Captured->Length = 0;
    Captured->MaximumLength = 0;
    Captured->Buffer = NULL;

    if (ExGetPreviousMode() == KernelMode)
    {
        Local = *Source;
    }
    else
    {
        _SEH2_TRY
        {
            ProbeForRead(Source, sizeof(UNICODE_STRING), sizeof(ULONG));
            Local = *Source;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if ((Local.Length == 0) || (Local.Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Captured->Buffer = ExAllocatePoolZero(PagedPool,
                                          Local.Length + sizeof(WCHAR),
                                          EXP_SYSSET_TAG);
    if (Captured->Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ExGetPreviousMode() == KernelMode)
    {
        RtlCopyMemory(Captured->Buffer, Local.Buffer, Local.Length);
    }
    else
    {
        _SEH2_TRY
        {
            ProbeForRead(Local.Buffer, Local.Length, sizeof(WCHAR));
            RtlCopyMemory(Captured->Buffer, Local.Buffer, Local.Length);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Captured->Buffer, EXP_SYSSET_TAG);
        Captured->Buffer = NULL;
        return Status;
    }

    Captured->Length = Local.Length;
    Captured->MaximumLength = Local.Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

VOID
NTAPI
ExpFreeCapturedUnicodeString(
    _Inout_ PUNICODE_STRING Captured)
{
    if (Captured->Buffer != NULL)
    {
        ExFreePoolWithTag(Captured->Buffer, EXP_SYSSET_TAG);
        Captured->Buffer = NULL;
    }

    Captured->Length = 0;
    Captured->MaximumLength = 0;
}

NTSTATUS
NTAPI
ExpSetTimeAdjustment(
    _In_ PSYSTEM_SET_TIME_ADJUST_INFORMATION Information,
    _In_ ULONG Length)
{
    if (Length != sizeof(SYSTEM_SET_TIME_ADJUST_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeSystemtimePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    /*
     * Enabling the adjustment puts the clock back on its own step; disabling it
     * holds the clock at the step the caller named, which is how a caller that
     * is keeping time from elsewhere pulls the clock along.
     */
    if (Information->Enable)
    {
        KeTimeAdjustment = KeMaximumIncrement;
        return STATUS_SUCCESS;
    }

    if ((Information->TimeAdjustment == 0) ||
        (Information->TimeAdjustment > KeMaximumIncrement * 2))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    KeTimeAdjustment = Information->TimeAdjustment;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetDpcBehaviour(
    _In_ PSYSTEM_DPC_BEHAVIOR_INFORMATION Information,
    _In_ ULONG Length)
{
    if (Length != sizeof(SYSTEM_DPC_BEHAVIOR_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Information->DpcQueueDepth != 0)
    {
        KiMaximumDpcQueueDepth = Information->DpcQueueDepth;
    }

    KiMinimumDpcRate = Information->MinimumDpcRate;
    KiAdjustDpcThreshold = Information->AdjustDpcThreshold;
    KiIdealDpcRate = Information->IdealDpcRate;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetRegistryQuota(
    _In_ PSYSTEM_REGISTRY_QUOTA_INFORMATION Information,
    _In_ ULONG Length)
{
    if (Length != sizeof(SYSTEM_REGISTRY_QUOTA_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeIncreaseQuotaPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    ExpRegistryQuotaAllowed = Information->RegistryQuotaAllowed;

    return STATUS_SUCCESS;
}

/*
 * How long a thread of a process in the foreground runs before the scheduler
 * looks elsewhere. The value is the one a user chose, and every process that
 * already exists is re-reckoned against it.
 */
NTSTATUS
NTAPI
ExpSetPrioritySeparation(
    _In_ PULONG Separation,
    _In_ ULONG Length)
{
    if (Length != sizeof(ULONG))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    PsPrioritySeparation = *Separation;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetTimeSlipEvent(
    _In_ PHANDLE EventHandle,
    _In_ ULONG Length)
{
    PKEVENT Event;
    NTSTATUS Status;

    if (Length != sizeof(HANDLE))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeSystemtimePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ObReferenceObjectByHandle(*EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Only one event is remembered; a second one replaces the first. */
    if (ExpTimeSlipEvent != NULL)
    {
        ObDereferenceObject(ExpTimeSlipEvent);
    }

    ExpTimeSlipEvent = Event;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetObjectSecurityMode(
    _In_ PULONG Mode,
    _In_ ULONG Length)
{
    if (Length != sizeof(ULONG))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    ObpObjectSecurityMode = *Mode;

    return STATUS_SUCCESS;
}

/*
 * Adds the window manager's own service table to the ones a call from user mode
 * may be dispatched through. It is the second table, and it may only be added
 * once, because the table it names has to outlive every call made through it.
 */
NTSTATUS
NTAPI
ExpExtendServiceTable(
    _In_ PSYSTEM_GDI_DRIVER_INFORMATION Information,
    _In_ ULONG Length)
{
    SYSTEM_GDI_DRIVER_INFORMATION Captured;
    NTSTATUS Status;

    PAGED_CODE();

    if (Length != sizeof(SYSTEM_GDI_DRIVER_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Captured = *Information;

    Status = ExpLoadGdiDriver(&Captured, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /*
     * The driver puts its own table in place as it starts, by calling the
     * kernel back, so nothing more is done with it here.
     */
    *Information = Captured;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpUnloadGdiDriver(
    _In_ PVOID *SectionPointer,
    _In_ ULONG Length)
{
    PAGED_CODE();

    if (Length != sizeof(PVOID))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    return MmUnloadSystemImage(*SectionPointer);
}

NTSTATUS
NTAPI
ExpSetLoadGdiDriver(
    _Inout_ PSYSTEM_GDI_DRIVER_INFORMATION Information,
    _In_ ULONG Length,
    _In_ BOOLEAN InSessionSpace)
{
    SYSTEM_GDI_DRIVER_INFORMATION Captured;
    NTSTATUS Status;

    PAGED_CODE();

    if (Length != sizeof(SYSTEM_GDI_DRIVER_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Captured = *Information;

    Status = ExpLoadGdiDriver(&Captured, InSessionSpace);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *Information = Captured;

    return STATUS_SUCCESS;
}

/*
 * How much of the cache the system will let grow. The figures are a request,
 * not a promise: the memory manager trims the cache when the machine needs the
 * pages for something else.
 */
NTSTATUS
NTAPI
ExpSetFileCacheInformation(
    _In_ PSYSTEM_FILECACHE_INFORMATION Information,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_FILECACHE_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeIncreaseQuotaPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Information->MaximumWorkingSet != 0)
    {
        CcNtSetDirtyPageThreshold(
            (ULONG)(Information->MaximumWorkingSet >> PAGE_SHIFT));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetLowPriorityIo(
    _In_ PULONG Enable,
    _In_ ULONG Length)
{
    if (Length != sizeof(ULONG))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    /* TODO: the I/O manager does not separate low-priority requests, so the
       request is accepted and nothing is throttled. */
    UNREFERENCED_PARAMETER(Enable);

    return STATUS_SUCCESS;
}

/* EOF */
