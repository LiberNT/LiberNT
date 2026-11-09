/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Timers, the table that holds them, and what happens when one comes due
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/*
 * Every armed timer in the system lives in one of these buckets, chosen from
 * its due time, so that a tick only has to look at the buckets that could hold
 * something due rather than at every timer. Each bucket keeps the earliest due
 * time it holds, which is what lets the clock decide to look at all.
 */
KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];

/* The DPC the clock queues once it finds something due in the table. */
KDPC KiTimerExpireDpc;

/* Reciprocal of the clock increment, so that a tick count can be turned into a
   table index with a multiply and a shift instead of a division. */
LARGE_INTEGER KiTimeIncrementReciprocal;
UCHAR KiTimeIncrementShiftCount;

/* Whether a timer that stays due for too long is reported. */
BOOLEAN KiEnableTimerWatchdog;

/* The boost given to a thread released by a timer. A timeout is not news worth
   jumping the queue for: the thread gets its turn in the ordinary way. */
#define KI_TIMER_BOOST 0

/* One millisecond, in the hundreds of nanoseconds a due time is counted in. */
#define KI_HUNDRED_NS_PER_MS 10000ULL

/* The widest tolerance the encoded field can carry back, as a power of two
   milliseconds. Stopping at thirty keeps the decoded value inside a ULONG. */
#define KI_MAX_TOLERANCE_LOG2 30

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Records how late a timer may be allowed to fire.
 *
 * The delay goes in the field Windows 11 keeps it in, encoded as one more than
 * its base-two logarithm in milliseconds so that zero still means no
 * tolerance. A delay that is not a power of two is rounded down, which can
 * only narrow the window the caller asked for and never widen it.
 *
 * FIXME: the encoding a Windows debugger or dump reader would decode from this
 * field is published in no header available here, so a foreign reader will
 * misread the value. The behaviour the kernel gives the caller is within the
 * documented window either way.
 */
FORCEINLINE
VOID
KiEncodeTolerableDelay(
    _Inout_ PKTIMER Timer,
    _In_ ULONG Milliseconds)
{
    UCHAR Code = 0;

    if (Milliseconds == 0)
    {
        Timer->Header.EncodedTolerableDelay = 0;
        return;
    }

    while (((Milliseconds >> Code) > 1) && (Code < KI_MAX_TOLERANCE_LOG2))
        Code++;

    Timer->Header.EncodedTolerableDelay = (UCHAR)(Code + 1);
}

FORCEINLINE
ULONG
KiDecodeTolerableDelay(
    _In_ PKTIMER Timer)
{
    UCHAR Code = Timer->Header.EncodedTolerableDelay;

    if (Code == 0)
        return 0;

    return 1UL << (Code - 1);
}

/*
 * Moves a due time later, within the tolerance the caller allowed, so that it
 * lands where the clock was going to interrupt anyway. Two timers rounded to
 * the same boundary are served by one interrupt instead of two, which is the
 * whole purpose of a tolerance.
 *
 * Answers the table index the moved due time belongs in.
 */
static
ULONG
KiCoalesceDueTime(
    _Inout_ PKTIMER Timer,
    _In_ ULONG TolerableDelay)
{
    ULONGLONG DueTime = Timer->DueTime.QuadPart;
    ULONGLONG Latest, Rounded;
    ULONG Hand;

    if ((TolerableDelay != 0) && (KeMaximumIncrement != 0))
    {
        Latest = DueTime + (TolerableDelay * KI_HUNDRED_NS_PER_MS);
        Rounded = KiRoundToDefaultTimerResolution(DueTime);

        /* Only worth taking when it is later and still inside the window */
        if ((Rounded > DueTime) && (Rounded <= Latest))
        {
            DueTime = Rounded;
            Timer->DueTime.QuadPart = DueTime;
        }
    }

    Hand = KiComputeTimerTableIndex(DueTime);
    Timer->Header.Hand = (UCHAR)Hand;

    return Hand;
}

/*
 * Turns a period in milliseconds into the relative interval a timer wants,
 * which is counted in hundreds of nanoseconds and negative for relative.
 */
FORCEINLINE
LARGE_INTEGER
KiPeriodToInterval(
    _In_ LONG Period)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = Int32x32To64(Period, -10000);
    return Interval;
}

/*
 * Arms a periodic timer for its next period. Where that moment has already
 * passed the timer is left where it is: the table scan will find it due and
 * expire it again, which is the behaviour a period shorter than the work it
 * triggers has to have.
 */
static
VOID
KiRearmPeriodicTimer(
    _Inout_ PKTIMER Timer)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    LARGE_INTEGER Interval;
    ULONG Hand;

    Interval = KiPeriodToInterval(Timer->Period);

    if (!KiComputeDueTime(Timer, Interval, &Hand))
        return;

    /* Each period of a coalescable timer gets the same tolerance as the first */
    Hand = KiCoalesceDueTime(Timer, KiDecodeTolerableDelay(Timer));

    LockQueue = KiAcquireTimerLock(Hand);
    KiInsertTimerTable(Timer, Hand);
    KiReleaseTimerLock(LockQueue);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Puts an armed timer in its bucket, in due-time order. Answers TRUE when the
 * moment it was armed for has already gone by, which the caller turns into an
 * immediate expiry rather than a wait for the next tick.
 *
 * Entered with that bucket's table lock held.
 */
BOOLEAN
FASTCALL
KiInsertTimerTable(
    _Inout_ PKTIMER Timer,
    _In_ ULONG Hand)
{
    PKTIMER_TABLE_ENTRY TableEntry;
    PLIST_ENTRY ListHead, Entry;
    PKTIMER Current;
    ULONGLONG DueTime = Timer->DueTime.QuadPart;
    ULONGLONG InterruptTime;

    ASSERT(Hand < TIMER_TABLE_SIZE);
    ASSERT(Hand == KiComputeTimerTableIndex(DueTime));

    TableEntry = &KiTimerTableListHead[Hand];
    ListHead = &TableEntry->Entry;

    /*
     * Searching from the tail is the right way round: a timer armed now is
     * usually due later than everything already in the bucket.
     */
    Entry = ListHead->Blink;
    while (Entry != ListHead)
    {
        Current = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);
        if (Current->DueTime.QuadPart <= DueTime)
            break;

        Entry = Entry->Blink;
    }

    InsertHeadList(Entry, &Timer->TimerListEntry);

    /* The bucket advertises the earliest thing it holds */
    if (Entry == ListHead)
        TableEntry->Time.QuadPart = DueTime;

    InterruptTime = KiQueryInterruptTimePrecise(NULL, FALSE);
    return DueTime <= InterruptTime;
}

/*
 * Works out when a relative or absolute interval falls due and puts the timer
 * in the table for it. Answers FALSE when the moment has already passed, and
 * then the timer is left signalled rather than armed.
 *
 * Entered with the dispatcher database held.
 */
BOOLEAN
FASTCALL
KiInsertTreeTimer(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER Interval)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    ULONG Hand;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Timer->Header.Absolute = FALSE;
    Timer->Period = 0;

    if (!KiComputeDueTime(Timer, Interval, &Hand))
        return FALSE;

    /* Object lock first, bucket lock second, as every table path does */
    KiAcquireDispatcherObject(&Timer->Header);
    LockQueue = KiAcquireTimerLock(Hand);

    if (KiInsertTimerTable(Timer, Hand))
    {
        /* Already due: take it straight back out and expire it */
        KiCompleteTimer(Timer, LockQueue);
        return TRUE;
    }

    KiReleaseTimerLock(LockQueue);
    KiReleaseDispatcherObject(&Timer->Header);
    return TRUE;
}

/*
 * The timer has come due: say so, release whoever was waiting for it, hand its
 * DPC to the processor, and arm it again if it is periodic.
 *
 * Entered with the dispatcher database held.
 */
VOID
FASTCALL
KiSignalTimer(
    _Inout_ PKTIMER Timer)
{
    LARGE_INTEGER SystemTime;
    PKDPC Dpc = Timer->Dpc;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Timer->Header.Inserted = FALSE;
    Timer->Header.SignalState = 1;

    if (!IsListEmpty(&Timer->Header.WaitListHead))
        KiWaitTest(Timer, KI_TIMER_BOOST);

    /*
     * A periodic timer is armed again before its routine runs, so that the
     * period is measured from when it came due and not from when the system
     * got round to the work.
     */
    if (Timer->Period != 0)
        KiRearmPeriodicTimer(Timer);

    if (Dpc == NULL)
        return;

    /*
     * The routine is told the system time the timer came due at, split across
     * the two arguments because they are pointer-sized and it is not.
     */
    KeQuerySystemTimePrecise(&SystemTime);
    KeInsertQueueDpc(Dpc,
                     UlongToPtr(SystemTime.LowPart),
                     (PVOID)(ULONG_PTR)(LONG_PTR)SystemTime.HighPart);
}

/*
 * Finishes a timer that was found to be due the moment it was armed.
 *
 * Entered with the dispatcher database, the timer object lock and the bucket's
 * table lock all held; the latter two are released here.
 */
VOID
FASTCALL
KiCompleteTimer(
    _Inout_ PKTIMER Timer,
    _In_ PKSPIN_LOCK_QUEUE LockQueue)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KiRemoveEntryTimer(Timer);
    Timer->Header.Inserted = FALSE;
    KiReleaseTimerLock(LockQueue);

    KiSignalTimer(Timer);

    KiReleaseDispatcherObject(&Timer->Header);
}

/*
 * A notification timer stays signalled once it fires, so every thread waiting
 * on it goes through; a synchronization timer releases one of them and closes
 * again.
 */
VOID
NTAPI
KeInitializeTimerEx(
    _Out_ PKTIMER Timer,
    _In_ TIMER_TYPE Type)
{
    ASSERT((Type == NotificationTimer) || (Type == SynchronizationTimer));

    Timer->Header.Type = (UCHAR)(TimerNotificationObject + Type);

    /* Clears Absolute, Wake and the tolerance together */
    Timer->Header.TimerControlFlags = 0;
    Timer->Header.Hand = 0;
    Timer->Header.TimerMiscFlags = 0;
    Timer->Header.SignalState = 0;
    InitializeListHead(&Timer->Header.WaitListHead);

    Timer->DueTime.QuadPart = 0;
    Timer->TimerListEntry.Flink = NULL;
    Timer->TimerListEntry.Blink = NULL;
    Timer->Dpc = NULL;
    Timer->Period = 0;
}

VOID
NTAPI
KeInitializeTimer(
    _Out_ PKTIMER Timer)
{
    KeInitializeTimerEx(Timer, NotificationTimer);
}

/*
 * Arms a timer, replacing whatever it was armed for before. Answers whether it
 * had been armed, which is how a caller tells a fresh arming from a re-arming.
 *
 * A negative due time is relative to now; a positive one is an absolute system
 * time. Period, in milliseconds, makes it fire again and again.
 */
BOOLEAN
NTAPI
KeSetTimerEx(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER DueTime,
    _In_ LONG Period,
    _In_opt_ PKDPC Dpc)
{
    KIRQL OldIrql;
    BOOLEAN WasInserted;
    ULONG Hand;

    ASSERT_TIMER(Timer);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT(Period >= 0);

    OldIrql = KiAcquireDispatcherLock();

    WasInserted = Timer->Header.Inserted;
    if (WasInserted)
        KxRemoveTreeTimer(Timer);

    Timer->Header.SignalState = 0;
    Timer->Dpc = Dpc;
    Timer->Period = Period;

    /* This timer is not coalescable; any tolerance from a previous arming goes */
    Timer->Header.EncodedTolerableDelay = 0;

    if (!KiComputeDueTime(Timer, DueTime, &Hand))
    {
        /* The moment asked for has gone by: the timer is due at once */
        KiSignalTimer(Timer);
        KiReleaseDispatcherLock(OldIrql);
        return WasInserted;
    }

    /*
     * The object lock establishes the order the table insertion expects:
     * object first, bucket second. KxInsertTimer gives it back.
     */
    KiAcquireDispatcherObject(&Timer->Header);
    KxInsertTimer(Timer, Hand);

    KiReleaseDispatcherLock(OldIrql);
    return WasInserted;
}

BOOLEAN
NTAPI
KeSetTimer(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER DueTime,
    _In_opt_ PKDPC Dpc)
{
    return KeSetTimerEx(Timer, DueTime, 0, Dpc);
}

/*
 * Disarms a timer. Answers whether it was armed; a FALSE answer does not say
 * that the timer never fired, only that it was not waiting to.
 *
 * A DPC already queued is not taken back here: a caller that must know the
 * routine is not running has to use KeRemoveQueueDpcEx as well.
 */
BOOLEAN
NTAPI
KeCancelTimer(
    _Inout_ PKTIMER Timer)
{
    KIRQL OldIrql;
    BOOLEAN WasInserted;

    ASSERT_TIMER(Timer);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KiAcquireDispatcherLock();

    WasInserted = Timer->Header.Inserted;
    if (WasInserted)
        KxRemoveTreeTimer(Timer);

    Timer->Period = 0;
    Timer->Header.EncodedTolerableDelay = 0;

    KiReleaseDispatcherLock(OldIrql);
    return WasInserted;
}

/*
 * Arms a timer that may be fired late, so that the system can serve it with an
 * interrupt it was going to take anyway.
 *
 * The tolerance is in milliseconds and is a licence, not a request: the timer
 * expires somewhere in the window from the due time to the due time plus the
 * tolerance. A caller that cannot accept that must use KeSetTimerEx, which is
 * also what a tolerance of zero turns this into.
 *
 * Answers whether the timer was already armed, exactly as KeSetTimerEx does.
 */
BOOLEAN
NTAPI
KeSetCoalescableTimer(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER DueTime,
    _In_ ULONG Period,
    _In_ ULONG TolerableDelay,
    _In_opt_ PKDPC Dpc)
{
    KIRQL OldIrql;
    BOOLEAN WasInserted;
    ULONG Hand;

    ASSERT_TIMER(Timer);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /*
     * A tolerance longer than the period would let one expiry slide past the
     * next, which the caller cannot have meant.
     */
    ASSERT((Period == 0) || (TolerableDelay <= Period));
    if ((Period != 0) && (TolerableDelay > Period))
        TolerableDelay = Period;

    if (TolerableDelay == 0)
        return KeSetTimerEx(Timer, DueTime, (LONG)Period, Dpc);

    OldIrql = KiAcquireDispatcherLock();

    WasInserted = Timer->Header.Inserted;
    if (WasInserted)
        KxRemoveTreeTimer(Timer);

    Timer->Header.SignalState = 0;
    Timer->Dpc = Dpc;
    Timer->Period = (LONG)Period;

    if (!KiComputeDueTime(Timer, DueTime, &Hand))
    {
        /*
         * The moment asked for has gone by. A tolerance only ever moves an
         * expiry later, so it cannot rescue one that is already overdue.
         */
        Timer->Header.EncodedTolerableDelay = 0;
        KiSignalTimer(Timer);
        KiReleaseDispatcherLock(OldIrql);
        return WasInserted;
    }

    KiEncodeTolerableDelay(Timer, TolerableDelay);
    Hand = KiCoalesceDueTime(Timer, KiDecodeTolerableDelay(Timer));

    KiAcquireDispatcherObject(&Timer->Header);
    KxInsertTimer(Timer, Hand);

    KiReleaseDispatcherLock(OldIrql);
    return WasInserted;
}

BOOLEAN
NTAPI
KeReadStateTimer(
    _In_ PKTIMER Timer)
{
    ASSERT_TIMER(Timer);

    return (BOOLEAN)(Timer->Header.SignalState != 0);
}

/*
 * Expires a list of timers that the table scan found due.
 *
 * Entered at SYNCH_LEVEL with the dispatcher database held; it is released
 * here, because a timer routine must not run under it. The first routines are
 * called directly rather than queued: this already runs at DISPATCH_LEVEL on
 * the right processor, so queueing them would only cost another round trip.
 */
VOID
FASTCALL
KiTimerListExpire(
    _Inout_ PLIST_ENTRY ExpiredListHead,
    _In_ KIRQL OldIrql)
{
    DPC_QUEUE_ENTRY DpcEntries[MAX_TIMER_DPCS];
    LARGE_INTEGER SystemTime;
    PLIST_ENTRY Entry;
    PKTIMER Timer;
    PKDPC Dpc;
    ULONG DpcCount = 0;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KeQuerySystemTimePrecise(&SystemTime);

    while (!IsListEmpty(ExpiredListHead))
    {
        Entry = RemoveHeadList(ExpiredListHead);
        Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);

        Timer->TimerListEntry.Flink = NULL;
        Timer->TimerListEntry.Blink = NULL;
        Timer->Header.Inserted = FALSE;
        Timer->Header.SignalState = 1;

        if (!IsListEmpty(&Timer->Header.WaitListHead))
            KiWaitTest(Timer, KI_TIMER_BOOST);

        if (Timer->Period != 0)
            KiRearmPeriodicTimer(Timer);

        Dpc = Timer->Dpc;
        if (Dpc == NULL)
            continue;

        if (DpcCount < MAX_TIMER_DPCS)
        {
            DpcEntries[DpcCount].Dpc = Dpc;
            DpcEntries[DpcCount].Routine = Dpc->DeferredRoutine;
            DpcEntries[DpcCount].Context = Dpc->DeferredContext;
            DpcCount++;
            continue;
        }

        /* More than fits on the stack: the rest go through the queue */
        KeInsertQueueDpc(Dpc,
                         UlongToPtr(SystemTime.LowPart),
                         (PVOID)(ULONG_PTR)(LONG_PTR)SystemTime.HighPart);
    }

    KiReleaseDispatcherLockFromSynchLevel();

    for (Index = 0; Index < DpcCount; Index++)
    {
        DpcEntries[Index].Routine(DpcEntries[Index].Dpc,
                                  DpcEntries[Index].Context,
                                  UlongToPtr(SystemTime.LowPart),
                                  (PVOID)(ULONG_PTR)(LONG_PTR)SystemTime.HighPart);
    }

    KiExitDispatcher(OldIrql);
}

/*
 * The routine the clock queues when the table holds something due. Collects
 * everything that has come due in the buckets it was pointed at and hands the
 * lot to the expiry path.
 *
 * SystemArgument1 is the first bucket to look at and SystemArgument2 the
 * number of buckets after it, which is how the clock describes the ticks that
 * went by since it last looked.
 */
VOID
NTAPI
KiTimerExpiration(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    PKTIMER_TABLE_ENTRY TableEntry;
    PLIST_ENTRY Entry;
    PKTIMER Timer;
    LIST_ENTRY ExpiredList;
    ULONGLONG InterruptTime;
    ULONG Hand, Span, Index, Bucket;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    InitializeListHead(&ExpiredList);

    Hand = PtrToUlong(SystemArgument1) & (TIMER_TABLE_SIZE - 1);
    Span = PtrToUlong(SystemArgument2);
    if (Span >= TIMER_TABLE_SIZE)
        Span = TIMER_TABLE_SIZE - 1;

    InterruptTime = KiQueryInterruptTimePrecise(NULL, FALSE);

    OldIrql = KiAcquireDispatcherLock();

    for (Index = 0; Index <= Span; Index++)
    {
        Bucket = (Hand + Index) & (TIMER_TABLE_SIZE - 1);
        TableEntry = &KiTimerTableListHead[Bucket];

        /* Nothing in this bucket is due yet: the rest of it is later still */
        if ((ULONGLONG)TableEntry->Time.QuadPart > InterruptTime)
            continue;

        LockQueue = KiAcquireTimerLock(Bucket);

        while (!IsListEmpty(&TableEntry->Entry))
        {
            Entry = TableEntry->Entry.Flink;
            Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);

            if (Timer->DueTime.QuadPart > InterruptTime)
                break;

            KiRemoveEntryTimer(Timer);
            Timer->Header.Inserted = FALSE;
            InsertTailList(&ExpiredList, &Timer->TimerListEntry);
        }

        KiReleaseTimerLock(LockQueue);
    }

    KiTimerListExpire(&ExpiredList, OldIrql);
}

/*
 * The same expiry, entered without being told which buckets to look at, so it
 * looks at all of them. Reached from a path that knows a timer is due but not
 * where in the table it landed.
 */
VOID
NTAPI
KiExpireTimers(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KiTimerExpiration(Dpc,
                      DeferredContext,
                      UlongToPtr(0),
                      UlongToPtr(TIMER_TABLE_SIZE - 1));
}

/*
 * Walks the whole table looking for a timer that is due and was not noticed.
 * Nothing in a correct run should ever be found, so what it reports is a
 * defect in the clock path rather than in the timer.
 */
VOID
NTAPI
KiCheckTimerTable(
    _In_ ULARGE_INTEGER CurrentTime)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    PKTIMER_TABLE_ENTRY TableEntry;
    PLIST_ENTRY Entry;
    PKTIMER Timer;
    ULONG Bucket;
    KIRQL OldIrql;

    if (!KiEnableTimerWatchdog)
        return;

    OldIrql = KeRaiseIrqlToSynchLevel();

    for (Bucket = 0; Bucket < TIMER_TABLE_SIZE; Bucket++)
    {
        TableEntry = &KiTimerTableListHead[Bucket];
        LockQueue = KiAcquireTimerLock(Bucket);

        for (Entry = TableEntry->Entry.Flink;
             Entry != &TableEntry->Entry;
             Entry = Entry->Flink)
        {
            Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);
            if (Timer->DueTime.QuadPart <= CurrentTime.QuadPart)
            {
                DPRINT1("Timer %p in bucket %lu overdue: %I64u <= %I64u\n",
                        Timer, Bucket,
                        Timer->DueTime.QuadPart, CurrentTime.QuadPart);
            }
        }

        KiReleaseTimerLock(LockQueue);
    }

    KeLowerIrql(OldIrql);
}

/* EOF */
