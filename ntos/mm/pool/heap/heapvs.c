/*
 * PROJECT:     LiberNT Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heapvs.c
 * PURPOSE:     Variable-size pool heap allocation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

#define POOL_VS_RUN_SIZE        ((POOL_VS_RUN_BYTES > 4 * PAGE_SIZE) ? POOL_VS_RUN_BYTES : 4 * PAGE_SIZE)
#define POOL_VS_RUN_PAGES       ((ULONG)(POOL_VS_RUN_SIZE >> PAGE_SHIFT))
#define POOL_VS_RUN_UNITS       ((ULONG)((POOL_VS_RUN_SIZE - POOL_VS_SUBSEGMENT_BYTES) / POOL_VS_UNIT))
#define POOL_VS_FLAG_FREE       0x2
#define POOL_VS_FLAG_LAST       0x1
#define POOL_VS_CHECK_MASK      0xFFFFFFFCULL
#define POOL_VS_SCAN_LIMIT      8

C_ASSERT(POOL_VS_RUN_SIZE / POOL_VS_UNIT <= 0xFFFF);

typedef struct _POOL_VS_DECODED
{
    ULONG Units;
    ULONG PrevUnits;
    BOOLEAN Free;
    BOOLEAN Last;
    BOOLEAN Valid;
} POOL_VS_DECODED, *PPOOL_VS_DECODED;

SIZE_T
PoolVsMaxBytes(VOID)
{
    return PAGE_SIZE - 3 * POOL_VS_UNIT;
}

FORCEINLINE
VOID
PoolVsEncode(PPOOL_HEAP Heap, PPOOL_VS_CHUNK Chunk, ULONG Units, ULONG PrevUnits, BOOLEAN Free, BOOLEAN Last)
{
    ULONG64 Raw = ((ULONG64)Units << 48) | ((ULONG64)PrevUnits << 32) |
                  (Free ? POOL_VS_FLAG_FREE : 0) | (Last ? POOL_VS_FLAG_LAST : 0);

    Chunk->Encoded = Raw ^ Heap->EncodeKey ^ (ULONG64)(ULONG_PTR)Chunk;
}

FORCEINLINE
VOID
PoolVsDecode(PPOOL_HEAP Heap, PPOOL_VS_CHUNK Chunk, PPOOL_VS_DECODED Decoded)
{
    ULONG64 Raw = Chunk->Encoded ^ Heap->EncodeKey ^ (ULONG64)(ULONG_PTR)Chunk;

    Decoded->Units = (ULONG)(Raw >> 48);
    Decoded->PrevUnits = (ULONG)((Raw >> 32) & 0xFFFF);
    Decoded->Free = (Raw & POOL_VS_FLAG_FREE) ? TRUE : FALSE;
    Decoded->Last = (Raw & POOL_VS_FLAG_LAST) ? TRUE : FALSE;
    Decoded->Valid = ((Raw & POOL_VS_CHECK_MASK) == 0 && Decoded->Units >= 2) ? TRUE : FALSE;
}

FORCEINLINE
ULONG
PoolVsBucket(ULONG Units)
{
    ULONG Bucket = 0;

    while ((Units >> (Bucket + 1)) != 0 && Bucket < POOL_VS_FREE_LISTS - 1)
        Bucket++;

    return Bucket;
}

FORCEINLINE
PPOOL_VS_CHUNK
PoolVsFirstChunk(PPOOL_VS_SUBSEGMENT Subsegment)
{
    return (PPOOL_VS_CHUNK)((ULONG_PTR)Subsegment + POOL_VS_SUBSEGMENT_BYTES);
}

FORCEINLINE
PPOOL_VS_CHUNK
PoolVsAdvance(PPOOL_VS_CHUNK Chunk, ULONG Units)
{
    return (PPOOL_VS_CHUNK)((ULONG_PTR)Chunk + (SIZE_T)Units * POOL_VS_UNIT);
}

FORCEINLINE
PPOOL_VS_CHUNK
PoolVsRetreat(PPOOL_VS_CHUNK Chunk, ULONG Units)
{
    return (PPOOL_VS_CHUNK)((ULONG_PTR)Chunk - (SIZE_T)Units * POOL_VS_UNIT);
}

#if DBG || defined(POOL_HOST_TEST)
static
VOID
PoolVsAssertContext(
    _In_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_CONTEXT Vs,
    _In_ ULONG Shard)
{
    PLIST_ENTRY Head = &Vs->Subsegments;
    PLIST_ENTRY Entry = Head->Flink;
    ULONG Subsegments = 0, Bucket;
    SIZE_T UnitsFree = 0, ListedUnits = 0;
    ULONG64 FreeChunks = 0, ListedChunks = 0;

    POOL_ASSERT(Head->Flink->Blink == Head && Head->Blink->Flink == Head);
    while (Entry != Head && Subsegments < Vs->SubsegmentCount)
    {
        PPOOL_VS_SUBSEGMENT Subsegment = CONTAINING_RECORD(Entry, POOL_VS_SUBSEGMENT, Link);
        PPOOL_VS_CHUNK Chunk = PoolVsFirstChunk(Subsegment);
        ULONG Offset = 0, Previous = 0;
        BOOLEAN PreviousFree = FALSE;

        POOL_ASSERT(Entry->Flink->Blink == Entry && Entry->Blink->Flink == Entry);
        POOL_ASSERT(Subsegment->Signature == POOL_VS_SIGNATURE && Subsegment->Shard == Shard);
        POOL_ASSERT(Subsegment->TotalUnits == POOL_VS_RUN_UNITS && Subsegment->Pages == POOL_VS_RUN_PAGES);
        while (Offset < Subsegment->TotalUnits)
        {
            POOL_VS_DECODED Decoded;

            PoolVsDecode(Heap, Chunk, &Decoded);
            POOL_ASSERT(Decoded.Valid && Decoded.Units <= Subsegment->TotalUnits - Offset);
            POOL_ASSERT(Decoded.PrevUnits == Previous);
            POOL_ASSERT((Decoded.Last != FALSE) == (Decoded.Units == Subsegment->TotalUnits - Offset));
            POOL_ASSERT(!PreviousFree || !Decoded.Free);
            if (Decoded.Free)
            {
                UnitsFree += Decoded.Units;
                FreeChunks++;
            }
            Previous = Decoded.Units;
            PreviousFree = Decoded.Free;
            Offset += Decoded.Units;
            Chunk = PoolVsAdvance(Chunk, Decoded.Units);
        }
        POOL_ASSERT(Offset == Subsegment->TotalUnits);
        Subsegments++;
        Entry = Entry->Flink;
    }
    POOL_ASSERT(Entry == Head && Subsegments == Vs->SubsegmentCount);
    for (Bucket = 0; Bucket < POOL_VS_FREE_LISTS; Bucket++)
    {
        Head = &Vs->FreeList[Bucket];
        Entry = Head->Flink;
        POOL_ASSERT(Head->Flink->Blink == Head && Head->Blink->Flink == Head);
        while (Entry != Head && ListedChunks < FreeChunks)
        {
            PPOOL_VS_CHUNK Chunk = (PPOOL_VS_CHUNK)Entry - 1;
            POOL_VS_DECODED Decoded;

            POOL_ASSERT(Entry->Flink->Blink == Entry && Entry->Blink->Flink == Entry);
            PoolVsDecode(Heap, Chunk, &Decoded);
            POOL_ASSERT(Decoded.Valid && Decoded.Free && PoolVsBucket(Decoded.Units) == Bucket);
            ListedUnits += Decoded.Units;
            ListedChunks++;
            Entry = Entry->Flink;
        }
        POOL_ASSERT(Entry == Head);
    }
    POOL_ASSERT(ListedChunks == FreeChunks && ListedUnits == UnitsFree && UnitsFree == Vs->UnitsFree);
}
#else
#define PoolVsAssertContext(Heap, Vs, Shard) ((void)0)
#endif

static
VOID
PoolVsInsertFree(
    _Inout_ PPOOL_VS_CONTEXT Vs,
    _In_ PPOOL_VS_CHUNK Chunk,
    _In_ ULONG Units)
{
    POOL_ASSERT(Units >= 2 && Units <= POOL_VS_RUN_UNITS);
    InsertHeadList(&Vs->FreeList[PoolVsBucket(Units)], (PLIST_ENTRY)(Chunk + 1));
    Vs->UnitsFree += Units;
}

static
VOID
PoolVsRemoveFree(
    _Inout_ PPOOL_VS_CONTEXT Vs,
    _In_ PPOOL_VS_CHUNK Chunk,
    _In_ ULONG Units)
{
    PLIST_ENTRY Entry = (PLIST_ENTRY)(Chunk + 1);

    POOL_ASSERT(Units >= 2 && Units <= POOL_VS_RUN_UNITS && Vs->UnitsFree >= Units);
    POOL_ASSERT(Entry->Flink->Blink == Entry && Entry->Blink->Flink == Entry);
    RemoveEntryList(Entry);
    Vs->UnitsFree -= Units;
}

static
VOID
PoolVsSetPrev(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_CHUNK Chunk,
    _In_ ULONG PrevUnits)
{
    POOL_VS_DECODED Decoded;

    PoolVsDecode(Heap, Chunk, &Decoded);
    POOL_ASSERT(Decoded.Valid);
    PoolVsEncode(Heap, Chunk, Decoded.Units, PrevUnits, Decoded.Free, Decoded.Last);
}

static
BOOLEAN
PoolVsFit(
    _In_ PPOOL_VS_CHUNK Chunk,
    _In_ ULONG Units,
    _In_ ULONG Wanted,
    _Out_ PULONG Lead)
{
    ULONG_PTR Start = (ULONG_PTR)Chunk;
    ULONG_PTR User = Start + POOL_VS_UNIT;
    ULONG_PTR Last = Start + (ULONG_PTR)Wanted * POOL_VS_UNIT - 1;
    ULONG_PTR Boundary;
    ULONG Skip;

    *Lead = 0;

    if (Units < Wanted)
        return FALSE;

    if ((User & (PAGE_SIZE - 1)) != 0 && (User >> PAGE_SHIFT) == (Last >> PAGE_SHIFT))
        return TRUE;

    Boundary = (User + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (Boundary == User)
        Boundary += POOL_VS_UNIT;

    Skip = (ULONG)((Boundary - Start) / POOL_VS_UNIT);
    if (Skip < 2)
        Skip = 2;

    if (Units < Skip + Wanted)
        return FALSE;

    *Lead = Skip;
    return TRUE;
}

static
PPOOL_VS_CHUNK
PoolVsSkipLead(
    _Inout_ PPOOL_HEAP Heap,
    _Inout_ PPOOL_VS_CONTEXT Vs,
    _In_ PPOOL_VS_CHUNK Chunk,
    _Inout_ PPOOL_VS_DECODED Decoded,
    _In_ ULONG Lead)
{
    PPOOL_VS_CHUNK Rest;

    if (Lead == 0)
        return Chunk;

    POOL_ASSERT(Lead >= 2 && Lead <= Decoded->Units - 2);
    Rest = PoolVsAdvance(Chunk, Lead);

    PoolVsEncode(Heap, Chunk, Lead, Decoded->PrevUnits, TRUE, FALSE);
    Chunk->Tag = 0;
    Chunk->Flags = 0;
    PoolVsInsertFree(Vs, Chunk, Lead);

    Decoded->Units -= Lead;
    Decoded->PrevUnits = Lead;
    PoolVsEncode(Heap, Rest, Decoded->Units, Lead, TRUE, Decoded->Last);

    if (!Decoded->Last)
        PoolVsSetPrev(Heap, PoolVsAdvance(Rest, Decoded->Units), Decoded->Units);

    return Rest;
}

static
ULONG
PoolVsCarve(
    _Inout_ PPOOL_HEAP Heap,
    _Inout_ PPOOL_VS_CONTEXT Vs,
    _In_ PPOOL_VS_CHUNK Chunk,
    _In_ PPOOL_VS_DECODED Decoded,
    _In_ ULONG Wanted,
    _In_ ULONG Tag)
{
    ULONG Rest = Decoded->Units - Wanted;

    POOL_ASSERT(Wanted >= 2 && Wanted <= Decoded->Units);
    POOL_ASSERT(((ULONG_PTR)(Chunk + 1) & (PAGE_SIZE - 1)) != 0);
    POOL_ASSERT(((ULONG_PTR)(Chunk + 1) >> PAGE_SHIFT) ==
                (((ULONG_PTR)Chunk + (SIZE_T)Wanted * POOL_VS_UNIT - 1) >> PAGE_SHIFT));
    if (Rest < 2)
    {
        PoolVsEncode(Heap, Chunk, Decoded->Units, Decoded->PrevUnits, FALSE, Decoded->Last);
    }
    else
    {
        PPOOL_VS_CHUNK Remainder = PoolVsAdvance(Chunk, Wanted);

        PoolVsEncode(Heap, Chunk, Wanted, Decoded->PrevUnits, FALSE, FALSE);
        PoolVsEncode(Heap, Remainder, Rest, Wanted, TRUE, Decoded->Last);
        Remainder->Tag = 0;
        Remainder->Flags = 0;
        PoolVsInsertFree(Vs, Remainder, Rest);

        if (!Decoded->Last)
            PoolVsSetPrev(Heap, PoolVsAdvance(Remainder, Rest), Rest);
    }

    Chunk->Tag = Tag;
    Chunk->Flags = 0;
    return (Rest < 2) ? Decoded->Units : Wanted;
}

PVOID
PoolVsAllocate(
    _Inout_ PPOOL_HEAP Heap,
    _In_ SIZE_T Bytes,
    _In_ ULONG Tag,
    _Out_ PSIZE_T ActualBytes)
{
    USHORT ShardIndex = (USHORT)(POOL_CURRENT_PROCESSOR() % Heap->ShardCount);
    PPOOL_VS_CONTEXT Vs = &Heap->Vs[ShardIndex];
    PPOOL_VS_SUBSEGMENT Subsegment;
    POOL_VS_DECODED Decoded;
    PPOOL_VS_CHUNK Chunk;
    PLIST_ENTRY Entry;
    ULONG Wanted;
    ULONG Bucket;
    ULONG Scanned;
    ULONG Lead;
    KIRQL OldIrql;

    *ActualBytes = 0;
    if (Bytes == 0 || Bytes > PoolVsMaxBytes())
        return NULL;

    Wanted = (ULONG)((Bytes + POOL_VS_UNIT - 1) / POOL_VS_UNIT) + 1;

    POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);

    for (Bucket = PoolVsBucket(Wanted); Bucket < POOL_VS_FREE_LISTS; Bucket++)
    {
        Scanned = 0;

        for (Entry = Vs->FreeList[Bucket].Flink;
             Entry != &Vs->FreeList[Bucket] && Scanned < POOL_VS_SCAN_LIMIT;
             Entry = Entry->Flink, Scanned++)
        {
            Chunk = (PPOOL_VS_CHUNK)Entry - 1;
            PoolVsDecode(Heap, Chunk, &Decoded);
            POOL_ASSERT(Decoded.Valid && Decoded.Free);
            POOL_ASSERT(PoolVsBucket(Decoded.Units) == Bucket);
            POOL_ASSERT(Vs->SubsegmentCount != 0);

            if (!PoolVsFit(Chunk, Decoded.Units, Wanted, &Lead))
                continue;

            PoolVsRemoveFree(Vs, Chunk, Decoded.Units);
            Chunk = PoolVsSkipLead(Heap, Vs, Chunk, &Decoded, Lead);
            *ActualBytes = (SIZE_T)(PoolVsCarve(Heap, Vs, Chunk, &Decoded, Wanted, Tag) - 1) * POOL_VS_UNIT;
            Vs->Allocations++;
            POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
            return Chunk + 1;
        }
    }

    POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);

    Subsegment = PoolSegAllocatePages(Heap, POOL_VS_RUN_PAGES, PoolRangeVs, ShardIndex, 0);
    if (Subsegment == NULL)
        return NULL;

    RtlZeroMemory(Subsegment, sizeof(*Subsegment));
    Subsegment->TotalUnits = POOL_VS_RUN_UNITS;
    Subsegment->Pages = (USHORT)POOL_VS_RUN_PAGES;
    Subsegment->Shard = ShardIndex;
    Subsegment->Signature = POOL_VS_SIGNATURE;

    Chunk = PoolVsFirstChunk(Subsegment);
    Decoded.Units = POOL_VS_RUN_UNITS;
    Decoded.PrevUnits = 0;
    Decoded.Free = TRUE;
    Decoded.Last = TRUE;

    POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);
    InsertHeadList(&Vs->Subsegments, &Subsegment->Link);
    Vs->SubsegmentCount++;
    PoolVsEncode(Heap, Chunk, Decoded.Units, 0, TRUE, TRUE);

    if (!PoolVsFit(Chunk, Decoded.Units, Wanted, &Lead))
    {
        Chunk->Tag = 0;
        Chunk->Flags = 0;
        PoolVsInsertFree(Vs, Chunk, Decoded.Units);
        POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
        return NULL;
    }

    Chunk = PoolVsSkipLead(Heap, Vs, Chunk, &Decoded, Lead);
    *ActualBytes = (SIZE_T)(PoolVsCarve(Heap, Vs, Chunk, &Decoded, Wanted, Tag) - 1) * POOL_VS_UNIT;
    Vs->Allocations++;
    POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
    return Chunk + 1;
}

static
BOOLEAN
PoolVsLocate(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PPOOL_VS_CHUNK *Chunk,
    _Out_ PPOOL_VS_DECODED Decoded)
{
    ULONG_PTR First = (ULONG_PTR)PoolVsFirstChunk(Subsegment);
    ULONG_PTR Address = (ULONG_PTR)Block - sizeof(POOL_VS_CHUNK);

    if (Subsegment->Signature != POOL_VS_SIGNATURE || Address < First ||
        ((Address - First) % POOL_VS_UNIT) != 0)
    {
        return FALSE;
    }

    *Chunk = (PPOOL_VS_CHUNK)Address;
    PoolVsDecode(Heap, *Chunk, Decoded);

    if (!Decoded->Valid || Decoded->Free)
        return FALSE;

    return (((Address - First) / POOL_VS_UNIT) + Decoded->Units <= Subsegment->TotalUnits);
}

BOOLEAN
PoolVsQuery(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_VS_CONTEXT Vs = &Heap->Vs[Subsegment->Shard];
    POOL_VS_DECODED Decoded;
    PPOOL_VS_CHUNK Chunk;
    BOOLEAN Found;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);

    Found = PoolVsLocate(Heap, Subsegment, Block, &Chunk, &Decoded);
    if (Found)
    {
        Info->Size = (SIZE_T)(Decoded.Units - 1) * POOL_VS_UNIT;
        Info->Tag = Chunk->Tag;
        Info->Flags = Chunk->Flags;
        Info->Kind = PoolBlockVs;
        PoolCaptureTrailer(Block, Info);
    }

    POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
    return Found;
}

BOOLEAN
PoolVsSetFlags(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _In_ UCHAR Flags)
{
    PPOOL_VS_CONTEXT Vs = &Heap->Vs[Subsegment->Shard];
    POOL_VS_DECODED Decoded;
    PPOOL_VS_CHUNK Chunk;
    BOOLEAN Found;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);

    Found = PoolVsLocate(Heap, Subsegment, Block, &Chunk, &Decoded);
    if (Found)
        Chunk->Flags = Flags;

    POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
    return Found;
}

BOOLEAN
PoolVsFree(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_VS_CONTEXT Vs = &Heap->Vs[Subsegment->Shard];
    POOL_VS_DECODED Decoded;
    POOL_VS_DECODED Neighbor;
    PPOOL_VS_CHUNK Chunk;
    PPOOL_VS_CHUNK Other;
    BOOLEAN Retire = FALSE;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);

    if (!PoolVsLocate(Heap, Subsegment, Block, &Chunk, &Decoded))
    {
        POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);
        return FALSE;
    }

    POOL_ASSERT(Vs->SubsegmentCount != 0 && Subsegment->TotalUnits == POOL_VS_RUN_UNITS);
    POOL_ASSERT(Decoded.PrevUnits <= ((ULONG_PTR)Chunk - (ULONG_PTR)PoolVsFirstChunk(Subsegment)) / POOL_VS_UNIT);
    POOL_ASSERT((Decoded.PrevUnits == 0) == (Chunk == PoolVsFirstChunk(Subsegment)));
    POOL_ASSERT(Decoded.Last ==
                (((ULONG_PTR)Chunk - (ULONG_PTR)PoolVsFirstChunk(Subsegment)) / POOL_VS_UNIT + Decoded.Units ==
                 Subsegment->TotalUnits));
    Info->Size = (SIZE_T)(Decoded.Units - 1) * POOL_VS_UNIT;
    Info->Tag = Chunk->Tag;
    Info->Flags = Chunk->Flags;
    Info->Kind = PoolBlockVs;
    PoolCaptureTrailer(Block, Info);

    if (Heap->Options & POOL_HEAP_POISON_ON_FREE)
        RtlFillMemory(Block, Info->Size, POOL_FREE_POISON);

    if (!Decoded.Last)
    {
        Other = PoolVsAdvance(Chunk, Decoded.Units);
        PoolVsDecode(Heap, Other, &Neighbor);
        POOL_ASSERT(Neighbor.Valid && Neighbor.PrevUnits == Decoded.Units);

        if (Neighbor.Free)
        {
            PoolVsRemoveFree(Vs, Other, Neighbor.Units);
            Decoded.Units += Neighbor.Units;
            Decoded.Last = Neighbor.Last;
        }
    }

    if (Decoded.PrevUnits != 0)
    {
        Other = PoolVsRetreat(Chunk, Decoded.PrevUnits);
        PoolVsDecode(Heap, Other, &Neighbor);
        POOL_ASSERT(Neighbor.Valid && Neighbor.Units == Decoded.PrevUnits);

        if (Neighbor.Free)
        {
            PoolVsRemoveFree(Vs, Other, Neighbor.Units);
            Decoded.Units += Neighbor.Units;
            Decoded.PrevUnits = Neighbor.PrevUnits;
            Chunk = Other;
        }
    }

    POOL_ASSERT(Decoded.Units <= Subsegment->TotalUnits);
    POOL_ASSERT(Decoded.Units != Subsegment->TotalUnits ||
                (Chunk == PoolVsFirstChunk(Subsegment) && Decoded.PrevUnits == 0 && Decoded.Last));
    Vs->Frees++;

    if (Decoded.Units == Subsegment->TotalUnits && Vs->SubsegmentCount > 1)
    {
        RemoveEntryList(&Subsegment->Link);
        Vs->SubsegmentCount--;
        Subsegment->Signature = 0;
        Retire = TRUE;
    }
    else
    {
        PoolVsEncode(Heap, Chunk, Decoded.Units, Decoded.PrevUnits, TRUE, Decoded.Last);
        Chunk->Tag = 0;
        Chunk->Flags = 0;
        PoolVsInsertFree(Vs, Chunk, Decoded.Units);

        if (!Decoded.Last)
            PoolVsSetPrev(Heap, PoolVsAdvance(Chunk, Decoded.Units), Decoded.Units);
    }

    POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);

    if (Retire)
        PoolSegFreePages(Heap, Subsegment);

    return TRUE;
}

VOID
PoolVsTrim(
    _Inout_ PPOOL_HEAP Heap)
{
    ULONG Index;

    for (Index = 0; Index < Heap->ShardCount; Index++)
    {
        PPOOL_VS_CONTEXT Vs = &Heap->Vs[Index];
        LIST_ENTRY Retired;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        InitializeListHead(&Retired);
        POOL_LOCK_ACQUIRE(&Vs->Lock, &OldIrql);
        PoolVsAssertContext(Heap, Vs, Index);

        Entry = Vs->Subsegments.Flink;
        while (Entry != &Vs->Subsegments)
        {
            PPOOL_VS_SUBSEGMENT Subsegment = CONTAINING_RECORD(Entry, POOL_VS_SUBSEGMENT, Link);
            PPOOL_VS_CHUNK Chunk = PoolVsFirstChunk(Subsegment);
            POOL_VS_DECODED Decoded;

            Entry = Entry->Flink;
            POOL_ASSERT(Subsegment->Signature == POOL_VS_SIGNATURE && Subsegment->Shard == Index);
            POOL_ASSERT(Subsegment->TotalUnits == POOL_VS_RUN_UNITS && Vs->SubsegmentCount != 0);
            PoolVsDecode(Heap, Chunk, &Decoded);
            if (!Decoded.Valid || !Decoded.Free || Decoded.Units != Subsegment->TotalUnits)
                continue;

            PoolVsRemoveFree(Vs, Chunk, Decoded.Units);
            RemoveEntryList(&Subsegment->Link);
            InsertTailList(&Retired, &Subsegment->Link);
            Vs->SubsegmentCount--;
            Subsegment->Signature = 0;
        }

        PoolVsAssertContext(Heap, Vs, Index);
        POOL_LOCK_RELEASE(&Vs->Lock, OldIrql);

        while (!IsListEmpty(&Retired))
        {
            Entry = Retired.Flink;
            RemoveEntryList(Entry);
            PoolSegFreePages(Heap, CONTAINING_RECORD(Entry, POOL_VS_SUBSEGMENT, Link));
        }
    }
}

BOOLEAN
PoolVsWalk(
    _In_ PPOOL_HEAP Heap,
    _In_ PPOOL_VS_SUBSEGMENT Subsegment,
    _Inout_ PVOID *Cursor,
    _Out_ PVOID *Block,
    _Out_ PPOOL_BLOCK_INFO Info,
    _Out_ PBOOLEAN Allocated)
{
    PPOOL_VS_CHUNK Chunk = *Cursor;
    ULONG_PTR End = (ULONG_PTR)PoolVsFirstChunk(Subsegment) + (SIZE_T)Subsegment->TotalUnits * POOL_VS_UNIT;
    POOL_VS_DECODED Decoded;

    if (Subsegment->Signature != POOL_VS_SIGNATURE)
        return FALSE;

    if (Chunk == NULL)
        Chunk = PoolVsFirstChunk(Subsegment);

    if ((ULONG_PTR)Chunk >= End)
        return FALSE;

    PoolVsDecode(Heap, Chunk, &Decoded);
    if (!Decoded.Valid || (ULONG_PTR)Chunk + (SIZE_T)Decoded.Units * POOL_VS_UNIT > End)
        return FALSE;

    *Block = Chunk + 1;
    *Allocated = !Decoded.Free;
    Info->Size = (SIZE_T)(Decoded.Units - 1) * POOL_VS_UNIT;
    Info->Tag = Chunk->Tag;
    Info->Flags = Chunk->Flags;
    Info->Kind = PoolBlockVs;
    Info->Trailer = 0;
    *Cursor = PoolVsAdvance(Chunk, Decoded.Units);
    return TRUE;
}
