/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test when freed heap blocks are decommitted
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static VOID
GetHeapState(PVOID Heap, PSIZE_T Uncommitted, PSIZE_T Free)
{
    PROCESS_HEAP_ENTRY Entry;

    *Uncommitted = 0;
    *Free = 0;
    Entry.lpData = NULL;
    while (HeapWalk(Heap, &Entry))
    {
        if (Entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)
            *Uncommitted += Entry.cbData;
        else if (!(Entry.wFlags & (PROCESS_HEAP_ENTRY_BUSY | PROCESS_HEAP_REGION)))
            *Free += Entry.cbData;
    }
}

static VOID
TestFirstDecommit(SIZE_T ReserveSize, SIZE_T BlockSize, BOOLEAN UseCommitted)
{
    const SIZE_T MinimumTotalFree = 0x10000 * 2 * sizeof(PVOID);
    PVOID Blocks[512];
    SIZE_T Uncommitted, Free, PreviousUncommitted, PreviousFree, Threshold;
    ULONG Count, Index;
    BOOLEAN Decommitted = FALSE;
    PVOID Heap;

    Heap = RtlCreateHeap(0, NULL, ReserveSize, 0, NULL, NULL);
    ok(Heap != NULL, "Could not create a %lu MB heap\n", (ULONG)(ReserveSize >> 20));
    if (!Heap)
        return;

    for (Count = 0; Count < RTL_NUMBER_OF(Blocks); ++Count)
    {
        Blocks[Count] = RtlAllocateHeap(Heap, 0, BlockSize);
        if (!Blocks[Count])
            break;
    }
    ok(Count > 16, "Only %lu blocks of %lu KB\n", Count, (ULONG)(BlockSize >> 10));

    GetHeapState(Heap, &PreviousUncommitted, &PreviousFree);
    for (Index = 0; Index < Count && !Decommitted; ++Index)
    {
        Threshold = MinimumTotalFree;
        if (UseCommitted)
            Threshold = (ReserveSize - PreviousUncommitted) / 16;

        RtlFreeHeap(Heap, 0, Blocks[Index]);
        GetHeapState(Heap, &Uncommitted, &Free);
        Decommitted = Uncommitted > PreviousUncommitted;
        ok(Decommitted == (PreviousFree + BlockSize >= Threshold),
           "%lu MB heap, %lu KB blocks, free %lu: %lu KB free against a %lu KB threshold, decommitted %lu KB\n",
           (ULONG)(ReserveSize >> 20), (ULONG)(BlockSize >> 10), Index, (ULONG)((PreviousFree + BlockSize) >> 10),
           (ULONG)(Threshold >> 10), (ULONG)((Uncommitted - PreviousUncommitted) >> 10));
        PreviousUncommitted = Uncommitted;
        PreviousFree = Free;
    }
    ok(Decommitted, "%lu MB heap, %lu KB blocks: nothing was decommitted\n",
       (ULONG)(ReserveSize >> 20), (ULONG)(BlockSize >> 10));

    for (; Index < Count; ++Index)
        RtlFreeHeap(Heap, 0, Blocks[Index]);
    ok(RtlDestroyHeap(Heap) == NULL, "Failed to destroy heap\n");
}

START_TEST(RtlHeapDecommit)
{
    TestFirstDecommit(4 * 1024 * 1024, 32 * 1024, FALSE);
    TestFirstDecommit(4 * 1024 * 1024, 64 * 1024, FALSE);
    TestFirstDecommit(64 * 1024 * 1024, 256 * 1024, TRUE);
}
