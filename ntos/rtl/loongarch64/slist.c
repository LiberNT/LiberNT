/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel names for the LoongArch64 SMP sequenced lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * The implementation is sdk/lib/rtl/loongarch64/slist.c: LoongArch64 has no
 * 128-bit compare-and-swap, so the header is serialized with a lock bit in
 * the Region word and the list primitives live in the shared RTL where both
 * kernel and user code can reach them.
 *
 * The kernel's own callers (ExInterlocked*, the lookaside lists) reference
 * the Exp* spellings, and the SDK uses those names inside the kernel as well.
 * Export aliases alone do not define symbols for those internal references,
 * so the two names are bound here, in kernel context only.
 */

#include <ntoskrnl.h>

/* The shared RTL implementation; declared here because the kernel does not
 * publish the RtlInterlocked* spellings in its NDK headers. */
PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(_Inout_ PSLIST_HEADER SListHead, _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry);
PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER SListHead);
PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER SListHead);

PSLIST_ENTRY
NTAPI
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY ListEntry)
{
    return RtlInterlockedPushEntrySList(ListHead, ListEntry);
}

PSLIST_ENTRY
NTAPI
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedPopEntrySList(ListHead);
}

PSLIST_ENTRY
NTAPI
ExpInterlockedFlushSList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedFlushSList(ListHead);
}

/* EOF */
