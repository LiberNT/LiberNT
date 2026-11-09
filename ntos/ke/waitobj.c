/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What it takes for a thread to stop until something happens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether this object would let this thread through right now. A mutant the
 * thread already owns is open to it however deep the recursion, which is what
 * makes KMUTANT recursive where a semaphore is not.
 */
FORCEINLINE
BOOLEAN
KiIsObjectSignalled(
    _In_ PKMUTANT Object,
    _In_ PKTHREAD Thread)
{
    if ((Object->Header.Type & KOBJECT_TYPE_MASK) == MutantObject)
    {
        if (Object->OwnerThread == Thread)
            return TRUE;
    }

    return Object->Header.SignalState > 0;
}

/*
 * A mutant cannot be entered more times than its signal state can count down.
 * Windows reports the exhaustion to the caller as an exception rather than a
 * status, because by then the thread's own recursion bookkeeping is wrong.
 */
FORCEINLINE
VOID
KiCheckMutantLimit(
    _In_ PKMUTANT Object,
    _In_ PKTHREAD Thread)
{
    if ((Object->Header.Type & KOBJECT_TYPE_MASK) != MutantObject)
        return;

    if ((Object->OwnerThread == Thread) &&
        (Object->Header.SignalState == MINLONG))
    {
        KiReleaseDispatcherLock(Thread->WaitIrql);
        ExRaiseStatus(STATUS_MUTANT_LIMIT_EXCEEDED);
    }
}

/*
 * Gives back everything a wait that never got published had taken: the blocks
 * come off the object lists and the timeout is disarmed. Called with the
 * dispatcher database lock still held.
 */
static
VOID
KiAbandonThreadWait(
    _Inout_ PKTHREAD Thread)
{
    PKTIMER Timer = &Thread->Timer;

    KiUnlinkWaitBlocks(Thread);

    Thread->TimerActive = FALSE;
    if (Timer->Header.Inserted)
        KxRemoveTreeTimer(Timer);
}

/*
 * Arms the thread's own timer for this wait and publishes its block on the
 * timer. Answers FALSE when the deadline is already behind us, in which case
 * nothing was armed and the caller must report the timeout instead of blocking.
 */
static
BOOLEAN
KiArmThreadWaitTimer(
    _Inout_ PKTHREAD Thread,
    _In_ PLARGE_INTEGER Interval,
    _In_ KPROCESSOR_MODE WaitMode,
    _Out_ PLARGE_INTEGER DueTime)
{
    PKTIMER Timer = &Thread->Timer;
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    ULONG Hand;

    KxSetTimerForThreadWait(Timer, *Interval, WaitMode, &Hand);
    DueTime->QuadPart = Timer->DueTime.QuadPart;

    if (Timer->DueTime.QuadPart == 0)
        return FALSE;

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

    /* Hands the timer to its table bucket and drops the object lock */
    KxInsertTimer(Timer, Hand);
    return TRUE;
}

/*
 * Links one wait block onto the object it watches. The dispatcher database lock
 * covers the list, so no object lock is taken here.
 */
FORCEINLINE
VOID
KiPublishWaitBlock(
    _Inout_ PKWAIT_BLOCK WaitBlock,
    _In_ PDISPATCHER_HEADER Object)
{
    InsertTailList(&Object->WaitListHead, &WaitBlock->WaitListEntry);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Waits for one dispatcher object.
 *
 * The body is a loop, not a single pass: a kernel APC delivered while the
 * thread was blocked ends the wait with STATUS_KERNEL_APC, and the wait has to
 * be set up again against the remaining part of the original timeout. A wait
 * that was never published is retried the same way.
 */
NTSTATUS
NTAPI
KeWaitForSingleObject(
    _In_ PVOID ObjectPointer,
    _In_ KWAIT_REASON WaitReason,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKMUTANT Object = (PKMUTANT)ObjectPointer;
    PKWAIT_BLOCK WaitBlock = &Thread->WaitBlock[0];
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime, NewDueTime;
    BOOLEAN Swappable;
    NTSTATUS Status;
    LONG_PTR WaitStatus;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DueTime.QuadPart = 0;

    for (;;)
    {
        /*
         * A caller that asked a signalling routine to hold the dispatcher for
         * it arrives with the lock already taken and IRQL already raised.
         */
        if (Thread->WaitNext)
        {
            Thread->WaitNext = FALSE;
        }
        else
        {
            Thread->WaitIrql = KiAcquireDispatcherLock();
        }

        Status = KiCheckAlertability(Thread, Alertable, WaitMode);
        if (Status != STATUS_WAIT_0)
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return Status;
        }

        KiCheckMutantLimit(Object, Thread);

        if (KiIsObjectSignalled(Object, Thread))
        {
            Thread->WaitStatus = STATUS_SUCCESS;
            KiSatisfyObjectWait(Object, Thread);
            Status = (NTSTATUS)(Thread->WaitStatus | STATUS_WAIT_0);
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return Status;
        }

        /* A caller that cannot block gets the answer it can have */
        if ((Timeout != NULL) && (Timeout->QuadPart == 0))
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return STATUS_TIMEOUT;
        }

        /*
         * A kernel APC that is already pending would be delivered the instant
         * IRQL drops. Publishing the wait first would make the thread sleep
         * with that APC undelivered, so go round again instead.
         */
        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = WaitBlock;
        Thread->WaitStatus = STATUS_SUCCESS;
        WaitBlock->Thread = Thread;
        WaitBlock->Object = Object;
        WaitBlock->WaitKey = (USHORT)STATUS_SUCCESS;
        WaitBlock->WaitType = WaitAny;
        WaitBlock->WaitListEntry.Flink = NULL;
        WaitBlock->WaitListEntry.Blink = NULL;
        TimerBlock->WaitListEntry.Flink = NULL;
        TimerBlock->WaitListEntry.Blink = NULL;

        if (Timeout != NULL)
        {
            KxChainSingleWithTimer();
            if (!KiArmThreadWaitTimer(Thread, Timeout, WaitMode, &DueTime))
            {
                Thread->TimerActive = FALSE;
                Thread->WaitBlockList = NULL;
                KiReleaseDispatcherLock(Thread->WaitIrql);
                return STATUS_TIMEOUT;
            }
        }
        else
        {
            KxChainSingleOnly();
        }

        Thread->Alertable = Alertable;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = (UCHAR)WaitReason;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        KiPublishWaitBlock(WaitBlock, &Object->Header);

        if (!KxTryBeginThreadWait(Thread))
        {
            KiAbandonThreadWait(Thread);
            Thread->WaitBlockList = NULL;
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        KxCommitThreadWait(Thread, Swappable);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus != STATUS_KERNEL_APC)
            return (NTSTATUS)WaitStatus;

        /* The APC ran; what is left of the original timeout has to be re-armed */
        if (Timeout != NULL)
        {
            Timeout = KiRecalculateDueTime(OriginalDueTime,
                                           &DueTime,
                                           &NewDueTime);
        }
    }
}

/*
 * Waits for a set of dispatcher objects, either for any one of them or for all
 * of them at once.
 *
 * WaitAll never consumes a part of the set: every object has to be open before
 * the first one is taken, and until then the waiter sits on all of their lists
 * as a WaitAll block, which tells a signalling object to make it look again
 * rather than to hand it the state.
 */
NTSTATUS
NTAPI
KeWaitForMultipleObjects(
    _In_ ULONG Count,
    _In_ PVOID Object[],
    _In_ WAIT_TYPE WaitType,
    _In_ KWAIT_REASON WaitReason,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_opt_ PKWAIT_BLOCK WaitBlockArray)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKMUTANT CurrentObject;
    PKWAIT_BLOCK WaitBlock;
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime, NewDueTime;
    BOOLEAN Swappable, AllSignalled;
    NTSTATUS Status;
    LONG_PTR WaitStatus;
    ULONG Index;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    if (Count == 0)
        return STATUS_INVALID_PARAMETER;

    if (Count > MAXIMUM_WAIT_OBJECTS)
    {
        KeBugCheckEx(MAXIMUM_WAIT_OBJECTS_EXCEEDED, Count, 0, 0, 0);
    }

    /*
     * Three objects fit in the thread's own blocks. More than that needs room
     * from the caller, because the fourth block belongs to the timeout.
     */
    if (WaitBlockArray == NULL)
    {
        if (Count > THREAD_WAIT_OBJECTS)
        {
            KeBugCheckEx(MAXIMUM_WAIT_OBJECTS_EXCEEDED, Count, 0, 0, 0);
        }
        WaitBlockArray = &Thread->WaitBlock[0];
    }

    DueTime.QuadPart = 0;

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

        Status = KiCheckAlertability(Thread, Alertable, WaitMode);
        if (Status != STATUS_WAIT_0)
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return Status;
        }

        for (Index = 0; Index < Count; Index++)
        {
            KiCheckMutantLimit((PKMUTANT)Object[Index], Thread);
        }

        if (WaitType == WaitAny)
        {
            for (Index = 0; Index < Count; Index++)
            {
                CurrentObject = (PKMUTANT)Object[Index];
                if (!KiIsObjectSignalled(CurrentObject, Thread))
                    continue;

                Thread->WaitStatus = STATUS_SUCCESS;
                KiSatisfyObjectWait(CurrentObject, Thread);
                Status = (NTSTATUS)(Thread->WaitStatus |
                                    (LONG_PTR)(STATUS_WAIT_0 + Index));
                KiReleaseDispatcherLock(Thread->WaitIrql);
                return Status;
            }
        }
        else
        {
            AllSignalled = TRUE;
            for (Index = 0; Index < Count; Index++)
            {
                if (!KiIsObjectSignalled((PKMUTANT)Object[Index], Thread))
                {
                    AllSignalled = FALSE;
                    break;
                }
            }

            if (AllSignalled)
            {
                Thread->WaitStatus = STATUS_WAIT_0;
                for (Index = 0; Index < Count; Index++)
                {
                    KiSatisfyObjectWait((PKMUTANT)Object[Index], Thread);
                }
                Status = (NTSTATUS)(Thread->WaitStatus | STATUS_WAIT_0);
                KiReleaseDispatcherLock(Thread->WaitIrql);
                return Status;
            }
        }

        if ((Timeout != NULL) && (Timeout->QuadPart == 0))
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return STATUS_TIMEOUT;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = WaitBlockArray;
        Thread->WaitStatus = STATUS_WAIT_0;

        Index = 0;
        do
        {
            WaitBlock = &WaitBlockArray[Index];
            WaitBlock->Thread = Thread;
            WaitBlock->Object = Object[Index];
            WaitBlock->WaitKey = (USHORT)(STATUS_WAIT_0 + Index);
            WaitBlock->WaitType = (UCHAR)WaitType;
            WaitBlock->WaitListEntry.Flink = NULL;
            WaitBlock->WaitListEntry.Blink = NULL;
            KxChainNextBlock(WaitBlockArray, Index);
            Index++;
        } while (Index < Count);

        KxChainLastBlock(WaitBlockArray, Count);

        TimerBlock->WaitListEntry.Flink = NULL;
        TimerBlock->WaitListEntry.Blink = NULL;

        if (Timeout != NULL)
        {
            KxChainTimerToArray(WaitBlockArray);
            if (!KiArmThreadWaitTimer(Thread, Timeout, WaitMode, &DueTime))
            {
                Thread->TimerActive = FALSE;
                Thread->WaitBlockList = NULL;
                KiReleaseDispatcherLock(Thread->WaitIrql);
                return STATUS_TIMEOUT;
            }
        }

        Thread->Alertable = Alertable;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = (UCHAR)WaitReason;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        for (Index = 0; Index < Count; Index++)
        {
            KiPublishWaitBlock(&WaitBlockArray[Index],
                               (PDISPATCHER_HEADER)Object[Index]);
        }

        if (!KxTryBeginThreadWait(Thread))
        {
            KiAbandonThreadWait(Thread);
            Thread->WaitBlockList = NULL;
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        KxCommitThreadWait(Thread, Swappable);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus != STATUS_KERNEL_APC)
            return (NTSTATUS)WaitStatus;

        if (Timeout != NULL)
        {
            Timeout = KiRecalculateDueTime(OriginalDueTime,
                                           &DueTime,
                                           &NewDueTime);
        }
    }
}

/*
 * Stops the thread for a stretch of time and nothing else. There is no object
 * in this wait, so only the timeout block takes part.
 */
NTSTATUS
NTAPI
KeDelayExecutionThread(
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_ PLARGE_INTEGER Interval)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PLARGE_INTEGER OriginalDueTime = Interval;
    LARGE_INTEGER DueTime, NewDueTime;
    BOOLEAN Swappable;
    NTSTATUS Status;
    LONG_PTR WaitStatus;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DueTime.QuadPart = 0;

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

        Status = KiCheckAlertability(Thread, Alertable, WaitMode);
        if (Status != STATUS_WAIT_0)
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return Status;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = TimerBlock;
        Thread->WaitStatus = STATUS_SUCCESS;
        TimerBlock->WaitListEntry.Flink = NULL;
        TimerBlock->WaitListEntry.Blink = NULL;

        KxChainTimerOnly();

        if (!KiArmThreadWaitTimer(Thread, Interval, WaitMode, &DueTime))
        {
            /* Nothing left to wait for; give the processor up and come back */
            Thread->TimerActive = FALSE;
            Thread->WaitBlockList = NULL;
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return STATUS_SUCCESS;
        }

        Thread->Alertable = Alertable;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = DelayExecution;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        if (!KxTryBeginThreadWait(Thread))
        {
            KiAbandonThreadWait(Thread);
            Thread->WaitBlockList = NULL;
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        KxCommitThreadWait(Thread, Swappable);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus != STATUS_KERNEL_APC)
            return (NTSTATUS)WaitStatus;

        Interval = KiRecalculateDueTime(OriginalDueTime, &DueTime, &NewDueTime);
    }
}

/*
 * Waits until another thread alerts this one by its id. The wait holds no
 * object: the waker recognises it by the wait reason and ends it directly, so
 * the address is the caller's own cookie and the kernel only passes it through.
 */
NTSTATUS
NTAPI
KeWaitForAlertByThreadId(
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime, NewDueTime;
    BOOLEAN Swappable;
    LONG_PTR WaitStatus;

    UNREFERENCED_PARAMETER(Address);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DueTime.QuadPart = 0;

    for (;;)
    {
        Thread->WaitIrql = KiAcquireDispatcherLock();

        /*
         * An alert that arrived before the wait is held in the thread flags.
         * Consume it and report it as if the wait had caught it.
         */
        if (InterlockedBitTestAndReset((PLONG)&Thread->ThreadFlags,
                                       KTHREAD_ALERTED_BY_THREAD_ID_BIT))
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return STATUS_ALERTED;
        }

        if ((Timeout != NULL) && (Timeout->QuadPart == 0))
        {
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return STATUS_TIMEOUT;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = TimerBlock;
        Thread->WaitStatus = STATUS_SUCCESS;
        TimerBlock->WaitListEntry.Flink = NULL;
        TimerBlock->WaitListEntry.Blink = NULL;

        KxChainTimerOnly();

        if (Timeout != NULL)
        {
            if (!KiArmThreadWaitTimer(Thread, Timeout, WaitMode, &DueTime))
            {
                Thread->TimerActive = FALSE;
                Thread->WaitBlockList = NULL;
                KiReleaseDispatcherLock(Thread->WaitIrql);
                return STATUS_TIMEOUT;
            }
        }

        Thread->Alertable = FALSE;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = WrAlertByThreadId;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        if (!KxTryBeginThreadWait(Thread))
        {
            KiAbandonThreadWait(Thread);
            Thread->WaitBlockList = NULL;
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        KxCommitThreadWait(Thread, Swappable);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus != STATUS_KERNEL_APC)
            return (NTSTATUS)WaitStatus;

        if (Timeout != NULL)
        {
            Timeout = KiRecalculateDueTime(OriginalDueTime,
                                           &DueTime,
                                           &NewDueTime);
        }
    }
}

/*
 * The delay a thread asks for itself. A user-mode interval is read once into
 * kernel memory: whatever the caller does to its own copy afterwards cannot
 * change the deadline the kernel armed.
 */
NTSTATUS
NTAPI
NtDelayExecution(
    _In_ BOOLEAN Alertable,
    _In_ PLARGE_INTEGER DelayInterval)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeInterval;
    NTSTATUS Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForReadLargeInteger(DelayInterval);
            SafeInterval = *(volatile LARGE_INTEGER *)DelayInterval;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        SafeInterval = *DelayInterval;
    }

    Status = KeDelayExecutionThread(PreviousMode, Alertable, &SafeInterval);

    /*
     * A delay that was broken by an alert or an APC is reported as such, not
     * as a failure: the caller decides whether to go back to sleep.
     */
    return Status;
}

/* EOF */
