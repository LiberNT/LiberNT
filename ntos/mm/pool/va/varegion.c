/*
 * PROJECT:     LiberNT Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/va/varegion.c
 * PURPOSE:     Pool virtual address region management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolva.h"

#define POOL_PAGE_ROUND_UP(x)   (((ULONG_PTR)(x) + PAGE_SIZE - 1) & ~((ULONG_PTR)PAGE_SIZE - 1))
#define POOL_CACHE_ROUND_UP(x)  (((ULONG_PTR)(x) + POOL_CACHE_ALIGNMENT - 1) & ~((ULONG_PTR)POOL_CACHE_ALIGNMENT - 1))

FORCEINLINE
BOOLEAN
PoolVaTestBit(PULONG64 Map, SIZE_T Bit)
{
    return (BOOLEAN)((Map[Bit >> 6] >> (Bit & 63)) & 1);
}

FORCEINLINE
VOID
PoolVaSetBits(PULONG64 Map, SIZE_T First, SIZE_T Count)
{
    SIZE_T i;

    for (i = First; i < First + Count; i++)
        Map[i >> 6] |= ((ULONG64)1 << (i & 63));
}

FORCEINLINE
VOID
PoolVaClearBits(PULONG64 Map, SIZE_T First, SIZE_T Count)
{
    SIZE_T i;

    for (i = First; i < First + Count; i++)
        Map[i >> 6] &= ~((ULONG64)1 << (i & 63));
}

#if DBG || defined(POOL_HOST_TEST)
static
VOID
PoolVaAssertMetadata(
    _In_ PPOOL_VA_REGION Region)
{
    SIZE_T Last;

    POOL_ASSERT(Region->MetadataNext >= Region->Base && Region->MetadataNext <= Region->MetadataCommitted);
    POOL_ASSERT(Region->MetadataCommitted <= Region->MetadataEnd && Region->MetadataEnd <= Region->End);
    POOL_ASSERT((Region->MetadataNext & (POOL_CACHE_ALIGNMENT - 1)) == 0);
    POOL_ASSERT((Region->MetadataCommitted & (PAGE_SIZE - 1)) == 0);
    POOL_ASSERT((Region->MetadataEnd & POOL_UNIT_MASK) == 0 && Region->MetadataEnd > Region->Base);
    Last = (Region->MetadataEnd - 1 - Region->Base) >> POOL_UNIT_SHIFT;
    POOL_ASSERT(Last < Region->UnitCount && PoolVaTestBit(Region->BusyMap, Last));
    POOL_ASSERT(Region->Directory[Last].State == PoolUnitMetadata || Region->Directory[Last].State == PoolUnitTail);
}
#else
#define PoolVaAssertMetadata(Region) ((void)0)
#endif

NTSTATUS
PoolVaCommit(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    NTSTATUS Status;
    LONG64 Peak;
    LONG64 Now;

    POOL_ASSERT(Region->Backing.Commit != NULL);
    POOL_ASSERT(((ULONG_PTR)Base & (PAGE_SIZE - 1)) == 0 && Bytes != 0 && (Bytes & (PAGE_SIZE - 1)) == 0);
    POOL_ASSERT((ULONG_PTR)Base >= Region->Base && (ULONG_PTR)Base <= Region->End &&
                Bytes <= Region->End - (ULONG_PTR)Base);
    Status = Region->Backing.Commit(Region->Backing.Context, Base, Bytes);
    if (!NT_SUCCESS(Status))
        return Status;

    Now = POOL_ATOMIC_ADD64(&Region->CommittedPages, (LONG64)(Bytes >> PAGE_SHIFT)) +
          (LONG64)(Bytes >> PAGE_SHIFT);
    POOL_ASSERT(Now >= (LONG64)(Bytes >> PAGE_SHIFT));
    POOL_ASSERT((ULONG64)Now <= ((Region->End - Region->Base) >> PAGE_SHIFT));

    for (;;)
    {
        Peak = POOL_ATOMIC_READ64(&Region->PeakCommittedPages);
        POOL_ASSERT(Peak >= 0);
        if (Now <= Peak || POOL_ATOMIC_CAS64(&Region->PeakCommittedPages, Now, Peak) == Peak)
            break;
    }

    return STATUS_SUCCESS;
}

VOID
PoolVaDecommit(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    LONG64 Previous;

    POOL_ASSERT(Region->Backing.Decommit != NULL);
    POOL_ASSERT(((ULONG_PTR)Base & (PAGE_SIZE - 1)) == 0 && Bytes != 0 && (Bytes & (PAGE_SIZE - 1)) == 0);
    POOL_ASSERT((ULONG_PTR)Base >= Region->Base && (ULONG_PTR)Base <= Region->End &&
                Bytes <= Region->End - (ULONG_PTR)Base);
    Region->Backing.Decommit(Region->Backing.Context, Base, Bytes);
    Previous = POOL_ATOMIC_ADD64(&Region->CommittedPages, -(LONG64)(Bytes >> PAGE_SHIFT));
    POOL_ASSERT(Previous >= (LONG64)(Bytes >> PAGE_SHIFT));
    UNREFERENCED_PARAMETER(Previous);
}

NTSTATUS
PoolVaRegionInitialize(
    _Out_ PPOOL_VA_REGION Region,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes,
    _In_ BOOLEAN Paged,
    _In_ PPOOL_BACKING Backing)
{
    ULONG_PTR AlignedBase;
    ULONG_PTR AlignedEnd;
    SIZE_T BootstrapBytes;
    SIZE_T BootstrapUnits;
    SIZE_T DirectoryBytes;
    SIZE_T MapBytes;
    SIZE_T Tail;
    NTSTATUS Status;

    RtlZeroMemory(Region, sizeof(*Region));

    AlignedBase = ((ULONG_PTR)Base + POOL_UNIT_MASK) & ~(ULONG_PTR)POOL_UNIT_MASK;
    AlignedEnd = ((ULONG_PTR)Base + Bytes) & ~(ULONG_PTR)POOL_UNIT_MASK;
    if (AlignedEnd <= AlignedBase)
        return STATUS_INVALID_PARAMETER;

    Region->Base = AlignedBase;
    Region->End = AlignedEnd;
    Region->UnitCount = (AlignedEnd - AlignedBase) >> POOL_UNIT_SHIFT;
    Region->MapWords = (Region->UnitCount + 63) / 64;
    Region->Backing = *Backing;
    Region->Paged = Paged;
    POOL_LOCK_INIT(&Region->Lock, Paged);

    DirectoryBytes = POOL_CACHE_ROUND_UP(Region->UnitCount * sizeof(POOL_UNIT));
    MapBytes = POOL_CACHE_ROUND_UP(Region->MapWords * sizeof(ULONG64));
    BootstrapBytes = POOL_PAGE_ROUND_UP(DirectoryBytes + MapBytes);
    BootstrapUnits = (BootstrapBytes + POOL_UNIT_MASK) >> POOL_UNIT_SHIFT;
    if (BootstrapUnits >= Region->UnitCount)
        return STATUS_INVALID_PARAMETER;

    Status = PoolVaCommit(Region, (PVOID)AlignedBase, BootstrapBytes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory((PVOID)AlignedBase, BootstrapBytes);
    Region->Directory = (PPOOL_UNIT)AlignedBase;
    Region->BusyMap = (PULONG64)(AlignedBase + DirectoryBytes);

    PoolVaSetBits(Region->BusyMap, 0, BootstrapUnits);
    for (Tail = Region->UnitCount; Tail < Region->MapWords * 64; Tail++)
        Region->BusyMap[Tail >> 6] |= ((ULONG64)1 << (Tail & 63));

    for (Tail = 0; Tail < BootstrapUnits; Tail++)
    {
        Region->Directory[Tail].State = (Tail == 0) ? PoolUnitMetadata : PoolUnitTail;
        Region->Directory[Tail].UnitCount = (Tail == 0) ? (ULONG)BootstrapUnits : 0;
    }

    Region->UnitsInUse = BootstrapUnits;
    Region->Hint = BootstrapUnits;
    Region->MetadataNext = AlignedBase + BootstrapBytes;
    Region->MetadataCommitted = AlignedBase + BootstrapBytes;
    Region->MetadataEnd = AlignedBase + (BootstrapUnits << POOL_UNIT_SHIFT);
    PoolVaAssertMetadata(Region);
    return STATUS_SUCCESS;
}

static
SIZE_T
PoolVaFindRun(
    _In_ PPOOL_VA_REGION Region,
    _In_ SIZE_T Start,
    _In_ SIZE_T Limit,
    _In_ SIZE_T Count)
{
    SIZE_T Bit = Start;
    SIZE_T Run = 0;

    while (Bit < Limit)
    {
        if ((Bit & 63) == 0 && Run == 0 && Region->BusyMap[Bit >> 6] == ~(ULONG64)0)
        {
            Bit += 64;
            continue;
        }

        if (PoolVaTestBit(Region->BusyMap, Bit))
        {
            Run = 0;
        }
        else
        {
            Run++;
            if (Run == Count)
                return Bit + 1 - Count;
        }

        Bit++;
    }

    return (SIZE_T)-1;
}

static
PVOID
PoolVaReserveLocked(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ SIZE_T UnitCount,
    _In_ UCHAR State)
{
    SIZE_T First;
    SIZE_T i;

    POOL_ASSERT(Region->UnitsInUse <= Region->UnitCount);
    POOL_ASSERT(Region->Hint < Region->UnitCount);
    POOL_ASSERT(State == PoolUnitMetadata || State == PoolUnitSegment || State == PoolUnitLarge);
    if (UnitCount == 0 || UnitCount > Region->UnitCount - Region->UnitsInUse)
        return NULL;

    First = PoolVaFindRun(Region, Region->Hint, Region->UnitCount, UnitCount);
    if (First == (SIZE_T)-1)
        First = PoolVaFindRun(Region, 0, Region->UnitCount, UnitCount);
    if (First == (SIZE_T)-1)
        return NULL;

    POOL_ASSERT(First < Region->UnitCount && UnitCount <= Region->UnitCount - First);
#if DBG || defined(POOL_HOST_TEST)
    for (i = 0; i < UnitCount; i++)
    {
        POOL_ASSERT(!PoolVaTestBit(Region->BusyMap, First + i));
        POOL_ASSERT(Region->Directory[First + i].State == PoolUnitFree);
    }
#endif
    PoolVaSetBits(Region->BusyMap, First, UnitCount);
    Region->UnitsInUse += UnitCount;
    Region->Hint = (First + UnitCount < Region->UnitCount) ? First + UnitCount : 0;

    for (i = 1; i < UnitCount; i++)
    {
        POOL_ASSERT(Region->Directory[First + i].State == PoolUnitFree);
        Region->Directory[First + i].UnitCount = 0;
        Region->Directory[First + i].State = PoolUnitTail;
    }

    Region->Directory[First].Flags = 0;
    Region->Directory[First].Tag = 0;
    Region->Directory[First].PageCount = 0;
    Region->Directory[First].UnitCount = (ULONG)UnitCount;
    Region->Directory[First].State = State;

    return (PVOID)(Region->Base + (First << POOL_UNIT_SHIFT));
}

PVOID
PoolVaReserve(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ SIZE_T UnitCount,
    _In_ UCHAR State)
{
    PVOID Base;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Region->Lock, &OldIrql);
    Base = PoolVaReserveLocked(Region, UnitCount, State);
    POOL_LOCK_RELEASE(&Region->Lock, OldIrql);
    return Base;
}

VOID
PoolVaRelease(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ PVOID Base,
    _In_ SIZE_T UnitCount)
{
    SIZE_T First = ((ULONG_PTR)Base - Region->Base) >> POOL_UNIT_SHIFT;
    KIRQL OldIrql;
    SIZE_T i;

    POOL_ASSERT(PoolVaContains(Region, Base));
    POOL_ASSERT(((ULONG_PTR)Base & POOL_UNIT_MASK) == 0);
    POOL_ASSERT(First < Region->UnitCount && UnitCount <= Region->UnitCount - First);

    POOL_LOCK_ACQUIRE(&Region->Lock, &OldIrql);
    POOL_ASSERT(UnitCount != 0 && UnitCount <= Region->UnitsInUse);
    POOL_ASSERT(Region->UnitsInUse <= Region->UnitCount);
    POOL_ASSERT(Region->Directory[First].State == PoolUnitMetadata ||
                Region->Directory[First].State == PoolUnitSegment ||
                Region->Directory[First].State == PoolUnitLarge);
    POOL_ASSERT(Region->Directory[First].UnitCount == UnitCount);

    for (i = 0; i < UnitCount; i++)
    {
        POOL_ASSERT(PoolVaTestBit(Region->BusyMap, First + i));
        POOL_ASSERT(i == 0 || Region->Directory[First + i].State == PoolUnitTail);
        Region->Directory[First + i].State = PoolUnitFree;
        Region->Directory[First + i].UnitCount = 0;
    }

    PoolVaClearBits(Region->BusyMap, First, UnitCount);
    Region->UnitsInUse -= UnitCount;
    if (First < Region->Hint)
        Region->Hint = First;

    POOL_LOCK_RELEASE(&Region->Lock, OldIrql);
}

PVOID
PoolVaAllocateMetadata(
    _Inout_ PPOOL_VA_REGION Region,
    _In_ SIZE_T Bytes)
{
    ULONG_PTR Block;
    ULONG_PTR NewNext;
    ULONG_PTR CommitEnd;
    KIRQL OldIrql;

    if (Bytes == 0 || Bytes > (SIZE_T)-1 - (POOL_CACHE_ALIGNMENT - 1))
        return NULL;
    Bytes = POOL_CACHE_ROUND_UP(Bytes);

    POOL_LOCK_ACQUIRE(&Region->Lock, &OldIrql);
    PoolVaAssertMetadata(Region);

    if (Bytes > Region->MetadataEnd - Region->MetadataNext)
    {
        SIZE_T Units = (Bytes >> POOL_UNIT_SHIFT) + ((Bytes & POOL_UNIT_MASK) != 0);
        PVOID Run = PoolVaReserveLocked(Region, Units, PoolUnitMetadata);

        if (Run == NULL)
        {
            POOL_LOCK_RELEASE(&Region->Lock, OldIrql);
            return NULL;
        }

        Region->MetadataNext = (ULONG_PTR)Run;
        Region->MetadataCommitted = (ULONG_PTR)Run;
        Region->MetadataEnd = (ULONG_PTR)Run + (Units << POOL_UNIT_SHIFT);
        PoolVaAssertMetadata(Region);
    }

    Block = Region->MetadataNext;
    NewNext = Block + Bytes;
    CommitEnd = POOL_PAGE_ROUND_UP(NewNext);

    if (CommitEnd > Region->MetadataCommitted)
    {
        if (!NT_SUCCESS(PoolVaCommit(Region, (PVOID)Region->MetadataCommitted,
                                     CommitEnd - Region->MetadataCommitted)))
        {
            PoolVaAssertMetadata(Region);
            POOL_LOCK_RELEASE(&Region->Lock, OldIrql);
            return NULL;
        }

        RtlZeroMemory((PVOID)Region->MetadataCommitted, CommitEnd - Region->MetadataCommitted);
        Region->MetadataCommitted = CommitEnd;
    }

    Region->MetadataNext = NewNext;
    PoolVaAssertMetadata(Region);
    POOL_LOCK_RELEASE(&Region->Lock, OldIrql);
    return (PVOID)Block;
}
