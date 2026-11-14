/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Slim reader/writer locks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

#define _ONE ((LONG_PTR)1)

#define RTL_SRWLOCK_OWNED_BIT       0
#define RTL_SRWLOCK_CONTENDED_BIT   1
#define RTL_SRWLOCK_SHARED_BIT      2
#define RTL_SRWLOCK_TURN_BIT        3
#define RTL_SRWLOCK_OWNED           (_ONE << RTL_SRWLOCK_OWNED_BIT)
#define RTL_SRWLOCK_CONTENDED       (_ONE << RTL_SRWLOCK_CONTENDED_BIT)
#define RTL_SRWLOCK_SHARED          (_ONE << RTL_SRWLOCK_SHARED_BIT)
#define RTL_SRWLOCK_TURN            (_ONE << RTL_SRWLOCK_TURN_BIT)
#define RTL_SRWLOCK_BITS            4
#define RTL_SRWLOCK_SHARED_ONE      (_ONE << RTL_SRWLOCK_BITS)

#define RTLP_SRWLOCK_SPIN_COUNT     1024
#define RTLP_SRWLOCK_BUCKET_SPINS   64
#define RTLP_SRWLOCK_BUCKET_COUNT   64

typedef struct _RTLP_SRWLOCK_WAITER
{
    LIST_ENTRY Link;
    PRTL_SRWLOCK Lock;
    HANDLE ThreadId;
    struct _RTLP_SRWLOCK_WAITER *NextWake;
    BOOLEAN Exclusive;
    volatile LONG Woken;
} RTLP_SRWLOCK_WAITER, *PRTLP_SRWLOCK_WAITER;

typedef struct _RTLP_SRWLOCK_BUCKET
{
    volatile LONG Lock;
    LIST_ENTRY Waiters;
} RTLP_SRWLOCK_BUCKET, *PRTLP_SRWLOCK_BUCKET;

static RTLP_SRWLOCK_BUCKET RtlpSrwLockBuckets[RTLP_SRWLOCK_BUCKET_COUNT];

#define RTLP_SRWLOCK_BACKOFF_SLOTS  32
#define RTLP_SRWLOCK_BACKOFF_MAX    13

#define RTLP_SRWLOCK_TURN_LEVELS    11
#define RTLP_SRWLOCK_TURN_CLAIM     4
#define RTLP_SRWLOCK_TURN_HELD      3
#define RTLP_SRWLOCK_TURN_UNSEEN    2
#define RTLP_SRWLOCK_TURN_GAP       256
#define RTLP_SRWLOCK_TURN_COOL      8
#define RTLP_SRWLOCK_TURN_CREDIT    8

typedef struct DECLSPEC_ALIGN(128) _RTLP_SRWLOCK_BACKOFF
{
    volatile LONG Level;
    volatile LONG TurnLevel;
    volatile LONG TurnClaim;
    volatile LONG TurnLook;
    volatile LONG TurnCredit;
    volatile LONG TurnCool;
    volatile ULONG TurnSeed;
    PRTL_SRWLOCK volatile SampleLock;
    volatile LONG_PTR SampleValue;
} RTLP_SRWLOCK_BACKOFF, *PRTLP_SRWLOCK_BACKOFF;

static RTLP_SRWLOCK_BACKOFF RtlpSrwBackoffSlots[RTLP_SRWLOCK_BACKOFF_SLOTS];

C_ASSERT((RTLP_SRWLOCK_BACKOFF_SLOTS & (RTLP_SRWLOCK_BACKOFF_SLOTS - 1)) == 0);

C_ASSERT((RTLP_SRWLOCK_BUCKET_COUNT & (RTLP_SRWLOCK_BUCKET_COUNT - 1)) == 0);

static
LONG_PTR
RtlpSrwRead(
    _In_ PRTL_SRWLOCK SRWLock)
{
    return (LONG_PTR)ReadPointerNoFence((PVOID const volatile *)&SRWLock->Ptr);
}

static
LONG_PTR
RtlpSrwCompareExchange(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR NewValue,
    _In_ LONG_PTR OldValue)
{
    return (LONG_PTR)InterlockedCompareExchangePointer(&SRWLock->Ptr, (PVOID)NewValue, (PVOID)OldValue);
}

static
LONG_PTR
RtlpSrwOr(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR Bits)
{
    if (sizeof(SRWLock->Ptr) == sizeof(LONG64))
        return (LONG_PTR)InterlockedOr64((LONG64 volatile *)&SRWLock->Ptr, Bits);
    return (LONG_PTR)InterlockedOr((LONG volatile *)&SRWLock->Ptr, (LONG)Bits);
}

static
ULONG
RtlpSrwSpinLimit(VOID)
{
    return NtCurrentPeb()->NumberOfProcessors > 1 ? RTLP_SRWLOCK_SPIN_COUNT : 0;
}

static
ULONG
RtlpSrwBackoff(
    _In_ ULONG Spins)
{
    ULONG Pause = 1u << min(Spins, 6u);
    ULONG Index;

    for (Index = 0; Index < Pause; Index++)
        YieldProcessor();
    return Pause;
}

static
PRTLP_SRWLOCK_BACKOFF
RtlpSrwBackoffSlot(VOID)
{
    ULONG_PTR Thread = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;

    return &RtlpSrwBackoffSlots[(Thread >> 2) & (RTLP_SRWLOCK_BACKOFF_SLOTS - 1)];
}

static
VOID
RtlpSrwCasBackoff(
    _Inout_ PULONG Failures)
{
    PRTLP_SRWLOCK_BACKOFF Slot = RtlpSrwBackoffSlot();
    LONG Level = Slot->Level;
    ULONG Window = 64u << min((ULONG)Level + *Failures, (ULONG)RTLP_SRWLOCK_BACKOFF_MAX);
    ULONG Seed = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread * 2654435761u + *Failures * 40503u;
    ULONG Pause = Window / 2 + (Seed >> 7) % (Window / 2);
    ULONG Index;

    ASSERT(Level >= 0 && Level <= RTLP_SRWLOCK_BACKOFF_MAX);
    for (Index = 0; Index < Pause; Index++)
        YieldProcessor();
    if (*Failures == 0 && Level < RTLP_SRWLOCK_BACKOFF_MAX)
        Slot->Level = Level + 1;
    (*Failures)++;
}

static
VOID
RtlpSrwBackoffSettle(
    _In_ ULONG Failures)
{
    PRTLP_SRWLOCK_BACKOFF Slot;

    if (Failures != 0)
        return;
    Slot = RtlpSrwBackoffSlot();
    if (Slot->Level > 0)
        Slot->Level = Slot->Level - 1;
}

static
VOID
RtlpSrwTurnSpin(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _In_ ULONG Window)
{
    ULONG Seed = Slot->TurnSeed * 1664525u + 1013904223u + (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    ULONG Pause = Window / 2 + (Seed >> 8) % (Window / 2);
    ULONG Index;

    Slot->TurnSeed = Seed;
    if (RtlpSrwSpinLimit() == 0)
        return;
    /* FIXME: windows are counted in YieldProcessor calls and were sized on ARM64; x86 PAUSE takes many times longer. */
    for (Index = 0; Index < Pause; Index++)
        YieldProcessor();
}

static
ULONG
RtlpSrwTurnWindow(
    _In_ PRTLP_SRWLOCK_BACKOFF Slot)
{
    LONG Level = Slot->TurnLevel;

    ASSERT(Level >= 0 && Level <= RTLP_SRWLOCK_TURN_LEVELS);
    return 64u << Level;
}

static
VOID
RtlpSrwTurnAdjust(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _In_ LONG Delta)
{
    LONG Level = Slot->TurnLevel + Delta;

    Slot->TurnLevel = max(0, min(Level, RTLP_SRWLOCK_TURN_LEVELS));
}

static
VOID
RtlpSrwTurnLook(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _In_ LONG_PTR Value)
{
    LONG Cool = Slot->TurnCool;

    if (Cool > 0)
    {
        Slot->TurnCool = Cool - 1;
        Slot->TurnClaim = FALSE;
        return;
    }
    RtlpSrwTurnAdjust(Slot, (Value & RTL_SRWLOCK_OWNED) ? RTLP_SRWLOCK_TURN_HELD : -1);
    Slot->TurnClaim = Slot->TurnLevel >= RTLP_SRWLOCK_TURN_CLAIM;
}

static
VOID
RtlpSrwTurnChanged(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot)
{
    LONG Credit = Slot->TurnCredit;

    if (Credit >= 2)
    {
        Slot->TurnCredit = Credit - 2;
        return;
    }
    Slot->TurnCredit = 0;
    RtlpSrwTurnAdjust(Slot, -1);
}

static
VOID
RtlpSrwTurnJoined(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _In_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR NewValue)
{
    if (Slot->TurnLevel == 0)
        return;
    if (Slot->SampleLock == SRWLock)
        RtlpSrwTurnChanged(Slot);
    Slot->SampleLock = SRWLock;
    Slot->SampleValue = NewValue;
}

static
VOID
RtlpSrwTurnLeft(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _In_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR Value)
{
    LONG Credit;

    if (Slot->SampleLock != SRWLock)
        return;
    if (Value != Slot->SampleValue)
    {
        RtlpSrwTurnChanged(Slot);
    }
    else
    {
        Credit = Slot->TurnCredit;
        if (Credit < RTLP_SRWLOCK_TURN_CREDIT)
            Slot->TurnCredit = Credit + 1;
    }
    Slot->SampleLock = NULL;
}

static
BOOLEAN
RtlpSrwMustWait(
    _In_ LONG_PTR Value,
    _In_ BOOLEAN Exclusive)
{
    if (!(Value & RTL_SRWLOCK_OWNED))
        return FALSE;
    if (Exclusive)
        return TRUE;
    return !(Value & RTL_SRWLOCK_SHARED) || (Value & RTL_SRWLOCK_CONTENDED);
}

static
PRTLP_SRWLOCK_BUCKET
RtlpSrwBucket(
    _In_ PRTL_SRWLOCK SRWLock)
{
    ULONG_PTR Value = (ULONG_PTR)SRWLock;

    return &RtlpSrwLockBuckets[((Value >> 3) ^ (Value >> 9)) & (RTLP_SRWLOCK_BUCKET_COUNT - 1)];
}

static
VOID
RtlpSrwLockBucket(
    _Inout_ PRTLP_SRWLOCK_BUCKET Bucket)
{
    LARGE_INTEGER Delay;
    ULONG Spins = 0;

    while (InterlockedExchange(&Bucket->Lock, 1) != 0)
    {
        while (ReadNoFence(&Bucket->Lock) != 0)
        {
            if (++Spins < RTLP_SRWLOCK_BUCKET_SPINS)
            {
                YieldProcessor();
            }
            else
            {
                /* The holder may be a preempted lower-priority thread on this CPU. */
                Spins = 0;
                Delay.QuadPart = -10000;
                NtDelayExecution(FALSE, &Delay);
            }
        }
    }

    if (Bucket->Waiters.Flink == NULL)
        InitializeListHead(&Bucket->Waiters);
}

static
VOID
RtlpSrwUnlockBucket(
    _Inout_ PRTLP_SRWLOCK_BUCKET Bucket)
{
    ASSERT(ReadNoFence(&Bucket->Lock) == 1);
    InterlockedExchange(&Bucket->Lock, 0);
}

static
VOID
RtlpSrwWait(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ BOOLEAN Exclusive)
{
    PRTLP_SRWLOCK_BUCKET Bucket = RtlpSrwBucket(SRWLock);
    RTLP_SRWLOCK_WAITER Waiter;
    LONG_PTR Value;

    Waiter.Lock = SRWLock;
    Waiter.ThreadId = NtCurrentTeb()->ClientId.UniqueThread;
    Waiter.NextWake = NULL;
    Waiter.Exclusive = Exclusive;
    Waiter.Woken = 0;

    RtlpSrwLockBucket(Bucket);
    for (;;)
    {
        Value = RtlpSrwRead(SRWLock);
        if (!RtlpSrwMustWait(Value, Exclusive))
        {
            RtlpSrwUnlockBucket(Bucket);
            return;
        }
        if ((Value & RTL_SRWLOCK_CONTENDED) ||
            RtlpSrwCompareExchange(SRWLock, Value | RTL_SRWLOCK_CONTENDED, Value) == Value)
        {
            break;
        }
    }
    InsertTailList(&Bucket->Waiters, &Waiter.Link);
    RtlpSrwUnlockBucket(Bucket);

    while (InterlockedCompareExchange(&Waiter.Woken, 0, 0) == 0)
        NtWaitForAlertByThreadId(SRWLock, NULL);

    ASSERT(Waiter.Lock == SRWLock);
}

static
VOID
RtlpSrwReleaseContended(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR Owned)
{
    PRTLP_SRWLOCK_BUCKET Bucket = RtlpSrwBucket(SRWLock);
    PRTLP_SRWLOCK_WAITER Wake = NULL, *WakeTail = &Wake, Waiter;
    PLIST_ENTRY Entry, Next;
    BOOLEAN WakeShared = FALSE;
    BOOLEAN Remaining = FALSE;
    LONG_PTR Previous;

    RtlpSrwLockBucket(Bucket);
    if (Owned != 0 && RtlpSrwRead(SRWLock) != (Owned | RTL_SRWLOCK_CONTENDED))
    {
        /* Only an ownerless retiring shared word can be released by more than one thread. */
        ASSERT(Owned == (RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED));
        RtlpSrwUnlockBucket(Bucket);
        return;
    }

    for (Entry = Bucket->Waiters.Flink; Entry != &Bucket->Waiters; Entry = Next)
    {
        Next = Entry->Flink;
        Waiter = CONTAINING_RECORD(Entry, RTLP_SRWLOCK_WAITER, Link);
        if (Waiter->Lock != SRWLock)
            continue;
        if (Wake != NULL && (!WakeShared || Waiter->Exclusive))
        {
            Remaining = TRUE;
            break;
        }
        if (Wake == NULL)
            WakeShared = !Waiter->Exclusive;
        RemoveEntryList(Entry);
        *WakeTail = Waiter;
        WakeTail = &Waiter->NextWake;
    }

    if (Owned != 0)
    {
        Previous = (LONG_PTR)InterlockedExchangePointer(&SRWLock->Ptr, (PVOID)(Remaining ? RTL_SRWLOCK_CONTENDED : 0));
        ASSERT(Previous == (Owned | RTL_SRWLOCK_CONTENDED));
    }
    else if (Remaining)
    {
        /* An exclusive owner has already cleared the word; the waiters left behind keep the lock alive */
        RtlpSrwOr(SRWLock, RTL_SRWLOCK_CONTENDED);
    }
    RtlpSrwUnlockBucket(Bucket);

    while (Wake != NULL)
    {
        HANDLE ThreadId = Wake->ThreadId;

        Waiter = Wake;
        Wake = Waiter->NextWake;
        InterlockedExchange(&Waiter->Woken, 1);
        NtAlertThreadByThreadId(ThreadId);
    }
}

VOID
NTAPI
RtlInitializeSRWLock(OUT PRTL_SRWLOCK SRWLock)
{
    SRWLock->Ptr = NULL;
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwTurnClaimed(
    _Inout_ PRTLP_SRWLOCK_BACKOFF Slot,
    _Inout_ PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value, Previous;
    ULONG Index;

    Slot->TurnClaim = FALSE;
    if (RtlpSrwSpinLimit() != 0)
    {
        for (Index = 0; Index < RTLP_SRWLOCK_TURN_GAP; Index++)
            YieldProcessor();
    }

    Value = RtlpSrwRead(SRWLock);
    ASSERT((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) == (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED));
    ASSERT((Value >> RTL_SRWLOCK_BITS) != 0);
    if (!(Value & RTL_SRWLOCK_TURN))
        return;

    if ((Value >> RTL_SRWLOCK_BITS) == 1)
    {
        RtlpSrwTurnAdjust(Slot, -RTLP_SRWLOCK_TURN_UNSEEN);
    }
    else
    {
        Slot->TurnLevel = 0;
        Slot->TurnCool = RTLP_SRWLOCK_TURN_COOL;
    }

    while (Value & RTL_SRWLOCK_TURN)
    {
        Previous = RtlpSrwCompareExchange(SRWLock, Value & ~RTL_SRWLOCK_TURN, Value);
        if (Previous == Value)
            break;
        Value = Previous;
    }
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwAcquireSharedSlow(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ BOOLEAN Raced)
{
    PRTLP_SRWLOCK_BACKOFF Slot = RtlpSrwBackoffSlot();
    ULONG Limit = RtlpSrwSpinLimit();
    ULONG Spun = 0, Spins = 0, Failures = 0;
    BOOLEAN Look = Raced || Slot->TurnLook;
    BOOLEAN Yielded = FALSE;
    LONG_PTR Value, NewValue;

    Slot->TurnLook = FALSE;
    for (;;)
    {
        Value = RtlpSrwRead(SRWLock);
        if (RtlpSrwMustWait(Value, FALSE))
        {
            if (Spun < Limit)
            {
                Spun += RtlpSrwBackoff(Spins++);
            }
            else
            {
                RtlpSrwWait(SRWLock, FALSE);
                Spun = Spins = 0;
            }
            continue;
        }

        if ((Value & RTL_SRWLOCK_TURN) && !Yielded)
        {
            Yielded = TRUE;
            RtlpSrwCompareExchange(SRWLock, Value & ~RTL_SRWLOCK_TURN, Value);
            RtlpSrwTurnSpin(Slot, RtlpSrwTurnWindow(Slot));
            Look = TRUE;
            continue;
        }

        if (Look)
        {
            Look = FALSE;
            RtlpSrwTurnLook(Slot, Value);
        }

        if (!(Value & RTL_SRWLOCK_OWNED))
        {
            ASSERT((Value & ~RTL_SRWLOCK_CONTENDED) == 0);
            NewValue = (Value & RTL_SRWLOCK_CONTENDED) | RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED;
        }
        else
        {
            NewValue = Value + RTL_SRWLOCK_SHARED_ONE;
        }

        if (Slot->TurnClaim && !(Value & RTL_SRWLOCK_CONTENDED))
        {
            if (RtlpSrwCompareExchange(SRWLock, NewValue | RTL_SRWLOCK_TURN, Value) != Value)
                continue;
            RtlpSrwTurnClaimed(Slot, SRWLock);
            return;
        }

        if (RtlpSrwCompareExchange(SRWLock, NewValue, Value) == Value)
        {
            if (Value & RTL_SRWLOCK_OWNED)
                RtlpSrwTurnJoined(Slot, SRWLock, NewValue);
            return;
        }

        RtlpSrwTurnSpin(Slot, 64u << min(Failures, 4u));
        Failures++;
        Look = TRUE;
    }
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwReleaseSharedSlow(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ BOOLEAN Raced)
{
    PRTLP_SRWLOCK_BACKOFF Slot = RtlpSrwBackoffSlot();
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    LONG_PTR First = Value, NewValue, Previous;

    for (;;)
    {
        if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) != (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED) ||
            (Value >> RTL_SRWLOCK_BITS) == 0)
        {
            RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
        }

        if ((Value >> RTL_SRWLOCK_BITS) == 1 && !(Value & RTL_SRWLOCK_CONTENDED))
            NewValue = 0;
        else
            NewValue = (Value - RTL_SRWLOCK_SHARED_ONE) & ~RTL_SRWLOCK_TURN;

        Previous = RtlpSrwCompareExchange(SRWLock, NewValue, Value);
        if (Previous == Value)
            break;
        Value = Previous;
    }

    if (Value & RTL_SRWLOCK_CONTENDED)
    {
        if ((NewValue >> RTL_SRWLOCK_BITS) == 0)
        {
            ASSERT(NewValue == (RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED));
            RtlpSrwReleaseContended(SRWLock, RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED);
        }
        return;
    }

    /* The word is not read from here on: with this count gone the owner may free the lock. */
    RtlpSrwTurnLeft(Slot, SRWLock, First);
    if (Value & RTL_SRWLOCK_TURN)
    {
        RtlpSrwTurnSpin(Slot, RtlpSrwTurnWindow(Slot));
        Slot->TurnLook = TRUE;
        Slot->TurnClaim = Slot->TurnLevel >= RTLP_SRWLOCK_TURN_CLAIM;
    }
    else if (Raced)
    {
        RtlpSrwTurnLook(Slot, NewValue);
    }
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwAcquireExclusiveSlow(
    _Inout_ PRTL_SRWLOCK SRWLock)
{
    ULONG Limit = RtlpSrwSpinLimit();
    ULONG Spun = 0, Spins = 0, Failures = 0;
    LONG_PTR Value = RtlpSrwRead(SRWLock), Previous;

    for (;;)
    {
        if (!(Value & RTL_SRWLOCK_OWNED))
        {
            ASSERT((Value & ~RTL_SRWLOCK_CONTENDED) == 0);
            Previous = RtlpSrwCompareExchange(SRWLock, Value | RTL_SRWLOCK_OWNED, Value);
            if (Previous == Value)
            {
                RtlpSrwBackoffSettle(Failures);
                return;
            }
            RtlpSrwCasBackoff(&Failures);
            Value = RtlpSrwRead(SRWLock);
            continue;
        }

        if (Spun < Limit)
        {
            Spun += RtlpSrwBackoff(Spins++);
        }
        else
        {
            RtlpSrwWait(SRWLock, TRUE);
            Spun = Spins = 0;
        }
        Value = RtlpSrwRead(SRWLock);
    }
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwReleaseExclusiveSlow(
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_ LONG_PTR Value)
{
    if (Value == (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED))
    {
        RtlpSrwReleaseContended(SRWLock, 0);
        return;
    }

    /* The caller did not own the lock exclusively: give back the word the release took */
    if (Value != 0)
        RtlpSrwCompareExchange(SRWLock, Value, 0);
    RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
}

VOID
NTAPI
RtlAcquireSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    PRTLP_SRWLOCK_BACKOFF Slot;
    BOOLEAN Raced = FALSE;

    if (Value == 0)
    {
        Value = RtlpSrwCompareExchange(SRWLock, RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED, 0);
        if (Value == 0)
            return;
        Raced = TRUE;
    }
    else if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_TURN)) ==
             (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED))
    {
        Slot = RtlpSrwBackoffSlot();
        if (!Slot->TurnClaim)
        {
            if (RtlpSrwCompareExchange(SRWLock, Value + RTL_SRWLOCK_SHARED_ONE, Value) == Value)
            {
                RtlpSrwTurnJoined(Slot, SRWLock, Value + RTL_SRWLOCK_SHARED_ONE);
                return;
            }
            Raced = TRUE;
        }
    }

    RtlpSrwAcquireSharedSlow(SRWLock, Raced);
}

VOID
NTAPI
RtlReleaseSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    BOOLEAN Raced = FALSE;

    if (Value == (RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED))
    {
        if (RtlpSrwCompareExchange(SRWLock, 0, Value) == Value)
            return;
        Raced = TRUE;
    }
    else if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_TURN)) ==
             (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED) &&
             (Value >> RTL_SRWLOCK_BITS) > 1)
    {
        if (RtlpSrwCompareExchange(SRWLock, Value - RTL_SRWLOCK_SHARED_ONE, Value) == Value)
        {
            RtlpSrwTurnLeft(RtlpSrwBackoffSlot(), SRWLock, Value);
            return;
        }
        Raced = TRUE;
    }

    RtlpSrwReleaseSharedSlow(SRWLock, Raced);
}

VOID
NTAPI
RtlAcquireSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    if ((RtlpSrwRead(SRWLock) & RTL_SRWLOCK_OWNED) ||
        (RtlpSrwOr(SRWLock, RTL_SRWLOCK_OWNED) & RTL_SRWLOCK_OWNED))
    {
        RtlpSrwAcquireExclusiveSlow(SRWLock);
    }
}

VOID
NTAPI
RtlReleaseSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = (LONG_PTR)InterlockedExchangePointer(&SRWLock->Ptr, NULL);

    if (Value != RTL_SRWLOCK_OWNED)
        RtlpSrwReleaseExclusiveSlow(SRWLock, Value);
}

BOOLEAN
NTAPI
RtlTryAcquireSRWLockShared(PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value, NewValue;

    for (;;)
    {
        Value = RtlpSrwRead(SRWLock);
        if (!(Value & RTL_SRWLOCK_OWNED))
            NewValue = (Value & RTL_SRWLOCK_CONTENDED) | RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED;
        else if ((Value & RTL_SRWLOCK_SHARED) && !(Value & RTL_SRWLOCK_CONTENDED))
            NewValue = Value + RTL_SRWLOCK_SHARED_ONE;
        else
            return FALSE;

        if (RtlpSrwCompareExchange(SRWLock, NewValue, Value) == Value)
            return TRUE;
    }
}

BOOLEAN
NTAPI
RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value;

    for (;;)
    {
        Value = RtlpSrwRead(SRWLock);
        if (Value & RTL_SRWLOCK_OWNED)
            return FALSE;
        if (RtlpSrwCompareExchange(SRWLock, Value | RTL_SRWLOCK_OWNED, Value) == Value)
            return TRUE;
    }
}
