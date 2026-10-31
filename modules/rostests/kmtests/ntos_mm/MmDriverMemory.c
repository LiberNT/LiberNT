/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Parameter rules of the driver memory routines (I/O space, non-cached, contiguous, MDL pages)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define TAG_MDM 'mDmK'

typedef PVOID (NTAPI *PMM_MAP_IO_SPACE_EX)(PHYSICAL_ADDRESS, SIZE_T, ULONG);
typedef PVOID (NTAPI *PMM_ALLOCATE_CONTIGUOUS_NODE_MEMORY)(SIZE_T, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS,
                                                          PHYSICAL_ADDRESS, ULONG, NODE_REQUIREMENT);

static
VOID
TestMapIoSpace(VOID)
{
    PMM_MAP_IO_SPACE_EX pMmMapIoSpaceEx;
    PHYSICAL_ADDRESS Physical;
    PUCHAR Buffer;
    PVOID Va;

    Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, PAGE_SIZE, TAG_MDM);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    RtlFillMemory(Buffer, PAGE_SIZE, 0x5A);
    Physical = MmGetPhysicalAddress(Buffer);

    ok(MmMapIoSpace(Physical, PAGE_SIZE, MmMaximumCacheType) == NULL, "Invalid cache type was mapped\n");
    Va = MmMapIoSpace(Physical, PAGE_SIZE, MmCached);
    ok(Va != NULL, "MmMapIoSpace(MmCached) failed\n");
    if (Va != NULL)
    {
        ok_eq_uint(((volatile UCHAR *)Va)[PAGE_SIZE - 1], 0x5A);
        MmUnmapIoSpace(Va, PAGE_SIZE);
    }

    pMmMapIoSpaceEx = (PMM_MAP_IO_SPACE_EX)KmtGetSystemRoutineAddress(L"MmMapIoSpaceEx");
    if (skip(pMmMapIoSpaceEx != NULL, "MmMapIoSpaceEx is not exported\n"))
    {
        ExFreePoolWithTag(Buffer, TAG_MDM);
        return;
    }

    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_READWRITE | PAGE_NOCACHE | PAGE_WRITECOMBINE) == NULL,
       "NOCACHE together with WRITECOMBINE was mapped\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_WRITECOPY) == NULL, "PAGE_WRITECOPY was mapped\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_EXECUTE_WRITECOPY) == NULL, "PAGE_EXECUTE_WRITECOPY was mapped\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_READWRITE | PAGE_GUARD) == NULL, "PAGE_GUARD was mapped\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_NOACCESS) == NULL, "PAGE_NOACCESS was mapped\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_EXECUTE_READWRITE | PAGE_WRITECOMBINE) == NULL,
       "Executable write-combined mapping was created\n");
    ok(pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_READWRITE | 0x800) == NULL, "Unknown protection bit was mapped\n");

    Va = pMmMapIoSpaceEx(Physical, PAGE_SIZE, PAGE_READONLY);
    ok(Va != NULL, "MmMapIoSpaceEx(PAGE_READONLY) failed\n");
    if (Va != NULL)
    {
        ok_eq_uint(((volatile UCHAR *)Va)[0], 0x5A);
        MmUnmapIoSpace(Va, PAGE_SIZE);
    }

    ExFreePoolWithTag(Buffer, TAG_MDM);
}

static
VOID
TestNonCachedMemory(VOID)
{
    SIZE_T Size = 16 * PAGE_SIZE;
    PUCHAR Memory;
    SIZE_T i;
    BOOLEAN Zero = TRUE;

    if (sizeof(SIZE_T) > sizeof(ULONG))
        ok(MmAllocateNonCachedMemory((SIZE_T)0xFFFFFFFF + 1) == NULL, "4 GB non-cached request succeeded\n");

    Memory = MmAllocateNonCachedMemory(Size);
    ok(Memory != NULL, "MmAllocateNonCachedMemory failed\n");
    if (skip(Memory != NULL, "No memory\n"))
        return;

    ok(((ULONG_PTR)Memory & (PAGE_SIZE - 1)) == 0, "Allocation %p is not page aligned\n", Memory);
    for (i = 0; i < Size; i++)
    {
        if (Memory[i] != 0)
        {
            Zero = FALSE;
            break;
        }
    }
    ok(Zero, "Non-cached memory byte %Iu is 0x%x\n", i, Zero ? 0 : Memory[i]);
    Memory[Size - 1] = 0xC3;
    ok_eq_uint(Memory[Size - 1], 0xC3);
    MmFreeNonCachedMemory(Memory, Size);
}

static
VOID
TestContiguousNodeMemory(VOID)
{
    PMM_ALLOCATE_CONTIGUOUS_NODE_MEMORY pMmAllocateContiguousNodeMemory;
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Memory;

    pMmAllocateContiguousNodeMemory =
        (PMM_ALLOCATE_CONTIGUOUS_NODE_MEMORY)KmtGetSystemRoutineAddress(L"MmAllocateContiguousNodeMemory");
    if (skip(pMmAllocateContiguousNodeMemory != NULL, "MmAllocateContiguousNodeMemory is not exported\n"))
        return;

    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)-1;
    Boundary.QuadPart = PAGE_SIZE / 2;
    ok(pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_READWRITE, MM_ANY_NODE_OK) == NULL,
       "Boundary below page alignment was accepted\n");

    Boundary.QuadPart = 0;
    ok(pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_READONLY, MM_ANY_NODE_OK) == NULL,
       "PAGE_READONLY was accepted\n");
    ok(pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_EXECUTE_READWRITE | PAGE_NOCACHE,
                                       MM_ANY_NODE_OK) == NULL,
       "Executable non-cached memory was accepted\n");
    ok(pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_READWRITE | PAGE_GUARD,
                                       MM_ANY_NODE_OK) == NULL,
       "PAGE_GUARD was accepted\n");
    ok(pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_READWRITE,
                                       (NODE_REQUIREMENT)KeQueryHighestNodeNumber() + 1) == NULL,
       "A node past the highest node was accepted\n");

    Memory = pMmAllocateContiguousNodeMemory(PAGE_SIZE, Low, High, Boundary, PAGE_READWRITE | PAGE_WRITECOMBINE,
                                             MM_ANY_NODE_OK);
    ok(Memory != NULL, "Write-combined contiguous allocation failed\n");
    if (Memory != NULL)
        MmFreeContiguousMemory(Memory);
}

static
VOID
TestAllocatePagesForMdlEx(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PPFN_NUMBER Pfns;
    ULONG Chunk;
    ULONG Page;
    BOOLEAN Contiguous = TRUE;
    PMDL Mdl;

    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)-1;
    Skip.QuadPart = 0;

    ok(MmAllocatePagesForMdlEx(Low, High, Skip, PAGE_SIZE, MmCached, 0x400) == NULL, "Flag 0x400 was accepted\n");
    ok(MmAllocatePagesForMdlEx(Low, High, Skip, PAGE_SIZE, MmCached, 0x800) == NULL, "Flag 0x800 was accepted\n");
    ok(MmAllocatePagesForMdlEx(Low, High, Skip, PAGE_SIZE, MmCached,
                               MM_ALLOCATE_AND_HOT_REMOVE | MM_ALLOCATE_FULLY_REQUIRED) == NULL,
       "Fully required hot-remove allocation was accepted\n");

    Skip.QuadPart = PAGE_SIZE / 2;
    ok(MmAllocatePagesForMdlEx(Low, High, Skip, PAGE_SIZE, MmCached, 0) == NULL,
       "SkipBytes below page alignment was accepted\n");

    Skip.QuadPart = 16 * PAGE_SIZE;
    ok(MmAllocatePagesForMdlEx(Low, High, Skip, 24 * PAGE_SIZE, MmCached,
                               MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS) == NULL,
       "A size that is not a chunk multiple was accepted\n");

    Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, 64 * PAGE_SIZE, MmCached,
                                  MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    ok(Mdl != NULL, "Four 16-page chunks were not allocated\n");
    if (skip(Mdl != NULL, "No MDL\n"))
        return;

    ok_eq_ulong(MmGetMdlByteCount(Mdl), 64UL * PAGE_SIZE);
    Pfns = MmGetMdlPfnArray(Mdl);
    for (Chunk = 0; Chunk < 4; Chunk++)
    {
        for (Page = 1; Page < 16; Page++)
        {
            if (Pfns[Chunk * 16 + Page] != Pfns[Chunk * 16] + Page)
                Contiguous = FALSE;
        }
    }
    ok(Contiguous, "A 16-page chunk is not physically contiguous\n");

    ok(MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmMaximumCacheType, NULL, FALSE, NormalPagePriority) == NULL,
       "Invalid cache type was mapped\n");

    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

static
VOID
TestAllocatePool2(VOID)
{
    PVOID Block;

    ok(ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED1, 64, TAG_MDM) == NULL,
       "POOL_FLAG_RESERVED1 was accepted\n");
    ok(ExAllocatePool2(POOL_FLAG_NON_PAGED | 0x1000, 64, TAG_MDM) == NULL, "Required flag 0x1000 was accepted\n");

    Block = ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED3, 64, TAG_MDM);
    ok(Block != NULL, "POOL_FLAG_RESERVED3 was refused\n");
    if (Block != NULL)
        ExFreePoolWithTag(Block, TAG_MDM);
}

START_TEST(MmDriverMemory)
{
    TestMapIoSpace();
    TestNonCachedMemory();
    TestContiguousNodeMemory();
    TestAllocatePagesForMdlEx();
    TestAllocatePool2();
}
