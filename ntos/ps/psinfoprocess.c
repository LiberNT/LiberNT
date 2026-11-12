/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a caller may ask about a process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* The one flag ProcessDebugFlags carries: whether a child inherits the port */
#define PSP_DEBUG_INHERIT 0x00000001

/* How many handles ProcessHandleTable will report in one answer */
#define PSP_MAX_REPORTED_HANDLES 0x10000

/* PRIVATE DATA ***************************************************************/

/*
 * What is reported about a process so that a run of it can be recognised
 * again later. The fixed part is followed by the caller's identity and the
 * names the process was started under, each at its own offset. The layout is
 * the one the kernel-mode test for this class pins.
 */
typedef struct _PSP_TELEMETRY_ID_INFORMATION
{
    ULONG HeaderSize;
    ULONG ProcessId;
    ULONGLONG ProcessStartKey;
    ULONGLONG CreateTime;
    ULONGLONG CreateInterruptTime;
    ULONGLONG CreateUnbiasedInterruptTime;
    ULONGLONG ProcessSequenceNumber;
    ULONGLONG SessionCreateTime;
    ULONG SessionId;
    ULONG BootId;
    ULONG ImageChecksum;
    ULONG ImageTimeDateStamp;
    ULONG UserSidOffset;
    ULONG ImagePathOffset;
    ULONG PackageNameOffset;
    ULONG RelativeAppNameOffset;
    ULONG CommandLineOffset;
} PSP_TELEMETRY_ID_INFORMATION, *PPSP_TELEMETRY_ID_INFORMATION;

C_ASSERT(sizeof(PSP_TELEMETRY_ID_INFORMATION) == 0x60);

/* What the handle-table walk is filling in */
typedef struct _PSP_HANDLE_WALK
{
    PHANDLE Handles;
    ULONG Room;
    ULONG Count;
} PSP_HANDLE_WALK, *PPSP_HANDLE_WALK;

/*
 * The lengths each class may be asked with, checked before the process is
 * looked up. A class with more than one shape names each length it accepts.
 */
static const PSP_INFORMATION_CLASS PspQueryProcessClasses[MaxProcessInfoClass] =
{
    [ProcessBasicInformation] = PSP_INFO_EXACT_OR(sizeof(PROCESS_BASIC_INFORMATION),
                                                  sizeof(PROCESS_EXTENDED_BASIC_INFORMATION)),
    [ProcessQuotaLimits] = PSP_INFO_EXACT_OR(sizeof(QUOTA_LIMITS), sizeof(QUOTA_LIMITS_EX)),
    [ProcessIoCounters] = PSP_INFO_EXACT(sizeof(IO_COUNTERS)),
    [ProcessVmCounters] = PSP_INFO_MINIMUM(sizeof(VM_COUNTERS)),
    [ProcessTimes] = PSP_INFO_EXACT(sizeof(KERNEL_USER_TIMES)),
    [ProcessBasePriority] = PSP_INFO_WRONG_DIRECTION,
    [ProcessRaisePriority] = PSP_INFO_WRONG_DIRECTION,
    [ProcessDebugPort] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ProcessExceptionPort] = PSP_INFO_WRONG_DIRECTION,
    [ProcessAccessToken] = PSP_INFO_WRONG_DIRECTION,
    /* TODO: x86 keeps a local descriptor table that this class reads */
    [ProcessLdtInformation] = PSP_INFO_NOT_IMPLEMENTED,
    [ProcessLdtSize] = PSP_INFO_WRONG_DIRECTION,
    [ProcessDefaultHardErrorMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessIoPortHandlers] = PSP_INFO_WRONG_DIRECTION,
    [ProcessPooledUsageAndLimits] = PSP_INFO_EXACT(sizeof(POOLED_USAGE_AND_LIMITS)),
    [ProcessWorkingSetWatch] = PSP_INFO_MINIMUM(sizeof(PROCESS_WS_WATCH_INFORMATION)),
    [ProcessUserModeIOPL] = PSP_INFO_WRONG_DIRECTION,
    [ProcessEnableAlignmentFaultFixup] = PSP_INFO_WRONG_DIRECTION,
    [ProcessPriorityClass] = PSP_INFO_EXACT_ALIGNED(sizeof(PROCESS_PRIORITY_CLASS), sizeof(UCHAR)),
    [ProcessWx86Information] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessHandleCount] = PSP_INFO_MINIMUM(sizeof(ULONG)),
    [ProcessAffinityMask] = PSP_INFO_EXACT(sizeof(KAFFINITY)),
    [ProcessPriorityBoost] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessDeviceMap] = PSP_INFO_EXACT_OR(RTL_FIELD_SIZE(PROCESS_DEVICEMAP_INFORMATION, Query),
                                           sizeof(PROCESS_DEVICEMAP_INFORMATION_EX)),
    [ProcessSessionInformation] = PSP_INFO_EXACT(sizeof(PROCESS_SESSION_INFORMATION)),
    [ProcessForegroundInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessWow64Information] = PSP_INFO_EXACT(sizeof(ULONG_PTR)),
    [ProcessImageFileName] = PSP_INFO_GUARDED(PspInformationAnyLength, 0),
    [ProcessLUIDDeviceMapsEnabled] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessBreakOnTermination] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessDebugObjectHandle] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ProcessDebugFlags] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessHandleTracing] = PSP_INFO_GUARDED(PspInformationAnyLength, 0),
    [ProcessIoPriority] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessExecuteFlags] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessTlsInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessCookie] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessImageInformation] = PSP_INFO_EXACT(sizeof(SECTION_IMAGE_INFORMATION)),
    [ProcessCycleTime] = PSP_INFO_EXACT(sizeof(PROCESS_CYCLE_TIME_INFORMATION)),
    [ProcessPagePriority] = PSP_INFO_EXACT(sizeof(PAGE_PRIORITY_INFORMATION)),
    [ProcessInstrumentationCallback] = PSP_INFO_WRONG_DIRECTION,
    [ProcessThreadStackAllocation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessImageFileNameWin32] = PSP_INFO_GUARDED(PspInformationAnyLength, 0),
    [ProcessAffinityUpdateMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessMemoryAllocationMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessGroupInformation] = PSP_INFO_MINIMUM_ALIGNED(sizeof(USHORT), sizeof(USHORT)),
    [ProcessTokenVirtualizationEnabled] = PSP_INFO_WRONG_DIRECTION,
    [ProcessMitigationPolicy] = PSP_INFO_MINIMUM(sizeof(ULONG) * 2),
    [ProcessDynamicFunctionTableInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessHandleCheckingMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessRevokeFileHandles] = PSP_INFO_WRONG_DIRECTION,
    [ProcessWorkingSetControl] = PSP_INFO_WRONG_DIRECTION,
    [ProcessHandleTable] = PSP_INFO_MINIMUM(sizeof(ULONG)),
    [ProcessCheckStackExtentsMode] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessCommandLineInformation] = PSP_INFO_GUARDED(PspInformationAnyLength, 0),
    [ProcessProtectionInformation] = PSP_INFO_EXACT_ALIGNED(sizeof(PS_PROTECTION), sizeof(UCHAR)),
    [ProcessMemoryExhaustion] = PSP_INFO_WRONG_DIRECTION,
    [ProcessFaultInformation] = PSP_INFO_WRONG_DIRECTION,
    [ProcessSubsystemInformation] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ProcessPowerThrottlingState] = PSP_INFO_EXACT(sizeof(PROCESS_POWER_THROTTLING_STATE)),
    [ProcessManageWritesToExecutableMemory] = PSP_INFO_WRONG_DIRECTION,
};

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Checks the arguments the way every information class does before the
 * object is looked up: where the buffer is and how it is aligned first,
 * without reading or writing it, then whether the class goes this way and
 * whether the length is one it accepts.
 */
NTSTATUS
NTAPI
PspCheckInformationClass(
    _In_ const PSP_INFORMATION_CLASS *Class,
    _In_reads_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Alignment = (Class->Alignment != 0) ? Class->Alignment : sizeof(ULONG);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Buffer, Length, Alignment);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    switch (Class->Rule)
    {
        case PspInformationWrongDirection:
            return STATUS_INVALID_INFO_CLASS;

        case PspInformationNotImplemented:
            return STATUS_NOT_IMPLEMENTED;

        case PspInformationExactLength:
            if ((Length != Class->Size) &&
                ((Class->AlternateSize == 0) || (Length != Class->AlternateSize)))
            {
                return STATUS_INFO_LENGTH_MISMATCH;
            }
            break;

        case PspInformationMinimumLength:
            if (Length < Class->Size) return STATUS_INFO_LENGTH_MISMATCH;
            break;

        case PspInformationMultipleLength:
            if ((Length % Class->Size) != 0) return STATUS_INFO_LENGTH_MISMATCH;
            break;

        default:
            break;
    }

    return STATUS_SUCCESS;
}

/*
 * Makes the caller's buffer writable once the object is known, so that the
 * answer can be copied into it without faulting. A class whose handler guards
 * its own writes is left alone: it may refuse before writing anything.
 */
NTSTATUS
NTAPI
PspProbeInformationForWrite(
    _In_ const PSP_INFORMATION_CLASS *Class,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Alignment = (Class->Alignment != 0) ? Class->Alignment : sizeof(ULONG);

    if ((PreviousMode == KernelMode) || (Length == 0) ||
        ((Class->Flags & PSP_INFORMATION_GUARDED_WRITE) != 0))
    {
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWrite(Buffer, Length, Alignment);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * Copies an answer out to the caller and says how long it is. A caller that
 * passed too little room is told what it needs and nothing is written, which
 * is how a caller learns the length of a variable-sized answer.
 */
static
NTSTATUS
PspReturnInformation(
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_reads_bytes_(DataLength) const VOID *Data,
    _In_ ULONG DataLength,
    _Out_ PULONG ReturnLength)
{
    *ReturnLength = DataLength;

    if (Length < DataLength) return STATUS_INFO_LENGTH_MISMATCH;

    RtlCopyMemory(Buffer, Data, DataLength);
    return STATUS_SUCCESS;
}

/* The extended device map query carries its flags in, behind the answer */
static
NTSTATUS
PspReadDeviceMapFlags(
    _In_ PVOID Buffer,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PULONG Flags)
{
    PPROCESS_DEVICEMAP_INFORMATION_EX Extended = Buffer;

    if (PreviousMode == KernelMode)
    {
        *Flags = Extended->Flags;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        *Flags = Extended->Flags;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * Writes a counted string whose characters follow the structure itself, which
 * is the shape every path and name class uses.
 */
static
NTSTATUS
PspReturnString(
    _In_ PCUNICODE_STRING String,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PUNICODE_STRING Out = Buffer;
    ULONG Needed;

    Needed = sizeof(UNICODE_STRING) + String->Length + sizeof(UNICODE_NULL);
    *ReturnLength = Needed;

    if (Length < Needed) return STATUS_INFO_LENGTH_MISMATCH;

    /* The caller's buffer is not touched before this, so a bad one faults here */
    _SEH2_TRY
    {
        Out->Buffer = (PWCHAR)((PUCHAR)Buffer + sizeof(UNICODE_STRING));
        Out->Length = String->Length;
        Out->MaximumLength = (USHORT)(String->Length + sizeof(UNICODE_NULL));

        if (String->Length != 0)
            RtlCopyMemory(Out->Buffer, String->Buffer, String->Length);

        Out->Buffer[String->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * The image the process was started from. The native path is what the kernel
 * holds; a caller that wants the form a user sees asks for the other class,
 * and the leading device is turned into the drive letter it is reached by.
 */
static
NTSTATUS
PspQueryImageFileName(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Win32Form,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    POBJECT_NAME_INFORMATION Name;
    UNICODE_STRING Empty;
    UNICODE_STRING DosName;
    UNICODE_STRING Combined;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    Name = Process->SeAuditProcessCreationInfo.ImageFileName;
    if (Name == NULL || Name->Name.Buffer == NULL)
    {
        RtlInitEmptyUnicodeString(&Empty, NULL, 0);
        return PspReturnString(&Empty, Buffer, Length, ReturnLength);
    }

    if (!Win32Form)
        return PspReturnString(&Name->Name, Buffer, Length, ReturnLength);

    /*
     * The drive letter is not something the kernel keeps: it is asked of the
     * volume the image lives on, so a process whose file is gone falls back
     * to the native path rather than failing.
     */
    Status = PsReferenceProcessFilePointer(Process, &FileObject);
    if (!NT_SUCCESS(Status))
        return PspReturnString(&Name->Name, Buffer, Length, ReturnLength);

    Status = IoVolumeDeviceToDosName(FileObject->DeviceObject, &DosName);
    ObDereferenceObject(FileObject);

    if (!NT_SUCCESS(Status))
        return PspReturnString(&Name->Name, Buffer, Length, ReturnLength);

    /*
     * The native path begins with the device the volume is reached by, and
     * that part is what the drive letter replaces.
     */
    Combined.Length = 0;
    Combined.MaximumLength = DosName.Length + Name->Name.Length;
    Combined.Buffer = ExAllocatePoolWithTag(PagedPool,
                                            Combined.MaximumLength,
                                            TAG_SEPA);
    if (Combined.Buffer == NULL)
    {
        ExFreePool(DosName.Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeStringToString(&Combined, &DosName);

    {
        UNICODE_STRING Tail = Name->Name;
        ULONG Index = 0;
        ULONG Slashes = 0;

        /*
         * The device part of a native path is its first three components,
         * as in \Device\HarddiskVolume1, and the rest is the path on it.
         */
        while (Index < Tail.Length / sizeof(WCHAR) && Slashes < 3)
        {
            if (Tail.Buffer[Index] == L'\\') Slashes++;
            if (Slashes == 3) break;
            Index++;
        }

        if (Slashes == 3)
        {
            Tail.Buffer += Index;
            Tail.Length -= (USHORT)(Index * sizeof(WCHAR));
            Tail.MaximumLength = Tail.Length;
        }

        RtlAppendUnicodeStringToString(&Combined, &Tail);
    }

    ExFreePool(DosName.Buffer);

    Status = PspReturnString(&Combined, Buffer, Length, ReturnLength);
    ExFreePoolWithTag(Combined.Buffer, TAG_SEPA);

    return Status;
}

/*
 * The command line the process was started with. It lives in the process's
 * own memory, so it is read with the process attached and under a guard: the
 * process may have freed or overwritten it.
 */
static
NTSTATUS
PspQueryCommandLine(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    UNICODE_STRING Captured;
    KAPC_STATE ApcState;
    PPEB Peb = Process->Peb;
    NTSTATUS Status = STATUS_SUCCESS;
    PWCHAR Copy = NULL;
    USHORT CopyLength = 0;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&Captured, NULL, 0);

    if (Peb == NULL) return STATUS_UNSUCCESSFUL;

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        PRTL_USER_PROCESS_PARAMETERS Parameters = Peb->ProcessParameters;

        if (Parameters != NULL &&
            Parameters->CommandLine.Length != 0 &&
            Parameters->CommandLine.Buffer != NULL)
        {
            CopyLength = Parameters->CommandLine.Length;

            Copy = ExAllocatePoolWithTag(PagedPool, CopyLength, TAG_SEPA);
            if (Copy != NULL)
            {
                RtlCopyMemory(Copy,
                              Parameters->CommandLine.Buffer,
                              CopyLength);
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);

    if (!NT_SUCCESS(Status))
    {
        if (Copy != NULL) ExFreePoolWithTag(Copy, TAG_SEPA);
        return Status;
    }

    Captured.Buffer = Copy;
    Captured.Length = CopyLength;
    Captured.MaximumLength = CopyLength;

    Status = PspReturnString(&Captured, Buffer, Length, ReturnLength);

    if (Copy != NULL) ExFreePoolWithTag(Copy, TAG_SEPA);

    return Status;
}

/*
 * Fills in what identifies this run of the process. The names come after the
 * fixed part, so a caller has to ask twice: once for the length and once
 * with room for all of it.
 */
static
NTSTATUS
PspQueryTelemetryId(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PSP_TELEMETRY_ID_INFORMATION Header;
    POBJECT_NAME_INFORMATION ImageName;
    PACCESS_TOKEN Token;
    UNICODE_STRING ImagePath;
    PUCHAR Cursor;
    ULONG SidLength = 0;
    ULONG PathLength;
    ULONG Needed;
    PSID UserSid = NULL;

    PAGED_CODE();

    RtlZeroMemory(&Header, sizeof(Header));
    Header.HeaderSize = sizeof(Header);
    Header.ProcessId = HandleToUlong(Process->UniqueProcessId);
    Header.ProcessSequenceNumber = Process->SequenceNumber;
    Header.ProcessStartKey = PsGetProcessStartKey(Process);
    Header.CreateTime = (ULONGLONG)Process->CreateTime.QuadPart;
    Header.CreateInterruptTime = Process->CreateInterruptTime;
    Header.CreateUnbiasedInterruptTime = Process->CreateUnbiasedInterruptTime;
    Header.SessionId = MmGetSessionId(Process);
    Header.BootId = SharedUserData->BootId;

    ImageName = Process->SeAuditProcessCreationInfo.ImageFileName;
    if (ImageName != NULL) ImagePath = ImageName->Name;
    else RtlInitEmptyUnicodeString(&ImagePath, NULL, 0);

    /*
     * The identity reported is the one the process runs as, which is what
     * tells two runs of the same image by two users apart.
     */
    Token = PsReferencePrimaryToken(Process);
    if (Token != NULL)
    {
        LUID Logon;

        if (NT_SUCCESS(SeQueryAuthenticationIdToken(Token, &Logon)))
        {
            Header.SessionCreateTime = ((ULONGLONG)Logon.HighPart << 32) |
                                       Logon.LowPart;
        }

        UserSid = ((PTOKEN)Token)->UserAndGroups[0].Sid;
        if (UserSid != NULL) SidLength = RtlLengthSid(UserSid);
    }

    PathLength = ImagePath.Length + sizeof(UNICODE_NULL);

    /*
     * The three names follow the identity. Only the image path is known
     * here, so the other two are reported as empty strings that still have
     * offsets of their own, which is what a caller walks.
     */
    Header.UserSidOffset = sizeof(Header);
    Header.ImagePathOffset = Header.UserSidOffset + SidLength;
    Header.PackageNameOffset = Header.ImagePathOffset + PathLength;
    Header.RelativeAppNameOffset = Header.PackageNameOffset +
                                   sizeof(UNICODE_NULL);
    Header.CommandLineOffset = Header.RelativeAppNameOffset +
                               sizeof(UNICODE_NULL);

    Needed = Header.CommandLineOffset + sizeof(UNICODE_NULL);
    *ReturnLength = Needed;

    if (Length < Needed)
    {
        /*
         * A caller with room for the fixed part is given it, so that it can
         * read the length out of the answer rather than guessing. Nothing
         * past the fixed part is described, which is what the offsets of
         * zero say.
         */
        if (Length >= sizeof(Header))
        {
            Header.UserSidOffset = 0;
            Header.ImagePathOffset = 0;
            Header.PackageNameOffset = 0;
            Header.RelativeAppNameOffset = 0;
            Header.CommandLineOffset = 0;
            RtlCopyMemory(Buffer, &Header, sizeof(Header));

            if (Token != NULL) PsDereferencePrimaryToken(Token);
            return STATUS_BUFFER_OVERFLOW;
        }

        if (Token != NULL) PsDereferencePrimaryToken(Token);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Buffer, Needed);
    RtlCopyMemory(Buffer, &Header, sizeof(Header));

    Cursor = (PUCHAR)Buffer;

    if (SidLength != 0)
        RtlCopyMemory(Cursor + Header.UserSidOffset, UserSid, SidLength);

    if (ImagePath.Length != 0)
    {
        RtlCopyMemory(Cursor + Header.ImagePathOffset,
                      ImagePath.Buffer,
                      ImagePath.Length);
    }

    if (Token != NULL) PsDereferencePrimaryToken(Token);

    return STATUS_SUCCESS;
}

/*
 * Called for each handle the process holds. The walk stops filling once the
 * caller's room runs out but keeps counting, so the caller learns how many
 * there were.
 */
static
BOOLEAN
NTAPI
PspCountHandle(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_ PVOID Context)
{
    PPSP_HANDLE_WALK Walk = Context;

    UNREFERENCED_PARAMETER(HandleTableEntry);

    if (Walk->Count < Walk->Room) Walk->Handles[Walk->Count] = Handle;

    Walk->Count++;
    return FALSE;
}

/*
 * The handles a process holds, as their 32-bit values alone. A caller uses
 * this to find what a process has open without having to read its handle
 * table, and a short buffer is given as many as fit.
 */
static
NTSTATUS
PspQueryHandleTable(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PSP_HANDLE_WALK Walk;
    PHANDLE_TABLE Table;
    PHANDLE Captured;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;
    ULONG Room;

    PAGED_CODE();

    *ReturnLength = 0;

    Room = ObGetProcessHandleCount(Process) + 16;
    if (Room > PSP_MAX_REPORTED_HANDLES) Room = PSP_MAX_REPORTED_HANDLES;

    Captured = ExAllocatePoolZero(PagedPool,
                                  Room * sizeof(HANDLE),
                                  TAG_SEPA);
    if (Captured == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Walk.Handles = Captured;
    Walk.Room = Room;
    Walk.Count = 0;

    Table = ObReferenceProcessHandleTable(Process);
    if (Table == NULL)
    {
        ExFreePoolWithTag(Captured, TAG_SEPA);
        return STATUS_PROCESS_IS_TERMINATING;
    }

    ExEnumHandleTable(Table, PspCountHandle, &Walk, NULL);
    ObDereferenceProcessHandleTable(Process);

    if (Walk.Count > Room) Walk.Count = Room;
    if (Walk.Count > Length / sizeof(ULONG)) Walk.Count = Length / sizeof(ULONG);

    _SEH2_TRY
    {
        for (Index = 0; Index < Walk.Count; Index++)
            ((PULONG)Buffer)[Index] = HandleToUlong(Captured[Index]);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExFreePoolWithTag(Captured, TAG_SEPA);

    if (NT_SUCCESS(Status)) *ReturnLength = Walk.Count * (ULONG)sizeof(ULONG);

    return Status;
}

/*
 * Which processor groups the process may run in. This system has one group,
 * so the answer is that group and nothing else.
 */
static
NTSTATUS
PspQueryGroupInformation(
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    USHORT Group = 0;

    return PspReturnInformation(Buffer,
                                Length,
                                &Group,
                                sizeof(Group),
                                ReturnLength);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtQueryInformationProcess(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS ProcessInformationClass,
    _Out_writes_bytes_(ProcessInformationLength) PVOID ProcessInformation,
    _In_ ULONG ProcessInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Access = PROCESS_QUERY_INFORMATION;
    PROCESS_EXTENDED_BASIC_INFORMATION Extended;
    PROCESS_DEVICEMAP_INFORMATION DeviceMap;
    PROCESS_BASIC_INFORMATION Basic;
    PROCESS_SESSION_INFORMATION Session;
    PROCESS_PRIORITY_CLASS PriorityClass;
    PROCESS_CYCLE_TIME_INFORMATION Cycles;
    PROCESS_POWER_THROTTLING_STATE Throttling;
    POOLED_USAGE_AND_LIMITS Pooled;
    KERNEL_USER_TIMES Times;
    VM_COUNTERS_EX2 Counters;
    IO_COUNTERS Io;
    PVOID Local = NULL;
    ULONG LocalLength;
    ULONG Length = 0;
    ULONG Value;
    ULONG_PTR Pointer;
    PEPROCESS Process;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (ProcessInformationClass >= MaxProcessInfoClass)
        return STATUS_INVALID_INFO_CLASS;

    Status = PspCheckInformationClass(&PspQueryProcessClasses[ProcessInformationClass],
                                      ProcessInformation,
                                      ProcessInformationLength,
                                      PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    if ((PreviousMode != KernelMode) && (ReturnLength != NULL))
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /*
     * A few of the classes say only what the process is, not what it is
     * doing, so they are open to a caller that may ask no more than that.
     */
    if (ProcessInformationClass == ProcessBasicInformation ||
        ProcessInformationClass == ProcessTimes ||
        ProcessInformationClass == ProcessImageFileName ||
        ProcessInformationClass == ProcessImageFileNameWin32 ||
        ProcessInformationClass == ProcessSessionInformation ||
        ProcessInformationClass == ProcessPriorityClass ||
        ProcessInformationClass == ProcessIoCounters ||
        ProcessInformationClass == ProcessVmCounters ||
        ProcessInformationClass == ProcessAffinityUpdateMode ||
        ProcessInformationClass == ProcessDefaultCpuSetsInformation ||
        ProcessInformationClass == ProcessCycleTime ||
        ProcessInformationClass == ProcessGroupInformation ||
        ProcessInformationClass == ProcessPagePriority ||
        ProcessInformationClass == ProcessSubsystemProcess ||
        ProcessInformationClass == ProcessSubsystemInformation ||
        ProcessInformationClass == ProcessProtectionInformation ||
        ProcessInformationClass == ProcessTelemetryIdInformation ||
        ProcessInformationClass == ProcessCommandLineInformation ||
        ProcessInformationClass == ProcessPowerThrottlingState ||
        ProcessInformationClass == ProcessInPrivate ||
        ProcessInformationClass == ProcessWow64Information)
    {
        Access = PROCESS_QUERY_LIMITED_INFORMATION;
    }

    /* The list of handles is what a duplicator of them needs */
    if (ProcessInformationClass == ProcessHandleTable)
        Access = PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE;

    /* The cookie is a process's own secret, so only it may read it */
    if (ProcessInformationClass == ProcessCookie &&
        ProcessHandle != NtCurrentProcess())
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       Access,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PspProbeInformationForWrite(&PspQueryProcessClasses[ProcessInformationClass],
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    switch (ProcessInformationClass)
    {
        case ProcessBasicInformation:
            Basic.ExitStatus = Process->ExitStatus;
            Basic.PebBaseAddress = Process->Peb;
            Basic.AffinityMask = Process->Pcb.Affinity;
            Basic.BasePriority = Process->Pcb.BasePriority;
            Basic.UniqueProcessId = (ULONG_PTR)Process->UniqueProcessId;
            Basic.InheritedFromUniqueProcessId =
                (ULONG_PTR)Process->InheritedFromUniqueProcessId;

            /*
             * The extended form is told apart by its length, and it starts
             * with its own size so that a caller knows what it was given.
             */
            if (ProcessInformationLength == sizeof(Extended))
            {
                RtlZeroMemory(&Extended, sizeof(Extended));
                Extended.Size = sizeof(Extended);
                Extended.BasicInfo = Basic;
                Extended.IsProtectedProcess =
                    PsIsProtectedProcess(Process) ? 1 : 0;
                Extended.IsWow64Process =
                    (Process->WoW64Process != NULL) ? 1 : 0;
                Extended.IsProcessDeleting = Process->ProcessDelete;
                Extended.IsCrossSessionCreate = Process->CrossSessionCreate;

                Status = PspReturnInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              &Extended,
                                              sizeof(Extended),
                                              &Length);
                break;
            }

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Basic,
                                          sizeof(Basic),
                                          &Length);
            break;

        case ProcessQuotaLimits:
            Status = PspQueryQuotaLimits(Process,
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         &Length);
            break;

        case ProcessIoCounters:
            Io.ReadOperationCount =
                (ULONGLONG)Process->ReadOperationCount.QuadPart;
            Io.WriteOperationCount =
                (ULONGLONG)Process->WriteOperationCount.QuadPart;
            Io.OtherOperationCount =
                (ULONGLONG)Process->OtherOperationCount.QuadPart;
            Io.ReadTransferCount =
                (ULONGLONG)Process->ReadTransferCount.QuadPart;
            Io.WriteTransferCount =
                (ULONGLONG)Process->WriteTransferCount.QuadPart;
            Io.OtherTransferCount =
                (ULONGLONG)Process->OtherTransferCount.QuadPart;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Io,
                                          sizeof(Io),
                                          &Length);
            break;

        case ProcessVmCounters:
            RtlZeroMemory(&Counters, sizeof(Counters));

            Counters.CountersEx.PeakVirtualSize = Process->PeakVirtualSize;
            Counters.CountersEx.VirtualSize = Process->VirtualSize;
            Counters.CountersEx.PageFaultCount =
                Process->Vm.Instance.PageFaultCount;
            Counters.CountersEx.PeakWorkingSetSize =
                Process->Vm.Instance.PeakWorkingSetSize;
            Counters.CountersEx.WorkingSetSize =
                Process->Vm.Instance.WorkingSetSize;
            Counters.CountersEx.QuotaPeakPagedPoolUsage =
                Process->ProcessQuotaPeak[1];
            Counters.CountersEx.QuotaPagedPoolUsage =
                Process->ProcessQuotaUsage[1];
            Counters.CountersEx.QuotaPeakNonPagedPoolUsage =
                Process->ProcessQuotaPeak[0];
            Counters.CountersEx.QuotaNonPagedPoolUsage =
                Process->ProcessQuotaUsage[0];
            Counters.CountersEx.PagefileUsage =
                Process->CommitCharge << PAGE_SHIFT;
            Counters.CountersEx.PeakPagefileUsage =
                Process->CommitChargePeak << PAGE_SHIFT;
            Counters.CountersEx.PrivateUsage =
                Counters.CountersEx.PagefileUsage;
            Counters.PrivateWorkingSetSize =
                Process->Vm.Instance.WorkingSetPrivateSize;
            Counters.SharedCommitUsage = Process->SharedCommitCharge;

            /*
             * Three lengths are accepted, each adding to the one before it,
             * and the caller's length is what picks which one it gets.
             */
            if (ProcessInformationLength >= sizeof(VM_COUNTERS_EX2))
                LocalLength = sizeof(VM_COUNTERS_EX2);
            else if (ProcessInformationLength >= sizeof(VM_COUNTERS_EX))
                LocalLength = sizeof(VM_COUNTERS_EX);
            else
                LocalLength = sizeof(VM_COUNTERS);

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Counters,
                                          LocalLength,
                                          &Length);
            break;

        case ProcessTimes:
            Times.CreateTime = Process->CreateTime;
            Times.ExitTime = Process->ExitTime;
            Times.KernelTime.QuadPart =
                (LONGLONG)Process->Pcb.KernelTime * KeMaximumIncrement;
            Times.UserTime.QuadPart =
                (LONGLONG)Process->Pcb.UserTime * KeMaximumIncrement;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Times,
                                          sizeof(Times),
                                          &Length);
            break;

        case ProcessDebugPort:
            /*
             * The port itself is never handed out: a caller is told only
             * whether there is one, because the object behind it is the
             * debugger's and not this caller's.
             */
            Pointer = (Process->DebugPort != NULL) ? (ULONG_PTR)-1 : 0;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Pointer,
                                          sizeof(Pointer),
                                          &Length);
            break;

        case ProcessDebugObjectHandle:
        {
            NTSTATUS OpenStatus = DbgkOpenProcessDebugPort(Process, PreviousMode, &Handle);

            /* A caller that gets no debug object is still handed a NULL handle */
            if (!NT_SUCCESS(OpenStatus)) Handle = NULL;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Handle,
                                          sizeof(Handle),
                                          &Length);
            if (NT_SUCCESS(Status)) Status = OpenStatus;
            break;
        }

        case ProcessDebugFlags:
            Value = Process->NoDebugInherit ? 0 : PSP_DEBUG_INHERIT;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessHandleCount:
            Value = ObGetProcessHandleCount(Process);

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessHandleTable:
            Status = PspQueryHandleTable(Process,
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         &Length);
            break;

        case ProcessHandleTracing:
            Status = PspQueryHandleTracing(Process,
                                           ProcessInformation,
                                           ProcessInformationLength,
                                           &Length);
            break;

        case ProcessSessionInformation:
            Session.SessionId = MmGetSessionId(Process);

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Session,
                                          sizeof(Session),
                                          &Length);
            break;

        case ProcessWow64Information:
            Pointer = (ULONG_PTR)PsGetProcessPeb32(Process);

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Pointer,
                                          sizeof(Pointer),
                                          &Length);
            break;

        case ProcessImageFileName:
        case ProcessImageFileNameWin32:
            Status = PspQueryImageFileName(
                         Process,
                         (BOOLEAN)(ProcessInformationClass ==
                                   ProcessImageFileNameWin32),
                         ProcessInformation,
                         ProcessInformationLength,
                         &Length);
            break;

        case ProcessCommandLineInformation:
            Status = PspQueryCommandLine(Process,
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         &Length);
            break;

        case ProcessPriorityClass:
            PriorityClass.Foreground = FALSE;
            PriorityClass.PriorityClass = Process->PriorityClass;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &PriorityClass,
                                          sizeof(PriorityClass),
                                          &Length);
            break;

        case ProcessPriorityBoost:
            Value = Process->Pcb.DisableBoost ? 0 : 1;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessAffinityMask:
            Pointer = Process->Pcb.Affinity;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Pointer,
                                          sizeof(Pointer),
                                          &Length);
            break;

        case ProcessGroupInformation:
            Status = PspQueryGroupInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              &Length);
            break;

        case ProcessDefaultHardErrorMode:
            Value = Process->DefaultHardErrorProcessing;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessBreakOnTermination:
            Value = Process->BreakOnTermination;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessCookie:
            /*
             * The cookie is made the first time it is asked for and never
             * changes after, so two readers of one process agree.
             */
            if (Process->Cookie == 0)
            {
                LARGE_INTEGER Now;

                KeQueryTickCount(&Now);
                Value = Now.LowPart ^ (ULONG)(ULONG_PTR)Process;
                if (Value == 0) Value = 1;

                InterlockedCompareExchange((PLONG)&Process->Cookie,
                                           (LONG)Value,
                                           0);
            }

            Value = Process->Cookie;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessCycleTime:
            Cycles.AccumulatedCycles =
                KeQueryTotalCycleTimeProcess(&Process->Pcb,
                                             &Cycles.CurrentCycleCount);

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Cycles,
                                          sizeof(Cycles),
                                          &Length);
            break;

        case ProcessPooledUsageAndLimits:
            RtlZeroMemory(&Pooled, sizeof(Pooled));

            /* FIXME: the memory manager does not charge the block's pagefile account, so the commit charge stands in for it */
            Pooled.PagefileUsage = Process->CommitCharge << PAGE_SHIFT;
            Pooled.PeakPagefileUsage = Process->CommitChargePeak << PAGE_SHIFT;

            if (Process->QuotaBlock != NULL)
            {
                Pooled.PagedPoolUsage =
                    Process->QuotaBlock->QuotaEntry[PsPagedPool].Usage;
                Pooled.PeakPagedPoolUsage =
                    Process->QuotaBlock->QuotaEntry[PsPagedPool].Peak;
                Pooled.NonPagedPoolUsage =
                    Process->QuotaBlock->QuotaEntry[PsNonPagedPool].Usage;
                Pooled.PeakNonPagedPoolUsage =
                    Process->QuotaBlock->QuotaEntry[PsNonPagedPool].Peak;
                Pooled.PagedPoolLimit =
                    Process->QuotaBlock->QuotaEntry[PsPagedPool].Limit;
                Pooled.NonPagedPoolLimit =
                    Process->QuotaBlock->QuotaEntry[PsNonPagedPool].Limit;
                Pooled.PagefileLimit =
                    Process->QuotaBlock->QuotaEntry[PsPageFile].Limit;
            }

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Pooled,
                                          sizeof(Pooled),
                                          &Length);
            break;

        case ProcessWorkingSetWatch:
        case ProcessWorkingSetWatchEx:
            Status = PspQueryWorkingSetWatch(
                         Process,
                         ProcessInformation,
                         ProcessInformationLength,
                         &Length,
                         (BOOLEAN)(ProcessInformationClass ==
                                   ProcessWorkingSetWatchEx));
            break;

        case ProcessExecuteFlags:
            if (Process != PsGetCurrentProcess())
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = MmGetExecuteOptions(&Value);
            if (NT_SUCCESS(Status))
            {
                Status = PspReturnInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              &Value,
                                              sizeof(Value),
                                              &Length);
            }
            break;

        case ProcessImageInformation:
            /* The image of a process that has exited is no longer answered for */
            if (!ExAcquireRundownProtection(&Process->RundownProtect))
            {
                Status = STATUS_PROCESS_IS_TERMINATING;
                break;
            }

            if (Process->SectionObject == NULL)
            {
                ExReleaseRundownProtection(&Process->RundownProtect);
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            LocalLength = sizeof(SECTION_IMAGE_INFORMATION);
            Local = ExAllocatePoolZero(PagedPool, LocalLength, TAG_SEPA);
            if (Local == NULL)
            {
                ExReleaseRundownProtection(&Process->RundownProtect);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Status = MmGetSectionImageInformation(Process->SectionObject,
                                                  Local);
            ExReleaseRundownProtection(&Process->RundownProtect);
            if (NT_SUCCESS(Status))
            {
                Status = PspReturnInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              Local,
                                              LocalLength,
                                              &Length);
            }

            ExFreePoolWithTag(Local, TAG_SEPA);
            break;

        case ProcessDeviceMap:
            /*
             * The two shapes differ only in their length: the longer one
             * also says which identity the map was looked up under.
             */
            RtlZeroMemory(&DeviceMap, sizeof(DeviceMap));
            Value = 0;

            if (ProcessInformationLength == sizeof(PROCESS_DEVICEMAP_INFORMATION_EX))
            {
                Length = sizeof(PROCESS_DEVICEMAP_INFORMATION_EX);
                Status = PspReadDeviceMapFlags(ProcessInformation, PreviousMode, &Value);
                if (!NT_SUCCESS(Status)) break;

                if ((Value & ~PROCESS_LUID_DOSDEVICES_ONLY) != 0)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
            }
            else if (ProcessInformationLength != sizeof(DeviceMap.Query))
            {
                Length = sizeof(DeviceMap.Query);
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = ObQueryDeviceMapInformation(Process, &DeviceMap, Value);
            if (NT_SUCCESS(Status))
            {
                Status = PspReturnInformation(ProcessInformation,
                                              ProcessInformationLength,
                                              &DeviceMap.Query,
                                              sizeof(DeviceMap.Query),
                                              &Length);
                if (ProcessInformationLength == sizeof(PROCESS_DEVICEMAP_INFORMATION_EX))
                    Length = sizeof(PROCESS_DEVICEMAP_INFORMATION_EX);
            }
            break;

        case ProcessLUIDDeviceMapsEnabled:
            Value = ObIsLUIDDeviceMapsEnabled();

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessIoPriority:
            Value = Process->DefaultIoPriority;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessPagePriority:
            Value = Process->DefaultPagePriority;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessAffinityUpdateMode:
            /*
             * Whether the process follows the system when processors come
             * and go, or keeps the affinity it was given.
             */
            Value = Process->AffinityUpdateEnable ? 1 : 0;
            if (Process->AffinityPermanent) Value |= 2;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessMemoryAllocationMode:
            Value = Process->VmTopDown ? 1 : 0;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessCheckStackExtentsMode:
            Value = Process->Pcb.CheckStackExtents ? 1 : 0;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessMitigationPolicy:
            /*
             * The caller names the policy in the first word and is given its
             * flags in the second, so the shortest answer is two words.
             */
            if (ProcessInformationLength < sizeof(ULONG) * 2)
            {
                Length = sizeof(ULONG) * 2;
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Value = *(PULONG)ProcessInformation;
            if (Value >= PSP_EXTENDED_POLICY_COUNT)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Value = PsGetProcessMitigationPolicyFlags(Process, Value);
            ((PULONG)ProcessInformation)[1] = Value;
            Length = sizeof(ULONG) * 2;
            Status = STATUS_SUCCESS;
            break;

        case ProcessSubsystemProcess:
            Value = Process->SubsystemProcess;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessSubsystemInformation:
            Value = Process->PicoCreated ? SubsystemInformationTypeWSL :
                                           SubsystemInformationTypeWin32;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessProtectionInformation:
            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Process->Protection,
                                          sizeof(PS_PROTECTION),
                                          &Length);
            break;

        case ProcessTelemetryIdInformation:
            Status = PspQueryTelemetryId(Process,
                                         ProcessInformation,
                                         ProcessInformationLength,
                                         &Length);
            break;

        case ProcessInPrivate:
            Value = Process->InPrivate;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(BOOLEAN),
                                          &Length);
            break;

        case ProcessHandleCheckingMode:
            Value = ObpIsStrictHandleCheckingEnabled() ? 1 : 0;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessHighGraphicsPriorityInformation:
            Value = Process->HighGraphicsPriority;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(BOOLEAN),
                                          &Length);
            break;

        case ProcessWin32kSyscallFilterInformation:
            Value = Process->Win32KFilterSet;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        case ProcessDisableSystemAllowedCpuSets:
            Value = Process->DisableSystemAllowedCpuSet;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(BOOLEAN),
                                          &Length);
            break;

        case ProcessDefaultCpuSetsInformation:
        case ProcessAllowedCpuSetsInformation:
            Status = PspQueryProcessCpuSets(
                         Process,
                         (BOOLEAN)(ProcessInformationClass ==
                                   ProcessAllowedCpuSetsInformation),
                         ProcessInformation,
                         ProcessInformationLength,
                         &Length);
            break;

        case ProcessPowerThrottlingState:
        {
            PPO_PROCESS_ENERGY_CONTEXT Energy;

            /* The caller says which version of the record it is asking for */
            _SEH2_TRY
            {
                Value = ((PROCESS_POWER_THROTTLING_STATE *)ProcessInformation)->Version;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            if (!NT_SUCCESS(Status)) break;

            if (Value != PROCESS_POWER_THROTTLING_CURRENT_VERSION)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Energy = PspGetEnergyContext(Process);

            RtlZeroMemory(&Throttling, sizeof(Throttling));
            Throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

            if (Energy != NULL)
            {
                Throttling.ControlMask =
                    (ULONG)Energy->PowerThrottlingControlMask;
                Throttling.StateMask = (ULONG)Energy->PowerThrottlingStateMask;
            }

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Throttling,
                                          sizeof(Throttling),
                                          &Length);
            break;
        }

        case ProcessEnergyValues:
        {
            PPO_PROCESS_ENERGY_CONTEXT Energy = PspGetEnergyContext(Process);

            if (Energy == NULL)
            {
                Status = STATUS_NOT_FOUND;
                break;
            }

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Energy->Values,
                                          sizeof(Energy->Values),
                                          &Length);
            break;
        }

        case ProcessWakeInformation:
            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Process->WakeInfo,
                                          sizeof(PS_PROCESS_WAKE_INFORMATION),
                                          &Length);
            break;

        case ProcessWx86Information:
            Value = Process->VdmAllowed;

            Status = PspReturnInformation(ProcessInformation,
                                          ProcessInformationLength,
                                          &Value,
                                          sizeof(Value),
                                          &Length);
            break;

        /*
         * The classes below are written and never read. A caller that asks
         * for one is told the class is wrong rather than being given a
         * made-up answer.
         */
        case ProcessBasePriority:
        case ProcessRaisePriority:
        case ProcessExceptionPort:
        case ProcessAccessToken:
        case ProcessLdtSize:
        case ProcessIoPortHandlers:
        case ProcessUserModeIOPL:
        case ProcessEnableAlignmentFaultFixup:
        case ProcessForegroundInformation:
        case ProcessTlsInformation:
        case ProcessInstrumentationCallback:
        case ProcessThreadStackAllocation:
        case ProcessTokenVirtualizationEnabled:
        case ProcessDynamicFunctionTableInformation:
        case ProcessRevokeFileHandles:
        case ProcessWorkingSetControl:
        case ProcessMemoryExhaustion:
        case ProcessFaultInformation:
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        case ProcessImageFileMapping:
            /* TODO: map the named image and report where it landed */
            Status = STATUS_NOT_SUPPORTED;
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

    if (ReturnLength == NULL) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ReturnLength = Length;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* The answer was written; only the length could not be */
        }
        _SEH2_END;
    }
    else
    {
        *ReturnLength = Length;
    }

    return Status;
}
