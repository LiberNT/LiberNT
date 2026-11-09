/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     List and counter operations a caller at any IRQL may make
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * These routines may be called from an interrupt handler, above the level at
 * which the scheduler's own locks are taken, so the processor's interrupts are
 * turned off for as long as the lock is held rather than the IRQL being raised.
 */
static
BOOLEAN
ExpLockAtAnyIrql(
    _Inout_ PKSPIN_LOCK Lock)
{
    BOOLEAN Enabled = KeDisableInterrupts();

    KxAcquireSpinLock(Lock);

    return Enabled;
}

static
VOID
ExpUnlockAtAnyIrql(
    _Inout_ PKSPIN_LOCK Lock,
    _In_ BOOLEAN Enabled)
{
    KxReleaseSpinLock(Lock);

    KeRestoreInterrupts(Enabled);
}

/* PUBLIC FUNCTIONS ***********************************************************/

PLIST_ENTRY
NTAPI
ExInterlockedInsertHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    PLIST_ENTRY First;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    First = IsListEmpty(ListHead) ? NULL : ListHead->Flink;
    InsertHeadList(ListHead, ListEntry);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return First;
}

PLIST_ENTRY
NTAPI
ExInterlockedInsertTailList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    PLIST_ENTRY Last;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Last = IsListEmpty(ListHead) ? NULL : ListHead->Blink;
    InsertTailList(ListHead, ListEntry);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Last;
}

PLIST_ENTRY
NTAPI
ExInterlockedRemoveHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock)
{
    PLIST_ENTRY Entry = NULL;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    if (!IsListEmpty(ListHead))
    {
        Entry = RemoveHeadList(ListHead);
    }

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Entry;
}

PSINGLE_LIST_ENTRY
NTAPI
ExInterlockedPushEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PSINGLE_LIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    PSINGLE_LIST_ENTRY First;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    First = ListHead->Next;
    PushEntryList(ListHead, ListEntry);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return First;
}

PSINGLE_LIST_ENTRY
NTAPI
ExInterlockedPopEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock)
{
    PSINGLE_LIST_ENTRY Entry;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Entry = PopEntryList(ListHead);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Entry;
}

LARGE_INTEGER
NTAPI
ExInterlockedAddLargeInteger(
    _Inout_ PLARGE_INTEGER Addend,
    _In_ LARGE_INTEGER Increment,
    _Inout_ PKSPIN_LOCK Lock)
{
    LARGE_INTEGER Old;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Old = *Addend;
    Addend->QuadPart += Increment.QuadPart;

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Old;
}

ULONG
NTAPI
ExInterlockedAddUlong(
    _Inout_ PULONG Addend,
    _In_ ULONG Increment,
    _Inout_ PKSPIN_LOCK Lock)
{
    ULONG Old;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Old = *Addend;
    *Addend = Old + Increment;

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Old;
}

#undef ExInterlockedExchangeUlong
ULONG
NTAPI
ExInterlockedExchangeUlong(
    _Inout_ PULONG Target,
    _In_ ULONG Value,
    _Inout_ PKSPIN_LOCK Lock)
{
    ULONG Old;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Old = *Target;
    *Target = Value;

    ExpUnlockAtAnyIrql(Lock, Enabled);

    return Old;
}

#undef ExInterlockedIncrementLong
INTERLOCKED_RESULT
NTAPI
ExInterlockedIncrementLong(
    _Inout_ PLONG Addend,
    _Inout_ PKSPIN_LOCK Lock)
{
    LONG Value;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Value = ++(*Addend);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    if (Value < 0)
    {
        return ResultNegative;
    }

    return (Value > 0) ? ResultPositive : ResultZero;
}

#undef ExInterlockedDecrementLong
INTERLOCKED_RESULT
NTAPI
ExInterlockedDecrementLong(
    _Inout_ PLONG Addend,
    _Inout_ PKSPIN_LOCK Lock)
{
    LONG Value;
    BOOLEAN Enabled;

    Enabled = ExpLockAtAnyIrql(Lock);

    Value = --(*Addend);

    ExpUnlockAtAnyIrql(Lock, Enabled);

    if (Value < 0)
    {
        return ResultNegative;
    }

    return (Value > 0) ? ResultPositive : ResultZero;
}

PSLIST_ENTRY
FirstEntrySList(
    _In_ PSLIST_HEADER SListHead)
{
    return RtlFirstEntrySList(SListHead);
}

/* EOF */
