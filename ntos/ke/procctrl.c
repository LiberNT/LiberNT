/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The kernel's half of a process, and running inside somebody else's
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Every process in the system, so that the balance manager and the swapper
   have somewhere to start from. */
LIST_ENTRY KiProcessListHead;

/* Processes and stacks the swapper has been asked to bring in or push out. */
LIST_ENTRY KiProcessInSwapListHead;
LIST_ENTRY KiProcessOutSwapListHead;
LIST_ENTRY KiStackInSwapListHead;

/* How the swapper is told there is something on those lists. */
KEVENT KiSwapEvent;

/* The system service tables. The shadow one carries the window manager's
   services as well, and a thread is pointed at one or the other by whether it
   has ever made a window manager call. */
KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTable[SSDT_MAX_ENTRIES];
KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTableShadow[SSDT_MAX_ENTRIES];

/* The user-mode routines the kernel returns to for the things it cannot do
   from kernel mode. ntdll publishes their addresses during its own startup. */
PVOID KeUserApcDispatcher;
PVOID KeUserCallbackDispatcher;
PVOID KeUserExceptionDispatcher;
PVOID KeRaiseUserExceptionDispatcher;
PVOID KeUserEmulationDispatcher;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Moves one thread's base priority with its process, keeping the distance the
 * thread had asked for. A thread at the saturation end of its band stays
 * there, because that is what saturation means.
 *
 * Entered with the process lock held at SYNCH_LEVEL.
 */
static
VOID
KiAdjustThreadToProcessPriority(
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY OldProcessBase,
    _In_ KPRIORITY NewProcessBase)
{
    KPRIORITY Priority;

    KiAcquireThreadLock(Thread);

    if (Thread->Saturation > 0)
    {
        Priority = (NewProcessBase >= LOW_REALTIME_PRIORITY)
                       ? HIGH_PRIORITY
                       : LOW_REALTIME_PRIORITY - 1;
    }
    else if (Thread->Saturation < 0)
    {
        Priority = (NewProcessBase >= LOW_REALTIME_PRIORITY)
                       ? LOW_REALTIME_PRIORITY
                       : 1;
    }
    else
    {
        Priority = Thread->BasePriority + (NewProcessBase - OldProcessBase);

        if (NewProcessBase >= LOW_REALTIME_PRIORITY)
        {
            if (Priority < LOW_REALTIME_PRIORITY)
                Priority = LOW_REALTIME_PRIORITY;
            if (Priority > HIGH_PRIORITY)
                Priority = HIGH_PRIORITY;
        }
        else
        {
            if (Priority >= LOW_REALTIME_PRIORITY)
                Priority = LOW_REALTIME_PRIORITY - 1;
            if (Priority <= LOW_PRIORITY)
                Priority = 1;
        }
    }

    Thread->BasePriority = (SCHAR)Priority;
    Thread->PriorityDecrement = 0;
    KiSetPriorityThread(Thread, Priority);

    KiReleaseThreadLock(Thread);
}

/*
 * Swaps the thread's APC environments so that the one it is running in belongs
 * to the process being attached to, and the one it came from is held aside.
 *
 * Entered with the thread's APC lock held at SYNCH_LEVEL; it is released here,
 * because the address space change must not be made under it.
 */
VOID
NTAPI
KiAttachProcess(
    _Inout_ struct _KTHREAD *Thread,
    _Inout_ struct _KPROCESS *Process,
    _Inout_ PKLOCK_QUEUE_HANDLE ApcLock,
    _Out_ struct _KAPC_STATE *SavedApcState)
{
    PKPROCESS OldProcess = Thread->ApcState.Process;

    ASSERT(Process != OldProcess);

    /* Put the environment the thread came from out of reach */
    KiMoveApcState(&Thread->ApcState, SavedApcState);

    InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->ApcState.ApcListHead[UserMode]);
    Thread->ApcState.Process = Process;
    Thread->ApcState.KernelApcInProgress = FALSE;
    Thread->ApcState.KernelApcPending = FALSE;
    Thread->ApcState.UserApcPending = FALSE;

    /*
     * Where the thread's own saved slot was used, the two environments trade
     * places so that an APC aimed at the original one still finds it.
     */
    if (SavedApcState == &Thread->SavedApcState)
    {
        Thread->ApcStatePointer[OriginalApcEnvironment] = &Thread->SavedApcState;
        Thread->ApcStatePointer[AttachedApcEnvironment] = &Thread->ApcState;
        Thread->ApcStateIndex = AttachedApcEnvironment;
    }

    KiAcquireDispatcherLockAtSynchLevel();
    Process->StackCount++;
    KiReleaseDispatcherLockFromSynchLevel();

    KiReleaseApcLockFromSynchLevel(ApcLock);

    /* The architecture layer puts the new address space in place */
    KiSwapProcess(Process, OldProcess);

    KiExitDispatcher(ApcLock->OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Builds the kernel's half of a process. The threads it will own inherit their
 * scheduling attributes from here, so what is set now decides how every one of
 * them starts.
 */
VOID
NTAPI
KeInitializeProcess(
    _Out_ struct _KPROCESS *Process,
    _In_ KPRIORITY Priority,
    _In_ KAFFINITY Affinity,
    _In_ PULONG_PTR DirectoryTableBase,
    _In_ BOOLEAN Enable)
{
    Process->Header.Type = ProcessObject;
    Process->Header.Signalling = FALSE;
    Process->Header.Size = sizeof(KPROCESS) / sizeof(LONG);
    Process->Header.Hand = 0;
    Process->Header.Inserted = FALSE;
    Process->Header.DebugActive = FALSE;
    Process->Header.SignalState = 0;
    InitializeListHead(&Process->Header.WaitListHead);

    InitializeListHead(&Process->ProfileListHead);
    InitializeListHead(&Process->ThreadListHead);
    InitializeListHead(&Process->ReadyListHead);
    Process->SwapListEntry.Next = NULL;

    KeInitializeSpinLock(&Process->ProcessLock);

    Process->Affinity = Affinity;
    Process->ActiveProcessors = 0;
    Process->BasePriority = (SCHAR)Priority;
    Process->QuantumReset = 6;
    Process->ThreadSeed = 0;
    Process->IdealNode = 0;
    Process->State = ProcessInMemory;
    Process->StackCount = 0;
    Process->ProcessFlags = 0;
    Process->AutoAlignment = Enable;
    Process->KernelTime = 0;
    Process->UserTime = 0;
    Process->ReadyTime = 0;
    Process->CycleTime = 0;
    Process->ContextSwitches = 0;
    Process->FreezeCount = 0;

    KPROCESS_DTB0(Process) = DirectoryTableBase[0];
    KPROCESS_DTB1(Process) = DirectoryTableBase[1];

    InsertTailList(&KiProcessListHead, &Process->ProcessListEntry);
}

/*
 * Signals a process. The object becomes signalled once, when the process has
 * no threads left, and stays that way. Answers the previous state.
 */
ULONG
NTAPI
KeSetProcess(
    _Inout_ struct _KPROCESS *Process,
    _In_ KPRIORITY Increment,
    _In_ BOOLEAN InWait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD Thread;

    ASSERT_PROCESS(Process);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();

    PreviousState = Process->Header.SignalState;
    Process->Header.SignalState = 1;

    if ((PreviousState == 0) && !IsListEmpty(&Process->Header.WaitListHead))
        KiWaitTest(Process, Increment);

    if (!InWait)
    {
        KiReleaseDispatcherLock(OldIrql);
    }
    else
    {
        Thread = KeGetCurrentThread();
        Thread->WaitNext = TRUE;
        Thread->WaitIrql = OldIrql;
    }

    return (ULONG)PreviousState;
}

/*
 * Moves a process's base priority, and every one of its threads with it.
 * Answers the previous base priority.
 *
 * A quantum of zero leaves the quantum alone, which is how the priority can be
 * changed without disturbing how long each thread gets.
 */
KPRIORITY
NTAPI
KeSetPriorityAndQuantumProcess(
    _Inout_ PKPROCESS Process,
    _In_ KPRIORITY Priority,
    _In_ UCHAR Quantum)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    KPRIORITY OldBase;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_PROCESS(Process);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT((Priority >= LOW_PRIORITY) && (Priority <= HIGH_PRIORITY));

    if (Priority == 0)
        Priority = 1;

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    OldBase = Process->BasePriority;

    if (Quantum != 0)
        Process->QuantumReset = (SCHAR)Quantum;

    if (OldBase == Priority)
    {
        KiReleaseProcessLock(&ProcessLock);
        return OldBase;
    }

    Process->BasePriority = (SCHAR)Priority;

    KiAcquireDispatcherLockAtSynchLevel();

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);

        if (Quantum != 0)
            Thread->QuantumReset = (UCHAR)Quantum;

        KiAdjustThreadToProcessPriority(Thread, OldBase, Priority);
    }

    KiReleaseDispatcherLockFromSynchLevel();
    KiReleaseProcessLockFromSynchLevel(&ProcessLock);
    KiExitDispatcher(ProcessLock.OldIrql);

    return OldBase;
}

/*
 * Changes how long each of a process's threads gets at a time, without
 * touching their priorities.
 */
VOID
NTAPI
KeSetQuantumProcess(
    _Inout_ PKPROCESS Process,
    _In_ UCHAR Quantum)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_PROCESS(Process);
    ASSERT(Quantum != 0);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    Process->QuantumReset = (SCHAR)Quantum;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
        Thread->QuantumReset = Quantum;
    }

    KiReleaseProcessLock(&ProcessLock);
}

/*
 * Confines a process, and every thread it owns, to a set of processors.
 * Answers the previous set.
 */
KAFFINITY
NTAPI
KeSetAffinityProcess(
    _Inout_ PKPROCESS Process,
    _In_ KAFFINITY Affinity)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    KAFFINITY OldAffinity;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_PROCESS(Process);
    ASSERT((Affinity & KeActiveProcessors) != 0);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    OldAffinity = Process->Affinity;
    Process->Affinity = Affinity;

    KiAcquireDispatcherLockAtSynchLevel();

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);

        KiAcquireThreadLock(Thread);
        KiThreadUserAffinityMask(Thread) = Affinity;
        if (!Thread->SystemAffinityActive)
            KiSetAffinityThread(Thread, Affinity);
        KiReleaseThreadLock(Thread);
    }

    KiReleaseDispatcherLockFromSynchLevel();
    KiReleaseProcessLockFromSynchLevel(&ProcessLock);
    KiExitDispatcher(ProcessLock.OldIrql);

    return OldAffinity;
}

/*
 * Turns priority boosting off for a process and every thread it owns. Answers
 * the previous setting.
 */
BOOLEAN
NTAPI
KeSetDisableBoostProcess(
    _Inout_ PKPROCESS Process,
    _In_ BOOLEAN Disable)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    BOOLEAN Previous;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_PROCESS(Process);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    Previous = (BOOLEAN)Process->DisableBoost;
    Process->DisableBoost = Disable;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
        KeSetDisableBoostThread(Thread, Disable);
    }

    KiReleaseProcessLock(&ProcessLock);
    return Previous;
}

/*
 * Whether unaligned accesses made in this process are fixed up. Answers the
 * previous setting.
 */
BOOLEAN
NTAPI
KeSetAutoAlignmentProcess(
    _Inout_ PKPROCESS Process,
    _In_ BOOLEAN Enable)
{
    BOOLEAN Previous;

    ASSERT_PROCESS(Process);

    Previous = (BOOLEAN)Process->AutoAlignment;
    Process->AutoAlignment = Enable;

    return Previous;
}

/*
 * Whether the stack bounds of this process's threads are checked on every
 * kernel entry. Answers the previous setting.
 */
BOOLEAN
NTAPI
KeSetCheckStackExtentsProcess(
    _Inout_ PKPROCESS Process,
    _In_ BOOLEAN Enable)
{
    BOOLEAN Previous;

    ASSERT_PROCESS(Process);

    Previous = (BOOLEAN)Process->CheckStackExtents;
    Process->CheckStackExtents = Enable;

    return Previous;
}

/*
 * How long a process has run, counted in clock ticks across all of its
 * threads. The threads that have already exited are counted in the process's
 * own totals, so the two have to be added.
 */
ULONG
NTAPI
KeQueryRuntimeProcess(
    _In_ PKPROCESS Process,
    _Out_ PULONG UserTime)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;
    ULONG TotalKernel, TotalUser;

    ASSERT_PROCESS(Process);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    TotalKernel = (ULONG)Process->KernelTime;
    TotalUser = (ULONG)Process->UserTime;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
        TotalKernel += Thread->KernelTime;
        TotalUser += Thread->UserTime;
    }

    KiReleaseProcessLock(&ProcessLock);

    *UserTime = TotalUser;
    return TotalKernel;
}

/*
 * The totals a caller reporting on a process needs: time in hundreds of
 * nanoseconds rather than ticks, and the input and output each of its threads
 * has done.
 */
VOID
NTAPI
KeQueryValuesProcess(
    _In_ PKPROCESS Process,
    _Out_ PPROCESS_VALUES Values)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    PEPROCESS EProcess = CONTAINING_RECORD(Process, EPROCESS, Pcb);
    PKTHREAD Thread;
    PLIST_ENTRY Entry;
    ULONG64 KernelTicks, UserTicks;

    ASSERT_PROCESS(Process);

    RtlZeroMemory(Values, sizeof(*Values));

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    KernelTicks = Process->KernelTime;
    UserTicks = Process->UserTime;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);

        KernelTicks += Thread->KernelTime;
        UserTicks += Thread->UserTime;
    }

    KiReleaseProcessLock(&ProcessLock);

    /*
     * The input and output totals are charged to the process rather than to
     * each thread, because only the process outlives the threads that did the
     * work and the caller is asking about the process.
     */
    Values->IoInfo.ReadOperationCount =
        (ULONGLONG)EProcess->ReadOperationCount.QuadPart;
    Values->IoInfo.WriteOperationCount =
        (ULONGLONG)EProcess->WriteOperationCount.QuadPart;
    Values->IoInfo.OtherOperationCount =
        (ULONGLONG)EProcess->OtherOperationCount.QuadPart;
    Values->IoInfo.ReadTransferCount =
        (ULONGLONG)EProcess->ReadTransferCount.QuadPart;
    Values->IoInfo.WriteTransferCount =
        (ULONGLONG)EProcess->WriteTransferCount.QuadPart;
    Values->IoInfo.OtherTransferCount =
        (ULONGLONG)EProcess->OtherTransferCount.QuadPart;

    Values->TotalKernelTime.QuadPart = KernelTicks * KeMaximumIncrement;
    Values->TotalUserTime.QuadPart = UserTicks * KeMaximumIncrement;
}

/* PUBLIC FUNCTIONS: ATTACHING *************************************************/

/*
 * Says whether this thread is running inside a process other than its own.
 * Code that touches user memory has to know, because the addresses it is
 * handed belong to whichever process is attached.
 */
BOOLEAN
NTAPI
KeIsAttachedProcess(VOID)
{
    return KeGetCurrentThread()->ApcStateIndex == AttachedApcEnvironment;
}

/*
 * Runs the calling thread in another process's address space, saving what it
 * needs to come back into a block the caller keeps. Nesting is allowed,
 * because each level has its own save block.
 */
VOID
NTAPI
KeStackAttachProcess(
    _Inout_ PKPROCESS Process,
    _Out_ PKAPC_STATE ApcState)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT_PROCESS(Process);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /*
     * Already there. The detach has to be told, or it would put back an
     * environment that was never taken away: the marker says so.
     */
    if (Process == Thread->ApcState.Process)
    {
        ApcState->Process = (PKPROCESS)1;
        KiReleaseApcLock(&ApcLock);
        return;
    }

    if (Thread->ApcStateIndex != OriginalApcEnvironment)
    {
        /* Already attached once: this level saves into the caller's block */
        KiAttachProcess(Thread, Process, &ApcLock, ApcState);
        return;
    }

    ApcState->Process = NULL;
    KiAttachProcess(Thread, Process, &ApcLock, &Thread->SavedApcState);
}

/*
 * Comes back from a stack attach. The environment the thread had is restored
 * from wherever the matching attach put it.
 */
VOID
NTAPI
KeUnstackDetachProcess(
    _In_ PKAPC_STATE ApcState)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = KeGetCurrentThread();
    PKPROCESS Process;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* The attach found the thread already in that process and did nothing */
    if (ApcState->Process == (PKPROCESS)1)
        return;

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /*
     * Anything queued against the attached environment belongs to a process
     * the thread is about to stop running in, so it can never be delivered.
     * Leaving it queued would strand it; saying so names the driver that did.
     */
    if (!IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]) ||
        !IsListEmpty(&Thread->ApcState.ApcListHead[UserMode]))
    {
        KeBugCheckEx(INVALID_PROCESS_DETACH_ATTEMPT,
                     (ULONG_PTR)Thread->ApcState.Process,
                     (ULONG_PTR)ApcState,
                     (ULONG_PTR)Thread->ApcStateIndex,
                     (ULONG_PTR)Thread);
    }

    Process = Thread->ApcState.Process;

    if (ApcState->Process == NULL)
    {
        /* The outermost level: the two environments trade back */
        Thread->ApcStatePointer[OriginalApcEnvironment] = &Thread->ApcState;
        Thread->ApcStatePointer[AttachedApcEnvironment] = &Thread->SavedApcState;
        Thread->ApcStateIndex = OriginalApcEnvironment;

        KiMoveApcState(&Thread->SavedApcState, &Thread->ApcState);

        InitializeListHead(&Thread->SavedApcState.ApcListHead[KernelMode]);
        InitializeListHead(&Thread->SavedApcState.ApcListHead[UserMode]);
        Thread->SavedApcState.Process = NULL;
        Thread->SavedApcState.KernelApcInProgress = FALSE;
        Thread->SavedApcState.KernelApcPending = FALSE;
        Thread->SavedApcState.UserApcPending = FALSE;
    }
    else
    {
        KiMoveApcState(ApcState, &Thread->ApcState);
    }

    /* What was held aside may be deliverable the moment it comes back */
    if (!IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]))
    {
        Thread->ApcState.KernelApcPending = TRUE;
        KiRequestApcInterrupt(TRUE, (UCHAR)Thread->NextProcessor);
    }

    KiAcquireDispatcherLockAtSynchLevel();
    Process->StackCount--;
    KiReleaseDispatcherLockFromSynchLevel();

    KiReleaseApcLockFromSynchLevel(&ApcLock);

    KiSwapProcess(Thread->ApcState.Process, Process);

    KiExitDispatcher(ApcLock.OldIrql);
}

/*
 * The older form of attaching, which has nowhere to save a second level and so
 * refuses to nest.
 */
VOID
NTAPI
KeAttachProcess(
    _Inout_ PKPROCESS Process)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT_PROCESS(Process);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    if (Process == Thread->ApcState.Process)
    {
        KiReleaseApcLock(&ApcLock);
        return;
    }

    if (Thread->ApcStateIndex != OriginalApcEnvironment)
    {
        KiReleaseApcLock(&ApcLock);
        KeBugCheckEx(INVALID_PROCESS_ATTACH_ATTEMPT,
                     (ULONG_PTR)Process,
                     (ULONG_PTR)Thread->ApcState.Process,
                     (ULONG_PTR)Thread->ApcStateIndex,
                     (ULONG_PTR)Thread);
    }

    KiAttachProcess(Thread, Process, &ApcLock, &Thread->SavedApcState);
}

VOID
NTAPI
KeDetachProcess(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KAPC_STATE ApcState;

    if (Thread->ApcStateIndex == OriginalApcEnvironment)
        return;

    ApcState.Process = NULL;
    KeUnstackDetachProcess(&ApcState);
}

/* PUBLIC FUNCTIONS: SYSTEM SERVICE TABLES ************************************/

/*
 * Publishes a system service table. Only the window manager's table is ever
 * added this way, and only into the shadow descriptor: a thread that has never
 * made such a call keeps the smaller table and cannot reach them at all.
 *
 * Answers FALSE where the slot is taken, which keeps a second caller from
 * replacing a live table under the threads using it.
 */
BOOLEAN
NTAPI
KeAddSystemServiceTable(
    _In_ PULONG_PTR Base,
    _In_opt_ PULONG Count,
    _In_ ULONG Limit,
    _In_ PUCHAR Number,
    _In_ ULONG Index)
{
    if (Index >= SSDT_MAX_ENTRIES)
        return FALSE;

    if ((KeServiceDescriptorTableShadow[Index].Limit != 0) ||
        (KeServiceDescriptorTableShadow[Index].Base != NULL))
    {
        return FALSE;
    }

    KeServiceDescriptorTableShadow[Index].Base = Base;
    KeServiceDescriptorTableShadow[Index].Count = Count;
    KeServiceDescriptorTableShadow[Index].Limit = Limit;
    KeServiceDescriptorTableShadow[Index].Number = Number;

    return TRUE;
}

/*
 * Withdraws a table published above. A thread in the middle of one of its
 * services is not waited for, so the caller has to have stopped them itself.
 */
BOOLEAN
NTAPI
KeRemoveSystemServiceTable(
    _In_ ULONG Index)
{
    if (Index >= SSDT_MAX_ENTRIES)
        return FALSE;

    KeServiceDescriptorTable[Index].Base = NULL;
    KeServiceDescriptorTable[Index].Count = NULL;
    KeServiceDescriptorTable[Index].Limit = 0;
    KeServiceDescriptorTable[Index].Number = NULL;

    KeServiceDescriptorTableShadow[Index].Base = NULL;
    KeServiceDescriptorTableShadow[Index].Count = NULL;
    KeServiceDescriptorTableShadow[Index].Limit = 0;
    KeServiceDescriptorTableShadow[Index].Number = NULL;

    return TRUE;
}

/*
 * The status a window manager service call is failed with when the calling
 * thread could not be converted to a GUI thread. The service number is kept
 * for the diagnostic, not used to choose the status: whichever service it was,
 * what failed was the conversion.
 */
NTSTATUS
NTAPI
KiGetGuiServiceFailureStatus(
    _In_ ULONG ServiceNumber)
{
    DPRINT1("GUI service %lx refused: no window manager thread\n", ServiceNumber);

    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Widens the affinity of the processes that existed before the other
 * processors did. Until they all started, every one of them could only have
 * been told about processor zero.
 */
CODE_SEG("INIT")
VOID
NTAPI
KiExpandInitialProcessAffinities(VOID)
{
    PKPROCESS Process;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    OldIrql = KiAcquireDispatcherLock();

    for (Entry = KiProcessListHead.Flink;
         Entry != &KiProcessListHead;
         Entry = Entry->Flink)
    {
        Process = CONTAINING_RECORD(Entry, KPROCESS, ProcessListEntry);
        Process->Affinity = KeActiveProcessors;
    }

    KiReleaseDispatcherLock(OldIrql);
}

/* EOF */
