/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/walk.c
 * PURPOSE:     Read-only walk of the live four-level LoongArch64 tables
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mienv.h>
#include "hardware.h"

/* Trap and user-copy paths validate translations before the memory manager
 * owns the tables, so the walk only trusts table pages inside the direct map's
 * RAM range. */
static PFN_NUMBER MiLa64HighestTableFrame;

VOID
NTAPI
MiLa64InitializePageTableAccess(
    _In_ PFN_NUMBER HighestTableFrame)
{
    ASSERT(HighestTableFrame < MI_LA64_MAX_FRAME);
    __atomic_store_n(&MiLa64HighestTableFrame, HighestTableFrame, __ATOMIC_RELEASE);
}

/* VALEN = PALEN = 48: in mapped mode Vol1 sec. 5.2 requires VA[63:PALEN] to
 * equal VA[PALEN-1], otherwise ADE is raised, so bits 63:48 must repeat bit
 * 47.  The loader accepts only the 48/48 layout (winldr.c
 * La64InitializeHandoff). */
static
BOOLEAN
MiLa64WalkCanonical(
    _In_ ULONG_PTR VirtualAddress)
{
    return (LONG64)((ULONG64)VirtualAddress << 16) >> 16 == (LONG64)VirtualAddress;
}

/* The live page walk needs PWCL/PWCH to describe the four-level 4 KiB layout
 * the rest of the kernel assumes: PTbase 12 with 9-bit indices at 21, 30 and
 * 39 and 64-bit entries (Vol1 sec. 7.5.8, Table 42; sec. 7.5.9, Table 43). */
static
NTSTATUS
MiLa64CheckLayout(VOID)
{
    if (MiLa64ReadCsrPwcl() != MI_LA64_PWCL_4LEVEL_4K ||
        MiLa64ReadCsrPwch() != MI_LA64_PWCH_4LEVEL_4K)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiLa64WalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _Out_ PMI_LA64_PAGE_WALK Result)
{
    PFN_NUMBER HighestTableFrame = __atomic_load_n(&MiLa64HighestTableFrame, __ATOMIC_ACQUIRE);
    PFN_NUMBER TablePageFrame = RootPageFrame;
    ULONG Level = MI_ARCH_PAGING_LEVELS;

    if (!Result || HighestTableFrame == 0)
        return STATUS_INVALID_DEVICE_STATE;

    if (!MiLa64WalkCanonical(VirtualAddress))
        return STATUS_INVALID_ADDRESS;

    while (Level-- != 0)
    {
        /* PWCL/PWCH were verified above, so every level has a 9-bit index:
         * VA[47:39] (root), VA[38:30], VA[29:21], VA[20:12]. */
        ULONG Shift = MI_ARCH_PAGE_SHIFT + Level * MI_ARCH_L0_BITS;
        ULONG Index = (ULONG)(((ULONG64)VirtualAddress >> Shift) & ((1ULL << MI_ARCH_L0_BITS) - 1));
        PMMPTE Table, Entry;
        MMPTE Value;
        ULONG64 Raw;
        ULONG64 Frame;
        BOOLEAN Leaf;

        if (TablePageFrame > HighestTableFrame)
            return STATUS_INVALID_ADDRESS;

        Table = (PMMPTE)MiArchMapFrame(TablePageFrame);
        if (Table == NULL)
            return STATUS_INVALID_ADDRESS;
        Entry = &Table[Index];
        Raw = MiArchPteRead((PMI_PTE)Entry);

        Value.u.Long = Raw;
        Leaf = MiLa64PteIsLeaf(Raw);

        if (!Leaf && !MiLa64PteIsTable(Raw))
            return STATUS_NOT_MAPPED_VIEW;

        Frame = MiArchPteFrame(Raw);
        if (Frame >= MI_LA64_MAX_FRAME)
            return STATUS_INVALID_ADDRESS;

        if (Leaf)
        {
            ULONG64 Span = 1ULL << Shift;

            /* Large pages are not produced (descriptor SupportsLargePages is
             * FALSE); a directory entry with the huge flag is therefore a
             * layout this backend cannot report. */
            if (Level != 0)
                return STATUS_NOT_SUPPORTED;

            Result->Entry = Entry;
            Result->Value = Value;
            Result->TablePageFrame = TablePageFrame;
            Result->PhysicalAddress.QuadPart = (Frame << MI_ARCH_PAGE_SHIFT) |
                                               ((ULONG64)VirtualAddress & (Span - 1));
            Result->Level = Level;
            Result->Global = (Raw & MI_LA64_PTE_GLOBAL) != 0;
            return STATUS_SUCCESS;
        }

        /* Level 0 slots are leaves; a child pointer there means the walk ran
         * into a table that maps nothing at this address. */
        if (Level == 0)
            return STATUS_INVALID_ADDRESS;

        if (Raw & MI_LA64_PTE_HUGE)
            return STATUS_NOT_SUPPORTED;

        TablePageFrame = (PFN_NUMBER)Frame;
    }

    return STATUS_NOT_MAPPED_VIEW;
}

NTSTATUS
NTAPI
MiLa64WalkCurrentPageTables(
    _In_ PVOID Address,
    _Out_ PMI_LA64_PAGE_WALK Result)
{
    ULONG64 Crmd = MiLa64ReadCsrCrmd();
    ULONG64 Root;
    NTSTATUS Status;

    /* Direct address mode has no tables to walk. */
    if ((Crmd & MI_LA64_CRMD_PG) == 0 || (Crmd & MI_LA64_CRMD_DA) != 0)
        return STATUS_NOT_SUPPORTED;

    Status = MiLa64CheckLayout();
    if (!NT_SUCCESS(Status))
        return Status;

    /* VA bit PALEN-1 selects the root: 0 for PGDL, 1 for PGDH (Vol1
     * sec. 5.4.5; the registers themselves are sec. 7.5.5 and 7.5.6).
     * PALEN is 48 here, so that is bit 47. */
    if ((((ULONG64)(ULONG_PTR)Address >> (MI_ARCH_PA_BITS - 1)) & 1) != 0)
        Root = MiLa64ReadCsrPgdh();
    else
        Root = MiLa64ReadCsrPgdl();

    return MiLa64WalkPageTables((PFN_NUMBER)(Root >> MI_ARCH_PAGE_SHIFT),
                                (ULONG_PTR)Address, Result);
}
