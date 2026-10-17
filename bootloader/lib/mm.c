/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader page allocator and page type table
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define MM_HEAP_CHUNK_SIZE 0x10000
#define MM_HEAP_ALIGNMENT  16

PVOID PageLookupTableAddress;
PFN_NUMBER MmLowestPhysicalPage;
PFN_NUMBER MmHighestPhysicalPage;

static PFN_NUMBER MmTotalPages;
static PFN_NUMBER MmLoaderPagesSpanned;
static PUCHAR MmOwnedPages;
static PUCHAR MmHeapChunk;
static SIZE_T MmHeapRemaining;

static
VOID
MmMarkPages(
    _In_ PFN_NUMBER Page,
    _In_ PFN_NUMBER Count,
    _In_ TYPE_OF_MEMORY Type,
    _In_ BOOLEAN Allocation)
{
    PPAGE_LOOKUP_TABLE_ITEM Table = PageLookupTableAddress;
    PFN_NUMBER End = Page + Count;
    PFN_NUMBER i;

    if (!Table || Page >= MmHighestPhysicalPage || End <= MmLowestPhysicalPage)
        return;

    Page = max(Page, MmLowestPhysicalPage);
    End = min(End, MmHighestPhysicalPage);

    for (i = Page; i < End; i++)
    {
        Table[i - MmLowestPhysicalPage].PageAllocated = Type;
        Table[i - MmLowestPhysicalPage].PageAllocationLength = 0;
        MmOwnedPages[i - MmLowestPhysicalPage] = Allocation;
    }

    if (Allocation)
        Table[Page - MmLowestPhysicalPage].PageAllocationLength = End - Page;
}

BOOLEAN
MmSynchronizeFirmwareMap(VOID)
{
    PPAGE_LOOKUP_TABLE_ITEM Table = PageLookupTableAddress;
    PFREELDR_MEMORY_DESCRIPTOR Map;
    ULONG Count, i;
    PFN_NUMBER Page, End;

    Map = FwGetMemoryMap(&Count);
    if (!Map || !Table)
        return FALSE;

    for (i = 0; i < Count; i++)
    {
        Page = max(Map[i].BasePage, MmLowestPhysicalPage);
        End = min(Map[i].BasePage + Map[i].PageCount, MmHighestPhysicalPage);

        for (; Page < End; Page++)
        {
            if (Map[i].MemoryType != LoaderLoadedProgram)
                MmOwnedPages[Page - MmLowestPhysicalPage] = FALSE;

            if (!MmOwnedPages[Page - MmLowestPhysicalPage])
            {
                Table[Page - MmLowestPhysicalPage].PageAllocated = Map[i].MemoryType;
                Table[Page - MmLowestPhysicalPage].PageAllocationLength = 0;
            }
        }
    }

    return TRUE;
}

BOOLEAN
MmInitializeMemoryManager(VOID)
{
    PPAGE_LOOKUP_TABLE_ITEM Table;
    PFREELDR_MEMORY_DESCRIPTOR Map;
    ULONG Count, i;
    PFN_NUMBER Lowest = (PFN_NUMBER)-1;
    PFN_NUMBER Highest = 0;
    PFN_NUMBER Page;

    Map = FwGetMemoryMap(&Count);
    if (!Map || Count == 0)
        return FALSE;

    for (i = 0; i < Count; i++)
    {
        if (Map[i].MemoryType == LoaderReserve || Map[i].PageCount == 0)
            continue;

        Lowest = min(Lowest, Map[i].BasePage);
        Highest = max(Highest, Map[i].BasePage + Map[i].PageCount);
    }

    if (Highest <= Lowest)
        return FALSE;

    Table = FwAllocatePages((SIZE_T)(Highest - Lowest) * sizeof(PAGE_LOOKUP_TABLE_ITEM));
    MmOwnedPages = FwAllocatePages((SIZE_T)(Highest - Lowest));
    if (!Table || !MmOwnedPages)
        return FALSE;

    for (Page = 0; Page < Highest - Lowest; Page++)
    {
        Table[Page].PageAllocated = LoaderMaximum;
        Table[Page].PageAllocationLength = 0;
        MmOwnedPages[Page] = FALSE;
    }

    MmLowestPhysicalPage = Lowest;
    MmHighestPhysicalPage = Highest;
    MmTotalPages = Highest - Lowest;
    PageLookupTableAddress = Table;

    return MmSynchronizeFirmwareMap();
}

static
PVOID
MmAllocateMemoryBelow(
    SIZE_T MemorySize,
    TYPE_OF_MEMORY MemoryType,
    PFN_NUMBER MaxPage)
{
    PFN_NUMBER Pages = MM_SIZE_TO_PAGES(MemorySize);
    PFN_NUMBER Page;
    PVOID Memory;

    if (Pages == 0)
        return NULL;

    Memory = FwAllocatePagesBelow((SIZE_T)Pages << MM_PAGE_SHIFT,
                                  (ULONGLONG)MaxPage << MM_PAGE_SHIFT);
    if (!Memory)
        return NULL;

    Page = (PFN_NUMBER)((ULONG_PTR)Memory >> MM_PAGE_SHIFT);
    MmMarkPages(Page, Pages, MemoryType, TRUE);
    MmLoaderPagesSpanned = max(MmLoaderPagesSpanned, Page + Pages);
    return Memory;
}

PVOID
MmAllocateMemoryWithType(
    SIZE_T MemorySize,
    TYPE_OF_MEMORY MemoryType)
{
    return MmAllocateMemoryBelow(MemorySize, MemoryType, MmArchMaxLoaderPage);
}

PVOID
MmAllocateHighMemoryWithType(
    SIZE_T MemorySize,
    TYPE_OF_MEMORY MemoryType)
{
    return MmAllocateMemoryBelow(MemorySize, MemoryType, MmArchMaxRamdiskPage);
}

PVOID
MmAllocateMemoryAtAddress(
    SIZE_T MemorySize,
    PVOID DesiredAddress,
    TYPE_OF_MEMORY MemoryType)
{
    PFN_NUMBER Pages = MM_SIZE_TO_PAGES(MemorySize);
    PFN_NUMBER Page = (PFN_NUMBER)((ULONG_PTR)DesiredAddress >> MM_PAGE_SHIFT);

    if (Pages == 0 || !FwAllocatePagesAt(DesiredAddress, (SIZE_T)Pages << MM_PAGE_SHIFT))
        return NULL;

    MmMarkPages(Page, Pages, MemoryType, TRUE);
    MmLoaderPagesSpanned = max(MmLoaderPagesSpanned, Page + Pages);
    return DesiredAddress;
}

VOID
MmFreeMemory(
    PVOID MemoryPointer)
{
    PPAGE_LOOKUP_TABLE_ITEM Table = PageLookupTableAddress;
    PFN_NUMBER Page = (PFN_NUMBER)((ULONG_PTR)MemoryPointer >> MM_PAGE_SHIFT);
    PFN_NUMBER Pages;

    if (!Table || Page < MmLowestPhysicalPage || Page >= MmHighestPhysicalPage)
        return;

    Pages = Table[Page - MmLowestPhysicalPage].PageAllocationLength;
    if (Pages == 0)
        return;

    FwFreePages(MemoryPointer, (SIZE_T)Pages << MM_PAGE_SHIFT);
    MmMarkPages(Page, Pages, LoaderFree, FALSE);
}

VOID
MmSetMemoryType(
    PVOID MemoryAddress,
    SIZE_T MemorySize,
    TYPE_OF_MEMORY NewType)
{
    MmMarkPages((PFN_NUMBER)((ULONG_PTR)MemoryAddress >> MM_PAGE_SHIFT),
                MM_SIZE_TO_PAGES(MemorySize),
                NewType,
                TRUE);
}

PPAGE_LOOKUP_TABLE_ITEM
MmGetMemoryMap(
    PFN_NUMBER *NoEntries)
{
    *NoEntries = MmTotalPages;
    return PageLookupTableAddress;
}

PFN_NUMBER
MmGetTotalPagesInLookupTable(VOID)
{
    return MmTotalPages;
}

PFN_NUMBER
MmGetLoaderPagesSpanned(VOID)
{
    return MmLoaderPagesSpanned;
}

PFN_NUMBER
MmGetHighestPhysicalPage(VOID)
{
    return MmHighestPhysicalPage;
}

PVOID
MmHeapAllocate(
    SIZE_T Size)
{
    PVOID Memory;

    Size = ALIGN_UP_BY(Size, MM_HEAP_ALIGNMENT);
    if (Size == 0)
        return NULL;

    if (Size > MM_HEAP_CHUNK_SIZE / 2)
        return MmAllocateMemoryWithType(Size, LoaderOsloaderHeap);

    if (Size > MmHeapRemaining)
    {
        MmHeapChunk = MmAllocateMemoryWithType(MM_HEAP_CHUNK_SIZE, LoaderOsloaderHeap);
        if (!MmHeapChunk)
        {
            MmHeapRemaining = 0;
            return NULL;
        }

        MmHeapRemaining = MM_HEAP_CHUNK_SIZE;
    }

    Memory = MmHeapChunk;
    MmHeapChunk += Size;
    MmHeapRemaining -= Size;
    RtlZeroMemory(Memory, Size);
    return Memory;
}
