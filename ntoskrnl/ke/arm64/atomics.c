/*
 * PROJECT:         LiberNT Kernel (ARM64)
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Minimal interlocked helper shims to satisfy MinGW CRT
 *                  atomics while a real A64 barrier-aware implementation is
 *                  brought up. These are intentionally simple wrappers around
 *                  the compiler builtins.
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ndk/rtlfuncs.h>

PVOID Arm64InterlockedCompareExchangePointer(
    _Inout_ PVOID volatile *Destination,
    _In_ PVOID Exchange,
    _In_ PVOID Comparand)
{
    PVOID Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

LONG Arm64InterlockedOr(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedAnd(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    return __atomic_fetch_and(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedXor(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    return __atomic_fetch_xor(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG Arm64InterlockedXor64(
    _Inout_ volatile LONGLONG *Destination,
    _In_ LONGLONG Value)
{
    return __atomic_fetch_xor(Destination, Value, __ATOMIC_SEQ_CST);
}

CHAR Arm64InterlockedCompareExchange8(
    _Inout_ volatile CHAR *Destination,
    _In_ CHAR Exchange,
    _In_ CHAR Comparand)
{
    CHAR Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

SHORT Arm64InterlockedCompareExchange16(
    _Inout_ volatile SHORT *Destination,
    _In_ SHORT Exchange,
    _In_ SHORT Comparand)
{
    SHORT Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

SHORT Arm64InterlockedDecrement16(
    _Inout_ volatile SHORT *Destination)
{
    return __atomic_sub_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

SHORT Arm64InterlockedIncrement16(
    _Inout_ volatile SHORT *Destination)
{
    return __atomic_add_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

#if defined(__GNUC__)
__asm__(".globl _InterlockedCompareExchangePointer\n"
        "_InterlockedCompareExchangePointer = Arm64InterlockedCompareExchangePointer\n"
        ".globl _InterlockedOr\n"
        "_InterlockedOr = Arm64InterlockedOr\n"
        ".globl _InterlockedAnd\n"
        "_InterlockedAnd = Arm64InterlockedAnd\n"
        ".globl _InterlockedXor\n"
        "_InterlockedXor = Arm64InterlockedXor\n"
        ".globl _InterlockedXor64\n"
        "_InterlockedXor64 = Arm64InterlockedXor64\n"
        ".globl _InterlockedCompareExchange8\n"
        "_InterlockedCompareExchange8 = Arm64InterlockedCompareExchange8\n"
        ".globl _InterlockedCompareExchange16\n"
        "_InterlockedCompareExchange16 = Arm64InterlockedCompareExchange16\n"
        ".globl _InterlockedDecrement16\n"
        "_InterlockedDecrement16 = Arm64InterlockedDecrement16\n"
        ".globl _InterlockedIncrement16\n"
        "_InterlockedIncrement16 = Arm64InterlockedIncrement16\n");
#endif

/*
 * ExpInterlocked*SList wrappers.
 * We provide explicit wrappers because #pragma redefine_extname behaves
 * inconsistently between GCC (renames symbol entirely, breaking callers)
 * and clang (doesn't work on COFF targets). The pragma is disabled for
 * ARM64 in sdk/lib/rtl/slist.c, so we must provide these here.
 */
PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(PSLIST_HEADER ListHead);
PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(PSLIST_HEADER ListHead, PSLIST_ENTRY ListEntry);
PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(PSLIST_HEADER ListHead);

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedPopEntrySList(ListHead);
}

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ PSLIST_ENTRY ListEntry)
{
    return RtlInterlockedPushEntrySList(ListHead, ListEntry);
}

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedFlushSList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedFlushSList(ListHead);
}
