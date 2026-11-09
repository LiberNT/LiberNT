/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a kernel thread is made of and how its state is changed from outside
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#undef KeGetCurrentThread
#undef KeGetPreviousMode

/* The boost a resumed thread is given. Coming back from a suspension is not a
   claim on the processor: the thread rejoins the queue it would have been in. */
#define KI_RESUME_BOOST 0

/* PRIVATE FUNCTIONS **********************************************************/

FORCEINLINE
ULONG
KiLowestSetProcessor(
    _In_ KAFFINITY Set)
{
    ASSERT(Set != 0);
    return (ULONG)(UCHAR)RtlFindLeastSignificantBit((ULONGLONG)Set);
}

/*
 * Arms the suspend APC if this is the first thing holding the thread. The
 * event is cleared first: the APC's own routine waits on it, and a stale
 * signal would let the thread straight back out.
 *
 * Entered with the thread's APC lock held at SYNCH_LEVEL.
 */
static
VOID
KiArmThreadSuspension(
    _Inout_ PKTHREAD Thread)
{
    if (Thread->SuspendApc.Inserted)
        return;

    KeClearEvent(&Thread->SuspendEvent);
    KiInsertQueueApc(&Thread->SuspendApc, KI_RESUME_BOOST);
}

/*
 * Lets a thread out of a suspension, if nothing else is still holding it.
 *
 * Entered with the thread's APC lock held at SYNCH_LEVEL.
 */
static
VOID
KiReleaseThreadSuspension(
    _Inout_ PKTHREAD Thread)
{
    if ((Thread->SuspendCount != 0) || (Thread->FreezeCount != 0))
        return;

    KeSetEvent(&Thread->SuspendEvent, KI_RESUME_BOOST, FALSE);
}

/*
 * Works out the band a base priority has to stay inside. A thread of a
 * real-time process cannot be moved out of the real-time range and one of a
 * dynamic process cannot be moved into it: a caller that could do either
 * would be changing the process's scheduling class from a thread call.
 */
static
KPRIORITY
KiClampBasePriority(
    _In_ PKPROCESS Process,
    _In_ KPRIORITY Priority)
{
    if (Process->BasePriority >= LOW_REALTIME_PRIORITY)
    {
        if (Priority < LOW_REALTIME_PRIORITY)
            return LOW_REALTIME_PRIORITY;
        if (Priority > HIGH_PRIORITY)
            return HIGH_PRIORITY;

        return Priority;
    }

    if (Priority >= LOW_REALTIME_PRIORITY)
        return LOW_REALTIME_PRIORITY - 1;
    if (Priority <= LOW_PRIORITY)
        return 1;

    return Priority;
}

/* PUBLIC FUNCTIONS: IDENTITY *************************************************/

/*
 * The out-of-line forms. Kernel code reads the thread out of the processor
 * block directly; these exist because drivers reach them through the export
 * table.
 */
PKTHREAD
NTAPI
KeGetCurrentThread(VOID)
{
    return _KeGetCurrentThread();
}

KPROCESSOR_MODE
NTAPI
KeGetPreviousMode(VOID)
{
    return _KeGetCurrentThread()->PreviousMode;
}

/* PUBLIC FUNCTIONS: CREATION **************************************************/

/*
 * Builds a kernel thread. Everything the dispatcher, the APC machinery and the
 * timer path will touch is set here, and the architecture layer is then asked
 * to lay out the stack the thread will start on.
 *
 * The thread is not runnable when this returns: it is Initialized, and
 * KeStartThread followed by KeReadyThread is what sets it going.
 */
NTSTATUS
NTAPI
KeInitThread(
    _Out_ PKTHREAD Thread,
    _In_opt_ PVOID KernelStack,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context,
    _In_opt_ PVOID Teb,
    _In_ PKPROCESS Process)
{
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];

    Thread->Header.Type = ThreadObject;
    Thread->Header.Signalling = FALSE;
    Thread->Header.Size = (UCHAR)(sizeof(KTHREAD) / sizeof(LONG));
    Thread->Header.Hand = 0;
    Thread->Header.Inserted = FALSE;
    Thread->Header.DebugActive = FALSE;
    Thread->Header.SignalState = 0;
    InitializeListHead(&Thread->Header.WaitListHead);

    Thread->MiscFlags = 0;
    Thread->ThreadFlags = 0;
    Thread->ApcQueueable = TRUE;
    Thread->EnableStackSwap = TRUE;
    Thread->KernelStackResident = TRUE;
    Thread->Alerted[KernelMode] = FALSE;
    Thread->Alerted[UserMode] = FALSE;

    Thread->Process = Process;
    Thread->State = Initialized;
    Thread->WaitStatus = STATUS_SUCCESS;
    Thread->WaitBlockList = NULL;
    Thread->WaitPrcb = NULL;
    Thread->Queue = NULL;
    Thread->WaitIrql = PASSIVE_LEVEL;
    Thread->WaitMode = KernelMode;
    Thread->PreviousMode = KernelMode;
    Thread->SuspendCount = 0;
    Thread->FreezeCount = 0;
    Thread->Saturation = 0;
    Thread->AdjustReason = AdjustNone;
    Thread->AdjustIncrement = 0;
    Thread->Preempted = FALSE;
    Thread->CombinedApcDisable = 0;
    Thread->KernelTime = 0;
    Thread->UserTime = 0;
    Thread->ContextSwitches = 0;

    InitializeListHead(&Thread->MutantListHead);

    /* The APC environments: the thread's own one, and the one it attaches to */
    InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->ApcState.ApcListHead[UserMode]);
    Thread->ApcState.Process = Process;
    Thread->ApcState.KernelApcInProgress = FALSE;
    Thread->ApcState.KernelApcPending = FALSE;
    Thread->ApcState.UserApcPending = FALSE;

    InitializeListHead(&Thread->SavedApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->SavedApcState.ApcListHead[UserMode]);
    Thread->SavedApcState.Process = NULL;
    Thread->SavedApcState.KernelApcInProgress = FALSE;
    Thread->SavedApcState.KernelApcPending = FALSE;
    Thread->SavedApcState.UserApcPending = FALSE;

    Thread->ApcStatePointer[OriginalApcEnvironment] = &Thread->ApcState;
    Thread->ApcStatePointer[AttachedApcEnvironment] = &Thread->SavedApcState;
    Thread->ApcStateIndex = OriginalApcEnvironment;

    KeInitializeSpinLock(&Thread->ApcQueueLock);
    Thread->ThreadLock = 0;

    /*
     * The suspend APC lives in the thread, so there is nothing to allocate
     * and nothing for its rundown to free.
     */
    KeInitializeApc(&Thread->SuspendApc,
                    Thread,
                    OriginalApcEnvironment,
                    KiSuspendNop,
                    KiSuspendRundown,
                    KiSuspendThread,
                    KernelMode,
                    NULL);

    KeInitializeEvent(&Thread->SuspendEvent, NotificationEvent, FALSE);

    /*
     * The thread's own timer, and the block that represents it in a wait. The
     * block is filled in once here because every timed wait uses the same one.
     */
    KeInitializeTimerEx(&Thread->Timer, SynchronizationTimer);
    TimerBlock->Thread = Thread;
    TimerBlock->Object = &Thread->Timer;
    TimerBlock->WaitKey = (USHORT)STATUS_TIMEOUT;
    TimerBlock->WaitType = WaitAny;
    TimerBlock->WaitListEntry.Flink = NULL;
    TimerBlock->WaitListEntry.Blink = NULL;
    Thread->TimerActive = FALSE;
    Thread->WaitBlockCount = 0;

    Thread->Teb = Teb;
    Thread->LargeStack = FALSE;

    Thread->InitialStack = KernelStack;
    Thread->StackBase = KernelStack;
    Thread->StackLimit = (ULONG_PTR)KernelStack - KERNEL_STACK_SIZE;
    Thread->KernelStack = KernelStack;

    Thread->Priority = (SCHAR)Process->BasePriority;
    Thread->BasePriority = (SCHAR)Process->BasePriority;
    Thread->QuantumReset = (UCHAR)Process->QuantumReset;
    Thread->PriorityDecrement = 0;
    KiSetThreadQuantum(Thread, Process->QuantumReset);

    KiThreadAffinityMask(Thread) = Process->Affinity;
    KiThreadUserAffinityMask(Thread) = Process->Affinity;
    Thread->IdealProcessor = 0;
    Thread->UserIdealProcessor = 0;
    Thread->NextProcessor = 0;

    if (KernelStack == NULL)
        return STATUS_SUCCESS;

    KiInitializeContextThread(Thread,
                              SystemRoutine,
                              StartRoutine,
                              StartContext,
                              Context);

    return STATUS_SUCCESS;
}

/*
 * The form the kernel's own early paths use, where a failure has nowhere to be
 * reported to and would leave a half-built thread behind.
 */
VOID
NTAPI
KeInitializeThread(
    _In_ PKPROCESS Process,
    _Out_ PKTHREAD Thread,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context,
    _In_opt_ PVOID Teb,
    _In_opt_ PVOID KernelStack)
{
    NTSTATUS Status;

    Status = KeInitThread(Thread,
                          KernelStack,
                          SystemRoutine,
                          StartRoutine,
                          StartContext,
                          Context,
                          Teb,
                          Process);

    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,
                     Status,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Process,
                     0);
    }
}

/*
 * Gives back what KeInitThread took. The stack itself belongs to whoever gave
 * it, so all that is undone here is the thread's claim on it.
 */
VOID
NTAPI
KeUninitThread(
    _Inout_ PKTHREAD Thread)
{
    Thread->InitialStack = NULL;
    Thread->StackBase = NULL;
    Thread->StackLimit = 0;
    Thread->KernelStack = NULL;
}

/*
 * Joins a thread to its process. Until this runs the thread has the process's
 * scheduling attributes copied but is not on its list, so the process cannot
 * see it and nothing will suspend or freeze it with the rest.
 */
VOID
NTAPI
KeStartThread(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    PKPROCESS Process = Thread->ApcState.Process;
    PKNODE Node;
    KAFFINITY Affinity;
    ULONG Seed;

    ASSERT(Thread->State == Initialized);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    Thread->BasePriority = (SCHAR)Process->BasePriority;
    Thread->Priority = Thread->BasePriority;
    Thread->QuantumReset = (UCHAR)Process->QuantumReset;
    Thread->DisableBoost = Process->DisableBoost;
    Thread->AutoAlignment = Process->AutoAlignment;
    Thread->DisableQuantum = Process->DisableQuantum;
    KiSetThreadQuantum(Thread, Process->QuantumReset);

    KiThreadAffinityMask(Thread) = Process->Affinity;
    KiThreadUserAffinityMask(Thread) = Process->Affinity;

    /*
     * Threads of one process are spread over the processors of its node in
     * turn, so that a process which creates many threads does not start them
     * all on whichever processor happened to run the creator.
     */
    Node = KeNodeBlock[Process->IdealNode];
    Affinity = Process->Affinity & KeActiveProcessors;
    if (Node != NULL)
        Affinity &= Node->ProcessorMask;
    if (Affinity == 0)
        Affinity = Process->Affinity & KeActiveProcessors;

    if (Affinity != 0)
    {
        Seed = Process->ThreadSeed;
        Thread->IdealProcessor =
            KeFindNextRightSetAffinity((UCHAR)Seed, Affinity);
        Process->ThreadSeed = (UCHAR)Thread->IdealProcessor;
        Thread->UserIdealProcessor = Thread->IdealProcessor;
        Thread->NextProcessor = Thread->IdealProcessor;
    }

    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);
    Process->StackCount++;

    KiReleaseProcessLock(&ProcessLock);
}

/*
 * The last thing a thread does. Its object is signalled so that whoever waited
 * for it is released, it is taken off its process, and the processor is given
 * up for good: the switch away from this stack is what hands it to the reaper.
 */
VOID
NTAPI
KeTerminateThread(
    _In_ KPRIORITY Increment)
{
    KLOCK_QUEUE_HANDLE ProcessLock;
    PKTHREAD Thread = KeGetCurrentThread();
    PKPROCESS Process = Thread->ApcState.Process;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* No APC may arrive between signalling the exit and leaving the processor */
    KeEnterCriticalRegion();

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);
    KiAcquireDispatcherLockAtSynchLevel();

    RemoveEntryList(&Thread->ThreadListEntry);
    Process->StackCount--;

    Thread->Header.SignalState = TRUE;
    if (!IsListEmpty(&Thread->Header.WaitListHead))
        KxUnwaitThread(&Thread->Header, Increment);

    Thread->State = Terminated;
    Thread->WaitIrql = ProcessLock.OldIrql;

    KiReleaseProcessLockFromSynchLevel(&ProcessLock);

    /* The dispatcher is released by the swap, which never comes back here */
    KiSwapThread(Thread, KeGetCurrentPrcb(), FALSE);

    /* Unreachable: a terminated thread is never chosen to run again */
    KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                 (ULONG_PTR)Thread,
                 0,
                 0,
                 0);
}

/*
 * Gives up everything a thread still holds that other threads could be waiting
 * on. A mutant held at this point is marked abandoned, which is how the next
 * holder learns that whatever it protected may be half-finished.
 */
VOID
NTAPI
KeRundownThread(VOID)
{
    KIRQL OldIrql;
    PKTHREAD Thread = KeGetCurrentThread();
    PKMUTANT Mutant;
    PLIST_ENTRY Entry;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    if (IsListEmpty(&Thread->MutantListHead))
        return;

    OldIrql = KiAcquireDispatcherLock();

    while (!IsListEmpty(&Thread->MutantListHead))
    {
        Entry = Thread->MutantListHead.Flink;
        Mutant = CONTAINING_RECORD(Entry, KMUTANT, MutantListEntry);

        /*
         * A mutant that keeps APCs out cannot be abandoned: the thread was
         * running with kernel APCs disabled and has reached its own rundown,
         * which it could not have done without releasing it.
         */
        if (Mutant->ApcDisable != 0)
        {
            KeBugCheckEx(THREAD_TERMINATE_HELD_MUTEX,
                         (ULONG_PTR)Thread,
                         (ULONG_PTR)Mutant,
                         0,
                         0);
        }

        RemoveEntryList(Entry);
        Mutant->OwnerThread = NULL;
        Mutant->Abandoned = TRUE;
        Mutant->Header.SignalState = 1;

        if (!IsListEmpty(&Mutant->Header.WaitListHead))
            KiWaitTest(Mutant, MUTANT_INCREMENT);
    }

    KiReleaseDispatcherLock(OldIrql);
}

/* PUBLIC FUNCTIONS: SUSPENSION ************************************************/

/*
 * The kernel routine of the suspend APC. There is nothing to clean up: the APC
 * is part of the thread, so it is neither freed nor reused.
 */
VOID
NTAPI
KiSuspendNop(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

/*
 * The rundown routine of the suspend APC, reached where the thread died with
 * the suspension still queued. Nothing is owned, so nothing is given back.
 */
VOID
NTAPI
KiSuspendRundown(
    _In_ PKAPC Apc)
{
    UNREFERENCED_PARAMETER(Apc);
}

/*
 * Where a suspended thread actually stops. The APC runs in the thread itself
 * and blocks it on its own event, which is why a suspension can only take
 * effect at a point the thread was willing to be interrupted at.
 */
VOID
NTAPI
KiSuspendThread(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeWaitForSingleObject(&KeGetCurrentThread()->SuspendEvent,
                          Suspended,
                          KernelMode,
                          FALSE,
                          NULL);
}

/*
 * Holds a thread. Suspensions nest, so the thread only stops on the first one
 * and only runs again when the last is taken back. Answers the count before
 * this call.
 */
ULONG
NTAPI
KeSuspendThread(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    PreviousCount = (ULONG)(UCHAR)Thread->SuspendCount;

    if (PreviousCount == MAXIMUM_SUSPEND_COUNT)
    {
        KiReleaseApcLock(&ApcLock);
        ExRaiseStatus(STATUS_SUSPEND_COUNT_EXCEEDED);
    }

    /* A thread that no longer takes APCs cannot be stopped; only counted */
    if (Thread->ApcQueueable)
    {
        Thread->SuspendCount++;
        if ((PreviousCount == 0) && (Thread->FreezeCount == 0))
            KiArmThreadSuspension(Thread);
    }

    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return PreviousCount;
}

/*
 * Takes one suspension back. Answers the count before this call, so a caller
 * that sees zero knows the thread was not suspended at all.
 */
ULONG
NTAPI
KeResumeThread(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    PreviousCount = (ULONG)(UCHAR)Thread->SuspendCount;
    if (PreviousCount != 0)
    {
        Thread->SuspendCount--;
        KiReleaseThreadSuspension(Thread);
    }

    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return PreviousCount;
}

/*
 * Takes every suspension back at once, however deep. Used where a thread must
 * run whatever anyone else asked for, such as to be made to terminate.
 */
ULONG
NTAPI
KeForceResumeThread(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    PreviousCount = (ULONG)(UCHAR)Thread->SuspendCount +
                    (ULONG)(UCHAR)Thread->FreezeCount;

    Thread->SuspendCount = 0;
    Thread->FreezeCount = 0;
    KiReleaseThreadSuspension(Thread);

    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return PreviousCount;
}

/*
 * Holds every other thread of this process. Used where one thread has to look
 * at the process as a whole and needs the rest of it to stand still.
 *
 * Freezes are counted separately from suspensions so that thawing cannot
 * accidentally resume a thread somebody else had suspended.
 */
VOID
NTAPI
KeFreezeAllThreads(VOID)
{
    KLOCK_QUEUE_HANDLE ProcessLock, ApcLock;
    PKTHREAD Current = KeGetCurrentThread();
    PKPROCESS Process = Current->ApcState.Process;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KeEnterCriticalRegion();
    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
        if (Thread == Current)
            continue;

        KiAcquireApcLockAtSynchLevel(Thread, &ApcLock);

        if (Thread->ApcQueueable &&
            ((ULONG)(UCHAR)Thread->FreezeCount != MAXIMUM_SUSPEND_COUNT))
        {
            Thread->FreezeCount++;
            if ((Thread->SuspendCount == 0) && (Thread->FreezeCount == 1))
                KiArmThreadSuspension(Thread);
        }

        KiReleaseApcLockFromSynchLevel(&ApcLock);
    }

    KiReleaseProcessLock(&ProcessLock);
}

/*
 * Lets the rest of the process go again. The critical region taken out by the
 * freeze is given back here, which pairs the two calls.
 */
VOID
NTAPI
KeThawAllThreads(VOID)
{
    KLOCK_QUEUE_HANDLE ProcessLock, ApcLock;
    PKTHREAD Current = KeGetCurrentThread();
    PKPROCESS Process = Current->ApcState.Process;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KiAcquireProcessLockRaiseToSynch(Process, &ProcessLock);

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
        if (Thread == Current)
            continue;

        KiAcquireApcLockAtSynchLevel(Thread, &ApcLock);

        if (Thread->FreezeCount != 0)
        {
            Thread->FreezeCount--;
            KiReleaseThreadSuspension(Thread);
        }

        KiReleaseApcLockFromSynchLevel(&ApcLock);
    }

    KiReleaseProcessLock(&ProcessLock);
    KeLeaveCriticalRegion();
}

/* PUBLIC FUNCTIONS: ALERTS ****************************************************/

/*
 * Tells a thread that something wants its attention. A thread waiting
 * alertably in that mode is released with STATUS_ALERTED; otherwise the alert
 * is remembered and the next alertable wait sees it at once.
 *
 * Answers whether the thread was already alerted in that mode.
 */
BOOLEAN
NTAPI
KeAlertThread(
    _Inout_ PKTHREAD Thread,
    _In_ KPROCESSOR_MODE AlertMode)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    BOOLEAN PreviousState;

    ASSERT((AlertMode == KernelMode) || (AlertMode == UserMode));

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireDispatcherLockAtSynchLevel();

    PreviousState = Thread->Alerted[(UCHAR)AlertMode];

    if (!PreviousState)
    {
        if ((Thread->State == Waiting) &&
            Thread->Alertable &&
            (AlertMode <= Thread->WaitMode))
        {
            KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        }
        else
        {
            Thread->Alerted[(UCHAR)AlertMode] = TRUE;
        }
    }

    KiReleaseDispatcherLockFromSynchLevel();
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return PreviousState;
}

/*
 * Alerts a thread and lets it out of a suspension in one step, so that an
 * alert cannot be swallowed by the thread still being held.
 */
ULONG
NTAPI
KeAlertResumeThread(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireDispatcherLockAtSynchLevel();

    if (!Thread->Alerted[KernelMode])
    {
        if ((Thread->State == Waiting) && Thread->Alertable)
            KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        else
            Thread->Alerted[KernelMode] = TRUE;
    }

    KiReleaseDispatcherLockFromSynchLevel();

    PreviousCount = (ULONG)(UCHAR)Thread->SuspendCount;
    if (PreviousCount != 0)
    {
        Thread->SuspendCount--;
        KiReleaseThreadSuspension(Thread);
    }

    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return PreviousCount;
}

/*
 * Consumes an alert aimed at the calling thread. A user-mode test with user
 * APCs queued and no alert pending reports them instead, which is how the
 * caller learns that there is work for it to run before it waits again.
 */
BOOLEAN
NTAPI
KeTestAlertThread(
    _In_ KPROCESSOR_MODE AlertMode)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN PreviousState;

    ASSERT((AlertMode == KernelMode) || (AlertMode == UserMode));

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    PreviousState = Thread->Alerted[(UCHAR)AlertMode];
    if (PreviousState)
    {
        Thread->Alerted[(UCHAR)AlertMode] = FALSE;
    }
    else if ((AlertMode == UserMode) &&
             !IsListEmpty(&Thread->ApcState.ApcListHead[UserMode]))
    {
        Thread->ApcState.UserApcPending = TRUE;
    }

    KiReleaseApcLock(&ApcLock);
    return PreviousState;
}

/*
 * Consumes an alert aimed at the calling thread in whichever mode it last
 * entered the kernel from, which is what a system call returning to user mode
 * needs before it reports success.
 */
BOOLEAN
NTAPI
KiTestAlert(VOID)
{
    return KeTestAlertThread(KeGetCurrentThread()->PreviousMode);
}

/*
 * The alert a thread waits for by its own identity rather than on an object.
 * A thread parked for one is released directly; otherwise the alert is left
 * pending in the thread's flags for its next such wait to find.
 *
 * Answers whether the thread was woken by this call.
 */
BOOLEAN
NTAPI
KeAlertThreadByThreadId(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    BOOLEAN Woken = FALSE;

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireDispatcherLockAtSynchLevel();

    if ((Thread->State == Waiting) &&
        (Thread->WaitReason == WrAlertByThreadId))
    {
        KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        Woken = TRUE;
    }
    else
    {
        InterlockedBitTestAndSet((PLONG)&Thread->ThreadFlags,
                                 KTHREAD_ALERTED_BY_THREAD_ID_BIT);
    }

    KiReleaseDispatcherLockFromSynchLevel();
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return Woken;
}

/* PUBLIC FUNCTIONS: SCHEDULING ATTRIBUTES *************************************/

/*
 * Makes a thread runnable. Used once after creation and by whoever brings a
 * thread back from a state the dispatcher does not manage.
 */
VOID
NTAPI
KeReadyThread(
    _Inout_ PKTHREAD Thread)
{
    KIRQL OldIrql;

    OldIrql = KiAcquireDispatcherLock();
    KiReadyThread(Thread);
    KiReleaseDispatcherLock(OldIrql);
}

/*
 * Whether a thread has finished. The thread object is signalled exactly once,
 * when the thread terminates, and stays signalled.
 */
BOOLEAN
NTAPI
KeReadStateThread(
    _In_ PKTHREAD Thread)
{
    ASSERT_THREAD(Thread);

    return (BOOLEAN)(Thread->Header.SignalState != 0);
}

/*
 * Moves a thread's current priority. The base priority is untouched, so the
 * usual decay will bring the thread back towards it; a caller that wants the
 * change to last has to move the base instead.
 */
KPRIORITY
NTAPI
KeSetPriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority)
{
    KIRQL OldIrql;
    KPRIORITY OldPriority;

    ASSERT_THREAD(Thread);
    ASSERT((Priority >= LOW_PRIORITY) && (Priority <= HIGH_PRIORITY));
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    OldPriority = Thread->Priority;

    /* An explicit priority replaces any boost rather than adding to it */
    Thread->PriorityDecrement = 0;
    KiSetThreadQuantum(Thread, Thread->QuantumReset);
    KiSetPriorityThread(Thread, Priority);

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return OldPriority;
}

/*
 * Moves a thread's base priority, which is the level its current priority
 * decays back to. The increment is relative to the process, and is clamped to
 * the band the process's own class allows.
 *
 * Answers the previous increment.
 */
LONG
NTAPI
KeSetBasePriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ LONG Increment)
{
    KIRQL OldIrql;
    PKPROCESS Process;
    KPRIORITY OldBase, NewBase;
    LONG OldIncrement;

    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    Process = Thread->ApcState.Process;
    OldBase = Thread->BasePriority;
    OldIncrement = OldBase - Process->BasePriority;

    /*
     * Saturation records a request that went past the band so that reversing
     * it lands back where it started rather than at the clamp.
     */
    if (Thread->Saturation != 0)
        OldIncrement = (Thread->Saturation > 0) ? (HIGH_PRIORITY + 1) / 2
                                                : -((HIGH_PRIORITY + 1) / 2);

    if (Increment >= ((HIGH_PRIORITY + 1) / 2))
    {
        Thread->Saturation = 1;
        NewBase = (Process->BasePriority >= LOW_REALTIME_PRIORITY)
                      ? HIGH_PRIORITY
                      : LOW_REALTIME_PRIORITY - 1;
    }
    else if (Increment <= -((HIGH_PRIORITY + 1) / 2))
    {
        Thread->Saturation = -1;
        NewBase = (Process->BasePriority >= LOW_REALTIME_PRIORITY)
                      ? LOW_REALTIME_PRIORITY
                      : 1;
    }
    else
    {
        Thread->Saturation = 0;
        NewBase = KiClampBasePriority(Process,
                                      Process->BasePriority + Increment);
    }

    Thread->BasePriority = (SCHAR)NewBase;
    Thread->PriorityDecrement = 0;
    KiSetThreadQuantum(Thread, Thread->QuantumReset);
    KiSetPriorityThread(Thread, NewBase);

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return OldIncrement;
}

/*
 * Moves a thread's base priority to an absolute level rather than to one
 * relative to its process. Answers the previous base priority.
 */
LONG
NTAPI
KeSetActualBasePriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ LONG Priority)
{
    KIRQL OldIrql;
    KPRIORITY OldBase, NewBase;

    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    OldBase = Thread->BasePriority;
    NewBase = KiClampBasePriority(Thread->ApcState.Process, Priority);

    Thread->Saturation = 0;
    Thread->BasePriority = (SCHAR)NewBase;
    Thread->PriorityDecrement = 0;
    KiSetThreadQuantum(Thread, Thread->QuantumReset);
    KiSetPriorityThread(Thread, NewBase);

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return OldBase;
}

/*
 * Raises a thread for one turn without touching its base priority, so the
 * decay takes the boost back. Used where something a thread was waiting for
 * has arrived and the waiter should see it before the system moves on.
 */
VOID
NTAPI
KeBoostPriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Increment)
{
    KIRQL OldIrql;
    KPRIORITY Priority;

    ASSERT_THREAD(Thread);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    /* Real-time threads are not boosted: their priority is the caller's word */
    if ((Thread->BasePriority < LOW_REALTIME_PRIORITY) &&
        !Thread->DisableBoost &&
        (Thread->PriorityDecrement == 0))
    {
        Priority = Thread->BasePriority + Increment;
        if (Priority > (LOW_REALTIME_PRIORITY - 1))
            Priority = LOW_REALTIME_PRIORITY - 1;

        if (Priority > Thread->Priority)
        {
            Thread->PriorityDecrement = Priority - Thread->BasePriority;
            KiSetThreadQuantum(Thread, Thread->QuantumReset);
            KiSetPriorityThread(Thread, Priority);
        }
    }

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);
}

KPRIORITY
NTAPI
KeQueryPriorityThread(
    _In_ PKTHREAD Thread)
{
    ASSERT_THREAD(Thread);

    return Thread->Priority;
}

/*
 * The base priority as an increment over the process's, which is the form
 * KeSetBasePriorityThread takes.
 */
LONG
NTAPI
KeQueryBasePriorityThread(
    _In_ PKTHREAD Thread)
{
    KIRQL OldIrql;
    LONG Increment;

    ASSERT_THREAD(Thread);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    if (Thread->Saturation != 0)
    {
        Increment = (Thread->Saturation > 0) ? (HIGH_PRIORITY + 1) / 2
                                             : -((HIGH_PRIORITY + 1) / 2);
    }
    else
    {
        Increment = Thread->BasePriority - Thread->ApcState.Process->BasePriority;
    }

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return Increment;
}

/*
 * The priority the thread would actually run at, which is its current priority
 * raised to the highest floor in force on it.
 */
KPRIORITY
NTAPI
KeQueryEffectivePriorityThread(
    _In_ PKTHREAD Thread)
{
    KIRQL OldIrql;
    KPRIORITY Priority;

    ASSERT_THREAD(Thread);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);
    Priority = KiApplyPriorityFloor(Thread, Thread->Priority);
    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return Priority;
}

/*
 * How long a thread has run. The two counts are in clock ticks and are charged
 * from the clock interrupt, so they move in increments of the clock and not
 * continuously.
 */
ULONG
NTAPI
KeQueryRuntimeThread(
    _In_ PKTHREAD Thread,
    _Out_ PULONG UserTime)
{
    ASSERT_THREAD(Thread);

    *UserTime = Thread->UserTime;
    return Thread->KernelTime;
}

/*
 * Confines a thread to a set of processors on the caller's behalf. The set
 * becomes the thread's user affinity, which is what a system affinity region
 * will restore when it ends.
 */
KAFFINITY
NTAPI
KeSetAffinityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KAFFINITY Affinity)
{
    KIRQL OldIrql;
    KAFFINITY OldAffinity;

    ASSERT_THREAD(Thread);
    ASSERT((Affinity & KeActiveProcessors) != 0);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    KiThreadUserAffinityMask(Thread) = Affinity;

    if (Thread->SystemAffinityActive)
    {
        /* A system affinity region is in force; it wins until it ends */
        OldAffinity = KiThreadAffinityMask(Thread);
    }
    else
    {
        OldAffinity = KiSetAffinityThread(Thread, Affinity);
    }

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return OldAffinity;
}

/*
 * Confines the calling thread for as long as the kernel needs it there,
 * overriding whatever the caller asked for. Answers what to give back, which
 * is zero when no region was already in force.
 */
KAFFINITY
NTAPI
KeSetSystemAffinityThreadEx(
    _In_ KAFFINITY Affinity)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KIRQL OldIrql;
    KAFFINITY OldAffinity;

    ASSERT((Affinity & KeActiveProcessors) != 0);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    OldAffinity = Thread->SystemAffinityActive
                      ? KiThreadAffinityMask(Thread)
                      : 0;

    Thread->SystemAffinityActive = TRUE;
    KiSetAffinityThread(Thread, Affinity);

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return OldAffinity;
}

VOID
NTAPI
KeSetSystemAffinityThread(
    _In_ KAFFINITY Affinity)
{
    KeSetSystemAffinityThreadEx(Affinity);
}

/*
 * Ends a system affinity region. With a non-zero affinity the region does not
 * end: the caller is restoring an outer region it found in force.
 */
VOID
NTAPI
KeRevertToUserAffinityThreadEx(
    _In_ KAFFINITY Affinity)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KIRQL OldIrql;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    if (Affinity != 0)
    {
        KiSetAffinityThread(Thread, Affinity);
    }
    else
    {
        Thread->SystemAffinityActive = FALSE;
        KiSetAffinityThread(Thread, KiThreadUserAffinityMask(Thread));
    }

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);
}

VOID
NTAPI
KeRevertToUserAffinityThread(VOID)
{
    KeRevertToUserAffinityThreadEx(0);
}

/*
 * Names the processor a thread would rather run on. It is a preference, not a
 * restriction: the affinity decides where the thread may run at all.
 *
 * Answers the previous preference.
 */
UCHAR
NTAPI
KeSetIdealProcessorThread(
    _Inout_ PKTHREAD Thread,
    _In_ UCHAR Processor)
{
    KIRQL OldIrql;
    UCHAR Previous;

    ASSERT_THREAD(Thread);
    ASSERT(Processor < MAXIMUM_PROCESSORS);

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireThreadLock(Thread);

    Previous = (UCHAR)Thread->UserIdealProcessor;
    Thread->UserIdealProcessor = Processor;

    /* TODO: no layout-neutral flag records that the choice was explicit */
    if (!Thread->SystemAffinityActive)
        Thread->IdealProcessor = Processor;

    KiReleaseThreadLock(Thread);
    KiReleaseDispatcherLock(OldIrql);

    return Previous;
}

/*
 * Lets the memory manager take this thread's kernel stack away while it waits,
 * or stops it doing so. A thread that holds state on its stack across a long
 * wait turns it off. Answers the previous setting.
 */
BOOLEAN
NTAPI
KeSetKernelStackSwapEnable(
    _In_ BOOLEAN Enable)
{
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN Previous;

    Previous = (BOOLEAN)Thread->EnableStackSwap;
    Thread->EnableStackSwap = Enable;

    return Previous;
}

/*
 * Turns priority boosting off for a thread, which a caller does when it has
 * set a priority it means to keep. Answers the previous setting.
 */
BOOLEAN
NTAPI
KeSetDisableBoostThread(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Disable)
{
    ASSERT_THREAD(Thread);

    if (Disable)
    {
        return InterlockedBitTestAndSet((PLONG)&Thread->ThreadFlags,
                                        KTHREAD_DISABLE_BOOST_BIT);
    }

    return InterlockedBitTestAndReset((PLONG)&Thread->ThreadFlags,
                                      KTHREAD_DISABLE_BOOST_BIT);
}

/*
 * Whether unaligned data accesses made by this thread are fixed up rather than
 * raised. Answers the previous setting.
 */
BOOLEAN
NTAPI
KeSetAutoAlignmentThread(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Enable)
{
    ASSERT_THREAD(Thread);

    if (Enable)
    {
        return InterlockedBitTestAndSet((PLONG)&Thread->ThreadFlags,
                                        KTHREAD_AUTO_ALIGNMENT_BIT);
    }

    return InterlockedBitTestAndReset((PLONG)&Thread->ThreadFlags,
                                      KTHREAD_AUTO_ALIGNMENT_BIT);
}

/*
 * The next processor of a set, starting after the one named and wrapping
 * round. It is what spreads successive choices over a set rather than
 * returning its lowest member every time.
 */
UCHAR
NTAPI
KeFindNextRightSetAffinity(
    _In_ UCHAR Number,
    _In_ KAFFINITY Set)
{
    KAFFINITY Above;

    ASSERT(Set != 0);

    /* Everything strictly above Number; empty once Number is the top bit */
    Above = Set & ~((AFFINITY_MASK(Number) << 1) - 1);

    if (Above != 0)
        return (UCHAR)KiLowestSetProcessor(Above);

    return (UCHAR)KiLowestSetProcessor(Set);
}

/*
 * Writes the state a crash dump reader needs to find this thread again into a
 * page the caller supplies.
 *
 * FIXME: only the thread and processor pointers are recorded; the triage
 * layout a dump reader expects is not published in any header available here,
 * so the rest of the page is left as the caller passed it.
 */
VOID
NTAPI
KeCapturePersistentThreadState(
    _In_ PVOID CurrentThread,
    _In_ ULONG Setting1,
    _In_ ULONG Setting2,
    _In_ ULONG Setting3,
    _In_ ULONG Setting4,
    _In_ ULONG Setting5,
    _In_ PVOID DataPage)
{
    UNREFERENCED_PARAMETER(Setting1);
    UNREFERENCED_PARAMETER(Setting2);
    UNREFERENCED_PARAMETER(Setting3);
    UNREFERENCED_PARAMETER(Setting4);
    UNREFERENCED_PARAMETER(Setting5);

    if (DataPage == NULL)
        return;

    DPRINT1("Persistent thread state for %p not captured\n", CurrentThread);
}

/* EOF */
