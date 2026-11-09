/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Out-of-line interlocked operations and user-buffer probes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

#undef InterlockedIncrement
LONG
NTAPI
InterlockedIncrement(
    _Inout_ volatile LONG *Addend)
{
    return _InterlockedIncrement(Addend);
}

#undef InterlockedDecrement
LONG
NTAPI
InterlockedDecrement(
    _Inout_ volatile LONG *Addend)
{
    return _InterlockedDecrement(Addend);
}

#undef InterlockedExchange
LONG
NTAPI
InterlockedExchange(
    _Inout_ volatile LONG *Target,
    _In_ LONG Value)
{
    return _InterlockedExchange(Target, Value);
}

#undef InterlockedExchangeAdd
LONG
NTAPI
InterlockedExchangeAdd(
    _Inout_ volatile LONG *Addend,
    _In_ LONG Value)
{
    return _InterlockedExchangeAdd(Addend, Value);
}

#undef InterlockedCompareExchange
LONG
NTAPI
InterlockedCompareExchange(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Exchange,
    _In_ LONG Comperand)
{
    return _InterlockedCompareExchange(Destination, Exchange, Comperand);
}

/*
 * A buffer a caller in user mode handed in has to lie wholly below the line the
 * memory manager draws, and start where the type it will be read as expects to
 * start. Nothing here touches the pages: a read that faults later raises in the
 * caller's own handler.
 */
VOID
NTAPI
ProbeForRead(
    _In_ CONST VOID *Address,
    _In_ SIZE_T Length,
    _In_ ULONG Alignment)
{
    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);

    if (Length == 0)
    {
        return;
    }

    if (((ULONG_PTR)Address & (Alignment - 1)) != 0)
    {
        ExRaiseDatatypeMisalignment();
    }

    if (((ULONG_PTR)Address + Length < (ULONG_PTR)Address) ||
        ((ULONG_PTR)Address + Length > (ULONG_PTR)MM_USER_PROBE_ADDRESS))
    {
        ExRaiseAccessViolation();
    }
}

/*
 * As well as checking the buffer, every page of it is written to, so a page that
 * is absent or copy-on-write is made writable now and a later write by the
 * kernel cannot fault where it must not.
 */
VOID
NTAPI
ProbeForWrite(
    _Out_ PVOID Address,
    _In_ SIZE_T Length,
    _In_ ULONG Alignment)
{
    volatile CHAR *Page;
    volatile CHAR *Last;

    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);

    if (Length == 0)
    {
        return;
    }

    if (((ULONG_PTR)Address & (Alignment - 1)) != 0)
    {
        ExRaiseDatatypeMisalignment();
    }

    if (((ULONG_PTR)Address + Length < (ULONG_PTR)Address) ||
        ((ULONG_PTR)Address + Length > (ULONG_PTR)MM_USER_PROBE_ADDRESS))
    {
        ExRaiseAccessViolation();
    }

    Page = (volatile CHAR *)Address;
    Last = (volatile CHAR *)((PUCHAR)Address + Length - 1);

    *Page = *Page;

    Page = (volatile CHAR *)(((ULONG_PTR)Address + PAGE_SIZE) & ~(PAGE_SIZE - 1));

    while (Page <= Last)
    {
        *Page = *Page;
        Page += PAGE_SIZE;
    }

    *Last = *Last;
}

/* EOF */
