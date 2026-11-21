/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 LPAE memory manager processor operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/mint.h>
#include <mm/include/miarch.h>
#include <mm/include/mienv.h>
#include <mm/include/mipte.h>
#include "hardware.h"

#define MI_ARM_VALID 1ULL

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    if (MiArmDirectFirstFrame == 0)
        MiArmSetDirectFirstFrame((ULONG)(LoaderBlock->u.Arm.Kseg0PhysicalBase >> PAGE_SHIFT));
}

VOID
MiArchSyncInstructionCache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Length)
{
    KeSweepICache(BaseAddress, Length);
}

NTSTATUS
MiArchSetFrameCache(ULONG Frame, ULONG Count, ULONG Flags)
{
    return MiSetDirectFrameCache(Frame, Count, Flags);
}

PVOID
MiArchMapFrame(
    _In_ ULONG64 Frame)
{
    /* Only frames in the KSEG0 window have a permanent kernel mapping. */
    if ((Frame < MiArmDirectFirstFrame) || (Frame - MiArmDirectFirstFrame >= MI_ARM_DIRECT_PAGES))
        return NULL;
    return (PVOID)(ULONG_PTR)(MI_ARM_DIRECT_BASE + ((ULONG)(Frame - MiArmDirectFirstFrame) << PAGE_SHIFT));
}

VOID
MiArchUnmapFrame(
    _In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

ULONG64
MiArchBootFrameAlias(ULONG Frame)
{
    return (ULONG64)(ULONG_PTR)MiArchMapFrame(Frame);
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    return MiArchMapFrame(Frame);
}

MI_PTE
MiArchPteRead(
    _In_ PMI_PTE Slot)
{
    return __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_ACQUIRE);
}

VOID
MiArchPteWrite(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Value)
{
    MI_PTE Old;

    if (!(Value & MI_ARM_VALID))
    {
        __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
        return;
    }

    /* Break before make (ARM ARM B3.10.1) when the output or attributes change. */
    Old = __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_RELAXED);
    if ((Old & MI_ARM_VALID) && MiArchPteNeedsBreak(Old, Value))
    {
        __atomic_store_n((volatile ULONG64 *)Slot, 0ULL, __ATOMIC_RELEASE);
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
    }
    __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
    __asm__ __volatile__("dsb ishst\n\tisb" ::: "memory");
}

/* The old and new tables agree wherever the old one maps, so the swap needs no break. */
VOID
MiArchPteReplaceTable(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Value)
{
    MI_ASSERT((Value & MI_ARM_VALID) && (MiArchPteRead(Slot) & MI_ARM_VALID));
    __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
    __asm__ __volatile__("dsb ishst\n\tisb" ::: "memory");
    MiArchInvalidateTlbAll(MiTlbAllProcessors);
}

BOOLEAN
MiArchPteCompareExchange(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Expected,
    _In_ MI_PTE Value)
{
    BOOLEAN Swapped = (BOOLEAN)__atomic_compare_exchange_n((volatile ULONG64 *)Slot, &Expected, Value, 0,
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

    if (Swapped && (Value & MI_ARM_VALID))
        __asm__ __volatile__("dsb ishst\n\tisb" ::: "memory");
    return Swapped;
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

/* TTBCR.T0SZ = 0: TTBR0 translates the whole 32-bit address space. */
ULONG64
MiArchBootRootFrame(VOID)
{
    return (KiArmReadCp15_64(0, 2) & MI_ARM_TTBR_FRAME_MASK) >> PAGE_SHIFT;
}

ULONG64
MiArchDebugRootFrame(_In_ ULONG64 VirtualAddress)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    return MiArchBootRootFrame();
}

VOID
MiArchBootClearUserHalf(
    _In_ ULONG64 RootFrame)
{
    PMI_PTE Root = MiArchMapFrame(RootFrame);
    ULONG Index;

    MI_ASSERT(Root != NULL);
    for (Index = 0; Index < MI_ARM_ROOT_KERNEL_INDEX; Index++)
        __atomic_store_n((volatile ULONG64 *)&Root[Index], 0ULL, __ATOMIC_RELEASE);
    __asm__ __volatile__("dsb ishst" ::: "memory");
    MiArchInvalidateTlbAll(MiTlbAllProcessors);
}
