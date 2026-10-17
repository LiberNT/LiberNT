/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader page allocator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define MM_PAGE_SIZE  PAGE_SIZE
#define MM_PAGE_SHIFT PAGE_SHIFT
#define MM_PAGE_MASK  (PAGE_SIZE - 1)
#define MM_SIZE_TO_PAGES(Size) (((Size) >> MM_PAGE_SHIFT) + (((Size) & MM_PAGE_MASK) ? 1 : 0))

typedef struct _FREELDR_MEMORY_DESCRIPTOR
{
    TYPE_OF_MEMORY MemoryType;
    PFN_NUMBER BasePage;
    PFN_NUMBER PageCount;
} FREELDR_MEMORY_DESCRIPTOR, *PFREELDR_MEMORY_DESCRIPTOR;

#pragma pack(push, 1)
typedef struct
{
    TYPE_OF_MEMORY PageAllocated;
    PFN_NUMBER PageAllocationLength;
} PAGE_LOOKUP_TABLE_ITEM, *PPAGE_LOOKUP_TABLE_ITEM;
#pragma pack(pop)

extern PVOID PageLookupTableAddress;
extern PFN_NUMBER MmLowestPhysicalPage;
extern PFN_NUMBER MmHighestPhysicalPage;

extern const PFN_NUMBER MmArchMaxLoaderPage;
extern const PFN_NUMBER MmArchMaxRamdiskPage;

BOOLEAN
MmInitializeMemoryManager(VOID);

BOOLEAN
MmSynchronizeFirmwareMap(VOID);

PVOID
MmHeapAllocate(
    SIZE_T Size);

PVOID
MmAllocateHighMemoryWithType(
    SIZE_T MemorySize,
    TYPE_OF_MEMORY MemoryType);

PVOID
MmAllocateMemoryWithType(
    SIZE_T MemorySize,
    TYPE_OF_MEMORY MemoryType);

PVOID
MmAllocateMemoryAtAddress(
    SIZE_T MemorySize,
    PVOID DesiredAddress,
    TYPE_OF_MEMORY MemoryType);

VOID
MmFreeMemory(
    PVOID MemoryPointer);

VOID
MmSetMemoryType(
    PVOID MemoryAddress,
    SIZE_T MemorySize,
    TYPE_OF_MEMORY NewType);

PPAGE_LOOKUP_TABLE_ITEM
MmGetMemoryMap(
    PFN_NUMBER *NoEntries);

PFN_NUMBER
MmGetTotalPagesInLookupTable(VOID);

PFN_NUMBER
MmGetLoaderPagesSpanned(VOID);

PFN_NUMBER
MmGetHighestPhysicalPage(VOID);

FREELDR_MEMORY_DESCRIPTOR*
UefiMemGetMemoryMap(
    PULONG MaxMemoryMapSize);
