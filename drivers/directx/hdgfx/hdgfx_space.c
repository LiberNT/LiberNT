/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-submit GPU address spaces on Sandy Bridge and Ivy Bridge
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

static PVOID
HdgfxSpaceAllocatePage(
    _In_ PHDGFX_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;

    Lowest.QuadPart = 0;
    Highest.QuadPart = (LONGLONG)Adapter->Generation->GttPhysicalLimit;
    Boundary.QuadPart = 0;
    return MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary, MmCached);
}

static VOID
HdgfxSpaceFill(
    _Out_writes_(HDGFX_PPGTT_TABLE_ENTRIES) PULONG Table,
    _In_ ULONG Value)
{
    ULONG Index;

    for (Index = 0; Index < HDGFX_PPGTT_TABLE_ENTRIES; Index++)
        Table[Index] = Value;
}

static VOID
HdgfxSpaceFreeTables(
    _In_reads_opt_(Count) PULONG *Tables,
    _In_ ULONG Count)
{
    ULONG Index;

    if (Tables == NULL)
        return;
    for (Index = 0; Index < Count; Index++)
    {
        if (Tables[Index] != NULL)
            MmFreeContiguousMemorySpecifyCache(Tables[Index], PAGE_SIZE, MmCached);
    }
    ExFreePoolWithTag(Tables, HDGFX_TAG);
}

static PULONG *
HdgfxSpaceAllocateTables(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Fill)
{
    PULONG *Tables;
    ULONG Index;

    Tables = ExAllocatePoolZero(NonPagedPoolNx, Adapter->Ppgtt.Tables * sizeof(PULONG), HDGFX_TAG);
    if (Tables == NULL)
        return NULL;
    for (Index = 0; Index < Adapter->Ppgtt.Tables; Index++)
    {
        Tables[Index] = HdgfxSpaceAllocatePage(Adapter);
        if (Tables[Index] == NULL)
        {
            HdgfxSpaceFreeTables(Tables, Adapter->Ppgtt.Tables);
            return NULL;
        }
        HdgfxSpaceFill(Tables[Index], Fill);
    }
    return Tables;
}

static VOID
HdgfxSpaceWriteDirectory(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Slot,
    _In_reads_(Adapter->Ppgtt.Tables) PULONG *Tables,
    _In_ PULONG ScratchTable)
{
    ULONG First = HdgfxPpgttDirectoryEntry(&Adapter->Ppgtt, Slot);
    ULONG Scratch = HdgfxPpgttPde((ULONGLONG)MmGetPhysicalAddress(ScratchTable).QuadPart);
    ULONG Index;

    KeMemoryBarrier();
    for (Index = 0; Index < HDGFX_PPGTT_DIRECTORY_ENTRIES; Index++)
    {
        WRITE_REGISTER_ULONG(&Adapter->PpgttGtt[First + Index],
                             Index < Adapter->Ppgtt.Tables ?
                                 HdgfxPpgttPde((ULONGLONG)MmGetPhysicalAddress(Tables[Index]).QuadPart) :
                                 Scratch);
    }
    READ_REGISTER_ULONG(&Adapter->PpgttGtt[First + HDGFX_PPGTT_DIRECTORY_ENTRIES - 1]);
    if (Slot + 1 > Adapter->PpgttSlotsUsed)
        Adapter->PpgttSlotsUsed = Slot + 1;
}

static VOID
HdgfxSpaceRelease(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PHDGFX_SPACE Space = &Adapter->PpgttSpaces[Index];
    ULONG Range;
    ULONG Page;
    ULONG Ring;

    for (Range = 0; Range < Space->RangeCount; Range++)
    {
        for (Page = Space->Ranges[Range].First;
             Page - Space->Ranges[Range].First < Space->Ranges[Range].Count; Page++)
        {
            Space->Tables[Page / HDGFX_PPGTT_TABLE_ENTRIES][Page % HDGFX_PPGTT_TABLE_ENTRIES] = Space->ScratchPte;
        }
    }
    RtlZeroMemory(Space->ScratchPage, PAGE_SIZE);
    KeMemoryBarrier();
    for (Ring = 0; Ring < HDGFX_RING_COUNT; Ring++)
    {
        if (Adapter->RingSlot[Ring] == Index + 1)
            Adapter->RingSlot[Ring] = HDGFX_PPGTT_NO_SLOT;
    }
    Space->RangeCount = 0;
    Space->Quarantined = FALSE;
    Space->Busy = FALSE;
}

VOID
HdgfxSpaceStop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_MMIO Mmio;
    ULONG Index;

    PAGED_CODE();

    if (Adapter->PpgttEnabled)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        Adapter->Generation->DisablePpgtt(&Mmio);
        Adapter->PpgttEnabled = FALSE;
    }
    if (Adapter->PpgttGtt != NULL)
    {
        for (Index = HdgfxPpgttDirectoryEntry(&Adapter->Ppgtt, 0);
             Index < HdgfxPpgttDirectoryEntry(&Adapter->Ppgtt, Adapter->PpgttSlotsUsed); Index++)
        {
            WRITE_REGISTER_ULONG(&Adapter->PpgttGtt[Index], 0);
        }
        READ_REGISTER_ULONG(&Adapter->PpgttGtt[0]);
        MmUnmapIoSpace(Adapter->PpgttGtt, Adapter->PpgttGttEntries * sizeof(ULONG));
        Adapter->PpgttGtt = NULL;
    }
    if (Adapter->PpgttSpaces != NULL)
    {
        for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
        {
            HdgfxSpaceFreeTables(Adapter->PpgttSpaces[Index].Tables, Adapter->Ppgtt.Tables);
            if (Adapter->PpgttSpaces[Index].ScratchTable != NULL)
                MmFreeContiguousMemorySpecifyCache(Adapter->PpgttSpaces[Index].ScratchTable, PAGE_SIZE, MmCached);
            if (Adapter->PpgttSpaces[Index].ScratchPage != NULL)
                MmFreeContiguousMemorySpecifyCache(Adapter->PpgttSpaces[Index].ScratchPage, PAGE_SIZE, MmCached);
        }
        ExFreePoolWithTag(Adapter->PpgttSpaces, HDGFX_TAG);
        Adapter->PpgttSpaces = NULL;
    }
    HdgfxSpaceFreeTables(Adapter->PpgttKernelTables, Adapter->Ppgtt.Tables);
    Adapter->PpgttKernelTables = NULL;
    if (Adapter->PpgttScratchTable != NULL)
        MmFreeContiguousMemorySpecifyCache(Adapter->PpgttScratchTable, PAGE_SIZE, MmCached);
    Adapter->PpgttScratchTable = NULL;
    Adapter->PpgttSlotsUsed = 0;
    RtlZeroMemory(&Adapter->Ppgtt, sizeof(Adapter->Ppgtt));
}

BOOLEAN
HdgfxSpaceStart(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONGLONG GttBase,
    _In_ ULONG GttEntries,
    _In_ ULONG MappedEntries)
{
    PHYSICAL_ADDRESS Address;
    PHDGFX_SPACE Space;
    HDGFX_MMIO Mmio;
    ULONG ScratchPte;
    ULONG Index = 0;
    ULONG Page;
    ULONG Ring;

    PAGED_CODE();

    if (Adapter->Generation->EnablePpgtt == NULL || Adapter->ScratchPage == NULL ||
        !HdgfxPpgttLayout(GttEntries, MappedEntries, &Adapter->Ppgtt) ||
        Adapter->Ppgtt.SlotCount <= HDGFX_SPACE_COUNT)
    {
        DPRINT1("No per-submit GTT: %lu GTT entries, %lu mapped\n", GttEntries, MappedEntries);
        RtlZeroMemory(&Adapter->Ppgtt, sizeof(Adapter->Ppgtt));
        return FALSE;
    }
    Address.QuadPart = (LONGLONG)GttBase;
    Adapter->PpgttGtt = MmMapIoSpace(Address, GttEntries * sizeof(ULONG), MmNonCached);
    Adapter->PpgttGttEntries = GttEntries;
    ScratchPte = Adapter->Generation->GttEntry((ULONGLONG)MmGetPhysicalAddress(Adapter->ScratchPage).QuadPart);
    Adapter->PpgttScratchTable = HdgfxSpaceAllocatePage(Adapter);
    if (Adapter->PpgttScratchTable != NULL)
        HdgfxSpaceFill(Adapter->PpgttScratchTable, ScratchPte);
    Adapter->PpgttKernelTables = HdgfxSpaceAllocateTables(Adapter, ScratchPte);
    Adapter->PpgttSpaces = ExAllocatePoolZero(NonPagedPoolNx, HDGFX_SPACE_COUNT * sizeof(HDGFX_SPACE), HDGFX_TAG);
    for (Index = 0; Adapter->PpgttSpaces != NULL && Index < HDGFX_SPACE_COUNT; Index++)
    {
        Space = &Adapter->PpgttSpaces[Index];
        Space->ScratchPage = HdgfxSpaceAllocatePage(Adapter);
        if (Space->ScratchPage == NULL)
            break;
        RtlZeroMemory(Space->ScratchPage, PAGE_SIZE);
        Space->ScratchPte =
            Adapter->Generation->GttEntry((ULONGLONG)MmGetPhysicalAddress(Space->ScratchPage).QuadPart);
        Space->ScratchTable = HdgfxSpaceAllocatePage(Adapter);
        if (Space->ScratchTable == NULL)
            break;
        HdgfxSpaceFill(Space->ScratchTable, Space->ScratchPte);
        Space->Tables = HdgfxSpaceAllocateTables(Adapter, Space->ScratchPte);
        if (Space->Tables == NULL)
            break;
    }
    if (Adapter->PpgttGtt == NULL || Adapter->PpgttScratchTable == NULL || Adapter->PpgttKernelTables == NULL ||
        Adapter->PpgttSpaces == NULL || Index < HDGFX_SPACE_COUNT)
    {
        DPRINT1("No memory for the per-submit GTT\n");
        HdgfxSpaceStop(Adapter);
        return FALSE;
    }
    for (Page = 0; Page < MappedEntries; Page++)
    {
        Adapter->PpgttKernelTables[Page / HDGFX_PPGTT_TABLE_ENTRIES][Page % HDGFX_PPGTT_TABLE_ENTRIES] =
            READ_REGISTER_ULONG(&Adapter->PpgttGtt[Page]);
    }
    HdgfxSpaceWriteDirectory(Adapter, HDGFX_PPGTT_KERNEL_SLOT, Adapter->PpgttKernelTables,
                             Adapter->PpgttScratchTable);
    for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
    {
        HdgfxSpaceWriteDirectory(Adapter, Index + 1, Adapter->PpgttSpaces[Index].Tables,
                                 Adapter->PpgttSpaces[Index].ScratchTable);
    }
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Adapter->Generation->EnablePpgtt(&Mmio, HdgfxPpgttDirectoryBase(&Adapter->Ppgtt, HDGFX_PPGTT_KERNEL_SLOT));
    for (Ring = 0; Ring < HDGFX_RING_COUNT; Ring++)
        Adapter->RingSlot[Ring] = HDGFX_PPGTT_KERNEL_SLOT;
    Adapter->PpgttEnabled = TRUE;
    DPRINT1("Per-submit GTT: %lu spaces of %lu tables over %lu MB, directories at entry %lu\n", HDGFX_SPACE_COUNT,
            Adapter->Ppgtt.Tables, MappedEntries >> 8, Adapter->Ppgtt.SlotFirst);
    return TRUE;
}

VOID
HdgfxSpaceRestart(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MMIO *Mmio)
{
    ULONG Ring;

    if (!Adapter->PpgttEnabled)
        return;
    Adapter->Generation->EnablePpgtt(Mmio, HdgfxPpgttDirectoryBase(&Adapter->Ppgtt, HDGFX_PPGTT_KERNEL_SLOT));
    for (Ring = 0; Ring < HDGFX_RING_COUNT; Ring++)
        Adapter->RingSlot[Ring] = HDGFX_PPGTT_KERNEL_SLOT;
}

VOID
HdgfxSpaceMirror(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG First,
    _In_ ULONG Count,
    _In_reads_opt_(Count) CONST ULONG *Values,
    _In_ ULONG Fill)
{
    ULONG Index;
    ULONG Page;

    if (Adapter->PpgttKernelTables == NULL)
        return;
    for (Index = 0; Index < Count; Index++)
    {
        Page = First + Index;
        if (Page < First || Page >= Adapter->Ppgtt.MappedEntries)
            break;
        Adapter->PpgttKernelTables[Page / HDGFX_PPGTT_TABLE_ENTRIES][Page % HDGFX_PPGTT_TABLE_ENTRIES] =
            Values != NULL ? Values[Index] : Fill;
    }
    KeMemoryBarrier();
}

NTSTATUS
HdgfxSpaceAcquire(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Fence,
    _In_ ULONG StateFirst,
    _In_ ULONG StatePages,
    _In_reads_(RangeCount) CONST HDGFX_USER_RANGE *Ranges,
    _In_ ULONG RangeCount,
    _Out_ PULONG Slot)
{
    PHDGFX_SPACE Space = NULL;
    ULONG Index;
    ULONG Range;
    ULONG Page;

    *Slot = HDGFX_PPGTT_NO_SLOT;
    if (!Adapter->PpgttEnabled || RangeCount > HDGFX_GEN_ALLOCATION_LIST_SIZE ||
        (LONG)(Fence - Adapter->CompletedFence) <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
    {
        if (!Adapter->PpgttSpaces[Index].Busy)
        {
            Space = &Adapter->PpgttSpaces[Index];
            break;
        }
    }
    if (Space == NULL)
        return STATUS_DEVICE_BUSY;
    Space->RangeCount = 0;
    if (StatePages != 0)
    {
        Space->Ranges[0].First = StateFirst;
        Space->Ranges[0].Count = StatePages;
        Space->RangeCount = 1;
    }
    for (Range = 0; Range < RangeCount; Range++)
    {
        if (Ranges[Range].Count != 0)
            Space->Ranges[Space->RangeCount++] = Ranges[Range];
    }
    for (Range = 0; Range < Space->RangeCount; Range++)
    {
        if (Space->Ranges[Range].First >= Adapter->Ppgtt.MappedEntries ||
            Space->Ranges[Range].Count > Adapter->Ppgtt.MappedEntries - Space->Ranges[Range].First)
        {
            Space->RangeCount = 0;
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (Range = 0; Range < Space->RangeCount; Range++)
    {
        for (Page = Space->Ranges[Range].First;
             Page - Space->Ranges[Range].First < Space->Ranges[Range].Count; Page++)
        {
            Space->Tables[Page / HDGFX_PPGTT_TABLE_ENTRIES][Page % HDGFX_PPGTT_TABLE_ENTRIES] =
                Adapter->PpgttKernelTables[Page / HDGFX_PPGTT_TABLE_ENTRIES][Page % HDGFX_PPGTT_TABLE_ENTRIES];
        }
    }
    KeMemoryBarrier();
    Space->Fence = Fence;
    Space->Busy = TRUE;
    *Slot = Index + 1;
    return STATUS_SUCCESS;
}

VOID
HdgfxSpaceRetire(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ULONG Index;

    if (Adapter->PpgttSpaces == NULL)
        return;
    for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
    {
        if (Adapter->PpgttSpaces[Index].Busy && !Adapter->PpgttSpaces[Index].Quarantined &&
            (LONG)(Adapter->PpgttSpaces[Index].Fence - Adapter->CompletedFence) <= 0)
        {
            HdgfxSpaceRelease(Adapter, Index);
        }
    }
}

VOID
HdgfxSpaceDrop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ULONG Index;

    if (Adapter->PpgttSpaces == NULL)
        return;
    for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
    {
        if (Adapter->PpgttSpaces[Index].Busy)
            HdgfxSpaceRelease(Adapter, Index);
    }
}

VOID
HdgfxSpaceQuarantine(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ULONG Index;

    if (Adapter->PpgttSpaces == NULL)
        return;
    for (Index = 0; Index < HDGFX_SPACE_COUNT; Index++)
    {
        if (Adapter->PpgttSpaces[Index].Busy)
            Adapter->PpgttSpaces[Index].Quarantined = TRUE;
    }
}
