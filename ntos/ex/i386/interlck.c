/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The x86 fastcall and i386 forms of the exported interlocked operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#undef ExInterlockedAddUlong
#undef ExInterlockedInsertHeadList
#undef ExInterlockedInsertTailList
#undef ExInterlockedRemoveHeadList
#undef ExInterlockedPopEntryList
#undef ExInterlockedPushEntryList
#undef ExInterlockedCompareExchange64
#undef InterlockedIncrement
#undef InterlockedDecrement
#undef InterlockedExchange
#undef InterlockedExchangeAdd
#undef InterlockedCompareExchange

ULONG
NTAPI
ExInterlockedAddUlong(
    _Inout_ PULONG Addend,
    _In_ ULONG Increment,
    _Inout_ PKSPIN_LOCK Lock);

PLIST_ENTRY
NTAPI
ExInterlockedInsertHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock);

PLIST_ENTRY
NTAPI
ExInterlockedInsertTailList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock);

PLIST_ENTRY
NTAPI
ExInterlockedRemoveHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock);

PSINGLE_LIST_ENTRY
NTAPI
ExInterlockedPopEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock);

PSINGLE_LIST_ENTRY
NTAPI
ExInterlockedPushEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PSINGLE_LIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock);

static
INTERLOCKED_RESULT
ExpInterlockedResult(
    _In_ LONG Value)
{
    if (Value < 0)
        return ResultNegative;
    if (Value == 0)
        return ResultZero;
    return ResultPositive;
}

ULONG
FASTCALL
ExfInterlockedAddUlong(
    _Inout_ PULONG Addend,
    _In_ ULONG Increment,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedAddUlong(Addend, Increment, Lock);
}

PLIST_ENTRY
FASTCALL
ExfInterlockedInsertHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedInsertHeadList(ListHead, ListEntry, Lock);
}

PLIST_ENTRY
FASTCALL
ExfInterlockedInsertTailList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedInsertTailList(ListHead, ListEntry, Lock);
}

PLIST_ENTRY
FASTCALL
ExfInterlockedRemoveHeadList(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedRemoveHeadList(ListHead, Lock);
}

PSINGLE_LIST_ENTRY
FASTCALL
ExfInterlockedPopEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedPopEntryList(ListHead, Lock);
}

PSINGLE_LIST_ENTRY
FASTCALL
ExfInterlockedPushEntryList(
    _Inout_ PSINGLE_LIST_ENTRY ListHead,
    _Inout_ PSINGLE_LIST_ENTRY ListEntry,
    _Inout_ PKSPIN_LOCK Lock)
{
    return ExInterlockedPushEntryList(ListHead, ListEntry, Lock);
}

LONGLONG
FASTCALL
ExfInterlockedCompareExchange64(
    _Inout_ LONGLONG volatile *Destination,
    _In_ PLONGLONG Exchange,
    _In_ PLONGLONG Comperand)
{
    return _InterlockedCompareExchange64(Destination, *Exchange, *Comperand);
}

LONGLONG
FASTCALL
ExInterlockedCompareExchange64(
    _Inout_ LONGLONG volatile *Destination,
    _In_ PLONGLONG Exchange,
    _In_ PLONGLONG Comperand,
    _Inout_ PKSPIN_LOCK Lock)
{
    UNREFERENCED_PARAMETER(Lock);

    return _InterlockedCompareExchange64(Destination, *Exchange, *Comperand);
}

INTERLOCKED_RESULT
FASTCALL
Exfi386InterlockedIncrementLong(
    _Inout_ LONG volatile *Addend)
{
    return ExpInterlockedResult(_InterlockedIncrement(Addend));
}

INTERLOCKED_RESULT
FASTCALL
Exfi386InterlockedDecrementLong(
    _Inout_ PLONG Addend)
{
    return ExpInterlockedResult(_InterlockedDecrement(Addend));
}

ULONG
FASTCALL
Exfi386InterlockedExchangeUlong(
    _Inout_ PULONG Target,
    _In_ ULONG Value)
{
    return (ULONG)_InterlockedExchange((PLONG)Target, (LONG)Value);
}

INTERLOCKED_RESULT
NTAPI
Exi386InterlockedIncrementLong(
    _Inout_ PLONG Addend)
{
    return ExpInterlockedResult(_InterlockedIncrement(Addend));
}

INTERLOCKED_RESULT
NTAPI
Exi386InterlockedDecrementLong(
    _Inout_ PLONG Addend)
{
    return ExpInterlockedResult(_InterlockedDecrement(Addend));
}

ULONG
NTAPI
Exi386InterlockedExchangeUlong(
    _Inout_ PULONG Target,
    _In_ ULONG Value)
{
    return (ULONG)_InterlockedExchange((PLONG)Target, (LONG)Value);
}

LONG
FASTCALL
InterlockedIncrement(
    _Inout_ LONG volatile *Addend)
{
    return _InterlockedIncrement(Addend);
}

LONG
FASTCALL
InterlockedDecrement(
    _Inout_ LONG volatile *Addend)
{
    return _InterlockedDecrement(Addend);
}

LONG
FASTCALL
InterlockedExchange(
    _Inout_ LONG volatile *Target,
    _In_ LONG Value)
{
    return _InterlockedExchange(Target, Value);
}

LONG
FASTCALL
InterlockedExchangeAdd(
    _Inout_ LONG volatile *Addend,
    _In_ LONG Value)
{
    return _InterlockedExchangeAdd(Addend, Value);
}

LONG
FASTCALL
InterlockedCompareExchange(
    _Inout_ LONG volatile *Destination,
    _In_ LONG Exchange,
    _In_ LONG Comperand)
{
    return _InterlockedCompareExchange(Destination, Exchange, Comperand);
}

/* EOF */
