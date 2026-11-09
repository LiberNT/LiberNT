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
 * Entered with the dispatcher database held.
 */
static
BOOLEAN
KiHandQueueEntryToWaiter(
    _Inout_ PKQUEUE Queue,
    _In_opt_ PLIST_ENTRY Entry)
{
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread;

    if (!KiQueueHasRoom(Queue))
        return FALSE;

    if (IsListEmpty(&Queue->Header.WaitListHead))
        return FALSE;

    WaitBlock = CONTAINING_RECORD(Queue->Header.WaitListHead.Blink,
                                  KWAIT_BLOCK,
                                  WaitListEntry);

    /*
     * A thread that waited on the queue as an ordinary dispatcher object is
     * not here to take work, so it is left to the usual satisfaction rules.
     */
    if (WaitBlock->WaitType != WaitDequeue)
        return FALSE;

    if (Entry == NULL)
    {
        if (IsListEmpty(&Queue->EntryListHead))
            return FALSE;

        Entry = RemoveHeadList(&Queue->EntryListHead);
        Queue->Header.SignalState--;
    }

    Thread = WaitBlock->Thread;

    /*
     * The entry itself is the wait status. That is what makes KeRemoveQueue
     * answer a list entry rather than a status, and why its callers have to
     * tell the two apart by value.
     */
    KiUnwaitThread(Thread, (LONG_PTR)Entry, 0);
    return TRUE;
}

/*
 * Takes one entry off a queue, blocking until there is one if need be.
 *
 * Answers the entry, or NULL with the reason in Status. The caller is on the
 * queue's thread list when this returns either way, and is counted among the
 * threads the queue considers active.
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
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

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
            Thread->WaitIrql = KiAcquireDispatcherLock();
        }

        PreviousQueue = Thread->Queue;
        if (PreviousQueue != Queue)
        {
            /*
             * Changing queues. The one being left has to be told, because a
             * thread it was counting is gone and one of its waiters may now
             * be allowed through.
             */
            if (PreviousQueue != NULL)
            {
                RemoveEntryList(&Thread->QueueListEntry);
                KiActivateWaiterQueue(PreviousQueue);
            }

            InsertTailList(&Queue->ThreadListHead, &Thread->QueueListEntry);
            Thread->Queue = Queue;
        }
        else
        {
            /* Looking for work is not doing work: stop counting until we have it */
            KiDecrementQueueCurrentCount(Queue);
        }

        if ((Queue->Header.SignalState > 0) &&
            !IsListEmpty(&Queue->EntryListHead) &&
            KiQueueHasRoom(Queue))
        {
            Entry = RemoveHeadList(&Queue->EntryListHead);
            Queue->Header.SignalState--;
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return Entry;
        }

        WaitCheck = KiCheckAlertability(Thread, Alertable, WaitMode);
        if (WaitCheck != STATUS_WAIT_0)
        {
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherLock(Thread->WaitIrql);
            *Status = WaitCheck;
            return NULL;
        }

        if ((Timeout != NULL) && (Timeout->QuadPart == 0))
        {
            KiIncrementQueueCurrentCount(Queue);
            KiReleaseDispatcherLock(Thread->WaitIrql);
            *Status = STATUS_TIMEOUT;
            return NULL;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KiIncrementQueueCurrentCount(Queue);
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

        if (Timeout != NULL)
        {
            KxChainSingleWithTimer();

            KxSetTimerForThreadWait(Timer, *Timeout, WaitMode, &Hand);
            DueTime.QuadPart = Timer->DueTime.QuadPart;

            if (Timer->DueTime.QuadPart == 0)
            {
                Thread->TimerActive = FALSE;
                Thread->WaitBlockList = NULL;
                KiIncrementQueueCurrentCount(Queue);
                KiReleaseDispatcherLock(Thread->WaitIrql);
                *Status = STATUS_TIMEOUT;
                return NULL;
            }

            TimerBlock->Thread = Thread;
            TimerBlock->Object = Timer;
            TimerBlock->WaitKey = (USHORT)STATUS_TIMEOUT;
            TimerBlock->WaitType = WaitAny;

            KiAcquireDispatcherObject(&Timer->Header);
            Timer->Header.SignalState = FALSE;
            Timer->Header.Inserted = TRUE;
            Timer->TimerListEntry.Flink = NULL;
            Timer->TimerListEntry.Blink = NULL;
            KxPublishTimerWaitBlock();
            KxInsertTimer(Timer, Hand);
        }
        else
        {
            KxChainSingleOnly();
        }

        Thread->Alertable = Alertable;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = WrQueue;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        InsertTailList(&Queue->Header.WaitListHead, &WaitBlock->WaitListEntry);

        if (!KxTryBeginThreadWait(Thread))
        {
            KiUnlinkWaitBlocks(Thread);
            Thread->TimerActive = FALSE;
            if (Timer->Header.Inserted)
                KxRemoveTreeTimer(Timer);

            Thread->WaitBlockList = NULL;
            KiIncrementQueueCurrentCount(Queue);
            KxExitDispatcherForRetry(Thread);
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
    Queue->Header.Hand = 0;
    Queue->Header.Inserted = FALSE;
    Queue->Header.DebugActive = FALSE;
    Queue->Header.SignalState = 0;
    InitializeListHead(&Queue->Header.WaitListHead);

    InitializeListHead(&Queue->EntryListHead);
    InitializeListHead(&Queue->ThreadListHead);

    Queue->CurrentCount = 0;
    Queue->MaximumCount = (Count != 0) ? Count : (ULONG)(CCHAR)KeNumberProcessors;
}

/*
 * Adds work to a queue. Where a thread is already parked on it and the queue
 * has room, the entry is handed over directly rather than queued and dequeued.
 *
 * Entered with the dispatcher database held. Answers the number of entries
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

    OldIrql = KiAcquireDispatcherLock();
    PreviousState = KiInsertQueue(Queue, Entry, FALSE);
    KiReleaseDispatcherLock(OldIrql);

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

    OldIrql = KiAcquireDispatcherLock();
    PreviousState = KiInsertQueue(Queue, Entry, TRUE);
    KiReleaseDispatcherLock(OldIrql);

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
 * Answers how many entries were written, which is zero when the wait ended
 * without work.
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
        return 0;

    EntryArray[Removed++] = Entry;

    if (Removed == Count)
        return Removed;

    OldIrql = KiAcquireDispatcherLock();

    while ((Removed < Count) && (Queue->Header.SignalState > 0) &&
           !IsListEmpty(&Queue->EntryListHead))
    {
        EntryArray[Removed++] = RemoveHeadList(&Queue->EntryListHead);
        Queue->Header.SignalState--;
    }

    KiReleaseDispatcherLock(OldIrql);
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

    OldIrql = KiAcquireDispatcherLock();

    if (!IsListEmpty(&Queue->EntryListHead))
    {
        FirstEntry = Queue->EntryListHead.Flink;
        RemoveEntryList(&Queue->EntryListHead);
        InitializeListHead(&Queue->EntryListHead);
    }

    Queue->Header.SignalState = 0;

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

        KiUnwaitThread(WaitBlock->Thread, STATUS_ABANDONED, 0);
    }

    while (!IsListEmpty(&Queue->ThreadListHead))
    {
        Entry = RemoveHeadList(&Queue->ThreadListHead);
        Thread = CONTAINING_RECORD(Entry, KTHREAD, QueueListEntry);
        Thread->Queue = NULL;
    }

    Queue->CurrentCount = 0;

    KiReleaseDispatcherLock(OldIrql);
    return FirstEntry;
}

/*
 * Called when a thread the queue was counting stops running for a reason of
 * its own. One fewer thread is at the work, so one more may be let through.
 *
 * Entered with the dispatcher database held.
 */
VOID
FASTCALL
KiActivateWaiterQueue(
    _Inout_ PKQUEUE Queue)
{
    ASSERT_QUEUE(Queue);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KiDecrementQueueCurrentCount(Queue);

    KiHandQueueEntryToWaiter(Queue, NULL);
}

/* EOF */
