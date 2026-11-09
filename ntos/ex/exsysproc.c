/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling a caller what is running: the processes and their threads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define EXP_SYSPROC_TAG 'corP'

/*
 * The idle process has no image file of its own, so the name every other
 * caller expects to see for it is given here instead.
 */
static const WCHAR ExpIdleProcessName[] = L"Idle";

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
ExpProcessNameLength(
    _In_ PEPROCESS Process)
{
    ULONG Index;

    if (Process == PsIdleProcess)
    {
        return sizeof(ExpIdleProcessName) - sizeof(WCHAR);
    }

    for (Index = 0; Index < sizeof(Process->ImageFileName); Index++)
    {
        if (Process->ImageFileName[Index] == ANSI_NULL)
        {
            break;
        }
    }

    return Index * sizeof(WCHAR);
}

/*
 * The name is kept as bytes in the process but handed out as characters, and
 * the string that is handed out points into the caller's own buffer, just past
 * the record it belongs to.
 */
static
VOID
ExpWriteProcessName(
    _In_ PEPROCESS Process,
    _Out_ PUNICODE_STRING Name,
    _Out_writes_bytes_(Length) PWCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Count = Length / sizeof(WCHAR);
    ULONG Index;

    Name->Length = (USHORT)Length;
    Name->MaximumLength = (USHORT)Length;
    Name->Buffer = (Length != 0) ? Buffer : NULL;

    if (Process == PsIdleProcess)
    {
        RtlCopyMemory(Buffer, ExpIdleProcessName, Length);
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = (WCHAR)Process->ImageFileName[Index];
    }
}

static
ULONG
ExpCountProcessThreads(
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Count++;
    }

    return Count;
}

static
VOID
ExpWriteThreadInformation(
    _In_ PETHREAD Thread,
    _Out_ PSYSTEM_THREAD_INFORMATION Info)
{
    RtlZeroMemory(Info, sizeof(*Info));

    Info->KernelTime.QuadPart =
        (LONGLONG)Thread->Tcb.KernelTime * KeMaximumIncrement;
    Info->UserTime.QuadPart =
        (LONGLONG)Thread->Tcb.UserTime * KeMaximumIncrement;
    Info->CreateTime = Thread->CreateTime;
    Info->WaitTime = Thread->Tcb.WaitTime;
    Info->StartAddress = Thread->StartAddress;
    Info->ClientId = Thread->Cid;
    Info->Priority = Thread->Tcb.Priority;
    Info->BasePriority = Thread->Tcb.BasePriority;
    Info->ContextSwitches = Thread->Tcb.ContextSwitches;
    Info->ThreadState = Thread->Tcb.State;
    Info->WaitReason = Thread->Tcb.WaitReason;
    Info->PadPadAlignment = 0;
}

static
VOID
ExpWriteProcessInformation(
    _In_ PEPROCESS Process,
    _Out_ PSYSTEM_PROCESS_INFORMATION Info,
    _In_ ULONG NameLength,
    _Out_writes_bytes_(NameLength) PWCHAR NameBuffer,
    _In_ ULONG ThreadCount)
{
    PMMSUPPORT_INSTANCE Vm = &Process->Vm.Instance;

    RtlZeroMemory(Info, sizeof(*Info));

    Info->NumberOfThreads = ThreadCount;
    Info->CreateTime = Process->CreateTime;
    Info->UniqueProcessId = Process->UniqueProcessId;
    Info->InheritedFromUniqueProcessId =
        Process->InheritedFromUniqueProcessId;
    Info->HandleCount = (Process->ObjectTable != NULL) ?
        (ULONG)ExpGetHandleCount(Process->ObjectTable) : 0;
    Info->SessionId = PsGetProcessSessionId(Process);
    Info->BasePriority = Process->Pcb.BasePriority;
    Info->PageDirectoryBase = Process->Pcb.DirectoryTableBase;
    Info->NumberOfThreadsHighWatermark = ThreadCount;
    Info->HardFaultCount = 0;
    Info->WorkingSetPrivateSize.QuadPart =
        (LONGLONG)(Process->CommitCharge << PAGE_SHIFT);

    Info->PeakVirtualSize = Process->PeakVirtualSize;
    Info->VirtualSize = Process->VirtualSize;
    Info->PageFaultCount = Vm->PageFaultCount;
    Info->PeakWorkingSetSize = Vm->PeakWorkingSetSize;
    Info->WorkingSetSize = Vm->WorkingSetSize;
    Info->QuotaPeakPagedPoolUsage = 0;
    Info->QuotaPagedPoolUsage = 0;
    Info->QuotaPeakNonPagedPoolUsage = 0;
    Info->QuotaNonPagedPoolUsage = 0;
    Info->PagefileUsage = Process->CommitCharge << PAGE_SHIFT;
    Info->PeakPagefileUsage = Info->PagefileUsage;
    Info->PrivatePageCount = Process->CommitCharge;

    Info->ReadOperationCount = Process->ReadOperationCount;
    Info->WriteOperationCount = Process->WriteOperationCount;
    Info->OtherOperationCount = Process->OtherOperationCount;
    Info->ReadTransferCount = Process->ReadTransferCount;
    Info->WriteTransferCount = Process->WriteTransferCount;
    Info->OtherTransferCount = Process->OtherTransferCount;

    /*
     * The time a process has spent is the sum of what its threads have spent,
     * which is what the kernel's own process block has been keeping.
     */
    Info->KernelTime.QuadPart =
        (LONGLONG)Process->Pcb.KernelTime * KeMaximumIncrement;
    Info->UserTime.QuadPart =
        (LONGLONG)Process->Pcb.UserTime * KeMaximumIncrement;

    ExpWriteProcessName(Process, &Info->ImageName, NameBuffer, NameLength);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Walks every process and, within each, every thread, writing one record for
 * the process followed by one for each of its threads. The records are chained
 * by the offset from one to the next, so a caller reads them in order without
 * knowing their size.
 *
 * The walk takes a reference on each process in turn, so a process that goes
 * away while its threads are being written cannot be freed underneath us.
 */
NTSTATUS
NTAPI
ExpQueryProcessInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ BOOLEAN Extended,
    _In_ ULONG SessionId,
    _In_ BOOLEAN OneSessionOnly)
{
    PEPROCESS Process = NULL;
    PETHREAD Thread;
    PSYSTEM_PROCESS_INFORMATION Info;
    PSYSTEM_PROCESS_INFORMATION Previous = NULL;
    PUCHAR Current = Buffer;
    ULONG ThreadSize = Extended ?
        (ULONG)sizeof(SYSTEM_EXTENDED_THREAD_INFORMATION) :
        (ULONG)sizeof(SYSTEM_THREAD_INFORMATION);
    ULONG Used = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    for (Process = PsGetNextProcess(NULL);
         Process != NULL;
         Process = PsGetNextProcess(Process))
    {
        ULONG ThreadCount;
        ULONG NameLength;
        ULONG Needed;

        if (OneSessionOnly && (PsGetProcessSessionId(Process) != SessionId))
        {
            continue;
        }

        ThreadCount = ExpCountProcessThreads(Process);
        NameLength = ExpProcessNameLength(Process);

        /*
         * A record is followed by its threads and then by its name, and the
         * next record starts on a pointer boundary.
         */
        Needed = (ULONG)sizeof(SYSTEM_PROCESS_INFORMATION) +
                 ThreadCount * ThreadSize +
                 NameLength + (ULONG)sizeof(WCHAR);
        Needed = ALIGN_UP_BY(Needed, sizeof(ULONG_PTR));

        if ((Buffer == NULL) || (Used + Needed > Length))
        {
            Used += Needed;
            Status = STATUS_INFO_LENGTH_MISMATCH;
            continue;
        }

        Info = (PSYSTEM_PROCESS_INFORMATION)Current;

        ExpWriteProcessInformation(Process,
                                   Info,
                                   NameLength,
                                   (PWCHAR)(Current +
                                            sizeof(SYSTEM_PROCESS_INFORMATION) +
                                            ThreadCount * ThreadSize),
                                   ThreadCount);

        if (ThreadCount != 0)
        {
            PUCHAR ThreadRecord = Current +
                                  sizeof(SYSTEM_PROCESS_INFORMATION);
            ULONG Written = 0;

            for (Thread = PsGetNextProcessThread(Process, NULL);
                 (Thread != NULL) && (Written < ThreadCount);
                 Thread = PsGetNextProcessThread(Process, Thread))
            {
                ExpWriteThreadInformation(
                    Thread,
                    (PSYSTEM_THREAD_INFORMATION)ThreadRecord);

                if (Extended)
                {
                    PSYSTEM_EXTENDED_THREAD_INFORMATION Ext =
                        (PSYSTEM_EXTENDED_THREAD_INFORMATION)ThreadRecord;

                    Ext->StackBase = (PVOID)Thread->Tcb.StackBase;
                    Ext->StackLimit = (PVOID)Thread->Tcb.StackLimit;
                    Ext->Win32StartAddress = Thread->Win32StartAddress;
                    Ext->TebBase = Thread->Tcb.Teb;
                    Ext->Reserved2 = 0;
                    Ext->Reserved3 = 0;
                    Ext->Reserved4 = 0;
                }

                Info->CycleTime += Thread->Tcb.CycleTime;

                ThreadRecord += ThreadSize;
                Written++;
            }

            /* A thread that went away leaves one record fewer to read. */
            Info->NumberOfThreads = Written;
        }

        if (Previous != NULL)
        {
            Previous->NextEntryOffset =
                (ULONG)((PUCHAR)Info - (PUCHAR)Previous);
        }

        Previous = Info;
        Current += Needed;
        Used += Needed;
    }

    if (Previous != NULL)
    {
        Previous->NextEntryOffset = 0;
    }

    *ReturnLength = Used;

    return Status;
}

/*
 * Names one process the caller already knows the identifier of. The name is
 * written into room the caller set aside, so the length it offered has to hold.
 */
NTSTATUS
NTAPI
ExpQueryProcessIdInformation(
    _Inout_ PSYSTEM_PROCESS_ID_INFORMATION Information)
{
    PEPROCESS Process;
    ULONG NameLength;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PsLookupProcessByProcessId(Information->ProcessId, &Process);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NameLength = ExpProcessNameLength(Process);

    if (Information->ImageName.MaximumLength < NameLength)
    {
        Information->ImageName.MaximumLength = (USHORT)NameLength;
        ObDereferenceObject(Process);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    ExpWriteProcessName(Process,
                        &Information->ImageName,
                        Information->ImageName.Buffer,
                        NameLength);

    Information->ImageName.MaximumLength = (USHORT)NameLength;

    ObDereferenceObject(Process);

    return STATUS_SUCCESS;
}

/*
 * The same list as the whole-system one, but only the processes of one session,
 * written into a buffer the caller named separately from the record itself.
 */
NTSTATUS
NTAPI
ExpQuerySessionProcessInformation(
    _Inout_ PSYSTEM_SESSION_PROCESS_INFORMATION Information,
    _Out_ PULONG ReturnLength)
{
    NTSTATUS Status;
    ULONG Used = 0;

    PAGED_CODE();

    if (Information->Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = ExpQueryProcessInformation(Information->Buffer,
                                        Information->SizeOfBuf,
                                        &Used,
                                        FALSE,
                                        Information->SessionId,
                                        TRUE);

    Information->SizeOfBuf = Used;
    *ReturnLength = (ULONG)sizeof(SYSTEM_SESSION_PROCESS_INFORMATION);

    return Status;
}

/* EOF */
