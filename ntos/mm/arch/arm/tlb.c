/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 TLB maintenance
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/include/mienv.h>
#include "hardware.h"

/* TLBIMVAA / TLBIMVAAIS (all ASIDs) and TLBIALL / TLBIALLIS (ARM ARM B4.2.2). */
static
VOID
MiArmTlbPage(
    _In_ ULONG_PTR Address,
    _In_ MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors)
        __asm__ __volatile__("mcr p15, 0, %0, c8, c3, 3" :: "r"(Address) : "memory");
    else
        __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 3" :: "r"(Address) : "memory");
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    if (Scope == MiTlbAllProcessors)
        __asm__ __volatile__("mcr p15, 0, %0, c8, c3, 0\n\tmcr p15, 0, %0, c7, c1, 6" :: "r"(0) : "memory");
    else
        __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 0\n\tmcr p15, 0, %0, c7, c5, 6" :: "r"(0) : "memory");
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

VOID
MiArchInvalidateTlbSingle(PVOID VirtualAddress, MI_TLB_SCOPE Scope)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    MiArmTlbPage((ULONG_PTR)VirtualAddress & ~(ULONG_PTR)(PAGE_SIZE - 1), Scope);
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    SIZE_T Pages;

    if (Size == 0)
        return;
    if ((Size >= (SIZE_T)PAGE_SIZE * 64) || (Size > ~(ULONG_PTR)BaseAddress))
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }

    Pages = (Size + ((ULONG_PTR)BaseAddress & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT;
    __asm__ __volatile__("dsb ishst" ::: "memory");
    for (; Pages != 0; Pages--, Address += PAGE_SIZE)
        MiArmTlbPage(Address, Scope);
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;

    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress, (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateProcessors(ULONG64 VirtualAddress, ULONG64 PageCount, ULONG64 Processors)
{
    /* Inner-shareable maintenance reaches every processor; the set only narrows the cost. */
    UNREFERENCED_PARAMETER(Processors);
    MiArchTlbInvalidate(VirtualAddress, PageCount, TRUE);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
