/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The objects a thread can be told to wait for, and what signalling one means
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Ends a signalling operation. With Wait the dispatcher is kept, raised, for
 * the wait the caller is about to make: that is what lets a driver signal one
 * object and wait on another without anything running in between.
 */
FORCEINLINE
VOID
KiCompleteSignalling(
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN Wait)
{
    PKTHREAD Thread;

    if (!Wait)
    {
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    Thread = KeGetCurrentThread();
    Thread->WaitNext = TRUE;
    Thread->WaitIrql = OldIrql;
}

/*
 * Hands a newly signalled object to its waiters. A notification object stays
 * signalled and releases all of them; every other kind lets the satisfaction
 * rules in the dispatcher decide how far down the list the state reaches.
 */
static
VOID
KiSignalWaiters(
    _Inout_ PDISPATCHER_HEADER Object,
    _In_ KPRIORITY Increment)
{
    if (IsListEmpty(&Object->WaitListHead))
        return;

    if ((Object->Type & TIMER_OR_EVENT_TYPE) == EventNotificationObject)
    {
        KxUnwaitThread(Object, Increment);
        return;
    }

    KiWaitTest(Object, Increment);
}

/* PUBLIC FUNCTIONS: EVENTS ***************************************************/

/*
 * An event is either a gate that stays open once opened (notification) or one
 * that lets exactly one waiter through each time it is opened (synchronization).
 */
VOID
NTAPI
KeInitializeEvent(
    _Out_ PKEVENT Event,
    _In_ EVENT_TYPE Type,
    _In_ BOOLEAN State)
{
    Event->Header.Type = (UCHAR)Type;
    Event->Header.Signalling = FALSE;
    Event->Header.Size = sizeof(KEVENT) / sizeof(LONG);
    Event->Header.Hand = 0;
    Event->Header.Inserted = FALSE;
    Event->Header.DebugActive = FALSE;
    Event->Header.SignalState = State ? 1 : 0;
    InitializeListHead(&Event->Header.WaitListHead);
}

/*
 * Opens an event. Answers the state it was in, which is how a caller tells
 * whether its own signal was the one that mattered.
 */
LONG
NTAPI
KeSetEvent(
    _Inout_ PKEVENT Event,
    _In_ KPRIORITY Increment,
    _In_ BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();

    PreviousState = Event->Header.SignalState;
    Event->Header.SignalState = 1;

    KiSignalWaiters(&Event->Header, Increment);

    KiCompleteSignalling(OldIrql, Wait);
    return PreviousState;
}

/*
 * Opens an event just long enough for whoever is already waiting to go
 * through, and closes it again. A thread that was not yet waiting misses it,
 * which is what makes a pulse a notification and not a handshake.
 */
LONG
NTAPI
KePulseEvent(
    _Inout_ PKEVENT Event,
    _In_ KPRIORITY Increment,
    _In_ BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();

    PreviousState = Event->Header.SignalState;
    Event->Header.SignalState = 1;

    KiSignalWaiters(&Event->Header, Increment);

    Event->Header.SignalState = 0;

    KiCompleteSignalling(OldIrql, Wait);
    return PreviousState;
}

/*
 * Closes an event and answers the state it was in. The answer is why this and
 * KeClearEvent are two routines rather than one.
 */
LONG
NTAPI
KeResetEvent(
    _Inout_ PKEVENT Event)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();
    PreviousState = Event->Header.SignalState;
    Event->Header.SignalState = 0;
    KiReleaseDispatcherLock(OldIrql);

    return PreviousState;
}

/*
 * Closes an event without reporting anything. Clearing the state cannot
 * release a waiter, so no lock is needed for it.
 */
VOID
NTAPI
KeClearEvent(
    _Inout_ PKEVENT Event)
{
    ASSERT_EVENT(Event);

    Event->Header.SignalState = 0;
}

/*
 * The current state, read without the dispatcher. It is a snapshot and the
 * caller must treat it as one: nothing keeps the state from changing on the
 * instruction after the read.
 */
LONG
NTAPI
KeReadStateEvent(
    _In_ PKEVENT Event)
{
    ASSERT_EVENT(Event);

    return Event->Header.SignalState;
}

/*
 * Which kind of event this is. The dispatcher object type and the public
 * EVENT_TYPE agree on both values, so the header byte is the answer.
 */
EVENT_TYPE
NTAPI
KeQueryTypeEvent(
    _In_ PKEVENT Event)
{
    ASSERT_EVENT(Event);

    return (EVENT_TYPE)(Event->Header.Type & KOBJECT_TYPE_MASK);
}

/*
 * Opens a synchronization event and gives the waiter it releases a boost, so
 * that an owner handing a lock over does not then outrun the thread it handed
 * it to. Names that thread to the caller where it asked.
 */
VOID
NTAPI
KeSetEventBoostPriority(
    _Inout_ PKEVENT Event,
    _In_opt_ PKTHREAD *Thread)
{
    KIRQL OldIrql;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitThread;

    ASSERT((Event->Header.Type & KOBJECT_TYPE_MASK) ==
           EventSynchronizationObject);

    OldIrql = KiAcquireDispatcherLock();

    if (IsListEmpty(&Event->Header.WaitListHead))
    {
        Event->Header.SignalState = 1;
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    WaitBlock = CONTAINING_RECORD(Event->Header.WaitListHead.Flink,
                                  KWAIT_BLOCK,
                                  WaitListEntry);

    /*
     * Only a thread waiting for this event alone can be handed it directly.
     * Anything else goes through the ordinary satisfaction rules.
     */
    if (WaitBlock->WaitType != WaitAny)
    {
        Event->Header.SignalState = 1;
        KiSignalWaiters(&Event->Header, EVENT_INCREMENT);
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    WaitThread = WaitBlock->Thread;
    if (Thread != NULL)
        *Thread = WaitThread;

    KiUnwaitThread(WaitThread, STATUS_SUCCESS, EVENT_INCREMENT);
    KiReleaseDispatcherLock(OldIrql);
}

/*
 * A pair of synchronization events used as one object, so that two threads can
 * hand work back and forth without naming each other's events.
 */
VOID
NTAPI
KeInitializeEventPair(
    _Out_ PKEVENT_PAIR EventPair)
{
    EventPair->Type = EventPairObject;
    EventPair->Size = sizeof(KEVENT_PAIR);

    KeInitializeEvent(&EventPair->LowEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&EventPair->HighEvent, SynchronizationEvent, FALSE);
}

/* PUBLIC FUNCTIONS: SEMAPHORES ***********************************************/

/*
 * A semaphore counts how many threads may hold it at once. Limit is the most
 * the count may ever reach, and releasing past it is an error rather than a
 * silent clamp: the caller's bookkeeping is wrong and should say so.
 */
VOID
NTAPI
KeInitializeSemaphore(
    _Out_ PKSEMAPHORE Semaphore,
    _In_ LONG Count,
    _In_ LONG Limit)
{
    ASSERT(Limit > 0);
    ASSERT(Count >= 0);
    ASSERT(Count <= Limit);

    Semaphore->Header.Type = SemaphoreObject;
    Semaphore->Header.Signalling = FALSE;
    Semaphore->Header.Size = sizeof(KSEMAPHORE) / sizeof(LONG);
    Semaphore->Header.Hand = 0;
    Semaphore->Header.Inserted = FALSE;
    Semaphore->Header.DebugActive = FALSE;
    Semaphore->Header.SignalState = Count;
    Semaphore->Limit = Limit;
    InitializeListHead(&Semaphore->Header.WaitListHead);
}

/*
 * Gives back one or more holds on a semaphore. Answers the count before the
 * release.
 */
LONG
NTAPI
KeReleaseSemaphore(
    _Inout_ PKSEMAPHORE Semaphore,
    _In_ KPRIORITY Increment,
    _In_ LONG Adjustment,
    _In_ BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState, NewState;

    ASSERT_SEMAPHORE(Semaphore);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT(Adjustment > 0);

    OldIrql = KiAcquireDispatcherLock();

    PreviousState = Semaphore->Header.SignalState;
    NewState = PreviousState + Adjustment;

    /* Both an overflow and a pass over the limit are the same mistake */
    if ((NewState > Semaphore->Limit) || (NewState < PreviousState))
    {
        KiReleaseDispatcherLock(OldIrql);
        ExRaiseStatus(STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    }

    Semaphore->Header.SignalState = NewState;

    /* Waiters can only be released where the count was nothing before */
    if ((PreviousState == 0) &&
        !IsListEmpty(&Semaphore->Header.WaitListHead))
    {
        KiWaitTest(Semaphore, Increment);
    }

    KiCompleteSignalling(OldIrql, Wait);
    return PreviousState;
}

LONG
NTAPI
KeReadStateSemaphore(
    _In_ PKSEMAPHORE Semaphore)
{
    ASSERT_SEMAPHORE(Semaphore);

    return Semaphore->Header.SignalState;
}

/* PUBLIC FUNCTIONS: MUTANTS **************************************************/

/*
 * Links a mutant to the thread that holds it, so that a thread which dies
 * without releasing it can be found and the mutant marked abandoned.
 *
 * Entered with the dispatcher held.
 */
static
VOID
KiAcquireMutantOwnership(
    _Inout_ PKMUTANT Mutant,
    _Inout_ PKTHREAD Thread)
{
    Mutant->OwnerThread = Thread;
    Thread->KernelApcDisable = Thread->KernelApcDisable - Mutant->ApcDisable;
    InsertTailList(&Thread->MutantListHead, &Mutant->MutantListEntry);
}

/*
 * A mutant is held by one thread at a time and may be entered again by that
 * same thread. InitialOwner gives it to the caller without a wait.
 */
VOID
NTAPI
KeInitializeMutant(
    _Out_ PKMUTANT Mutant,
    _In_ BOOLEAN InitialOwner)
{
    PKTHREAD Thread;
    KIRQL OldIrql;

    Mutant->Header.Type = MutantObject;
    Mutant->Header.Signalling = FALSE;
    Mutant->Header.Size = sizeof(KMUTANT) / sizeof(LONG);
    Mutant->Header.Inserted = FALSE;
    Mutant->Header.DebugActive = FALSE;
    Mutant->Abandoned = FALSE;
    Mutant->ApcDisable = 0;
    InitializeListHead(&Mutant->Header.WaitListHead);

    if (!InitialOwner)
    {
        Mutant->Header.SignalState = 1;
        Mutant->OwnerThread = NULL;
        return;
    }

    Thread = KeGetCurrentThread();
    Mutant->Header.SignalState = 0;

    OldIrql = KiAcquireDispatcherLock();
    KiAcquireMutantOwnership(Mutant, Thread);
    KiReleaseDispatcherLock(OldIrql);
}

/*
 * A mutex is a mutant that also keeps normal kernel APCs out while it is held,
 * so that code under it cannot be made to run something that tries to take it
 * again. The level argument is carried for source compatibility only.
 */
VOID
NTAPI
KeInitializeMutex(
    _Out_ PKMUTEX Mutex,
    _In_ ULONG Level)
{
    UNREFERENCED_PARAMETER(Level);

    Mutex->Header.Type = MutantObject;
    Mutex->Header.Signalling = FALSE;
    Mutex->Header.Size = sizeof(KMUTEX) / sizeof(LONG);
    Mutex->Header.Inserted = FALSE;
    Mutex->Header.DebugActive = FALSE;
    Mutex->Header.SignalState = 1;
    Mutex->OwnerThread = NULL;
    Mutex->Abandoned = FALSE;
    Mutex->ApcDisable = 1;
    InitializeListHead(&Mutex->Header.WaitListHead);
}

/*
 * Gives a mutant up. Answers the state before the release, so a caller can see
 * how deep its own recursion was.
 *
 * Abandoned is for the path that releases a mutant on behalf of a thread that
 * is gone: the ownership check is skipped and the next holder is told.
 */
LONG
NTAPI
KeReleaseMutant(
    _Inout_ PKMUTANT Mutant,
    _In_ KPRIORITY Increment,
    _In_ BOOLEAN Abandoned,
    _In_ BOOLEAN Wait)
{
    KIRQL OldIrql;
    PKTHREAD Thread = KeGetCurrentThread();
    LONG PreviousState;

    ASSERT_MUTANT(Mutant);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();

    PreviousState = Mutant->Header.SignalState;

    if (!Abandoned)
    {
        if (Mutant->OwnerThread != Thread)
        {
            KiReleaseDispatcherLock(OldIrql);
            ExRaiseStatus(STATUS_MUTANT_NOT_OWNED);
        }

        Mutant->Header.SignalState++;
    }
    else
    {
        Mutant->Header.SignalState = 1;
        Mutant->Abandoned = TRUE;
    }

    /* Still held one level deep: nothing to hand on yet */
    if (Mutant->Header.SignalState <= 0)
    {
        KiCompleteSignalling(OldIrql, Wait);
        return PreviousState;
    }

    if (Mutant->OwnerThread != NULL)
    {
        PKTHREAD Owner = Mutant->OwnerThread;

        RemoveEntryList(&Mutant->MutantListEntry);
        Mutant->OwnerThread = NULL;

        /* Give back the APC exclusion the acquisition took out */
        Owner->KernelApcDisable = Owner->KernelApcDisable + Mutant->ApcDisable;
    }

    if (!IsListEmpty(&Mutant->Header.WaitListHead))
        KiWaitTest(Mutant, Increment);

    KiCompleteSignalling(OldIrql, Wait);
    return PreviousState;
}

LONG
NTAPI
KeReleaseMutex(
    _Inout_ PKMUTEX Mutex,
    _In_ BOOLEAN Wait)
{
    return KeReleaseMutant(Mutex, MUTANT_INCREMENT, FALSE, Wait);
}

LONG
NTAPI
KeReadStateMutant(
    _In_ PKMUTANT Mutant)
{
    ASSERT_MUTANT(Mutant);

    return Mutant->Header.SignalState;
}

/* PUBLIC FUNCTIONS: GATES ****************************************************/

/*
 * A gate is the cheapest thing a thread can wait on: one state, one list, no
 * timeout and no alerts. It is what the guarded mutex and the pushlock use
 * when they have to block.
 */
VOID
FASTCALL
KeInitializeGate(
    _Out_ PKGATE Gate)
{
    Gate->Header.Type = GateObject;
    Gate->Header.Signalling = FALSE;
    Gate->Header.Size = sizeof(KGATE) / sizeof(LONG);
    Gate->Header.Hand = 0;
    Gate->Header.Inserted = FALSE;
    Gate->Header.DebugActive = FALSE;
    Gate->Header.SignalState = 0;
    InitializeListHead(&Gate->Header.WaitListHead);
}

/*
 * Opens a gate for one waiter, boosting it. Where nobody is waiting the state
 * is left open so the next waiter goes straight through.
 */
VOID
FASTCALL
KeSignalGateBoostPriority(
    _Inout_ PKGATE Gate)
{
    KIRQL OldIrql;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitThread;

    ASSERT_GATE(Gate);

    OldIrql = KiAcquireDispatcherLock();

    if (Gate->Header.SignalState != 0)
    {
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    if (IsListEmpty(&Gate->Header.WaitListHead))
    {
        Gate->Header.SignalState = 1;
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    WaitBlock = CONTAINING_RECORD(Gate->Header.WaitListHead.Flink,
                                  KWAIT_BLOCK,
                                  WaitListEntry);
    WaitThread = WaitBlock->Thread;

    KiUnwaitThread(WaitThread, STATUS_SUCCESS, EVENT_INCREMENT);
    KiReleaseDispatcherLock(OldIrql);
}

/*
 * Waits for a gate. There is no status to report: the wait ends only when the
 * gate opens. A kernel APC can still break it, and then the wait is simply
 * made again, which is why the caller never learns that it happened.
 */
VOID
FASTCALL
KeWaitForGate(
    _Inout_ PKGATE Gate,
    _In_ KWAIT_REASON WaitReason,
    _In_ KPROCESSOR_MODE WaitMode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK WaitBlock = &Thread->WaitBlock[0];
    BOOLEAN Swappable;
    LONG_PTR WaitStatus;

    ASSERT_GATE(Gate);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    for (;;)
    {
        Thread->WaitIrql = KiAcquireDispatcherLock();

        if (Gate->Header.SignalState != 0)
        {
            Gate->Header.SignalState = 0;
            KiReleaseDispatcherLock(Thread->WaitIrql);
            return;
        }

        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        Thread->WaitBlockList = WaitBlock;
        Thread->WaitStatus = STATUS_SUCCESS;
        WaitBlock->Thread = Thread;
        WaitBlock->Object = Gate;
        WaitBlock->WaitKey = (USHORT)STATUS_SUCCESS;
        WaitBlock->WaitType = WaitAny;
        WaitBlock->WaitListEntry.Flink = NULL;
        WaitBlock->WaitListEntry.Blink = NULL;
        KxChainSingleOnly();

        Thread->Alertable = FALSE;
        Thread->WaitMode = WaitMode;
        Thread->WaitReason = (UCHAR)WaitReason;
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitTime = KeTickCount.LowPart;
        Swappable = KiCheckThreadStackSwap(Thread, WaitMode);

        InsertTailList(&Gate->Header.WaitListHead, &WaitBlock->WaitListEntry);

        if (!KxTryBeginThreadWait(Thread))
        {
            KiUnlinkWaitBlocks(Thread);
            Thread->WaitBlockList = NULL;
            KxExitDispatcherForRetry(Thread);
            continue;
        }

        /*
         * GateWait rather than Waiting: it tells a debugger, and anything that
         * walks thread states, that this wait answers only to its gate.
         */
        Thread->State = GateWait;
        KiAddThreadToWaitList(Thread, Swappable);
        KiSetThreadSwapBusy(Thread);
        KiReleaseThreadLock(Thread);

        WaitStatus = KiSwapThread(Thread, KeGetCurrentPrcb(), FALSE);
        Thread->WaitBlockList = NULL;

        if (WaitStatus != STATUS_KERNEL_APC)
            return;
    }
}

/* EOF */
