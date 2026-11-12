/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Contiguous memory of every cache type and of multi-megabyte sizes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
CheckAllocation(
    _In_ SIZE_T Size,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PHYSICAL_ADDRESS Low, High, Boundary, First;
    PULONG Buffer;
    SIZE_T Offset;
    BOOLEAN Contiguous = TRUE, Readable = TRUE;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Buffer = MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Boundary, CacheType);
    ok(Buffer != NULL, "cache type %d: no %Iu contiguous bytes\n", CacheType, Size);
    if (!Buffer)
        return;

    First = MmGetPhysicalAddress(Buffer);
    for (Offset = 0; Offset < Size; Offset += PAGE_SIZE)
    {
        PHYSICAL_ADDRESS Physical = MmGetPhysicalAddress((PUCHAR)Buffer + Offset);

        if (Physical.QuadPart != First.QuadPart + (LONGLONG)Offset)
            Contiguous = FALSE;
        Buffer[Offset / sizeof(ULONG)] = (ULONG)(Offset ^ 0x5a5a5a5a);
    }
    for (Offset = 0; Offset < Size; Offset += PAGE_SIZE)
    {
        if (Buffer[Offset / sizeof(ULONG)] != (ULONG)(Offset ^ 0x5a5a5a5a))
            Readable = FALSE;
    }
    ok(Contiguous, "cache type %d: %Iu bytes at %I64x are not physically contiguous\n", CacheType, Size, First.QuadPart);
    ok(Readable, "cache type %d: %Iu bytes do not read back\n", CacheType, Size);
    MmFreeContiguousMemorySpecifyCache(Buffer, Size, CacheType);
}

START_TEST(MmContiguousCacheTypes)
{
    static const SIZE_T Sizes[] = { PAGE_SIZE, 16 * PAGE_SIZE, 64 * PAGE_SIZE, 122 * PAGE_SIZE, 256 * PAGE_SIZE,
                                    1024 * PAGE_SIZE };
    static const MEMORY_CACHING_TYPE Types[] = { MmNonCached, MmCached, MmWriteCombined };
    PVOID Held[8], Rings[32];
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG Size, Type, Index, Failed;

    for (Type = 0; Type < RTL_NUMBER_OF(Types); Type++)
    {
        for (Size = 0; Size < RTL_NUMBER_OF(Sizes); Size++)
            CheckAllocation(Sizes[Size], Types[Type]);
    }

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Held); Index++)
    {
        Held[Index] = MmAllocateContiguousMemorySpecifyCache(65 * 16384, Low, High, Boundary, MmCached);
        ok(Held[Index] != NULL, "cached table chunk %lu not allocated\n", Index);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Held); Index++)
    {
        if (Held[Index])
            MmFreeContiguousMemorySpecifyCache(Held[Index], 65 * 16384, MmCached);
    }

    {
        PVOID Mappings[4];

        for (Index = 0; Index < RTL_NUMBER_OF(Mappings); Index++)
        {
            Mappings[Index] = MmAllocateMappingAddress(16 * 1024 * 1024, 'cCmK');
            ok(Mappings[Index] != NULL, "16 MB mapping range %lu not reserved\n", Index);
        }
        CheckAllocation(122 * PAGE_SIZE, MmNonCached);
        CheckAllocation(256 * PAGE_SIZE, MmCached);
        for (Index = 0; Index < RTL_NUMBER_OF(Mappings); Index++)
        {
            if (Mappings[Index])
                MmFreeMappingAddress(Mappings[Index], 'cCmK');
        }
    }

    Failed = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Rings); Index++)
    {
        Rings[Index] = MmAllocateContiguousMemorySpecifyCache(122 * PAGE_SIZE, Low, High, Boundary, MmNonCached);
        if (!Rings[Index])
            Failed++;
    }
    ok_eq_ulong(Failed, 0UL);
    for (Index = 0; Index < RTL_NUMBER_OF(Rings); Index++)
    {
        if (Rings[Index])
            MmFreeContiguousMemorySpecifyCache(Rings[Index], 122 * PAGE_SIZE, MmNonCached);
    }
}
