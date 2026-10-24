/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/hardware.h
 * PURPOSE:     AMD64 memory management hardware definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "archdef.h"

#define MI_AMD64_PTE_PRESENT  (1ULL << 0)
#define MI_AMD64_PTE_WRITE    (1ULL << 1)
#define MI_AMD64_PTE_USER     (1ULL << 2)
#define MI_AMD64_PTE_PWT      (1ULL << 3)
#define MI_AMD64_PTE_PCD      (1ULL << 4)
#define MI_AMD64_PTE_ACCESSED (1ULL << 5)
#define MI_AMD64_PTE_DIRTY    (1ULL << 6)
#define MI_AMD64_PTE_LARGE    (1ULL << 7)
#define MI_AMD64_PTE_GLOBAL   (1ULL << 8)
#define MI_AMD64_PTE_COPY     (1ULL << 9)
#define MI_AMD64_PTE_EXECUTE_GRANT (1ULL << 10)
#define MI_AMD64_PTE_WRITABLE (1ULL << 11)
#define MI_AMD64_PTE_NX       (1ULL << 63)
#define MI_AMD64_PTE_FRAME    0x000FFFFFFFFFF000ULL
#define MI_AMD64_SELF_BASE   (0xFFFF000000000000ULL | ((ULONG64)MI_AMD64_SELF_INDEX << 39))
#define MI_AMD64_SELF_BYTES  (1ULL << 39)

FORCEINLINE
ULONG64
MiAmd64SelfMapSlotAddress(ULONG64 Address, ULONG Level)
{
    ULONG Index;
    ULONG Shift = MI_ARCH_PAGE_SHIFT + 9 * Level;
    ULONG64 Base = MI_AMD64_SELF_BASE;

    MI_ASSERT(Level < MI_ARCH_PAGING_LEVELS);
    for (Index = 1; Index <= Level; Index++)
        Base |= (ULONG64)MI_AMD64_SELF_INDEX << (39 - 9 * Index);
    return Base + (((Address >> Shift) & ((1ULL << (48 - Shift)) - 1)) << 3);
}

FORCEINLINE
BOOLEAN
MiAmd64CanonicalAddress(ULONG64 Address)
{
    return Address < (1ULL << 47) || Address >= 0xFFFF800000000000ULL;
}

#if defined(_MSC_VER) && !defined(__clang__)
FORCEINLINE
ULONG
MiAmd64ReadAcquire32(_In_ ULONG volatile *Address)
{
    ULONG Value = *Address;
    _ReadWriteBarrier();
    return Value;
}

FORCEINLINE
ULONG64
MiAmd64ReadAcquire64(_In_ ULONG64 volatile *Address)
{
    ULONG64 Value = *Address;
    _ReadWriteBarrier();
    return Value;
}

FORCEINLINE
VOID
MiAmd64WriteRelease32(_Out_ ULONG volatile *Address, _In_ ULONG Value)
{
    _ReadWriteBarrier();
    *Address = Value;
}

FORCEINLINE
VOID
MiAmd64WriteRelease64(_Out_ ULONG64 volatile *Address, _In_ ULONG64 Value)
{
    _ReadWriteBarrier();
    *Address = Value;
}

FORCEINLINE
ULONG
MiAmd64CountTrailingZeros64(_In_ ULONG64 Value)
{
    ULONG Index;

    _BitScanForward64(&Index, Value);
    return Index;
}

#define MI_AMD64_READ_ACQUIRE32(Address) MiAmd64ReadAcquire32(Address)
#define MI_AMD64_READ_ACQUIRE64(Address) MiAmd64ReadAcquire64(Address)
#define MI_AMD64_READ_NOFENCE64(Address) ReadULong64NoFence(Address)
#define MI_AMD64_WRITE_RELEASE32(Address, Value) MiAmd64WriteRelease32(Address, Value)
#define MI_AMD64_WRITE_RELEASE64(Address, Value) MiAmd64WriteRelease64(Address, Value)
#define MI_AMD64_COMPARE_EXCHANGE64(Address, Expected, Value) \
    (InterlockedCompareExchange64((volatile LONG64 *)(Address), (LONG64)(Value), (LONG64)(Expected)) == (LONG64)(Expected))
#define MI_AMD64_INCREMENT_NOFENCE(Address) InterlockedIncrementNoFence(Address)
#define MI_AMD64_CTZ64(Value) MiAmd64CountTrailingZeros64(Value)
#else
#define MI_AMD64_READ_ACQUIRE32(Address) __atomic_load_n(Address, __ATOMIC_ACQUIRE)
#define MI_AMD64_READ_ACQUIRE64(Address) __atomic_load_n(Address, __ATOMIC_ACQUIRE)
#define MI_AMD64_READ_NOFENCE64(Address) __atomic_load_n(Address, __ATOMIC_RELAXED)
#define MI_AMD64_WRITE_RELEASE32(Address, Value) __atomic_store_n(Address, Value, __ATOMIC_RELEASE)
#define MI_AMD64_WRITE_RELEASE64(Address, Value) __atomic_store_n(Address, Value, __ATOMIC_RELEASE)
#define MI_AMD64_COMPARE_EXCHANGE64(Address, Expected, Value) \
    __atomic_compare_exchange_n(Address, &(Expected), Value, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
#define MI_AMD64_INCREMENT_NOFENCE(Address) __atomic_fetch_add(Address, 1, __ATOMIC_RELAXED)
#define MI_AMD64_CTZ64(Value) ((ULONG)__builtin_ctzll(Value))
#endif
