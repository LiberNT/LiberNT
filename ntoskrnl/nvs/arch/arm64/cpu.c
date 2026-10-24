/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/cpu.c
 * PURPOSE:     ARM64 memory manager processor operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include <nvs/include/miarch.h>
#include <nvs/include/mienv.h>
#include <nvs/include/mipte.h>
#include "archdef.h"

#define MiArm64Dsb(scope)  ARM64_DSB(scope)
#define MiArm64Isb()       ARM64_ISB()

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
}

VOID
MiArchSyncInstructionCache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Length)
{
    ULONG DLine, ILine;
    ULONG_PTR Address, End = (ULONG_PTR)BaseAddress + Length;

    KiArm64GetCacheLineSizes(&DLine, &ILine);
    for (Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(DLine - 1); Address < End; Address += DLine)
        ARM64_DC(CVAU, Address);
    ARM64_DSB(ISH);
    KeSweepICache(NULL, 0);
}

NTSTATUS
MiArchSetFrameCache(ULONG Frame, ULONG Count, ULONG Flags)
{
    return MiSetDirectFrameCache(Frame, Count, Flags);
}

ULONG64
MiArchBootFrameAlias(ULONG Frame)
{
    return KSEG0_BASE | ((ULONG64)Frame << PAGE_SHIFT);
}

VOID
MiArchWriteBootPte(PMI_PTE Slot, MI_PTE Value)
{
    KIRQL OldIrql;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    MiArchPteWrite(Slot, Value);
    MiArchTlbInvalidateAll(FALSE);
    KeLowerIrql(OldIrql);
}

PVOID
MiArchMapFrame(
    _In_ ULONG64 Frame)
{
    return (PVOID)MI_ARM64_PFN_TO_VA(Frame);
}

VOID
MiArchUnmapFrame(
    _In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    if (Frame > ((~MI_ARM64_PHYS_MAP_BASE & MI_ARM64_PHYS_ADDR_MASK) >> PAGE_SHIFT))
        return NULL;
    return MiArchMapFrame(Frame);
}

MI_PTE
MiArchPteRead(
    _In_ PMI_PTE Slot)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return ReadULong64Acquire((volatile ULONG64 *)Slot);
#else
    return __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_ACQUIRE);
#endif
}

VOID
MiArchPteWrite(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Value)
{
    MI_PTE Old;

    if (!(Value & 1ULL))
    {
#if defined(_MSC_VER) && !defined(__clang__)
        WriteULong64Release((volatile ULONG64 *)Slot, Value);
#else
        __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
#endif
        return;
    }

#if defined(_MSC_VER) && !defined(__clang__)
    Old = ReadULong64NoFence((volatile ULONG64 *)Slot);
#else
    Old = __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_RELAXED);
#endif
    if ((Old & 1ULL) && MiArchPteNeedsBreak(Old, Value))
    {
#if defined(_MSC_VER) && !defined(__clang__)
        WriteULong64Release((volatile ULONG64 *)Slot, 0ULL);
#else
        __atomic_store_n((volatile ULONG64 *)Slot, 0ULL, __ATOMIC_RELEASE);
#endif
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
    }

#if defined(_MSC_VER) && !defined(__clang__)
    WriteULong64Release((volatile ULONG64 *)Slot, Value);
#else
    __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
#endif

    if (!MiArchPteIsUser(Value))
    {
        MiArm64Dsb(ISHST);
        MiArm64Isb();
    }
}

BOOLEAN
MiArchPteCompareExchange(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Expected,
    _In_ MI_PTE Value)
{
#if defined(_MSC_VER) && !defined(__clang__)
    BOOLEAN Swapped = (BOOLEAN)(_InterlockedCompareExchange64((volatile LONG64 *)Slot, (LONG64)Value, (LONG64)Expected) ==
                                (LONG64)Expected);
#else
    BOOLEAN Swapped = (BOOLEAN)__atomic_compare_exchange_n((volatile ULONG64 *)Slot, &Expected, Value, 0,
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif

    if (Swapped && (Value & 1ULL) && !MiArchPteIsUser(Value))
    {
        MiArm64Dsb(ISHST);
        MiArm64Isb();
    }

    return Swapped;
}

ULONG64
MiArchBootRootFrame(VOID)
{
    ULONG64 Ttbr1;

    Ttbr1 = ARM64_READ_SYSREG(TTBR1_EL1);
    return (Ttbr1 & 0x0000FFFFFFFFF000ULL) >> PAGE_SHIFT;
}

ULONG64
MiArchDebugRootFrame(_In_ ULONG64 VirtualAddress)
{
    ULONG64 Ttbr;

    if (VirtualAddress & (1ULL << 63))
        Ttbr = ARM64_READ_SYSREG(TTBR1_EL1);
    else
        Ttbr = ARM64_READ_SYSREG(TTBR0_EL1);

    return (Ttbr & 0x0000FFFFFFFFF000ULL) >> PAGE_SHIFT;
}

VOID
MiArchBootClearUserHalf(
    _In_ ULONG64 RootFrame)
{
    PMI_PTE Root = (PMI_PTE)MI_ARM64_PFN_TO_VA(RootFrame);
    ULONG Index;

    for (Index = 0; Index < 256; Index++)
#if defined(_MSC_VER) && !defined(__clang__)
        WriteULong64Release((volatile ULONG64 *)&Root[Index], 0ULL);
#else
        __atomic_store_n((volatile ULONG64 *)&Root[Index], 0ULL, __ATOMIC_RELEASE);
#endif

    MiArm64Dsb(ISHST);
    MiArchInvalidateTlbAll(MiTlbAllProcessors);
    DbgPrint("TLB shootdown: broadcast TLBI\n");
}
