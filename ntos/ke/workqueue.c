/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A list of work plus a limit on how many threads may be at it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether the queue would let one more thread take work right now. The count
 * excludes the thread asking, which has already stopped counting itself.
 */
FORCEINLINE
BOOLEAN
KiQueueHasRoom(
    _In_ PKQUEUE Queue)
{
    return (ULONG)Queue->CurrentCount < Queue->MaximumCount;
}

/*
 * Hands the entry at the head of the queue to a thread parked on it. Answers
 * FALSE when there was nobody suitable, which leaves the entry where it is.
 *
 * The most recently blocked thread is chosen rather than the longest waiting
 * one: its stack and its share of the caches are the warmest, and for a work
 * queue that is worth more than fairness between its own threads.
 *
 * Entered with the queue's object lock held.
 */
static
BOOLEAN
KiHandQueueEntryToWaiter(
    _Inout_ PKQUEUE Queue,
    _In_opt_ PLIST_ENTRY Entry)
{
    PKWAIT_BLOCK WaitBlock;
    PLIST_ENTRY WaitEntry;
    PKTHREAD Thread;

    ASSERT(Queue->Header.Lock & KOBJECT_LOCK_BIT);

    if (!KiQueueHasRoom(Queue))
        return FALSE;

    if ((Entry == NULL) && IsListEmpty(&Queue->EntryListHead))
        return FALSE;

    for (;;)
    {
        WaitBlock = NULL;

        /*
         * A thread that waited on the queue as an ordinary dispatcher object is
         * not here to take work, so it is left to the usual satisfaction rules.
         */
        for (WaitEntry = Queue->Header.WaitListHead.Blink;
             WaitEntry != &Queue->Header.WaitListHead;
             WaitEntry = WaitEntry->Blink)
        {
            PKWAIT_BLOCK Candidate = CONTAINING_RECORD(WaitEntry,
                                                       KWAIT_BLOCK,
                                                       WaitListEntry);

            if (Candidate->WaitType == WaitDequeue)
            {
                WaitBlock = Candidate;
                break;
            }
        }

        if (WaitBlock == NULL)
            return FALSE;

        /* A thread that has not committed comes back and takes the entry from the queue itself */
        Thread = KiLockCommittedWaiter(WaitBlock);
        if (Thread != NULL)
            break;
    }

    if (Entry == NULL)
    {
        Entry = RemoveHeadList(&Queue->EntryListHead);
        Entry->Flink = NULL;
        Queue->Header.SignalState--;
    }

    /*
     * The entry itself is the wait status. That is what makes KeRemoveQueue
     * answer a list entry rather than a status, and why its callers have to
     * tell the two apart by value.
     */
    KiUnwaitThread(Thread, (LONG_PTR)Entry, 0);
    KiReleaseThreadLock(Thread);
    KiUnlinkWaitBlock(WaitBlock);
    return TRUE;
}

static
NTSTATUS
KiCancelDequeueWait(
    _Inout_ PKQUEUE Queue,
    _Inout_ PKTHREAD Thread,
    _In_ NTSTATUS Status)
{
    KiEndThreadWait(Thread);
    Thread->WaitBlockList = NULL;
    KiAcquireDispatcherObject(&Queue->Header);
    if (Queue->Header.Abandoned)
        Status = STATUS_ABANDONED;
    else
        KiIncrementQueueCurrentCount(Queue);
    KiReleaseDispatcherObject(&Queue->Header);
    return Status;
}

/*
 * Takes one entry off a queue, blocking until there is one if need be.
 *
 * Answers the entry, or NULL with the reason in Status. The caller is on the
 * queue's thread list when this returns either way, and is counted among the
 * threads the queue considers active, unless an APC or alert ended the call
 * before it joined the queue.
 */
static
PLIST_ENTRY
KiDequeueOrWait(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PNTSTATUS Status)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK WaitBlock = &Thread->WaitBlock[0];
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PKTIMER Timer = &Thread->Timer;
    PKQUEUE PreviousQueue;
    PLIST_ENTRY Entry;
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime, NewDueTime;
    BOOLEAN Swappable;
    LONG_PTR WaitStatus;
    NTSTATUS WaitCheck;
    ULONG Hand;

    ASSERT_QUEUE(Queue);
    ASSERT(Thread->WaitNext || (KeGetCurrentIrql() <= DISPATCH_LEVEL));

    DueTime.QuadPart = 0;
    *Status = STATUS_SUCCESS;

    for (;;)
    {
        if (Thread->WaitNext)
        {
            Thread->WaitNext = FALSE;
        }
        else
        {
            Thread->WaitIrql = KiEnterDispatcher();
        }

        if (Thread->Queue != Queue)
        {
            /* A thread not yet serving this queue takes a pending APC before work and stays where it was */
            WaitCheck = KiCheckAlertability(Thread, Alertable, WaitMode);
            if (WaitCheck != STATUS_WAIT_0)
            {
                KiExitDispatcher(Thread->WaitIrql);
                *Status = WaitCheck;
                return NULL;
            }

            /*
             * Changing queues. The one being left has to be told, because a
             * thread it was counting is gone and one of its waiters may now
             * be allowed through.
             */
            PreviousQueue = KiLockThreadQueue(Thread);
            if (PreviousQueue != NULL)
            {
                KiAcquireThreadLock(Thread);
                RemoveEntryList(&Thread->QueueListEntry);
                Thread->Queue = NULL;
                KiReleaseThreadLock(Thread);

                KiActivateWaiterQueue(PreviousQueue);
                KiReleaseDispatcherObject(&PreviousQueue->Header);
            }
        }

        KiAcquireDispatcherObject(&Queue->Header);
        if (Queue->Header.Abandoned)
        {
            KiReleaseDispatcherObject(&Queue->Header);
            KiExitDispatcher(Thread->WaitIrql);
            *Status = STATUS_ABANDONED;
            return NULL;
        }

        if (Thread->Queue != Queue)
        {
            KiAcquireThreadLock(Thread);
            InsertTailList(&Queue->ThreadListHead, &Thread->QueueListEntry);
            Thread->Queue = Queue;
            KiReleaseThreadLock(Thread);
        }
        else
        {
            /* Looking for work is not doing work: stop counting until we have it */
            KiDecrementQueueCurrentCount(Queue);
        }

        KiBeginThreadWait(Thread);

        if ((Queue->Header.SignalState > 0) &&
            !IsListEmpty(&Queue->EntryListHead) &&
            KiQueueHasRoom(Queue))
        {
            Entry = RemoveHeadList(&Queue->EntryListHead);
            Entry->Flink = NULL;
            Queue->Header.SignalState--;
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherObject(&Queue->Header);
            KiExitDispatcher(Thread->WaitIrql);
            return Entry;
        }

        WaitCheck = KiCheckAlertability(Thread, Alertable, WaitMode);
        if (WaitCheck != STATUS_WAIT_0)
        {
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherObject(&Queue->Header);
            KiExitDispatcher(Thread->WaitIrql);
            *Status = WaitCheck;
            return NULL;
        }

        if ((Timeout != NULL) && (Timeout->QuadPart == 0))
        {
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherObject(&Queue->Header);
            KiExitDispatcher(Thread->WaitIrql);
            *Status = STATUS_TIMEOUT;
            return NULL;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherObject(&Queue->Header);
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = WaitBlock;
        Thread->WaitStatus = STATUS_SUCCESS;
        WaitBlock->Thread = Thread;
        WaitBlock->Object = Queue;
        WaitBlock->WaitKey = (USHORT)STATUS_SUCCESS;
        WaitBlock->WaitType = WaitDequeue;
        WaitBlock->WaitListEntry.Flink = NULL;
        WaitBlock->WaitListEntry.Blink = NULL;
        TimerBlock->WaitListEntry.Flink = NULL;
        TimerBlock->WaitListEntry.Blink = NULL;
        KxChainSingleOnly();

        WaitBlock->BlockState = WaitBlockActive;
        InsertTailList(&Queue->Header.WaitListHead, &WaitBlock->WaitListEntry);
        KiReleaseDispatcherObject(&Queue->Header);

        if (Timeout != NULL)
        {
            KxChainSingleWithTimer();

            KiAcquireDispatcherObject(&Timer->Header);
            ASSERT(!Timer->Header.Inserted);
            KxSetTimerForThreadWait(Timer, *Timeout, WaitMode, &Hand);
            DueTime.QuadPart = Timer->DueTime.QuadPart;

            if (Timer->DueTime.QuadPart == 0)
            {
                KiReleaseDispatcherObject(&Timer->Header);
                Thread->TimerActive = FALSE;
                *Status = KiCancelDequeueWait(Queue, Thread, STATUS_TIMEOUT);
                KiExitDispatcher(Thread->WaitIrql);
                return NULL;
            }

            ASSERT(Timer == &Thread->Timer);
            TimerBlock->Thread = Thread;
            TimerBlock->WaitKey = (USHORT)STATUS_TIMEOUT;
            TimerBlock->WaitType = WaitAny;

            Timer->Header.SignalState = FALSE;
            Timer->Header.Inserted = TRUE;
            Timer->TimerListEntry.Flink = NULL;
            Timer->TimerListEntry.Blink = NULL;
            KxPublishTimerWaitBlock();
            if (!KxInsertThreadWaitTimer(Timer, Hand))
            {
                KiAcquireDispatcherObject(&Timer->Header);
                ASSERT(TimerBlock->WaitListEntry.Flink != NULL);
                KiUnlinkWaitBlock(TimerBlock);
                KiReleaseDispatcherObject(&Timer->Header);
                Thread->TimerActive = FALSE;
                *Status = KiCancelDequeueWait(Queue, Thread, STATUS_TIMEOUT);
                KiExitDispatcher(Thread->WaitIrql);
                return NULL;
            }
        }

        Thread->Alertable = Alertable;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = WrQueue;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        if (!KxTryBeginThreadWait(Thread))
        {
            WaitCheck = KiCancelDequeueWait(Queue, Thread, STATUS_KERNEL_APC);
            if (WaitCheck == STATUS_ABANDONED)
            {
                KiExitDispatcher(Thread->WaitIrql);
                *Status = WaitCheck;
                return NULL;
            }
            KxExitDispatcherForRetry(Thread);
            if ((Timeout != NULL) && (DueTime.QuadPart != 0))
            {
                Timeout = KiRecalculateDueTime(OriginalDueTime,
                                               &DueTime,
                                               &NewDueTime);
            }
            continue;
        }

        KxCommitThreadWait(Thread, Swappable);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus == STATUS_KERNEL_APC)
        {
            if (Timeout != NULL)
            {
                Timeout = KiRecalculateDueTime(OriginalDueTime,
                                               &DueTime,
                                               &NewDueTime);
            }
            continue;
        }

        /*
         * Anything other than a status is the entry that was handed to us.
         * Only the handful of statuses the wait can end with are small enough
         * to be confused with a pointer, and none of them is a valid one.
         */
        if ((WaitStatus == STATUS_TIMEOUT) ||
            (WaitStatus == STATUS_USER_APC) ||
            (WaitStatus == STATUS_ALERTED) ||
            (WaitStatus == STATUS_ABANDONED))
        {
            *Status = (NTSTATUS)WaitStatus;
            return NULL;
        }

        return (PLIST_ENTRY)WaitStatus;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * A queue holds work and limits how many threads may be running it at once.
 * A count of zero asks for one thread per processor, which is what makes a
 * queue a sensible default for a pool.
 */
VOID
NTAPI
KeInitializeQueue(
    _Out_ PKQUEUE Queue,
    _In_ ULONG Count)
{
    Queue->Header.Type = QueueObject;
    Queue->Header.Signalling = FALSE;
    Queue->Header.Size = sizeof(KQUEUE) / sizeof(ULONG);
    Queue->Header.Inserted = FALSE;
    Queue->Header.DebugActive = FALSE;
    Queue->Header.SignalState = 0;
    InitializeListHead(&Queue->Header.WaitListHead);

    InitializeListHead(&Queue->EntryListHead);
    InitializeListHead(&Queue->ThreadListHead);

    Queue->CurrentCount = 0;
    Queue->MaximumCount = (Count != 0) ? Count : KeProcessorCount;
}

/*
 * Adds work to a queue. Where a thread is already parked on it and the queue
 * has room, the entry is handed over directly rather than queued and dequeued.
 *
 * Entered with the queue's object lock held. Answers the number of entries
 * that were waiting before this one.
 */
LONG
NTAPI
KiInsertQueue(
    _Inout_ PKQUEUE Queue,
    _Inout_ PLIST_ENTRY Entry,
    _In_ BOOLEAN Head)
{
    LONG PreviousState;

    ASSERT_QUEUE(Queue);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Queue->Header.Lock & KOBJECT_LOCK_BIT);

    PreviousState = Queue->Header.SignalState;

    if (KiHandQueueEntryToWaiter(Queue, Entry))
        return PreviousState;

    Queue->Header.SignalState++;

    if (Head)
        InsertHeadList(&Queue->EntryListHead, Entry);
    else
        InsertTailList(&Queue->EntryListHead, Entry);

    /* A thread waiting on the queue as a plain object is told the usual way */
    if (!IsListEmpty(&Queue->Header.WaitListHead))
        KiWaitTest(Queue, 0);

    return PreviousState;
}

LONG
NTAPI
KeInsertQueue(
    _Inout_ PKQUEUE Queue,
    _Inout_ PLIST_ENTRY Entry)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiEnterDispatcher();
    KiAcquireDispatcherObject(&Queue->Header);
    PreviousState = KiInsertQueue(Queue, Entry, FALSE);
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);

    return PreviousState;
}

/*
 * As above, but the entry goes to the front. Used where the work is a reply to
 * something already in flight and ordering it behind new work would hold that
 * up for no reason.
 */
LONG
NTAPI
KeInsertHeadQueue(
    _Inout_ PKQUEUE Queue,
    _Inout_ PLIST_ENTRY Entry)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiEnterDispatcher();
    KiAcquireDispatcherObject(&Queue->Header);
    PreviousState = KiInsertQueue(Queue, Entry, TRUE);
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);

    return PreviousState;
}

/*
 * Takes work off a queue, waiting for it if there is none.
 *
 * The answer is either a list entry or one of a few small status values. The
 * caller tells them apart by value, which is the contract this interface has
 * always had.
 */
PLIST_ENTRY
NTAPI
KiRemoveQueue(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    Entry = KiDequeueOrWait(Queue, WaitMode, Alertable, Timeout, &Status);
    if (Entry != NULL)
        return Entry;

    return (PLIST_ENTRY)(ULONG_PTR)(ULONG)Status;
}

PLIST_ENTRY
NTAPI
KeRemoveQueue(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    return KiRemoveQueue(Queue, WaitMode, FALSE, Timeout);
}

/*
 * Takes up to Count entries in one call. Only the first may wait; once there
 * is work the rest are taken while the dispatcher is still held, which is the
 * whole point of the call.
 *
 * Answers how many entries were written. A wait that ended without work
 * writes its status in the first slot and counts as one.
 */
ULONG
NTAPI
KeRemoveQueueEx(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PLIST_ENTRY *EntryArray,
    _In_ ULONG Count)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    ULONG Removed = 0;

    ASSERT_QUEUE(Queue);
    ASSERT(Count > 0);

    Entry = KiDequeueOrWait(Queue, WaitMode, Alertable, Timeout, &Status);
    if (Entry == NULL)
    {
        EntryArray[0] = (PLIST_ENTRY)(ULONG_PTR)(ULONG)Status;
        return 1;
    }

    EntryArray[Removed++] = Entry;

    if (Removed == Count)
        return Removed;

    OldIrql = KiEnterDispatcher();
    KiAcquireDispatcherObject(&Queue->Header);

    while ((Removed < Count) && (Queue->Header.SignalState > 0) &&
           !IsListEmpty(&Queue->EntryListHead))
    {
        Entry = RemoveHeadList(&Queue->EntryListHead);
        Entry->Flink = NULL;
        EntryArray[Removed++] = Entry;
        Queue->Header.SignalState--;
    }

    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);
    return Removed;
}

/*
 * The number of entries sitting on the queue. A snapshot, and a caller that
 * acts on it has to be ready for it to have changed.
 */
LONG
NTAPI
KeReadStateQueue(
    _In_ PKQUEUE Queue)
{
    ASSERT_QUEUE(Queue);

    return Queue->Header.SignalState;
}

/*
 * Empties a queue and lets go of every thread associated with it. Answers the
 * entries that were still on it, as a circular list, so the caller can finish
 * or discard each one.
 *
 * Any thread still blocked on the queue is woken with nothing: a queue being
 * torn down will never have work for it.
 */
PLIST_ENTRY
NTAPI
KeRundownQueue(
    _Inout_ PKQUEUE Queue)
{
    KIRQL OldIrql;
    PLIST_ENTRY FirstEntry = NULL;
    PLIST_ENTRY Entry;
    PKTHREAD Thread;

    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiEnterDispatcher();
    KiAcquireDispatcherObject(&Queue->Header);

    if (!IsListEmpty(&Queue->EntryListHead))
    {
        FirstEntry = Queue->EntryListHead.Flink;
        RemoveEntryList(&Queue->EntryListHead);
        InitializeListHead(&Queue->EntryListHead);
    }

    Queue->Header.SignalState = 0;
    Queue->Header.Abandoned = TRUE;

    /*
     * A thread that asked the queue for work is told there will be none. One
     * that only waits on the queue as an object is let go as if it had been
     * signalled, unless it waits for all of several objects: that one looks at
     * its whole set again and finds the queue has nothing for it.
     */
    while (!IsListEmpty(&Queue->Header.WaitListHead))
    {
        PKWAIT_BLOCK WaitBlock =
            CONTAINING_RECORD(Queue->Header.WaitListHead.Flink,
                              KWAIT_BLOCK,
                              WaitListEntry);

        if (WaitBlock->WaitType == WaitDpc)
        {
            IopSignalWaitCompletionPacket(WaitBlock);
            continue;
        }

        if (WaitBlock->WaitType == WaitDequeue)
            Thread = KiLockWaiterOrSatisfy(WaitBlock, STATUS_ABANDONED, NULL);
        else if (WaitBlock->WaitType == WaitAny)
            Thread = KiLockWaiterOrSatisfy(WaitBlock, WaitBlock->WaitKey, NULL);
        else
            Thread = KiLockCommittedWaiter(WaitBlock);
        if (Thread == NULL)
            continue;

        if (WaitBlock->WaitType == WaitDequeue)
        {
            KiUnwaitThread(Thread, STATUS_ABANDONED, 0);
        }
        else if (WaitBlock->WaitType == WaitAny)
        {
            KiUnwaitThread(Thread, WaitBlock->WaitKey, 0);
        }
        else
        {
            KiUnwaitThread(Thread, STATUS_KERNEL_APC, 0);
        }

        KiReleaseThreadLock(Thread);
        KiUnlinkWaitBlock(WaitBlock);
    }

    while (!IsListEmpty(&Queue->ThreadListHead))
    {
        Entry = RemoveHeadList(&Queue->ThreadListHead);
        Thread = CONTAINING_RECORD(Entry, KTHREAD, QueueListEntry);
        KiAcquireThreadLock(Thread);
        Thread->Queue = NULL;
        KiReleaseThreadLock(Thread);
    }

    Queue->CurrentCount = 0;

    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);
    return FirstEntry;
}

/*
 * Called when a thread the queue was counting stops running for a reason of
 * its own. One fewer thread is at the work, so one more may be let through.
 *
 * Entered with the queue's object lock held.
 */
VOID
FASTCALL
KiActivateWaiterQueue(
    _Inout_ PKQUEUE Queue)
{
    ASSERT_QUEUE(Queue);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Queue->Header.Lock & KOBJECT_LOCK_BIT);

    KiDecrementQueueCurrentCount(Queue);

    KiHandQueueEntryToWaiter(Queue, NULL);
}

/* EOF */
