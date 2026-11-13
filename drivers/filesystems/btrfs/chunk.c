/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chunk map: translation of logical addresses to the device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_PROFILE_MIRRORED (BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1C3 | BTRFS_BLOCK_GROUP_RAID1C4)

static
PBTRFS_CHUNK_MAP
BtrfsFindChunk(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical)
{
    ULONG Low = 0, High = Vcb->ChunkCount;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;
        PBTRFS_CHUNK_MAP Chunk = Vcb->Chunks[Middle];

        if (Logical < Chunk->Logical)
            High = Middle;
        else if (Logical - Chunk->Logical >= Chunk->Length)
            Low = Middle + 1;
        else
            return Chunk;
    }

    return NULL;
}

NTSTATUS
BtrfsAddChunk(
    _Inout_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_reads_bytes_(Size) const BTRFS_CHUNK *Chunk,
    _In_ ULONG Size)
{
    PBTRFS_CHUNK_MAP Map;
    ULONG Index, Insert;
    USHORT Stripe;

    if (Size < BTRFS_CHUNK_FIXED_SIZE) return STATUS_FILE_CORRUPT_ERROR;
    if (Chunk->StripeCount == 0 || Size < BTRFS_CHUNK_FIXED_SIZE + (ULONG)Chunk->StripeCount * sizeof(BTRFS_STRIPE))
        return STATUS_FILE_CORRUPT_ERROR;
    if (Chunk->Length == 0 || Logical + Chunk->Length < Logical) return STATUS_FILE_CORRUPT_ERROR;
    if ((Chunk->Type & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10)) &&
        (Chunk->StripeLength == 0 || (Chunk->StripeLength & (Chunk->StripeLength - 1)) != 0))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if ((Chunk->Type & BTRFS_BLOCK_GROUP_RAID10) && (Chunk->SubStripes == 0 || Chunk->StripeCount % Chunk->SubStripes != 0))
        return STATUS_FILE_CORRUPT_ERROR;
    if (Chunk->Type & (BTRFS_BLOCK_GROUP_RAID5 | BTRFS_BLOCK_GROUP_RAID6 | BTRFS_BLOCK_GROUP_REMAPPED))
        return STATUS_NOT_SUPPORTED;

    if (BtrfsFindChunk(Vcb, Logical) != NULL) return STATUS_SUCCESS;

    for (Stripe = 0; Stripe < Chunk->StripeCount; Stripe++)
    {
        if (Chunk->Stripes[Stripe].DeviceId != Vcb->DeviceId) return STATUS_NOT_SUPPORTED;
    }

    if (Vcb->ChunkCount == Vcb->ChunkCapacity)
    {
        ULONG Capacity = Vcb->ChunkCapacity ? Vcb->ChunkCapacity * 2 : 32;
        PBTRFS_CHUNK_MAP *Grown = ExAllocatePoolZero(PagedPool, Capacity * sizeof(PBTRFS_CHUNK_MAP), TAG_BTRFS);

        if (Grown == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        if (Vcb->Chunks != NULL)
        {
            RtlCopyMemory(Grown, Vcb->Chunks, Vcb->ChunkCount * sizeof(PBTRFS_CHUNK_MAP));
            ExFreePoolWithTag(Vcb->Chunks, TAG_BTRFS);
        }
        Vcb->Chunks = Grown;
        Vcb->ChunkCapacity = Capacity;
    }

    Map = ExAllocatePoolZero(PagedPool, FIELD_OFFSET(BTRFS_CHUNK_MAP, StripeOffsets) + Chunk->StripeCount * sizeof(ULONGLONG), TAG_BTRFS);
    if (Map == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Map->Logical = Logical;
    Map->Length = Chunk->Length;
    Map->Type = Chunk->Type;
    Map->StripeLength = Chunk->StripeLength;
    Map->StripeCount = Chunk->StripeCount;
    Map->SubStripes = Chunk->SubStripes;
    for (Stripe = 0; Stripe < Chunk->StripeCount; Stripe++)
        Map->StripeOffsets[Stripe] = Chunk->Stripes[Stripe].Offset;

    for (Insert = 0; Insert < Vcb->ChunkCount && Vcb->Chunks[Insert]->Logical < Logical; Insert++);
    for (Index = Vcb->ChunkCount; Index > Insert; Index--)
        Vcb->Chunks[Index] = Vcb->Chunks[Index - 1];
    Vcb->Chunks[Insert] = Map;
    Vcb->ChunkCount++;

    ASSERT(Insert == 0 || Vcb->Chunks[Insert - 1]->Logical + Vcb->Chunks[Insert - 1]->Length <= Logical);
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsLoadSystemChunks(
    _Inout_ PBTRFS_VCB Vcb)
{
    const UCHAR *Array = Vcb->Super.SysChunkArray;
    ULONG Size = Vcb->Super.SysChunkArraySize;
    ULONG Offset = 0;
    NTSTATUS Status;

    if (Size > BTRFS_SYSTEM_CHUNK_ARRAY_SIZE) return STATUS_FILE_CORRUPT_ERROR;

    while (Offset < Size)
    {
        const BTRFS_KEY *Key = (const BTRFS_KEY *)(Array + Offset);
        const BTRFS_CHUNK *Chunk;
        ULONG ChunkSize;

        if (Size - Offset < sizeof(BTRFS_KEY) + BTRFS_CHUNK_FIXED_SIZE) return STATUS_FILE_CORRUPT_ERROR;
        if (Key->Type != BTRFS_CHUNK_ITEM_KEY) return STATUS_FILE_CORRUPT_ERROR;

        Chunk = (const BTRFS_CHUNK *)(Array + Offset + sizeof(BTRFS_KEY));
        ChunkSize = BTRFS_CHUNK_FIXED_SIZE + (ULONG)Chunk->StripeCount * sizeof(BTRFS_STRIPE);
        if (Size - Offset - sizeof(BTRFS_KEY) < ChunkSize) return STATUS_FILE_CORRUPT_ERROR;

        Status = BtrfsAddChunk(Vcb, Key->Offset, Chunk, ChunkSize);
        if (!NT_SUCCESS(Status)) return Status;

        Offset += sizeof(BTRFS_KEY) + ChunkSize;
    }

    return Vcb->ChunkCount != 0 ? STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
BtrfsLoadChunkTree(
    _Inout_ PBTRFS_VCB Vcb)
{
    BTRFS_ROOT ChunkRoot;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    ChunkRoot.Id = BTRFS_CHUNK_TREE_OBJECTID;
    ChunkRoot.ByteNr = Vcb->Super.ChunkRoot;
    ChunkRoot.Level = Vcb->Super.ChunkRootLevel;
    ChunkRoot.RootDirId = 0;

    Key.ObjectId = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
    Key.Type = BTRFS_CHUNK_ITEM_KEY;
    Key.Offset = 0;

    Status = BtrfsSearch(Vcb, &ChunkRoot, &Key, &Path);
    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        ULONG Size;
        PVOID Data;

        if (Item->Key.ObjectId != BTRFS_FIRST_CHUNK_TREE_OBJECTID || Item->Key.Type != BTRFS_CHUNK_ITEM_KEY) break;

        Data = BtrfsPathItemData(Vcb, &Path, &Size);
        if (Data == NULL)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        Status = BtrfsAddChunk(Vcb, Item->Key.Offset, Data, Size);
        if (!NT_SUCCESS(Status)) break;

        Status = BtrfsNextItem(Vcb, &Path);
    }

    BtrfsReleasePath(Vcb, &Path);
    return (Status == STATUS_NO_MORE_ENTRIES || Status == STATUS_NOT_FOUND || NT_SUCCESS(Status)) ? STATUS_SUCCESS : Status;
}

VOID
BtrfsFreeChunks(
    _Inout_ PBTRFS_VCB Vcb)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->ChunkCount; Index++)
        ExFreePoolWithTag(Vcb->Chunks[Index], TAG_BTRFS);
    if (Vcb->Chunks != NULL) ExFreePoolWithTag(Vcb->Chunks, TAG_BTRFS);

    Vcb->Chunks = NULL;
    Vcb->ChunkCount = 0;
    Vcb->ChunkCapacity = 0;
}

ULONG
BtrfsMirrorCount(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical)
{
    PBTRFS_CHUNK_MAP Chunk = BtrfsFindChunk(Vcb, Logical);

    if (Chunk == NULL) return 1;
    if (Chunk->Type & BTRFS_PROFILE_MIRRORED) return Chunk->StripeCount;
    if (Chunk->Type & BTRFS_BLOCK_GROUP_RAID10) return Chunk->SubStripes;
    return 1;
}

/* The device range that holds Logical in one copy, and how many bytes from there stay in the same stripe */
static
NTSTATUS
BtrfsMapLogical(
    _In_ PBTRFS_CHUNK_MAP Chunk,
    _In_ ULONGLONG Logical,
    _In_ ULONG Mirror,
    _Out_ PULONGLONG Physical,
    _Out_ PULONGLONG Contiguous)
{
    ULONGLONG Offset = Logical - Chunk->Logical;

    ASSERT(Logical >= Chunk->Logical && Offset < Chunk->Length);

    if (Chunk->Type & BTRFS_BLOCK_GROUP_RAID0)
    {
        ULONGLONG StripeNumber = Offset / Chunk->StripeLength;
        ULONGLONG InStripe = Offset % Chunk->StripeLength;
        ULONG Index = (ULONG)(StripeNumber % Chunk->StripeCount);

        *Physical = Chunk->StripeOffsets[Index] + (StripeNumber / Chunk->StripeCount) * Chunk->StripeLength + InStripe;
        *Contiguous = Chunk->StripeLength - InStripe;
    }
    else if (Chunk->Type & BTRFS_BLOCK_GROUP_RAID10)
    {
        ULONG Factor = Chunk->StripeCount / Chunk->SubStripes;
        ULONGLONG StripeNumber = Offset / Chunk->StripeLength;
        ULONGLONG InStripe = Offset % Chunk->StripeLength;
        ULONG Index = (ULONG)(StripeNumber % Factor) * Chunk->SubStripes + (Mirror % Chunk->SubStripes);

        *Physical = Chunk->StripeOffsets[Index] + (StripeNumber / Factor) * Chunk->StripeLength + InStripe;
        *Contiguous = Chunk->StripeLength - InStripe;
    }
    else
    {
        ULONG Index = (Chunk->Type & BTRFS_PROFILE_MIRRORED) ? Mirror % Chunk->StripeCount : 0;

        *Physical = Chunk->StripeOffsets[Index] + Offset;
        *Contiguous = Chunk->Length - Offset;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsReadLogical(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ ULONG Mirror)
{
    PUCHAR Cursor = Buffer;
    NTSTATUS Status;

    ASSERT(Vcb->Signature == BTRFS_VCB_SIGNATURE);

    while (Length != 0)
    {
        PBTRFS_CHUNK_MAP Chunk = BtrfsFindChunk(Vcb, Logical);
        ULONGLONG Physical, Contiguous;
        ULONG Part;

        if (Chunk == NULL) return STATUS_FILE_CORRUPT_ERROR;

        Status = BtrfsMapLogical(Chunk, Logical, Mirror, &Physical, &Contiguous);
        if (!NT_SUCCESS(Status)) return Status;

        Part = (ULONG)min((ULONGLONG)Length, Contiguous);
        if (Physical + Part > Vcb->PartitionLength && Vcb->PartitionLength != 0) return STATUS_FILE_CORRUPT_ERROR;

        Status = BtrfsReadDevice(Vcb->TargetDeviceObject, Physical, Cursor, Part, FALSE);
        if (!NT_SUCCESS(Status)) return Status;

        Logical += Part;
        Cursor += Part;
        Length -= Part;
    }

    return STATUS_SUCCESS;
}

/* Every copy of a mirrored range gets the same bytes */
NTSTATUS
BtrfsWriteLogical(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    const UCHAR *Cursor = Buffer;
    NTSTATUS Status;

    ASSERT(Vcb->Signature == BTRFS_VCB_SIGNATURE);
    ASSERT((Logical % Vcb->SectorSize) == 0 && (Length % Vcb->SectorSize) == 0);

    while (Length != 0)
    {
        PBTRFS_CHUNK_MAP Chunk = BtrfsFindChunk(Vcb, Logical);
        ULONGLONG Physical, Contiguous;
        ULONG Part = Length, Mirror, Mirrors;

        if (Chunk == NULL) return STATUS_FILE_CORRUPT_ERROR;
        Mirrors = BtrfsMirrorCount(Vcb, Logical);

        for (Mirror = 0; Mirror < Mirrors; Mirror++)
        {
            Status = BtrfsMapLogical(Chunk, Logical, Mirror, &Physical, &Contiguous);
            if (!NT_SUCCESS(Status)) return Status;

            Part = (ULONG)min((ULONGLONG)Length, Contiguous);
            if (Vcb->PartitionLength != 0 && Physical + Part > Vcb->PartitionLength) return STATUS_FILE_CORRUPT_ERROR;

            Status = BtrfsWriteDevice(Vcb->TargetDeviceObject, Physical, Cursor, Part);
            if (!NT_SUCCESS(Status)) return Status;
        }

        Logical += Part;
        Cursor += Part;
        Length -= Part;
    }

    return STATUS_SUCCESS;
}
