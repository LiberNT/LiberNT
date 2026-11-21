/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/root.c
 * PURPOSE:     ARM64 address-space root and translation windows
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mienv.h>

#define A64_SELF_MAP_INDEX   493
#define A64_HYPERSPACE_INDEX 494

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    return VirtualAddress >= 0xFFFFF68000000000ULL && VirtualAddress <= 0xFFFFF6FFFFFFFFFFULL;
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
    for (Index = 256; Index < 512; Index++)
    {
        if (Index != A64_SELF_MAP_INDEX && Index != A64_HYPERSPACE_INDEX)
            Process[Index] = System[Index];
    }

    Process[A64_SELF_MAP_INDEX] = MiArchPteMakeTable(ProcessRootFrame, 0);

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
