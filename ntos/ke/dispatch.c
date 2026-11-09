/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     How a signalled object hands its state to the threads waiting on it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The blocks one thread has published. On the NT10 layout a wait block carries
 * no link to the next one: the thread names how many of them are in use
 * (WaitBlockCount, counted from WaitBlockList) and says separately whether its
 * own timer block joined the wait (TimerActive). Walking in that order visits
 * every block exactly once whether the array is the thread's own or one the
 * caller supplied.
 */
FORCEINLINE
PKWAIT_BLOCK
KiThreadWaitBlock(
    _In_ PKTHREAD Thread,
    _In_ ULONG Index)
{
    ULONG Count = Thread->WaitBlockCount;

    if (Index < Count)
        return &Thread->WaitBlockList[Index];

    ASSERT(Thread->TimerActive);
    ASSERT(Index == Count);
    return &Thread->WaitBlock[TIMER_WAIT_BLOCK];
}

FORCEINLINE
ULONG
KiThreadWaitBlockCount(
    _In_ PKTHREAD Thread)
{
    ULONG Count = Thread->WaitBlockCount;

    if (Thread->TimerActive)
        Count++;

    return Count;
}

/*
 * A block is linked into its object's wait list only while the wait is
 * published. Satisfaction and cancellation both come through here, so the
 * list entry is cleared after removal: a second pass over the same thread
 * must not unlink a block twice.
 */
static
VOID
KiUnlinkOneWaitBlock(
    _Inout_ PKWAIT_BLOCK WaitBlock)
{
    if (WaitBlock->WaitListEntry.Flink == NULL)
        return;

    ASSERT(WaitBlock->WaitListEntry.Blink != NULL);
    ASSERT(WaitBlock->WaitListEntry.Flink->Blink == &WaitBlock->WaitListEntry);
    ASSERT(WaitBlock->WaitListEntry.Blink->Flink == &WaitBlock->WaitListEntry);

    RemoveEntryList(&WaitBlock->WaitListEntry);
    WaitBlock->WaitListEntry.Flink = NULL;
    WaitBlock->WaitListEntry.Blink = NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes every block this thread published out of the lists it sits on. The
 * thread's own timer stays in the timer table: cancelling it needs the table
 * lock, which KiUnlinkThread takes separately.
 */
VOID
FASTCALL
KiUnlinkWaitBlocks(
    _In_ PKTHREAD Thread)
{
    ULONG Count, Index;

    Count = KiThreadWaitBlockCount(Thread);
    for (Index = 0; Index < Count; Index++)
    {
        KiUnlinkOneWaitBlock(KiThreadWaitBlock(Thread, Index));
    }
}

/*
 * Ends one thread's wait without yet making it runnable. The status accumulates
 * rather than replaces: a mutant satisfied while abandoned has already written
 * STATUS_ABANDONED into WaitStatus, and the wait index ORed in on top is what
 * turns it into STATUS_ABANDONED_WAIT_n.
 */
VOID
FASTCALL
KiUnlinkThread(
    _Inout_ PKTHREAD Thread,
    _In_ LONG_PTR WaitStatus)
{
    PKTIMER Timer;
    PKPRCB WaitPrcb;

    ASSERT(Thread->State == Waiting || Thread->State == GateWait);

    Thread->WaitStatus |= WaitStatus;

    KiUnlinkWaitBlocks(Thread);

    /*
     * Cancel the thread's own timeout. Inserted, not TimerActive, is the
     * question: a delay wait puts the timer block in the array itself and
     * leaves TimerActive clear, yet its timer is in the table all the same.
     */
    Timer = &Thread->Timer;
    Thread->TimerActive = FALSE;
    if (Timer->Header.Inserted)
        KxRemoveTreeTimer(Timer);

    /*
     * WaitPrcb is the membership token for the processor's wait list. It was
     * published together with the list link under that processor's wait lock,
     * so it has to be withdrawn the same way.
     */
    WaitPrcb = Thread->WaitPrcb;
    if (WaitPrcb != NULL)
    {
        KiAcquireWaitLock(WaitPrcb);
        if (Thread->WaitPrcb == WaitPrcb)
        {
            RemoveEntryList(&Thread->WaitListEntry);
            Thread->WaitPrcb = NULL;
        }
        KiReleaseWaitLock(WaitPrcb);
    }

    Thread->WaitListEntry.Flink = NULL;
    Thread->WaitListEntry.Blink = NULL;
}

/*
 * Ends a wait and puts the thread back in the running population. A thread
 * that belongs to a queue counts as active again from this moment, which pays
 * back the decrement made when it stopped running.
 */
VOID
FASTCALL
KiUnwaitThread(
    _Inout_ PKTHREAD Thread,
    _In_ LONG_PTR WaitStatus,
    _In_ KPRIORITY Increment)
{
    PKQUEUE Queue;

    KiUnlinkThread(Thread, WaitStatus);

    Queue = Thread->Queue;
    if (Queue != NULL)
        KiIncrementQueueCurrentCount(Queue);

    Thread->AdjustIncrement = (CHAR)Increment;
    Thread->AdjustReason = AdjustUnwait;

    KiReadyThread(Thread);
}

/*
 * Hands a signalled object's state to whoever is waiting for it. A WaitAny
 * waiter consumes the state and is released on its own; a WaitAll waiter is
 * only told to re-examine its whole set, which it does with STATUS_KERNEL_APC
 * as the wake reason. The list is re-read from the head after each release
 * because satisfying a wait edits it.
 */
VOID
FASTCALL
KiWaitTest(
    _In_ PVOID ObjectPointer,
    _In_ KPRIORITY Increment)
{
    PDISPATCHER_HEADER Object = (PDISPATCHER_HEADER)ObjectPointer;
    PLIST_ENTRY WaitEntry, WaitList;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitThread;

    WaitList = &Object->WaitListHead;
    WaitEntry = WaitList->Flink;

    while ((Object->SignalState > 0) && (WaitEntry != WaitList))
    {
        WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);

        /*
         * An I/O completion packet is not a thread: it is signalled in place
         * and takes itself off the list.
         */
        if (WaitBlock->WaitType == WaitDpc)
        {
            IopSignalWaitCompletionPacket(WaitBlock);
            WaitEntry = WaitList->Flink;
            continue;
        }

        WaitThread = WaitBlock->Thread;

        if (WaitBlock->WaitType == WaitAny)
        {
            KiSatisfyObjectWait((PKMUTANT)Object, WaitThread);
            KiUnwaitThread(WaitThread, WaitBlock->WaitKey, Increment);
        }
        else
        {
            KiUnwaitThread(WaitThread, STATUS_KERNEL_APC, Increment);
        }

        WaitEntry = WaitList->Flink;
    }
}

/*
 * Leaves the dispatcher. Anything readied on the way in is first published to
 * the processor it belongs to; then, if a better thread is standing by for this
 * processor, the caller gives up the processor to it. Above DISPATCH_LEVEL that
 * cannot happen here, so a dispatch interrupt is asked for instead.
 */
VOID
FASTCALL
KiExitDispatcher(
    _In_ KIRQL OldIrql)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread, NextThread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KiCheckDeferredReadyList(Prcb);

    if (OldIrql >= DISPATCH_LEVEL)
    {
        if ((Prcb->NextThread != NULL) && !Prcb->DpcRoutineActive)
            KiRequestDispatchInterrupt(Prcb);

        KeLowerIrql(OldIrql);
        return;
    }

    if (Prcb->NextThread == NULL)
    {
        KeLowerIrql(OldIrql);
        return;
    }

    Thread = KeGetCurrentThread();

    KiAcquirePrcbLock(Prcb);

    NextThread = Prcb->NextThread;
    if (NextThread == NULL)
    {
        KiReleasePrcbLock(Prcb);
        KeLowerIrql(OldIrql);
        return;
    }

    /* A placement of ourselves is stale by definition: we are already running */
    if (KiConsumeSelfNextThread(Prcb, Thread))
    {
        KiReleasePrcbLock(Prcb);
        KeLowerIrql(OldIrql);
        return;
    }

    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NextThread;
    NextThread->State = Running;

    Thread->WaitIrql = OldIrql;
    Thread->Preempted = TRUE;

    /* The outgoing thread goes back on this processor's ready queues */
    Thread->NextProcessor = Prcb->Number;
    KxQueueReadyThread(Thread, Prcb);

    KiSwapContext(OldIrql, Thread);

    KeLowerIrql(OldIrql);
}

/*
 * Says whether anything is waiting on a dispatcher object. The answer is a
 * snapshot: nothing keeps a waiter from arriving the moment after it is read.
 */
BOOLEAN
NTAPI
KeIsWaitListEmpty(
    _In_ PVOID ObjectPointer)
{
    PDISPATCHER_HEADER Object = (PDISPATCHER_HEADER)ObjectPointer;

    return IsListEmpty(&Object->WaitListHead);
}

/* EOF */
