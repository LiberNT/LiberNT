/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choosing which thread a processor runs next, and handing it over
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* Spin loops count yields, and drivers size them on Windows 11, where a yield with nothing to run takes about 1 us */
#define KI_EMPTY_YIELD_STALL_US 1

/* GLOBALS ********************************************************************/

/* Processors with nothing to run. A bit here is a promise to whoever is
   looking for somewhere to put a thread, not a lock on the processor. */
KAFFINITY KiIdleSummary;

/* Of the idle processors above, those whose physical core is wholly idle.
   Placing a thread on one of these costs no sibling any throughput. */
KAFFINITY KiIdleSMTSummary;

/* PRIVATE FUNCTIONS **********************************************************/

FORCEINLINE
KAFFINITY
KiThreadRunnableSet(
    _In_ PKTHREAD Thread)
{
    KAFFINITY Affinity;

    Affinity = KiThreadAffinityMask(Thread) & KeActiveProcessors;

    /*
     * A parked processor is still active but should not be given work while
     * anything unparked can take it.
     */
    if (Affinity & KiGetNonParkedProcessorSet())
        Affinity &= KiGetNonParkedProcessorSet();

    return Affinity;
}

FORCEINLINE
ULONG
KiFirstSetProcessor(
    _In_ KAFFINITY Set)
{
    ASSERT(Set != 0);
    return (ULONG)(UCHAR)RtlFindLeastSignificantBit((ULONGLONG)Set);
}

/*
 * The idle summary is read by every processor and written by whichever one
 * changes its own state, under that processor's own lock. Nothing orders two
 * such writers against each other, so the whole word is updated atomically.
 * KAFFINITY is pointer-wide on every target, which is what makes the pointer
 * exchange the right primitive here.
 */
FORCEINLINE
VOID
KiUpdateAffinitySummary(
    _Inout_ KAFFINITY *Summary,
    _In_ KAFFINITY SetMask,
    _In_ KAFFINITY ClearMask)
{
    KAFFINITY Old, New;

    do
    {
        Old = *(volatile KAFFINITY *)Summary;
        New = (Old | SetMask) & ~ClearMask;
        if (New == Old)
            return;
    } while ((KAFFINITY)(ULONG_PTR)InterlockedCompareExchangePointer(
                 (PVOID volatile *)Summary,
                 (PVOID)(ULONG_PTR)New,
                 (PVOID)(ULONG_PTR)Old) != Old);
}

/*
 * Announces a processor as idle. Its bit also joins the summary of wholly idle
 * cores when every processor sharing its core is already idle: a thread placed
 * on such a processor takes throughput from nobody, which is worth more than
 * placing it on an idle processor whose sibling is busy.
 */
FORCEINLINE
VOID
KiMarkProcessorIdle(
    _In_ PKPRCB Prcb)
{
    KAFFINITY Mask = AFFINITY_MASK(Prcb->Number);
    KAFFINITY Siblings = (KAFFINITY)Prcb->MultiThreadProcessorSet;

    KiUpdateAffinitySummary(&KiIdleSummary, Mask, 0);

    if ((Siblings == 0) || ((Siblings & ~Mask) == 0))
    {
        /* No siblings: the processor is a whole core on its own */
        KiUpdateAffinitySummary(&KiIdleSMTSummary, Mask, 0);
        return;
    }

    if ((Siblings & ~KiIdleSummary) == 0)
        KiUpdateAffinitySummary(&KiIdleSMTSummary, Siblings, 0);
}

/*
 * Announces a processor as busy. Its whole core leaves the wholly-idle summary
 * with it: a sibling that is still idle is no longer free of competition, so
 * placing work there is no better than placing it anywhere else idle.
 */
FORCEINLINE
VOID
KiMarkProcessorBusy(
    _In_ PKPRCB Prcb)
{
    KAFFINITY Mask = AFFINITY_MASK(Prcb->Number);
    KAFFINITY Siblings = (KAFFINITY)Prcb->MultiThreadProcessorSet;

    KiUpdateAffinitySummary(&KiIdleSummary, 0, Mask);
    KiUpdateAffinitySummary(&KiIdleSMTSummary,
                            0,
                            (Siblings != 0) ? Siblings : Mask);
}

/*
 * Settles the priority and the quantum a thread will run with, acting on the
 * reason it was made ready. A boost is written as a decrement from the base
 * priority so that the usual decay can take it back again; it is never applied
 * to a real-time thread, whose priority Windows keeps exactly where the caller
 * put it.
 */
static
VOID
KiApplyThreadAdjustment(
    _Inout_ PKTHREAD Thread)
{
    KPRIORITY Priority;
    SCHAR Increment;
    UCHAR Reason;

    Reason = Thread->AdjustReason;
    Increment = Thread->AdjustIncrement;
    Thread->AdjustReason = AdjustNone;
    Thread->AdjustIncrement = 0;

    if (KiGetThreadQuantum(Thread) <= 0)
        KiSetThreadQuantum(Thread, Thread->QuantumReset);

    if (Reason == AdjustNone)
        return;

    if (Thread->BasePriority >= LOW_REALTIME_PRIORITY)
    {
        KiSetThreadQuantum(Thread, Thread->QuantumReset);
        return;
    }

    if (Reason == AdjustUnwait)
    {
        /*
         * One boost at a time: a thread that is still carrying the last one
         * keeps it rather than stacking a second on top.
         */
        if (Thread->DisableBoost || (Thread->PriorityDecrement != 0) ||
            (Increment <= 0))
        {
            return;
        }

        Priority = Thread->BasePriority + Increment;
    }
    else
    {
        Priority = Increment;
    }

    if (Priority > (LOW_REALTIME_PRIORITY - 1))
        Priority = LOW_REALTIME_PRIORITY - 1;

    if (Priority <= Thread->Priority)
        return;

    Thread->PriorityDecrement = Priority - Thread->BasePriority;
    Thread->Priority = (SCHAR)KiApplyPriorityFloor(Thread, Priority);
}

/*
 * Where this thread should run. An idle processor is worth more than the ideal
 * one, because a thread placed on a busy processor waits for a quantum while an
 * idle processor would have started it at once. Among idle processors, one whose
 * whole core is idle is preferred: its siblings are not paying for the choice.
 */
static
KAFFINITY
KiProcessorsRunningDpcs(
    _In_ KAFFINITY Set)
{
    KAFFINITY Busy = 0;
    ULONG Processor;

    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        if ((Set & AFFINITY_MASK(Processor)) &&
            KiProcessorBlock[Processor]->DpcRoutineActive)
        {
            Busy |= AFFINITY_MASK(Processor);
        }
    }

    return Busy;
}

static
ULONG
KiSelectTargetProcessor(
    _In_ PKTHREAD Thread)
{
    KAFFINITY Affinity, IdleSet, QuietSet, IdleSmtSet;
    KAFFINITY IdealMask;

    Affinity = KiThreadRunnableSet(Thread);
    if (Affinity == 0)
    {
        /* Nothing it may use is running: leave it where it last was */
        return Thread->NextProcessor;
    }

    Affinity = KiHeteroPreferredProcessorSet(Thread, Affinity);
    ASSERT(Affinity != 0);

    IdealMask = AFFINITY_MASK(Thread->IdealProcessor);

    IdleSet = Affinity & KiIdleSummary;
    if (IdleSet != 0)
    {
        /* A processor in the middle of a DPC is idle only in name: a thread sent there waits for the routine to end */
        QuietSet = IdleSet & ~KiProcessorsRunningDpcs(IdleSet);
        if (QuietSet != 0)
            IdleSet = QuietSet;

        if (IdleSet & KiHeteroPerformanceSet)
            IdleSet &= KiHeteroPerformanceSet;

        if (IdleSet & IdealMask)
            return Thread->IdealProcessor;

        IdleSmtSet = IdleSet & *(volatile KAFFINITY *)&KiIdleSMTSummary;
        if (IdleSmtSet != 0)
            return KiFirstSetProcessor(IdleSmtSet);

        return KiFirstSetProcessor(IdleSet);
    }

    KiCheckProcessorUnpark(Thread);

    if (Affinity & IdealMask)
        return Thread->IdealProcessor;

    return KiFindIdealProcessor(Affinity, (UCHAR)Thread->IdealProcessor);
}

static
BOOLEAN
KiCanStealToProcessor(
    _In_ PKTHREAD Thread,
    _In_ ULONG TargetCpu)
{
    KAFFINITY Affinity = KiThreadAffinityMask(Thread) & KeActiveProcessors;
    KAFFINITY NonParkedSet = KiGetNonParkedProcessorSet();

    if (!(Affinity & AFFINITY_MASK(TargetCpu)) ||
        !(KiHeteroPreferredProcessorSet(Thread, Affinity) & AFFINITY_MASK(TargetCpu)))
    {
        return FALSE;
    }

    return (NonParkedSet & AFFINITY_MASK(TargetCpu)) || !(Affinity & NonParkedSet);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Puts a ready thread on a processor's queues. Unlike the current-processor
 * form in the dispatcher headers this may be a remote processor, so nothing
 * here may touch the current processor's own state.
 *
 * Entered with that processor's PRCB lock held; it is released before return.
 */
_Requires_lock_held_(Prcb->PrcbLock)
_Releases_lock_(Prcb->PrcbLock)
VOID
FASTCALL
KiQueueReadyThread(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKPRCB Prcb)
{
    KPRIORITY Priority;
    BOOLEAN Preempted;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* The idle thread is never queued: it is what a processor falls back to */
    if (Thread == Prcb->IdleThread)
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    if ((KiThreadAffinityMask(Thread) & Prcb->SetMember) == 0)
    {
        /* It may not run here after all; let the deferred pass place it again */
        Thread->State = DeferredReady;
        KiReleasePrcbLock(Prcb);
        KiDeferredReadyThread(Thread);
        return;
    }

    Priority = Thread->Priority;
    ASSERT((Priority >= 0) && (Priority <= HIGH_PRIORITY));

    Preempted = Thread->Preempted;
    Thread->Preempted = FALSE;
    Thread->State = Ready;
    Thread->NextProcessor = Prcb->Number;
    Thread->WaitTime = KeTickCount.LowPart;

    KiAssertReadyQueue(Prcb, Priority);

    /*
     * A thread that was taken off the processor before its turn ended goes
     * back to the front of its queue: it is owed the rest of that turn.
     */
    if (Preempted)
    {
        InsertHeadList(&Prcb->DispatcherReadyListHead[Priority],
                       &Thread->WaitListEntry);
    }
    else
    {
        InsertTailList(&Prcb->DispatcherReadyListHead[Priority],
                       &Thread->WaitListEntry);
    }

    Prcb->ReadySummary |= PRIORITY_MASK(Priority);
    KiAssertReadyQueue(Prcb, Priority);

    KiReleasePrcbLock(Prcb);
}

/*
 * Takes a thread that has been declared ready and gives it a processor: either
 * the processor it displaces something on, or a place in that processor's
 * queues.
 */
VOID
FASTCALL
KiDeferredReadyThread(
    _Inout_ PKTHREAD Thread)
{
    PKPRCB Prcb;
    PKTHREAD Current, Displaced;
    KPRIORITY Priority;
    ULONG Processor;

    ASSERT(Thread->State == DeferredReady);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KiApplyThreadAdjustment(Thread);
    Priority = Thread->Priority;

    Processor = KiSelectTargetProcessor(Thread);
    ASSERT(Processor < MAXIMUM_PROCESSORS);
    Prcb = KiProcessorBlock[Processor];

    KiAcquirePrcbLock(Prcb);

    Current = Prcb->CurrentThread;

    Displaced = Prcb->NextThread;
    if ((Displaced != NULL) &&
        (Displaced != Thread) &&
        (Displaced != Prcb->IdleThread) &&
        (Priority > (KPRIORITY)Displaced->Priority))
    {
        ASSERT(Displaced->State == Standby);
        ASSERT(Displaced->NextProcessor == Processor);

        Thread->State = Standby;
        Thread->NextProcessor = Processor;
        Prcb->NextThread = Thread;

        KiQueueReadyThread(Displaced, Prcb);
        return;
    }

    /*
     * Displace what is running only when nothing else is already standing by
     * for that processor; two candidates would otherwise lose one of them.
     */
    if ((Prcb->NextThread == NULL) &&
        (Current != NULL) &&
        (Current != Thread) &&
        (Priority > (KPRIORITY)Current->Priority) &&
        !Current->DeferPreemption &&
        !((Current == Prcb->IdleThread) && Prcb->DpcRoutineActive &&
          (KiThreadAffinityMask(Thread) & KeActiveProcessors & ~Prcb->SetMember)))
    {
        if (Current != Prcb->IdleThread)
            Current->Preempted = TRUE;

        Thread->State = Standby;
        Thread->NextProcessor = Processor;
        Prcb->NextThread = Thread;
        KiMarkProcessorBusy(Prcb);

        KiReleasePrcbLock(Prcb);

        if (SmpDbgEnabled)
            SmpDbgSchedulerIpi(Processor, Thread, (ULONG)Priority);

        KiRescheduleThread(TRUE, Processor);
        return;
    }

    /*
     * A thread that asked not to be preempted keeps the processor, but the
     * request is remembered: the next time its turn comes up for review the
     * thread that should have displaced it gets its chance.
     */
    if ((Current != NULL) &&
        Current->DeferPreemption &&
        (Priority > (KPRIORITY)Current->Priority))
    {
        Current->QueueDeferPreemption = TRUE;
    }

    KiQueueReadyThread(Thread, Prcb);

    /*
     * The processor was idle but something was already standing by for it, so
     * the dispatch interrupt it owes has been asked for already.
     */
    if (Current == Prcb->IdleThread)
        KiRescheduleThread(TRUE, Processor);
}

/*
 * Works through the threads this processor declared ready while it held the
 * dispatcher. They were only pushed onto a list then, because placing a thread
 * needs another processor's lock and the dispatcher must not be held across it.
 */
VOID
FASTCALL
KiProcessDeferredReadyList(
    _Inout_ PKPRCB Prcb)
{
    PSINGLE_LIST_ENTRY Entry;
    PKTHREAD Thread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Entry = Prcb->DeferredReadyListHead.Next;
    Prcb->DeferredReadyListHead.Next = NULL;

    while (Entry != NULL)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, SwapListEntry);
        Entry = Entry->Next;
        KiDeferredReadyThread(Thread);
    }
}

/*
 * Declares a thread ready. The work of placing it is deferred so that the
 * caller, which holds the dispatcher database, never has to reach for another
 * processor's lock underneath it.
 */
VOID
NTAPI
KiReadyThread(
    _Inout_ PKTHREAD Thread)
{
    KiInsertDeferredReadyList(Thread);
}

/*
 * Picks the thread this processor runs next. When nothing is ready the idle
 * thread is the answer and the processor announces itself as available, which
 * is how another processor comes to place work here without an interrupt.
 *
 * Entered with the PRCB lock held.
 */
PKTHREAD
FASTCALL
KiSelectNextThread(
    _Inout_ PKPRCB Prcb)
{
    PKTHREAD Thread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Thread = KiSelectReadyThread(0, Prcb);
    if (Thread != NULL)
    {
        KiMarkProcessorBusy(Prcb);
        Thread->NextProcessor = Prcb->Number;
        return Thread;
    }

    Prcb->IdleSchedule = FALSE;
    KiMarkProcessorIdle(Prcb);

    Thread = Prcb->IdleThread;
    Thread->NextProcessor = Prcb->Number;
    return Thread;
}

/*
 * Takes the highest-priority thread on another processor's ready queues that
 * may run on this one. An idle processor does this so that work queued while
 * every processor was busy does not wait for its own processor to come free.
 * The other processor's lock is only tried, because this one's is held.
 */
_Requires_lock_held_(Prcb->PrcbLock)
static
PKTHREAD
KiStealReadyThread(
    _Inout_ PKPRCB Prcb,
    _In_ KPRIORITY MinimumPriority)
{
    ULONG Offset, Index;
    KAFFINITY Self = AFFINITY_MASK(Prcb->Number);
    ULONG Floor = ~(PRIORITY_MASK(MinimumPriority) - 1);

    for (Offset = 1; Offset < (ULONG)KeNumberProcessors; Offset++)
    {
        PKPRCB Remote;
        ULONG Summary;
        LONG Priority;

        Index = (Prcb->Number + Offset) % (ULONG)KeNumberProcessors;
        Remote = KiProcessorBlock[Index];
        if ((Remote == NULL) || ((ReadNoFence((PLONG)&Remote->ReadySummary) & Floor) == 0))
            continue;

        if (InterlockedExchange((PLONG)&Remote->PrcbLock, 1) != 0)
            continue;

        for (Summary = Remote->ReadySummary & Floor; Summary != 0; Summary &= ~PRIORITY_MASK(Priority))
        {
            PLIST_ENTRY Head, Entry;

            BitScanReverse((PULONG)&Priority, Summary);
            KiAssertReadyQueue(Remote, Priority);
            Head = &Remote->DispatcherReadyListHead[Priority];

            for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
            {
                PKTHREAD Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);

                ASSERT(Thread->State == Ready);
                ASSERT(Thread != Remote->IdleThread);
                if ((KiThreadAffinityMask(Thread) & Self) == 0)
                    continue;
                if (!KiCanStealToProcessor(Thread, Prcb->Number))
                    continue;

                if (RemoveEntryList(&Thread->WaitListEntry))
                    Remote->ReadySummary ^= PRIORITY_MASK(Priority);

                /* Off that queue for good before its lock goes: nothing may unlink it again there */
                Thread->NextProcessor = Prcb->Number;
                Thread->State = Standby;

                KiAssertReadyQueue(Remote, Priority);
                KiReleasePrcbLock(Remote);
                return Thread;
            }
        }

        KiReleasePrcbLock(Remote);
    }

    return NULL;
}

/*
 * What the idle loop asks for once it has been told there is work. Answers the
 * ready thread to run, or the idle thread itself when none is ready.
 *
 * Entered and left with the PRCB lock held; the idle loop makes the thread current.
 */
_Requires_lock_held_(Prcb->PrcbLock)
PKTHREAD
FASTCALL
KiIdleSchedule(
    _Inout_ PKPRCB Prcb)
{
    PKTHREAD Thread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Prcb->IdleSchedule = FALSE;

    Thread = KiSelectReadyThread(0, Prcb);
    if (Thread == NULL)
        Thread = KiStealReadyThread(Prcb, 0);

    if (Thread == NULL) return Prcb->IdleThread;

    Thread->NextProcessor = Prcb->Number;
    return Thread;
}

VOID
FASTCALL
KiYieldDelayExecution(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTHREAD Next;
    PKPRCB Prcb;
    KIRQL OldIrql;
    BOOLEAN Yielded;

    OldIrql = KeRaiseIrqlToSynchLevel();
    Prcb = KeGetCurrentPrcb();
    KiAcquirePrcbLock(Prcb);

    if (Prcb->NextThread == NULL)
    {
        Next = KiSelectReadyThread(Thread->Priority, Prcb);
        if (Next == NULL)
            Next = KiStealReadyThread(Prcb, Thread->Priority);

        if (Next != NULL)
        {
            Next->State = Standby;
            Next->NextProcessor = Prcb->Number;
            Prcb->NextThread = Next;
            Thread->Preempted = FALSE;
        }
    }

    Yielded = (Prcb->NextThread != NULL);

    KiReleasePrcbLock(Prcb);
    KiExitDispatcher(OldIrql);

    if (!Yielded)
        KeStallExecutionProcessor(KI_EMPTY_YIELD_STALL_US);
}

/*
 * Gives the processor up. Returns the status the wait ended with, which is only
 * known once this thread runs again.
 *
 * Entered at SYNCH_LEVEL with the dispatcher database held and the thread's
 * wait already published; returns at the thread's own wait IRQL.
 */
LONG_PTR
FASTCALL
KiSwapThread(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKPRCB Prcb,
    _In_ BOOLEAN NormalWait)
{
    KIRQL WaitIrql = Thread->WaitIrql;
    PKTHREAD NextThread;
    LONG_PTR WaitStatus;
    BOOLEAN ApcPending;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Thread == Prcb->CurrentThread);

    /*
     * While this thread is off the processor it does not count against its
     * queue's concurrency, so another thread may be let through. A thread
     * blocking on the queue itself has already accounted for that.
     */
    if (NormalWait && (Thread->Queue != NULL) &&
        (Thread->WaitReason != WrQueue))
    {
        KxActivateWaiterQueue(Thread);
    }

    KiReleaseDispatcherLockFromSynchLevel();

    /* Placing these needs other processors' locks, so it happens unlocked */
    KiCheckDeferredReadyList(Prcb);

    KiAcquirePrcbLock(Prcb);

    NextThread = Prcb->NextThread;
    if (NextThread != NULL)
    {
        Prcb->NextThread = NULL;
    }
    else
    {
        NextThread = KiSelectNextThread(Prcb);
    }

    ASSERT(NextThread != NULL);

    if (NextThread == Thread)
    {
        /* Another processor ended the wait and placed the thread here before it could leave */
        ASSERT(Prcb->CurrentThread == Thread);
        Thread->State = Running;
        KiReleasePrcbLock(Prcb);

        ApcPending = Thread->ApcState.KernelApcPending &&
                     (Thread->SpecialApcDisable == 0) &&
                     (WaitIrql == PASSIVE_LEVEL);
    }
    else
    {
        NextThread->State = Running;
        Prcb->CurrentThread = NextThread;

        KiReleasePrcbLock(Prcb);

        /* The incoming thread must be off whatever processor last ran it */
        KiWaitForThreadSwapOut(NextThread);

        ApcPending = KiSwapContext(WaitIrql, Thread);
    }

    /*
     * Back on this thread's own stack. A kernel APC that came in while it was
     * blocked is delivered now, before the caller sees the wait status.
     */
    WaitStatus = Thread->WaitStatus;

    if (ApcPending)
    {
        ASSERT(WaitIrql == PASSIVE_LEVEL);
        KeLowerIrql(APC_LEVEL);
        KiDeliverApc(KernelMode, NULL, NULL);
    }

    KeLowerIrql(WaitIrql);
    return WaitStatus;
}

/* The queue may change between reading NextProcessor and locking it, so both are checked again under the lock */
static
PKPRCB
KiAcquireReadyThreadPrcb(
    _In_ PKTHREAD Thread)
{
    PKPRCB Prcb;
    ULONG Processor;

    for (;;)
    {
        Processor = Thread->NextProcessor;
        Prcb = KiProcessorBlock[Processor];
        KiAcquirePrcbLock(Prcb);

        if (Thread->State != Ready)
        {
            KiReleasePrcbLock(Prcb);
            return NULL;
        }

        if (Thread->NextProcessor == Processor)
            return Prcb;

        KiReleasePrcbLock(Prcb);
    }
}

/*
 * Moves a thread to a new priority, honouring whichever floors are in force.
 * The caller holds the thread lock, and the PRCB lock of the processor the
 * thread is queued on if it is queued at all.
 */
VOID
FASTCALL
KiSetPriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority)
{
    PKPRCB Prcb;
    KPRIORITY OldPriority, QueuedPriority;
    PKTHREAD Current, Better;
    ULONG Processor;
    UCHAR State;

    ASSERT((Priority >= 0) && (Priority <= HIGH_PRIORITY));
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Priority = KiApplyPriorityFloor(Thread, Priority);

    for (;;)
    {
        if (Thread->Priority == (SCHAR)Priority)
            return;

        State = Thread->State;

        if (State == Ready)
        {
            /*
             * A queued thread has to change queue, and the summary bit of the
             * queue it leaves goes with it when that queue empties.
             */
            Prcb = KiAcquireReadyThreadPrcb(Thread);
            if (Prcb == NULL)
                continue;

            /* The queue it is on now, read under the lock that owns it */
            QueuedPriority = Thread->Priority;

            KiAssertReadyQueue(Prcb, QueuedPriority);
            if (RemoveEntryList(&Thread->WaitListEntry))
                Prcb->ReadySummary ^= PRIORITY_MASK(QueuedPriority);
            KiAssertReadyQueue(Prcb, QueuedPriority);

            Thread->Priority = (SCHAR)Priority;
            Thread->Preempted = FALSE;
            Thread->State = DeferredReady;
            KiReleasePrcbLock(Prcb);

            /* Placed again on the way out of the dispatcher, where it can displace the running thread */
            KiInsertDeferredReadyList(Thread);
            return;
        }

        if ((State == Standby) || (State == Running))
        {
            Processor = Thread->NextProcessor;
            Prcb = KiProcessorBlock[Processor];
            KiAcquirePrcbLock(Prcb);

            /*
             * The priority indexes the queue the thread is placed on, so it may
             * only be written under the lock of the processor that owns it. A
             * thread that moved on since its state was read is looked at again.
             */
            if ((Thread->State != State) || (Thread->NextProcessor != Processor))
            {
                KiReleasePrcbLock(Prcb);
                continue;
            }

            OldPriority = Thread->Priority;
            Thread->Priority = (SCHAR)Priority;

            /*
             * Lowering a thread that holds a processor can make a queued one
             * the better choice; let that processor pick again.
             */
            if (Priority < OldPriority)
            {
                if (State == Standby)
                {
                    if (Prcb->NextThread == Thread)
                    {
                        Better = KiSelectReadyThread(Priority + 1, Prcb);
                        if (Better != NULL)
                        {
                            ASSERT(Better != Thread);
                            Better->State = Standby;
                            Better->NextProcessor = Processor;
                            Prcb->NextThread = Better;
                            KiQueueReadyThread(Thread, Prcb);
                            return;
                        }
                    }
                }
                else
                {
                    Current = Prcb->CurrentThread;
                    if ((Current == Thread) && (Prcb->NextThread == NULL))
                    {
                        Better = KiSelectReadyThread(Priority + 1, Prcb);
                        if (Better != NULL)
                        {
                            Better->State = Standby;
                            Better->NextProcessor = Processor;
                            Prcb->NextThread = Better;
                            Thread->Preempted = TRUE;
                            KiReleasePrcbLock(Prcb);
                            KiRescheduleThread(TRUE, Processor);
                            return;
                        }
                    }
                }
            }

            KiReleasePrcbLock(Prcb);
            return;
        }

        /* FIXME: a waiting thread readied concurrently can be queued at the old priority between this read and the write */
        Thread->Priority = (SCHAR)Priority;
        return;
    }
}

/*
 * Confines a thread to a set of processors. A thread already running somewhere
 * the new set excludes is moved off it; one that is queued there is queued
 * again where it is allowed to be.
 *
 * Entered with the thread lock held at SYNCH_LEVEL. Answers the previous set.
 */
KAFFINITY
FASTCALL
KiSetAffinityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KAFFINITY Affinity)
{
    KAFFINITY OldAffinity = KiThreadAffinityMask(Thread);
    PKPRCB Prcb;
    ULONG Processor;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT((Affinity & KeActiveProcessors) != 0);

    KiThreadAffinityMask(Thread) = Affinity;
    Thread->AffinityVersion++;

    /*
     * Keep the ideal processor inside the new set; otherwise every placement
     * would start from a processor the thread may not use.
     */
    if ((Affinity & AFFINITY_MASK(Thread->IdealProcessor)) == 0)
        Thread->IdealProcessor = KiFirstSetProcessor(Affinity & KeActiveProcessors);

    switch (Thread->State)
    {
        case Ready:
        {
            if (Affinity & AFFINITY_MASK(Thread->NextProcessor))
                break;

            Prcb = KiAcquireReadyThreadPrcb(Thread);
            if (Prcb == NULL)
                break;

            if (Affinity & Prcb->SetMember)
            {
                KiReleasePrcbLock(Prcb);
                break;
            }

            KiAssertReadyQueue(Prcb, Thread->Priority);
            if (RemoveEntryList(&Thread->WaitListEntry))
                Prcb->ReadySummary ^= PRIORITY_MASK(Thread->Priority);

            Thread->State = DeferredReady;
            KiReleasePrcbLock(Prcb);

            KiDeferredReadyThread(Thread);
            break;
        }

        case Standby:
        {
            Processor = Thread->NextProcessor;
            if (Affinity & AFFINITY_MASK(Processor))
                break;

            Prcb = KiProcessorBlock[Processor];
            KiAcquirePrcbLock(Prcb);

            if (Prcb->NextThread == Thread)
            {
                Prcb->NextThread = NULL;
                KiReleasePrcbLock(Prcb);
                Thread->State = DeferredReady;
                KiDeferredReadyThread(Thread);
                break;
            }

            KiReleasePrcbLock(Prcb);
            break;
        }

        case Running:
        {
            Processor = Thread->NextProcessor;
            if (Affinity & AFFINITY_MASK(Processor))
                break;

            /*
             * It is running where it is no longer allowed to. Asking that
             * processor to dispatch moves it at the next safe point; the
             * ready path will then place it inside the new set.
             */
            Prcb = KiProcessorBlock[Processor];
            KiAcquirePrcbLock(Prcb);
            if (Prcb->NextThread == NULL)
            {
                /* What it picks is off the ready queue from here on, so it must not still look ready */
                Prcb->NextThread = KiSelectNextThread(Prcb);
                Prcb->NextThread->State = Standby;
            }
            KiReleasePrcbLock(Prcb);
            KiRescheduleThread(TRUE, Processor);
            break;
        }

        default:
            break;
    }

    return OldAffinity;
}

/*
 * Charges the wait decrement against a thread's turn. A thread that waits
 * repeatedly inside one turn still uses it up, which is what keeps a tight
 * wait loop from holding a processor indefinitely.
 */
VOID
NTAPI
KiAdjustQuantumThread(
    _Inout_ PKTHREAD Thread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KPRIORITY Priority;

    ASSERT(Thread == Prcb->CurrentThread);

    if (Thread->BasePriority >= LOW_REALTIME_PRIORITY)
        return;

    if (Thread->DisableQuantum)
        return;

    if (!KiDecrementThreadQuantum(Thread, WAIT_QUANTUM_DECREMENT))
        return;

    /* The turn is over: hand back the boost and start a new one */
    KiSetThreadQuantum(Thread, Thread->QuantumReset);

    Priority = KiComputeNewPriority(Thread, 1);
    if (Priority < Thread->BasePriority)
        Priority = Thread->BasePriority;

    KiAcquireThreadLock(Thread);
    KiSetPriorityThread(Thread, Priority);
    KiReleaseThreadLock(Thread);

    KiAcquirePrcbLock(Prcb);
    if ((Prcb->NextThread == NULL) && (Prcb->CurrentThread == Thread))
    {
        PKTHREAD Next = KiSelectReadyThread(Priority, Prcb);

        if (Next != NULL)
        {
            Next->State = Standby;
            Next->NextProcessor = Prcb->Number;
            Prcb->NextThread = Next;
        }
    }
    KiReleasePrcbLock(Prcb);
}

/*
 * Gives the processor to the thread standing by for it. The architecture's
 * dispatch interrupt asks for a turn to be ended and expects the switch to have
 * happened when that returns; the outgoing thread queues behind its peers.
 */
static
VOID
KiSwitchToStandbyThread(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKTHREAD NextThread;
    KIRQL OldIrql;

    if (Prcb->NextThread == NULL)
        return;

    /* The idle loop takes a standby thread itself, and the idle thread is never put on a queue */
    if (Thread == Prcb->IdleThread)
        return;

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquirePrcbLock(Prcb);

    NextThread = Prcb->NextThread;
    if ((NextThread == NULL) || KiConsumeSelfNextThread(Prcb, Thread))
    {
        KiReleasePrcbLock(Prcb);
        KeLowerIrql(OldIrql);
        return;
    }

    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NextThread;
    NextThread->State = Running;

    Thread->WaitReason = WrQuantumEnd;
    Thread->NextProcessor = Prcb->Number;
    KxQueueReadyThread(Thread, Prcb);

    /* A thread placed here by another processor may still be leaving it */
    KiWaitForThreadSwapOut(NextThread);

    KiSwapContext(APC_LEVEL, Thread);

    KeLowerIrql(OldIrql);
}

/*
 * The end of a thread's turn, reached from the clock interrupt by way of a
 * dispatch interrupt. The thread keeps running only if nothing of its priority
 * or better is waiting for this processor.
 */
VOID
NTAPI
KiQuantumEnd(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread = Prcb->CurrentThread;
    PKTHREAD Next;
    KPRIORITY Priority;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (SmpDbgEnabled)
        SmpDbgQuantumEnd(Prcb->Number);

    Prcb->QuantumEnd = FALSE;

    /* The thread working through threaded DPCs gives the processor up when its queue is empty, not when the clock says so */
    if (KiIsDpcThreadBusy(Prcb, Thread))
    {
        KiSetThreadQuantum(Thread, Thread->QuantumReset);
        return;
    }

    if (Thread != Prcb->IdleThread)
        KiCheckProcessorUnpark(Thread);

    /*
     * A preemption that was deferred is honoured here. This is the point the
     * thread agreed to: it asked not to be interrupted mid-operation, not to
     * keep the processor indefinitely.
     */
    if (Thread->QueueDeferPreemption)
    {
        Thread->QueueDeferPreemption = FALSE;
        Thread->DeferPreemption = FALSE;
        Thread->WaitReason = WrDeferredPreempt;

        KiAcquirePrcbLock(Prcb);
        if (Prcb->NextThread == NULL)
        {
            Next = KiSelectReadyThread(Thread->Priority + 1, Prcb);
            if (Next != NULL)
            {
                Next->State = Standby;
                Next->NextProcessor = Prcb->Number;
                Prcb->NextThread = Next;
                Thread->Preempted = TRUE;
            }
        }
        KiReleasePrcbLock(Prcb);
    }

    /* An expired turn is replenished whether or not the thread keeps running */
    if (!KiIsThreadQuantumExpired(Thread))
    {
        KiAcquirePrcbLock(Prcb);
        if (Prcb->NextThread == NULL)
        {
            Next = KiSelectReadyThread(Thread->Priority + 1, Prcb);
            if (Next != NULL)
            {
                Next->State = Standby;
                Next->NextProcessor = Prcb->Number;
                Prcb->NextThread = Next;
                Thread->Preempted = TRUE;
            }
        }
        KiReleasePrcbLock(Prcb);
        KiSwitchToStandbyThread(Prcb, Thread);
        return;
    }

    KiAcquireThreadLock(Thread);

    KiSetThreadQuantum(Thread, Thread->QuantumReset);

    /* A turn that ran out goes to the back of its queue, even behind a preemption already standing by */
    Thread->Preempted = FALSE;

    if ((Thread->BasePriority < LOW_REALTIME_PRIORITY) &&
        !Thread->DisableQuantum)
    {
        Priority = KiComputeNewPriority(Thread, 1);
        KiSetPriorityThread(Thread, Priority);
    }

    KiReleaseThreadLock(Thread);

    if (Thread == Prcb->IdleThread)
        return;

    KiAcquirePrcbLock(Prcb);

    if (Prcb->NextThread == NULL)
    {
        Next = KiSelectReadyThread(Thread->Priority, Prcb);
        if (Next != NULL)
        {
            Next->State = Standby;
            Next->NextProcessor = Prcb->Number;
            Prcb->NextThread = Next;
            Thread->Preempted = FALSE;
        }
    }

    if ((Prcb->NextThread == NULL) && KiShouldVacateProcessor(Thread, Prcb))
    {
        Prcb->IdleThread->State = Standby;
        Prcb->NextThread = Prcb->IdleThread;
    }

    KiReleasePrcbLock(Prcb);

    /* A balance pass is cheap here and keeps one processor from hoarding work */
    KiBalanceReadyQueues(Prcb, KiBalanceQuantum);

    KiSwitchToStandbyThread(Prcb, Thread);
}

/*
 * Hands a dead thread to whoever reaps it. The thread cannot do this for
 * itself: its stack is still in use until the switch away from it completes.
 */
VOID
FASTCALL
KiQueueThreadForReaping(
    _Inout_ PKTHREAD Thread)
{
    PETHREAD EThread = CONTAINING_RECORD(Thread, ETHREAD, Tcb);
    PVOID Head, Next;

    ASSERT(Thread->State == Terminated);

    /*
     * The reaper's list is a lock-free stack: the thread being reaped cannot
     * take a lock, because this runs on the processor that just switched away
     * from its stack.
     */
    do
    {
        Head = PspReaperListHead.Flink;
        EThread->ReaperLink = (PETHREAD)Head;
        Next = Head;
    } while (InterlockedCompareExchangePointer((PVOID volatile *)&PspReaperListHead.Flink,
                                               EThread,
                                               Next) != Next);

    /*
     * Only the thread that found the list empty owes the reaper a wake-up. This
     * runs at SYNCH_LEVEL right after a switch, so the work item goes into the
     * worker queue under the dispatcher lock rather than through ExQueueWorkItem.
     */
    if (Head == NULL)
    {
        KiAcquireDispatcherLockAtSynchLevel();
        KiInsertQueue(&ExWorkerQueue[CriticalWorkQueue].WorkerQueue,
                      &PspReaperWorkItem.List,
                      FALSE);
        KiReleaseDispatcherLockFromSynchLevel();
    }
}

/*
 * Looks for a processor in the given set that is the least busy. Used when no
 * idle processor is available and the ideal one is out of bounds, so the answer
 * only has to be the best of a set of busy processors.
 */
ULONG
NTAPI
KiFindIdealProcessor(
    _In_ KAFFINITY ProcessorSet,
    _In_ UCHAR OriginalIdealProcessor)
{
    KAFFINITY Remaining = ProcessorSet;
    ULONG Best = OriginalIdealProcessor;
    ULONG BestSummary = MAXULONG;
    ULONG Processor;
    PKPRCB Prcb;
    ULONG Summary;

    if (ProcessorSet == 0)
        return OriginalIdealProcessor;

    if ((ProcessorSet & AFFINITY_MASK(OriginalIdealProcessor)) == 0)
        Best = KiFirstSetProcessor(ProcessorSet);

    while (Remaining != 0)
    {
        Processor = KiFirstSetProcessor(Remaining);
        Remaining &= ~AFFINITY_MASK(Processor);

        Prcb = KiProcessorBlock[Processor];
        if (Prcb == NULL)
            continue;

        /*
         * The ready summary is a good enough measure of load here: its highest
         * set bit is the best thread already queued, and a processor with
         * nothing queued at all reads as zero.
         */
        Summary = Prcb->ReadySummary;
        if (Summary == 0)
            return Processor;

        if (Summary < BestSummary)
        {
            BestSummary = Summary;
            Best = Processor;
        }
    }

    return Best;
}

/*
 * Pulls work towards a processor that has none. Only a thread that may run on
 * the target is taken, and only from a processor that would still have work
 * left, so balancing never empties one queue to fill another.
 */
BOOLEAN
FASTCALL
KiBalanceReadyQueues(
    _Inout_ PKPRCB Target,
    _In_ KI_BALANCE_REASON Reason)
{
    KAFFINITY Candidates;
    PKPRCB Source;
    PKTHREAD Thread;
    PLIST_ENTRY Entry;
    ULONG Processor;
    LONG Priority;
    BOOLEAN Moved = FALSE;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Only a processor with nothing of its own to run goes looking */
    if (Target->ReadySummary != 0)
        return FALSE;

    Candidates = KeActiveProcessors & ~AFFINITY_MASK(Target->Number);
    while (Candidates != 0)
    {
        Processor = KiFirstSetProcessor(Candidates);
        Candidates &= ~AFFINITY_MASK(Processor);

        Source = KiProcessorBlock[Processor];
        if (Source == NULL)
            continue;

        /* One queued thread is that processor's own next turn; leave it */
        if (Source->ReadySummary == 0)
            continue;

        KiAcquirePrcbLock(Source);

        if (Source->ReadySummary == 0)
        {
            KiReleasePrcbLock(Source);
            continue;
        }

        BitScanReverse((PULONG)&Priority, Source->ReadySummary);
        KiAssertReadyQueue(Source, Priority);

        Entry = Source->DispatcherReadyListHead[Priority].Blink;
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);

        if ((Thread->State != Ready) ||
            ((KiThreadAffinityMask(Thread) & Target->SetMember) == 0) ||
            !KiCanStealToProcessor(Thread, Target->Number))
        {
            KiReleasePrcbLock(Source);
            continue;
        }

        if (RemoveEntryList(&Thread->WaitListEntry))
            Source->ReadySummary ^= PRIORITY_MASK(Priority);

        Thread->State = DeferredReady;
        KiReleasePrcbLock(Source);

        Thread->NextProcessor = Target->Number;
        Thread->AdjustReason = AdjustNone;
        KiDeferredReadyThread(Thread);

        Moved = TRUE;
        break;
    }

    if (Moved && (Reason == KiBalanceIdle))
        KiRescheduleThread(TRUE, Target->Number);

    return Moved;
}

/*
 * Gives up the rest of this thread's turn to anything else that is ready. The
 * answer tells the caller whether anything took it.
 */
NTSTATUS
NTAPI
NtYieldExecution(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPRCB Prcb;
    PKTHREAD Next;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_NO_YIELD_PERFORMED;

    OldIrql = KeRaiseIrqlToSynchLevel();
    Prcb = KeGetCurrentPrcb();

    if (Prcb->ReadySummary == 0)
    {
        KeLowerIrql(OldIrql);
        KeStallExecutionProcessor(KI_EMPTY_YIELD_STALL_US);
        return Status;
    }

    /* Handing the processor over ends this turn as a quantum end does, so a boost decays */
    KiAcquireThreadLock(Thread);
    KiSetThreadQuantum(Thread, Thread->QuantumReset);
    if ((Thread->BasePriority < LOW_REALTIME_PRIORITY) && !Thread->DisableQuantum)
        KiSetPriorityThread(Thread, KiComputeNewPriority(Thread, 1));
    KiReleaseThreadLock(Thread);

    KiAcquirePrcbLock(Prcb);

    /* Any thread ready on this processor takes the turn, whatever its priority */
    if (Prcb->NextThread == NULL)
    {
        Next = KiSelectReadyThread(0, Prcb);
        if (Next != NULL)
        {
            Next->State = Standby;
            Next->NextProcessor = Prcb->Number;
            Prcb->NextThread = Next;
        }
    }

    if (Prcb->NextThread != NULL)
    {
        Thread->Preempted = FALSE;
        Thread->WaitReason = WrYieldExecution;
        Status = STATUS_SUCCESS;
    }

    KiReleasePrcbLock(Prcb);

    KiExitDispatcher(OldIrql);

    if (Status == STATUS_NO_YIELD_PERFORMED)
        KeStallExecutionProcessor(KI_EMPTY_YIELD_STALL_US);

    return Status;
}

/* EOF */
