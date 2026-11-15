/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 page attribute table and global page enabling
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define PDE_BITS 10
#define PTE_BITS 10

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableGlobalPage(IN ULONG_PTR Context)
{
#if defined(_GLOBAL_PAGES_ARE_AWESOME_)
    ULONG Cr4;
#endif
    BOOLEAN Enable;

    Enable = KeDisableInterrupts();

#if defined(_GLOBAL_PAGES_ARE_AWESOME_)

    Cr4 = __readcr4();
    ASSERT(!(Cr4 & CR4_PGE));

    __writecr3(__readcr3());

    __writecr4(Cr4 | CR4_PGE);

#endif

    KeRestoreInterrupts(Enable);
    return 0;
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializePAT(VOID)
{
    /* FIXME: Support this */
    DPRINT("PAT support detected but not yet taken advantage of\n");
}

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableTargetLargePage(IN ULONG_PTR Context)
{
    PLARGE_IDENTITY_MAP IdentityMap = (PLARGE_IDENTITY_MAP)Context;

    Ki386EnableCurrentLargePage(IdentityMap->StartAddress, IdentityMap->Cr3);

    return 0;
}

PVOID
NTAPI
Ki386AllocateContiguousMemory(
    PLARGE_IDENTITY_MAP IdentityMap,
    ULONG PagesCount,
    BOOLEAN InLower4Gb)
{
    PHYSICAL_ADDRESS AddressMask;
    PVOID Result;
    ULONG SizeInBytes = PagesCount * PAGE_SIZE;

    AddressMask.LowPart = 0xFFFFFFFF;
    AddressMask.HighPart = 0xFFFFFFFF;

    if (InLower4Gb) AddressMask.HighPart = 0;

    Result = MmAllocateContiguousMemory(SizeInBytes, AddressMask);
    if (!Result) return NULL;

    RtlZeroMemory(Result, SizeInBytes);

    IdentityMap->PagesList[IdentityMap->PagesCount] = Result;
    IdentityMap->PagesCount++;

    return Result;
}

PHYSICAL_ADDRESS
NTAPI
Ki386BuildIdentityBuffer(
    PLARGE_IDENTITY_MAP IdentityMap,
    PVOID StartPtr,
    ULONG Length)
{
    // TODO: Check whether all pages are below 4GB boundary
    return MmGetPhysicalAddress(StartPtr);
}

BOOLEAN
NTAPI
Ki386IdentityMapMakeValid(
    PLARGE_IDENTITY_MAP IdentityMap,
    PHARDWARE_PTE Pde,
    PHARDWARE_PTE *PageTable)
{
    ULONG NewPage;

    if (Pde->Valid == 0)
    {
        NewPage = (ULONG)Ki386AllocateContiguousMemory(IdentityMap, 1, FALSE);
        if (!NewPage) return FALSE;

        Pde->PageFrameNumber = NewPage >> PAGE_SHIFT;
        Pde->Valid = 1;

        if (PageTable) *PageTable = (PHARDWARE_PTE)NewPage;
    }
    else
    {
        if (PageTable)
            *PageTable = (PHARDWARE_PTE)(Pde->PageFrameNumber << PAGE_SHIFT);
    }

    return TRUE;
}

BOOLEAN
NTAPI
Ki386MapAddress(
    PLARGE_IDENTITY_MAP IdentityMap,
    ULONG_PTR VirtualPtr,
    PHYSICAL_ADDRESS PhysicalPtr)
{
    PHARDWARE_PTE Pde, Pte;
    PHARDWARE_PTE PageTable;

    if (!IdentityMap->TopLevelDirectory)
    {
        IdentityMap->TopLevelDirectory = Ki386AllocateContiguousMemory(IdentityMap, 1, 1);
        if (!IdentityMap->TopLevelDirectory) return FALSE;
    }

    Pde = &IdentityMap->TopLevelDirectory[(VirtualPtr >> 22) & ((1 << PDE_BITS) - 1)];
    if (!Ki386IdentityMapMakeValid(IdentityMap, Pde, &PageTable)) return FALSE;
    Pde->Write = 1;

    Pte = &PageTable[(VirtualPtr >> 12) & ((1 << PTE_BITS) - 1)];
    Pte->Valid = 1;
    Pte->PageFrameNumber = (PFN_NUMBER)(PhysicalPtr.QuadPart >> PAGE_SHIFT);

    return TRUE;
}

VOID
NTAPI
Ki386ConvertPte(
    PHARDWARE_PTE Pte)
{
    PVOID VirtualPtr;
    PHYSICAL_ADDRESS PhysicalPtr;

    VirtualPtr = (PVOID)(Pte->PageFrameNumber << PAGE_SHIFT);
    PhysicalPtr = MmGetPhysicalAddress(VirtualPtr);

    Pte->PageFrameNumber = (PFN_NUMBER)(PhysicalPtr.QuadPart >> PAGE_SHIFT);
}

BOOLEAN
NTAPI
Ki386CreateIdentityMap(
    PLARGE_IDENTITY_MAP IdentityMap,
    PVOID StartPtr,
    ULONG PagesCount)
{
    ULONG_PTR Ptr;
    ULONG PteIndex;
    PHYSICAL_ADDRESS IdentityPtr;

    RtlZeroMemory(IdentityMap, sizeof(LARGE_IDENTITY_MAP));

    IdentityPtr = Ki386BuildIdentityBuffer(IdentityMap, StartPtr, PagesCount);
    IdentityMap->StartAddress = IdentityPtr.LowPart;
    if (IdentityMap->StartAddress == 0)
    {
        DPRINT1("Failed to get buffer for large pages identity mapping\n");
        return FALSE;
    }
    DPRINT("IdentityMap->StartAddress %p\n", IdentityMap->StartAddress);

    for (Ptr = (ULONG_PTR)StartPtr;
         Ptr < (ULONG_PTR)StartPtr + PagesCount * PAGE_SIZE;
         Ptr += PAGE_SIZE, IdentityPtr.QuadPart += PAGE_SIZE)
    {
        if (!Ki386MapAddress(IdentityMap, Ptr, IdentityPtr)) return FALSE;

        if (!Ki386MapAddress(IdentityMap, IdentityPtr.LowPart, IdentityPtr)) return FALSE;
    }

    /* Convert all PTEs in the page directory from virtual to physical,
       because Ki386IdentityMapMakeValid mapped only virtual addresses */
    for (PteIndex = 0; PteIndex < (PAGE_SIZE / sizeof(HARDWARE_PTE)); PteIndex++)
    {
        if (IdentityMap->TopLevelDirectory[PteIndex].Valid != 0)
            Ki386ConvertPte(&IdentityMap->TopLevelDirectory[PteIndex]);
    }

    IdentityMap->Cr3 = MmGetPhysicalAddress(IdentityMap->TopLevelDirectory).LowPart;

    DPRINT("IdentityMap->Cr3 0x%x\n", IdentityMap->Cr3);

    return TRUE;
}

VOID
NTAPI
Ki386FreeIdentityMap(
    PLARGE_IDENTITY_MAP IdentityMap)
{
    ULONG Page;

    DPRINT("Freeing %lu pages allocated for identity mapping\n", IdentityMap->PagesCount);

    for (Page = 0; Page < IdentityMap->PagesCount; Page++)
        MmFreeContiguousMemory(IdentityMap->PagesList[Page]);
}
