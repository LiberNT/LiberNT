/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/cpu.c
 * PURPOSE:     LoongArch64 memory manager processor operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/nt/mint.h>
#include "hardware.h"

/*
 * Boot handoff.  The loader leaves PG=1/DA=0 with DATF=DATM=01 (the only legal
 * DA/PG pair and the "consistent cacheable" direct-mode type, Vol1 sec. 7.4.1
 * Table 15), one 4 KiB root in both PGDL and PGDH (sec. 7.5.5/7.5.6 require
 * 4 KiB alignment), PWCL/PWCH describing the four-level layout and
 * STLBPS.PS = 12 (12 = log2 4 KiB, sec. 7.5.10 Table 44), and DMW0/1/2 with
 * VSEG 0x8/0x9/0xA (sec. 7.5.18) -- the handoff contract sec. 2.1-2.5 records
 * the same values.  Everything the kernel touches before its own memory
 * manager runs is in the DMW1 direct map, so the PGDH half holds only
 * KI_USER_SHARED_DATA.
 */
VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    const LOONGARCH64_LOADER_BLOCK *LaBlock = &LoaderBlock->u.LoongArch64;
    ULONG64 Crmd;

    if (LaBlock->Version != LOONGARCH64_LOADER_BLOCK_VERSION ||
        LaBlock->Size != sizeof(*LaBlock) ||
        (LaBlock->Flags & LOONGARCH64_LOADER_REQUIRED_FLAGS) != LOONGARCH64_LOADER_REQUIRED_FLAGS ||
        LaBlock->DirectMapBase != LOONGARCH64_LOADER_DIRECT_MAP_BASE ||
        LaBlock->UncachedBase != LOONGARCH64_LOADER_UNCACHED_BASE ||
        LaBlock->WriteCombineBase != LOONGARCH64_LOADER_WRITE_COMBINE_BASE ||
        LaBlock->PhysicalLimit != LOONGARCH64_LOADER_PHYSICAL_LIMIT ||
        LaBlock->HighestMappedPhysicalAddress >= LaBlock->PhysicalLimit ||
        LaBlock->Pwcl != MI_LA64_PWCL_4LEVEL_4K ||
        LaBlock->Pwch != MI_LA64_PWCH_4LEVEL_4K ||
        LaBlock->StlbPs != MI_ARCH_PAGE_SHIFT ||
        LaBlock->Pgdl == 0 || (LaBlock->Pgdl & (PAGE_SIZE - 1)) != 0 ||
        LaBlock->Pgdl != LaBlock->Pgdh ||
        LaBlock->Pgdl != LaBlock->PageDirectoryRoot)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x4C413634, LaBlock->Pgdl, LaBlock->Pgdh, LaBlock->Pwcl);
    }

    /* Mapped mode with a coherent cached default, as the loader delivered it;
     * DMW windows are only meaningful with PG=1 (Vol1 sec. 7.4.1, 7.5.18). */
    Crmd = MiLa64ReadCsrCrmd();
    if ((Crmd & MI_LA64_CRMD_PG) == 0 || (Crmd & MI_LA64_CRMD_DA) != 0)
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x4C414D44, Crmd, 0, 0);

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MiArchDescribe()->SystemAddressStart;
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
    /* The memory type lives in the mapping (PTE MAT, DMW MAT), not in the
     * frame: the MAT field of the entry that maps a page fixes its access
     * type, so two mappings of one frame may differ (Vol1 sec. 2.1.7,
     * sec. 7.5.3 Table 36).  Nothing to record.
     * TODO: if a caller relies on a per-frame default, it must pass the
     * attribute to MiArchPteMakeLeaf instead. */
    UNREFERENCED_PARAMETER(Frame);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

ULONG64
MiArchBootFrameAlias(ULONG Frame)
{
    MI_ASSERT((ULONG64)Frame < MI_LA64_MAX_FRAME);
    return MI_LA64_DIRECT_MAP_BASE + ((ULONG64)Frame << MI_ARCH_PAGE_SHIFT);
}

VOID
MiArchWriteBootPte(PMI_PTE Slot, MI_PTE Value)
{
    KIRQL OldIrql;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    MiArchPteWrite(Slot, Value);
    MiArchInvalidateTlbAll(MiTlbLocal);
    KeLowerIrql(OldIrql);
}

PVOID
MiArchMapFrame(_In_ ULONG64 Frame)
{
    MI_ASSERT(Frame < MI_LA64_MAX_FRAME);
    return (PVOID)(ULONG_PTR)(MI_LA64_DIRECT_MAP_BASE + (Frame << MI_ARCH_PAGE_SHIFT));
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    if (Frame >= MI_LA64_MAX_FRAME)
        return NULL;
    return MiArchMapFrame(Frame);
}

VOID
MiArchUnmapFrame(_In_ PVOID Mapping)
{
    /* Frames are reached through the DMW1 window, which needs no entry. */
    UNREFERENCED_PARAMETER(Mapping);
}

MI_PTE
MiArchPteRead(_In_ PMI_PTE Slot)
{
    return __atomic_load_n(Slot, __ATOMIC_ACQUIRE);
}

/* Once a directory entry stops pointing at a table, every hart must drop the
 * walks it cached through that table before the page can be reused. */
static
VOID
MiLa64CheckTableUnlink(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    if (MiLa64PteIsTable(Old) && Old != New)
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
}

/* Vol1 sec. 2.1.7.1: instruction-cache coherence with other cores and with
 * cache-coherent I/O masters is maintained by hardware, but coherence between
 * the I cache and the D cache inside a core may be left to software, and
 * software must execute IBAR to observe its own stores.  This port assumes no
 * hardware I/D coherence (descriptor HasCoherentDcache = FALSE), so before a
 * leaf lets any hart execute contents written since the frame was last
 * executable (paged in, copied, zeroed or written through a writable mapping)
 * every hart must discard stale instructions.  An unchanged read-only code
 * leaf needs nothing. */
static
VOID
MiLa64CheckInstructionSync(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    if (Old == New)
        return;

    if (!MiLa64PteIsLeaf(New) || (New & (MI_LA64_PTE_NX | MI_LA64_PTE_NR)) != 0)
        return;

    if (MiLa64PteIsLeaf(Old) && (Old & (MI_LA64_PTE_NX | MI_LA64_PTE_WRITE)) == 0 &&
        (Old & MI_LA64_PTE_PFN_MASK) == (New & MI_LA64_PTE_PFN_MASK))
    {
        return;
    }

    KeSweepICache(NULL, 0);
}

static
VOID
MiLa64CheckNewTranslation(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    if (MiArchPteIsValid(Old) || !MiArchPteIsValid(New))
        return;

    /* A new supervisor leaf is visible to every processor running kernel
     * code; a user leaf belongs to one address space whose other processors
     * the core reaches through MiArchTlbInvalidateProcessors, and a new table
     * only matters here. */
    if (MiLa64PteIsLeaf(New) && !MiArchPteIsUser(New))
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
    else
        MiArchInvalidateTlbAll(MiTlbLocal);
}

VOID
MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    MI_PTE Old = __atomic_exchange_n(Slot, Value, __ATOMIC_ACQ_REL);

    MiLa64CheckTableUnlink(Old, Value);
    MiLa64CheckNewTranslation(Old, Value);
    MiLa64CheckInstructionSync(Old, Value);
}

BOOLEAN
MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value)
{
    MI_PTE Old = Expected;
    BOOLEAN Swapped = (BOOLEAN)__atomic_compare_exchange_n(Slot, &Old, Value, FALSE,
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

    if (Swapped)
    {
        MiLa64CheckTableUnlink(Expected, Value);
        MiLa64CheckNewTranslation(Expected, Value);
        MiLa64CheckInstructionSync(Expected, Value);
    }

    return Swapped;
}

/*
 * The loader fills every unused directory and PTE slot with a pointer to one
 * shared zero page, with P clear (winldr.c La64BuildPageTables, handoff
 * contract D-21).  That encoding exists because a directory slot is a bare
 * page frame to the walker, so a zero slot could send it to physical page 0.
 *
 * The memory manager's convention is the opposite one: a slot is occupied iff
 * MiArchPteIsValid() and a non-zero invalid slot is never overwritten
 * (core/pagetable.c MiPtWalk, core/adopt.c MiSystemPopulateTopLevel).  A
 * shared-empty pointer would therefore look like an unusable occupied slot and
 * the kernel half could never gain a table.  Collapse those pointers to 0
 * here, once, before the core adopts the tree; every table the core allocates
 * afterwards carries P and is recognised by MiArchPteIsValid.
 *
 * Level 3 is the root, level 0 the PTE table: a slot at level > 0 is a child
 * pointer, so only those are descended into.
 */
static
VOID
MiLa64CanonicalizeTable(_In_ ULONG64 Frame, _In_ ULONG Level)
{
    PMI_PTE Table;
    ULONG Index;

    if (Frame >= MI_LA64_MAX_FRAME)
        return;

    Table = MiArchMapFrame(Frame);
    if (Table == NULL)
        return;

    for (Index = 0; Index < MI_LA64_ROOT_ENTRIES; Index++)
    {
        MI_PTE Value = MiArchPteRead(&Table[Index]);

        if (Value == 0)
            continue;

        if ((Value & (PAGE_SIZE - 1)) == 0)
        {
            /* The loader's shared empty-table pointer. */
            __atomic_store_n(&Table[Index], 0ULL, __ATOMIC_RELEASE);
            continue;
        }

        if (Level != 0 && (Value & MI_LA64_PTE_PRESENT) && (Value & MI_LA64_PTE_HUGE) == 0)
            MiLa64CanonicalizeTable(MiArchPteFrame(Value), Level - 1);
    }
}

ULONG64
MiArchBootRootFrame(VOID)
{
    ULONG64 Root;

    /* CRMD.PG = 1 is the handoff contract, so PGDH is live: the root for a
     * virtual address with bit PALEN-1 set (Vol1 sec. 5.4.5, 7.5.6). */
    Root = MiLa64ReadCsrPgdh();
    if ((Root & (PAGE_SIZE - 1)) != 0)
        return 0;

    MiLa64CanonicalizeTable(Root >> MI_ARCH_PAGE_SHIFT, MI_ARCH_PAGING_LEVELS - 1);
    MiArchInvalidateTlbAll(MiTlbLocal);

    return Root >> MI_ARCH_PAGE_SHIFT;
}

ULONG64
MiArchDebugRootFrame(_In_ ULONG64 VirtualAddress)
{
    /* VA bit PALEN-1 selects the root, PGDH when it is 1 and PGDL when it is
     * 0 (Vol1 sec. 5.4.5; the registers are sec. 7.5.5 and 7.5.6), so bit 47
     * picks the kernel or the user half. */
    if ((VirtualAddress >> (MI_ARCH_PA_BITS - 1)) & 1)
        return MiLa64ReadCsrPgdh() >> MI_ARCH_PAGE_SHIFT;

    return MiLa64ReadCsrPgdl() >> MI_ARCH_PAGE_SHIFT;
}

VOID
MiArchBootClearUserHalf(_In_ ULONG64 RootFrame)
{
    PMI_PTE Root = MiArchMapFrame(RootFrame);
    ULONG Index;

    MI_ASSERT(RootFrame < MI_LA64_MAX_FRAME);

    /* Drop the loader's user-half entries; its tables stay loader data.  The
     * write is the core's "empty slot" value.  Vol1 sec. 5.4.5 defines P as
     * "whether the physical page exists", so a P-clear slot denotes an absent
     * page, but it does not say what a hardware walker does with a P-clear
     * directory slot.  TODO: G2 - verify on hardware that a P-clear directory
     * entry stops the walk; if the walker instead takes the slot as a bare
     * frame, this and every unlink in the core would translate to physical
     * page 0. */
    for (Index = 0; Index < MI_LA64_ROOT_KERNEL_INDEX; Index++)
        __atomic_store_n(&Root[Index], 0ULL, __ATOMIC_RELEASE);

    MiArchInvalidateTlbAll(MiTlbAllProcessors);
    MiArchUnmapFrame(Root);
    DbgPrint("TLB shootdown: remote invtlb requests\n");
}
