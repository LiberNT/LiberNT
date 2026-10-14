/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel memory descriptor list and loader mappings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_MAX_DESCRIPTORS 2048

static PMEMORY_ALLOCATION_DESCRIPTOR OslDescriptors;
static ULONG OslDescriptorCount;

VOID
OslConvertList(
    _Inout_ PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Entry = ListHead->Flink;
    PLIST_ENTRY Next;

    while (Entry != ListHead)
    {
        Next = Entry->Flink;
        Entry->Flink = PaToVa(Entry->Flink);
        Entry->Blink = PaToVa(Entry->Blink);
        Entry = Next;
    }

    ListHead->Flink = PaToVa(ListHead->Flink);
    ListHead->Blink = PaToVa(ListHead->Blink);
}

static
BOOLEAN
OslMapRegion(
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY Type)
{
    if (BasePage + PageCount > MmGetLoaderPagesSpanned())
        return TRUE;

    switch (Type)
    {
        case LoaderLoadedProgram:
        case LoaderOsloaderStack:
        case LoaderFirmwareTemporary:
            return MempSetupPaging(BasePage, PageCount, FALSE);

        case LoaderExceptionBlock:
        case LoaderSystemBlock:
        case LoaderFirmwarePermanent:
        case LoaderSystemCode:
        case LoaderHalCode:
        case LoaderBootDriver:
        case LoaderConsoleInDriver:
        case LoaderConsoleOutDriver:
        case LoaderStartupDpcStack:
        case LoaderStartupKernelStack:
        case LoaderStartupPanicStack:
        case LoaderStartupPcrPage:
        case LoaderStartupPdrPage:
        case LoaderRegistryData:
        case LoaderMemoryData:
        case LoaderNlsData:
        case LoaderXIPRom:
        case LoaderOsloaderHeap:
            return MempSetupPaging(BasePage, PageCount, TRUE);

        default:
            return TRUE;
    }
}

static
BOOLEAN
OslAddDescriptor(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY Type)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;

    if (Type == LoaderMaximum || Type == LoaderReserve)
        return TRUE;

    if (OslDescriptorCount >= OSL_MAX_DESCRIPTORS)
        return FALSE;

    Descriptor = &OslDescriptors[OslDescriptorCount++];
    Descriptor->BasePage = BasePage;
    Descriptor->PageCount = PageCount;
    Descriptor->MemoryType = Type;
    InsertTailList(&LoaderBlock->MemoryDescriptorListHead, &Descriptor->ListEntry);
    return TRUE;
}

BOOLEAN
WinLdrSetupMemoryLayout(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPAGE_LOOKUP_TABLE_ITEM Table;
    PFN_NUMBER Count, Start, i;
    TYPE_OF_MEMORY Type;
    PVOID ImageBase;
    SIZE_T ImageSize;

    OslDescriptors = MmAllocateMemoryWithType(OSL_MAX_DESCRIPTORS * sizeof(*OslDescriptors),
                                              LoaderMemoryData);
    if (!OslDescriptors || !MmSynchronizeFirmwareMap())
        return FALSE;

    Table = MmGetMemoryMap(&Count);
    if (!Table || Count == 0 || !MempSetupPaging(0, 1, FALSE))
        return FALSE;

    for (Start = 0, i = 1; i <= Count; i++)
    {
        if (i < Count && Table[i].PageAllocated == Table[Start].PageAllocated)
            continue;

        if (!OslMapRegion(MmLowestPhysicalPage + Start, i - Start, Table[Start].PageAllocated))
            return FALSE;

        Start = i;
    }

    Table = MmGetMemoryMap(&Count);
    for (Start = 0, i = 1; i <= Count; i++)
    {
        Type = OslArchDescriptorType(MmLowestPhysicalPage + Start, Table[Start].PageAllocated);
        if (i < Count && OslArchDescriptorType(MmLowestPhysicalPage + i, Table[i].PageAllocated) == Type)
            continue;

        if (!OslAddDescriptor(LoaderBlock,
                              MmLowestPhysicalPage + Start,
                              i - Start,
                              Type))
        {
            return FALSE;
        }

        Start = i;
    }

    if (FwGetLoaderImage(&ImageBase, &ImageSize) &&
        !MempSetupPaging((PFN_NUMBER)((ULONG_PTR)ImageBase >> MM_PAGE_SHIFT),
                         MM_SIZE_TO_PAGES(ImageSize),
                         FALSE))
    {
        return FALSE;
    }

    if (!MempSetupPaging((PFN_NUMBER)((ULONG_PTR)OslSystemBlock >> MM_PAGE_SHIFT),
                         MM_SIZE_TO_PAGES(sizeof(*OslSystemBlock)),
                         TRUE))
    {
        return FALSE;
    }

    OslConvertList(&LoaderBlock->MemoryDescriptorListHead);
    MempDump();
    return TRUE;
}
