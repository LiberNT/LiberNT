/*
 * PROJECT:     LiberNT Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/exinit.c
 * PURPOSE:     Executive pool initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "expool.h"

#define NDEBUG
#if !defined(POOL_HOST_TEST)
#include <debug.h>
#endif

EX_POOL_STATE ExpPoolState;
ULONG ExpPoolFlags;

static POOL_TAG_TABLE ExpBootTagTable;

static
ULONG64
ExpPoolEncodeKey(VOID)
{
    ULONG64 Key = 0x9E3779B97F4A7C15ULL;

    Key ^= (ULONG64)KeQueryInterruptTime();
    Key ^= (ULONG64)(ULONG_PTR)&Key << 17;
    Key ^= (ULONG64)(ULONG_PTR)&ExpPoolState >> 3;
    return Key;
}

static
ULONG
ExpPoolHeapOptions(VOID)
{
#if DBG
    return POOL_HEAP_POISON_ON_FREE;
#else
    return 0;
#endif
}

static
SIZE_T
ExpPoolUnitsFor(
    _In_ SIZE_T NumberOfBytes)
{
    if (NumberOfBytes <= POOL_RANGE_MAX_BYTES)
        return 1;
    if (NumberOfBytes > (SIZE_T)-1 - POOL_UNIT_MASK)
        return 0;
    return (NumberOfBytes + POOL_UNIT_MASK) >> POOL_UNIT_SHIFT;
}

static
BOOLEAN
ExpPoolHeapHasRoom(
    _In_ PPOOL_HEAP Heap,
    _In_ SIZE_T Units)
{
    ULONG Count = PoolHeapRegionCount(Heap);
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (PoolVaHasRun(Heap->Region[i], Units))
            return TRUE;
    }

    return FALSE;
}

static
NTSTATUS
ExpPoolExtend(
    _In_ ULONG HeapIndex,
    _In_ SIZE_T Units)
{
    ULONG Slot = ExpPoolState.RegionsUsed[HeapIndex];
    SIZE_T Gap = ExpPoolState.SharedHigh - ExpPoolState.SharedLow;
    SIZE_T Bytes = ExpPoolState.GrownBytes[HeapIndex];
    SIZE_T Share = (Gap / 2) & ~(SIZE_T)POOL_UNIT_MASK;
    PPOOL_VA_REGION Region;
    POOL_BACKING Backing;
    ULONG_PTR Base;
    NTSTATUS Status;

    POOL_ASSERT(HeapIndex == EX_POOL_HEAP_NONPAGED || HeapIndex == EX_POOL_HEAP_EXECUTABLE);
    POOL_ASSERT(ExpPoolState.SharedLow <= ExpPoolState.SharedHigh);
    POOL_ASSERT(((ExpPoolState.SharedLow | ExpPoolState.SharedHigh) & POOL_UNIT_MASK) == 0);
    POOL_ASSERT((Bytes & POOL_UNIT_MASK) == 0);
    POOL_ASSERT(ExpPoolState.Heap[HeapIndex] == NULL ||
                Slot == PoolHeapRegionCount(ExpPoolState.Heap[HeapIndex]));

    if (Slot >= POOL_MAX_REGIONS || Units == 0 || Units >= (Gap >> POOL_UNIT_SHIFT))
        return STATUS_NO_MEMORY;

    if (Share < EX_POOL_GROW_MINIMUM)
        Share = EX_POOL_GROW_MINIMUM;
    if (Bytes < EX_POOL_GROW_MINIMUM)
        Bytes = EX_POOL_GROW_MINIMUM;
    if (Bytes > Share)
        Bytes = Share;
    if (Bytes < ((Units + 1) << POOL_UNIT_SHIFT))
        Bytes = (Units + 1) << POOL_UNIT_SHIFT;
    if (Bytes > Gap)
        Bytes = Gap;
    POOL_ASSERT((Bytes & POOL_UNIT_MASK) == 0 && Bytes > (Units << POOL_UNIT_SHIFT));

    Base = (HeapIndex == EX_POOL_HEAP_NONPAGED) ? ExpPoolState.SharedLow : ExpPoolState.SharedHigh - Bytes;
    Region = &ExpPoolState.Region[HeapIndex][Slot];
    PoolBackDemandBacking(&Backing, (BOOLEAN)(HeapIndex == EX_POOL_HEAP_EXECUTABLE));
    Status = PoolVaRegionInitialize(Region, (PVOID)Base, Bytes, FALSE, &Backing);
    if (!NT_SUCCESS(Status))
        return Status;
    POOL_ASSERT(Region->Base == Base && Region->End == Base + Bytes);

    if (ExpPoolState.Heap[HeapIndex] != NULL)
    {
        Status = PoolHeapAddRegion(ExpPoolState.Heap[HeapIndex], Region);
        if (!NT_SUCCESS(Status))
        {
            PoolVaDecommit(Region, (PVOID)Region->Base, Region->MetadataCommitted - Region->Base);
            return Status;
        }
    }

    if (HeapIndex == EX_POOL_HEAP_NONPAGED)
        ExpPoolState.SharedLow += Bytes;
    else
        ExpPoolState.SharedHigh -= Bytes;
    ExpPoolState.RegionsUsed[HeapIndex] = Slot + 1;
    ExpPoolState.GrownBytes[HeapIndex] += Bytes;
    POOL_ATOMIC_ADD32(&ExpPoolState.Generation[HeapIndex], 1);
    POOL_ASSERT(ExpPoolState.SharedLow <= ExpPoolState.SharedHigh);
    return STATUS_SUCCESS;
}

EX_POOL_GROWTH
ExpPoolGrow(
    _In_ ULONG HeapIndex,
    _In_ SIZE_T NumberOfBytes,
    _In_ LONG Generation)
{
    SIZE_T Units = ExpPoolUnitsFor(NumberOfBytes);
    EX_POOL_GROWTH Growth;
    KIRQL OldIrql;

    if (HeapIndex == EX_POOL_HEAP_PAGED || Units == 0)
        return ExpPoolGrowthFailed;

    POOL_ASSERT(HeapIndex == EX_POOL_HEAP_NONPAGED || HeapIndex == EX_POOL_HEAP_EXECUTABLE);
    POOL_ASSERT(ExpPoolState.Heap[HeapIndex] != NULL);

    POOL_LOCK_ACQUIRE(&ExpPoolState.GrowLock, &OldIrql);
    if ((LONG)POOL_LOAD_ACQUIRE32(&ExpPoolState.Generation[HeapIndex]) != Generation)
        Growth = ExpPoolGrowthExtended;
    else if (ExpPoolHeapHasRoom(ExpPoolState.Heap[HeapIndex], Units))
        Growth = ExpPoolGrowthRoom;
    else if (NT_SUCCESS(ExpPoolExtend(HeapIndex, Units)))
        Growth = ExpPoolGrowthExtended;
    else
        Growth = ExpPoolGrowthFailed;
    POOL_LOCK_RELEASE(&ExpPoolState.GrowLock, OldIrql);
    return Growth;
}

CODE_SEG("INIT")
VOID
NTAPI
InitializePool(
    IN POOL_TYPE PoolType,
    IN ULONG Threshold)
{
    POOL_KM_LAYOUT Layout;
    POOL_BACKING Backing;
    PPOOL_VA_REGION First;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Threshold);
    PoolBackQueryLayout(&Layout);

    if (PoolType == NonPagedPool)
    {
        First = &ExpPoolState.Region[EX_POOL_HEAP_NONPAGED][0];

        PoolTagTrackerInitialize(&ExpPoolState.Tracker);
        PoolTagTrackerAddTable(&ExpPoolState.Tracker, &ExpBootTagTable);
        POOL_LOCK_INIT(&ExpPoolState.GrowLock, FALSE);
        ExpPoolState.SharedLow = ((ULONG_PTR)Layout.NonPagedSharedBase + POOL_UNIT_MASK) & ~(ULONG_PTR)POOL_UNIT_MASK;
        ExpPoolState.SharedHigh = ((ULONG_PTR)Layout.NonPagedSharedBase + Layout.NonPagedSharedBytes) &
                                  ~(ULONG_PTR)POOL_UNIT_MASK;
        if (ExpPoolState.SharedHigh < ExpPoolState.SharedLow)
            ExpPoolState.SharedHigh = ExpPoolState.SharedLow;

        PoolBackResidentBacking(&Backing);
        Status = PoolVaRegionInitialize(First, Layout.NonPagedResidentBase, Layout.NonPagedResidentBytes,
                                        FALSE, &Backing);
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 0, (ULONG_PTR)Status, 0, 0);

        Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], First, EX_POOL_HEAP_NONPAGED,
                                EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 1, (ULONG_PTR)Status, 0, 0);

        ExpPoolState.RegionsUsed[EX_POOL_HEAP_NONPAGED] = 1;
        ExpPoolExtend(EX_POOL_HEAP_NONPAGED, 1);

        Status = ExpPoolExtend(EX_POOL_HEAP_EXECUTABLE, 1);
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 4, (ULONG_PTR)Status, 0, 0);

        First = &ExpPoolState.Region[EX_POOL_HEAP_EXECUTABLE][0];
        Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE], First, EX_POOL_HEAP_EXECUTABLE,
                                EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 5, (ULONG_PTR)Status, 0, 0);
        return;
    }

    POOL_LOCK_ACQUIRE(&ExpPoolState.GrowLock, &OldIrql);
    if (ExpPoolState.GrownBytes[EX_POOL_HEAP_NONPAGED] == 0)
        ExpPoolExtend(EX_POOL_HEAP_NONPAGED, 1);
    POOL_LOCK_RELEASE(&ExpPoolState.GrowLock, OldIrql);

    First = &ExpPoolState.Region[EX_POOL_HEAP_PAGED][0];
    PoolBackDemandBacking(&Backing, FALSE);

    Status = PoolVaRegionInitialize(First, Layout.PagedBase, Layout.PagedBytes, TRUE, &Backing);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 2, (ULONG_PTR)Status, 0, 0);

    Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_PAGED], First, EX_POOL_HEAP_PAGED,
                            EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 3, (ULONG_PTR)Status, 0, 0);
}

VOID
NTAPI
ExInitializePoolDescriptor(
    IN PVOID PoolDescriptor,
    IN POOL_TYPE PoolType,
    IN ULONG PoolIndex,
    IN ULONG Threshold,
    IN PVOID PoolLock)
{
    UNREFERENCED_PARAMETER(PoolDescriptor);
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(PoolIndex);
    UNREFERENCED_PARAMETER(Threshold);
    UNREFERENCED_PARAMETER(PoolLock);
}
