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

/* The timer block's Object slot holds WaitTime (x86) or Ucb (x64) in the NT10 KTHREAD, so its object is the thread's own timer */
FORCEINLINE
PDISPATCHER_HEADER
KiWaitBlockObject(
    _In_ PKWAIT_BLOCK WaitBlock)
{
    PKTHREAD Thread = WaitBlock->Thread;

    ASSERT(WaitBlock->WaitType != WaitDpc);

    if (WaitBlock == &Thread->WaitBlock[TIMER_WAIT_BLOCK])
        return &Thread->Timer.Header;

    return (PDISPATCHER_HEADER)WaitBlock->Object;
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
    WaitBlock->BlockState = WaitBlockInactive;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes every block this thread published out of the lists it sits on. The
 * thread's own timer stays in the timer table: cancelling it needs the table
 * lock, which KiEndThreadWait takes separately.
 */
VOID
FASTCALL
KiUnlinkWaitBlocks(
    _In_ PKTHREAD Thread)
{
    ULONG Count, Index;

    PKWAIT_BLOCK WaitBlock;
    PDISPATCHER_HEADER Object;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Thread->WaitRegister.State == KI_WAIT_ABORTED);

    Count = KiThreadWaitBlockCount(Thread);
    for (Index = 0; Index < Count; Index++)
    {
        WaitBlock = KiThreadWaitBlock(Thread, Index);
        ASSERT(WaitBlock->Thread == Thread);
        Object = KiWaitBlockObject(WaitBlock);

        /* A signaller that found the wait over takes the block off under the same lock, so the link is only looked at inside it */
        KiAcquireDispatcherObject(Object);
        KiUnlinkOneWaitBlock(WaitBlock);
        KiReleaseDispatcherObject(Object);
    }
}

/* Whoever ended the wait held one object at most, so the thread itself takes its leftover blocks off the others and disarms its timeout */
VOID
FASTCALL
KiEndThreadWait(
    _Inout_ PKTHREAD Thread)
{
    PKTIMER Timer = &Thread->Timer;

    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* A signaller can be ending a wait still in progress at this very moment, so the register is only touched under the lock */
    KiAcquireThreadLock(Thread);
    ASSERT(Thread->WaitRegister.State != KI_WAIT_COMMITTED);
    Thread->WaitRegister.State = KI_WAIT_ABORTED;
    KiReleaseThreadLock(Thread);

    if (Thread->WaitBlockList != NULL)
        KiUnlinkWaitBlocks(Thread);

    /*
     * Cancel the thread's own timeout. Inserted, not TimerActive, is the
     * question: a delay wait puts the timer block in the array itself and
     * leaves TimerActive clear, yet its timer is in the table all the same.
     */
    Thread->TimerActive = FALSE;
    KiAcquireDispatcherObject(&Timer->Header);
    if (Timer->Header.Inserted)
        KxRemoveTreeTimer(Timer);
    KiReleaseDispatcherObject(&Timer->Header);
}

/*
 * Ends a wait and puts the thread back in the running population. The status
 * accumulates rather than replaces: a mutant satisfied while abandoned has
 * already written STATUS_ABANDONED into WaitStatus, and the wait index ORed in
 * on top is what turns it into STATUS_ABANDONED_WAIT_n. A thread that belongs
 * to a queue counts as active again from this moment, which pays back the
 * decrement made when it stopped running.
 *
 * Entered with the thread lock held, which makes the caller the only one to end this wait.
 */
VOID
FASTCALL
KiUnwaitThread(
    _Inout_ PKTHREAD Thread,
    _In_ LONG_PTR WaitStatus,
    _In_ KPRIORITY Increment)
{
    PKPRCB WaitPrcb;
    PKQUEUE Queue;

    ASSERT(Thread->ThreadLock != 0);
    ASSERT(Thread->State == Waiting || Thread->State == GateWait);
    ASSERT(Thread->WaitRegister.State == KI_WAIT_COMMITTED);

    Thread->WaitRegister.State = KI_WAIT_ABORTED;
    Thread->WaitStatus |= WaitStatus;

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

    Queue = Thread->Queue;
    if (Queue != NULL)
        KiIncrementQueueCurrentCount(Queue);

    Thread->AdjustIncrement = (CHAR)Increment;
    Thread->AdjustReason = AdjustUnwait;

    KiReadyThread(Thread);
}

/* Answers a committed waiter with its lock held; a thread still setting its wait up is made to look again, and a block left by a finished wait just comes off */
PKTHREAD
FASTCALL
KiLockCommittedWaiter(
    _Inout_ PKWAIT_BLOCK WaitBlock)
{
    PKTHREAD Thread = WaitBlock->Thread;

    ASSERT(WaitBlock->WaitType != WaitDpc);
    ASSERT(KiWaitBlockObject(WaitBlock)->Lock & KOBJECT_LOCK_BIT);

    KiAcquireThreadLock(Thread);
    if (Thread->WaitRegister.State == KI_WAIT_COMMITTED)
        return Thread;

    Thread->WaitRegister.State = KI_WAIT_ABORTED;
    KiReleaseThreadLock(Thread);

    KiUnlinkOneWaitBlock(WaitBlock);
    return NULL;
}

VOID
FASTCALL
KiUnlinkWaitBlock(
    _Inout_ PKWAIT_BLOCK WaitBlock)
{
    ASSERT(KiWaitBlockObject(WaitBlock)->Lock & KOBJECT_LOCK_BIT);

    KiUnlinkOneWaitBlock(WaitBlock);
}

/*
 * Hands a signalled object's state to whoever is waiting for it. A WaitAny
 * waiter consumes the state and is released on its own; a WaitAll waiter is
 * only told to re-examine its whole set, which it does with STATUS_KERNEL_APC
 * as the wake reason. The list is re-read from the head after each release
 * because satisfying a wait edits it.
 *
 * Entered with the object locked.
 */
VOID
FASTCALL
KiWaitTest(
    _In_ PVOID ObjectPointer,
    _In_ KPRIORITY Increment)
{
    PDISPATCHER_HEADER Object = (PDISPATCHER_HEADER)ObjectPointer;
    PLIST_ENTRY WaitList = &Object->WaitListHead;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitThread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Object->Lock & KOBJECT_LOCK_BIT);

    while ((Object->SignalState > 0) && !IsListEmpty(WaitList))
    {
        WaitBlock = CONTAINING_RECORD(WaitList->Flink, KWAIT_BLOCK, WaitListEntry);

        /*
         * An I/O completion packet is not a thread: it is signalled in place
         * and takes itself off the list.
         */
        if (WaitBlock->WaitType == WaitDpc)
        {
            IopSignalWaitCompletionPacket(WaitBlock);
            continue;
        }

        WaitThread = KiLockCommittedWaiter(WaitBlock);
        if (WaitThread == NULL)
            continue;

        if (WaitBlock->WaitType == WaitAny)
        {
            KiSatisfyObjectWait((PKMUTANT)Object, WaitThread);
            KiUnwaitThread(WaitThread, WaitBlock->WaitKey, Increment);
        }
        else
        {
            KiUnwaitThread(WaitThread, STATUS_KERNEL_APC, Increment);
        }

        KiReleaseThreadLock(WaitThread);
        KiUnlinkOneWaitBlock(WaitBlock);
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

    /* The outgoing thread goes back on this processor's ready queues */
    Thread->NextProcessor = Prcb->Number;
    KxQueueReadyThread(Thread, Prcb);

    KiWaitForThreadSwapOut(NextThread);

    if (KiSwapContext(OldIrql, Thread))
    {
        ASSERT(OldIrql == PASSIVE_LEVEL);
        KeLowerIrql(APC_LEVEL);
        KiDeliverApc(KernelMode, NULL, NULL);
    }

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
