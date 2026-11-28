/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test in-place heap growth and low fragmentation heap activation against Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
BOOL
GrowsInPlace(
    _In_ HANDLE Heap,
    _In_ SIZE_T Size,
    _In_ SIZE_T Growth)
{
    PVOID Block = HeapAlloc(Heap, 0, Size), Grown;

    ok(Block != NULL, "HeapAlloc(%Iu) failed\n", Size);
    if (!Block)
        return FALSE;
    Grown = HeapReAlloc(Heap, HEAP_REALLOC_IN_PLACE_ONLY, Block, Size + Growth);
    ok(Grown == NULL || Grown == Block, "in-place growth moved the block\n");
    HeapFree(Heap, 0, Grown ? Grown : Block);
    return Grown != NULL;
}

static
VOID
TestFreshHeapGrowth(VOID)
{
    static const struct { SIZE_T Growth; BOOL InPlace; } Cases[] =
    {
        { 0x10, TRUE }, { 0x100, TRUE }, { 0x800, TRUE }, { 0x1000, TRUE },
        { 0x2000, TRUE }, { 0x8000, TRUE }, { 0x20000, FALSE },
    };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        HANDLE Heap = HeapCreate(0, 0, 0);

        ok(Heap != NULL, "HeapCreate failed: %lu\n", GetLastError());
        if (!Heap)
            continue;
        ok(GrowsInPlace(Heap, 3000, Cases[Index].Growth) == Cases[Index].InPlace,
           "growing a fresh heap's first block by %#Ix in place: expected %d\n", Cases[Index].Growth, Cases[Index].InPlace);
        HeapDestroy(Heap);
    }
}

static
VOID
TestLowFragmentationActivation(VOID)
{
    static const SIZE_T Sizes[] = { 3000, 4000, 7000 };
    ULONG Index, Cycle, First;

    for (Index = 0; Index < ARRAYSIZE(Sizes); Index++)
    {
        HANDLE Heap = HeapCreate(0, 0, 0);

        ok(Heap != NULL, "HeapCreate failed: %lu\n", GetLastError());
        if (!Heap)
            continue;
        for (First = 0, Cycle = 0; Cycle < 64; Cycle++)
        {
            if (!GrowsInPlace(Heap, Sizes[Index], 0x8000))
            {
                First = Cycle;
                break;
            }
        }
        ok(First == 16, "size %Iu: the first block that cannot grow in place is number %lu, expected 16\n", Sizes[Index], First);
        HeapDestroy(Heap);
    }
}

START_TEST(HeapGrowInPlace)
{
    TestFreshHeapGrowth();
    TestLowFragmentationActivation();
}
