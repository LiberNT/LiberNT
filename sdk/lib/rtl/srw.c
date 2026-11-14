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
#define RTL_SRWLOCK_OWNED           (_ONE << RTL_SRWLOCK_OWNED_BIT)
#define RTL_SRWLOCK_CONTENDED       (_ONE << RTL_SRWLOCK_CONTENDED_BIT)
#define RTL_SRWLOCK_SHARED          (_ONE << RTL_SRWLOCK_SHARED_BIT)
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

typedef struct DECLSPEC_ALIGN(128) _RTLP_SRWLOCK_BACKOFF
{
    volatile LONG Level;
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
    if (RtlpSrwRead(SRWLock) != (Owned | RTL_SRWLOCK_CONTENDED))
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

    Previous = (LONG_PTR)InterlockedExchangePointer(&SRWLock->Ptr, (PVOID)(Remaining ? RTL_SRWLOCK_CONTENDED : 0));
    ASSERT(Previous == (Owned | RTL_SRWLOCK_CONTENDED));
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
RtlpSrwAcquireSharedSlow(
    _Inout_ PRTL_SRWLOCK SRWLock)
{
    ULONG Limit = RtlpSrwSpinLimit();
    ULONG Spun = 0, Spins = 0, Failures = 0;
    LONG_PTR Value = RtlpSrwRead(SRWLock), NewValue, Previous;

    for (;;)
    {
        if (!(Value & RTL_SRWLOCK_OWNED))
        {
            ASSERT((Value & ~RTL_SRWLOCK_CONTENDED) == 0);
            NewValue = (Value & RTL_SRWLOCK_CONTENDED) | RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED;
        }
        else if ((Value & RTL_SRWLOCK_SHARED) && !(Value & RTL_SRWLOCK_CONTENDED))
        {
            NewValue = Value + RTL_SRWLOCK_SHARED_ONE;
        }
        else
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
            Value = RtlpSrwRead(SRWLock);
            continue;
        }

        Previous = RtlpSrwCompareExchange(SRWLock, NewValue, Value);
        if (Previous == Value)
        {
            RtlpSrwBackoffSettle(Failures);
            return;
        }
        RtlpSrwCasBackoff(&Failures);
        Value = RtlpSrwRead(SRWLock);
    }
}

static
DECLSPEC_NOINLINE
VOID
RtlpSrwReleaseSharedSlow(
    _Inout_ PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    LONG_PTR Previous;
    ULONG Failures = 0;

    if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) != (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED) ||
        (Value >> RTL_SRWLOCK_BITS) == 0)
    {
        RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
    }

    /* A zero count with SHARED|OWNED still set is retired by whoever reached zero, unless a reader rejoins first. */
    for (;;)
    {
        Previous = RtlpSrwCompareExchange(SRWLock, Value - RTL_SRWLOCK_SHARED_ONE, Value);
        if (Previous == Value)
            break;
        RtlpSrwCasBackoff(&Failures);
        Value = Previous;
        if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) != (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED) ||
            (Value >> RTL_SRWLOCK_BITS) == 0)
        {
            RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
        }
    }
    RtlpSrwBackoffSettle(Failures);
    Value -= RTL_SRWLOCK_SHARED_ONE;
    ASSERT((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) == (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED));

    while ((Value >> RTL_SRWLOCK_BITS) == 0 &&
           (Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) == (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED))
    {
        if (Value & RTL_SRWLOCK_CONTENDED)
        {
            RtlpSrwReleaseContended(SRWLock, RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED);
            return;
        }
        Previous = RtlpSrwCompareExchange(SRWLock, 0, Value);
        if (Previous == Value)
            return;
        Value = Previous;
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
    _Inout_ PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    LONG_PTR Previous;

    for (;;)
    {
        if ((Value & (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_SHARED)) != RTL_SRWLOCK_OWNED)
            RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
        ASSERT((Value & ~(RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED)) == 0);

        if (Value & RTL_SRWLOCK_CONTENDED)
        {
            RtlpSrwReleaseContended(SRWLock, RTL_SRWLOCK_OWNED);
            return;
        }

        Previous = RtlpSrwCompareExchange(SRWLock, 0, Value);
        if (Previous == Value)
            return;
        Value = Previous;
    }
}

VOID
NTAPI
RtlAcquireSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);
    LONG_PTR NewValue;

    if (Value == 0)
    {
        Value = RtlpSrwCompareExchange(SRWLock, RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED, 0);
        if (Value == 0)
            return;
    }
    if (!(Value & RTL_SRWLOCK_OWNED))
        NewValue = (Value & RTL_SRWLOCK_CONTENDED) | RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED;
    else if ((Value & (RTL_SRWLOCK_SHARED | RTL_SRWLOCK_CONTENDED)) == RTL_SRWLOCK_SHARED)
        NewValue = Value + RTL_SRWLOCK_SHARED_ONE;
    else
        NewValue = 0;

    if (NewValue == 0 || RtlpSrwCompareExchange(SRWLock, NewValue, Value) != Value)
        RtlpSrwAcquireSharedSlow(SRWLock);
}

VOID
NTAPI
RtlReleaseSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);

    if (Value != (RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED) ||
        RtlpSrwCompareExchange(SRWLock, 0, RTL_SRWLOCK_SHARED_ONE | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED) != Value)
    {
        RtlpSrwReleaseSharedSlow(SRWLock);
    }
}

VOID
NTAPI
RtlAcquireSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    LONG_PTR Value = RtlpSrwRead(SRWLock);

    if (Value == 0)
    {
        Value = RtlpSrwCompareExchange(SRWLock, RTL_SRWLOCK_OWNED, 0);
        if (Value == 0)
            return;
    }
    if ((Value & RTL_SRWLOCK_OWNED) ||
        RtlpSrwCompareExchange(SRWLock, Value | RTL_SRWLOCK_OWNED, Value) != Value)
    {
        RtlpSrwAcquireExclusiveSlow(SRWLock);
    }
}

VOID
NTAPI
RtlReleaseSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    if (RtlpSrwCompareExchange(SRWLock, 0, RTL_SRWLOCK_OWNED) != RTL_SRWLOCK_OWNED)
        RtlpSrwReleaseExclusiveSlow(SRWLock);
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
