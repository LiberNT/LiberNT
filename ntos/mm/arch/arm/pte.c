/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm/pte.c
 * PURPOSE:     ARMv7 LPAE address-space root
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mienv.h>
#include "hardware.h"

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    return FALSE;
}

BOOLEAN
MiArchTranslateWindow(
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 PhysicalAddress,
    _Out_opt_ PMI_PTE LeafPte)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(LeafPte);
    return FALSE;
}

VOID
MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    MI_ASSERT(SystemRootFrame != ProcessRootFrame);
    MI_ASSERT(System != NULL && Process != NULL);
    for (Index = 0; Index < MI_ARM_ROOT_KERNEL_INDEX; Index++)
        MI_ASSERT(Process[Index] == 0);
    for (Index = MI_ARM_ROOT_KERNEL_INDEX; Index < MI_ARM_ROOT_ENTRIES; Index++)
    {
        MI_ASSERT(MiArchPteIsValid(System[Index]));
        Process[Index] = System[Index];
    }

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
