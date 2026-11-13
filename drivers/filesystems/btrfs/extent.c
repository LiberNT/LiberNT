/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Extent references and data checksums
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_INLINE_DATA_REF_SIZE (1 + sizeof(BTRFS_EXTENT_DATA_REF))
#define BTRFS_INLINE_TREE_REF_SIZE (1 + sizeof(ULONGLONG))

NTSTATUS
BtrfsQueueRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ UCHAR Type,
    _In_ ULONGLONG ByteNr,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG Root,
    _In_ ULONGLONG ObjectId,
    _In_ ULONGLONG Offset)
{
    PBTRFS_PENDING_REF Ref = ExAllocatePoolZero(PagedPool, sizeof(BTRFS_PENDING_REF), TAG_BTRFS);

    ASSERT(Vcb->TransActive);
    Vcb->ChangeSequence++;
    if (Ref == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Ref->Type = Type;
    Ref->ByteNr = ByteNr;
    Ref->Length = Length;
    Ref->Root = Root;
    Ref->ObjectId = ObjectId;
    Ref->Offset = Offset;
    InsertTailList(&Vcb->PendingRefs, &Ref->Links);
    return STATUS_SUCCESS;
}

static
ULONG
BtrfsInlineRefSize(
    _In_ UCHAR Type)
{
    switch (Type)
    {
        case BTRFS_TREE_BLOCK_REF_KEY:
        case BTRFS_SHARED_BLOCK_REF_KEY:
        case 172:
            return BTRFS_INLINE_TREE_REF_SIZE;
        case BTRFS_EXTENT_DATA_REF_KEY:
            return BTRFS_INLINE_DATA_REF_SIZE;
        case BTRFS_SHARED_DATA_REF_KEY:
            return 1 + sizeof(ULONGLONG) + sizeof(ULONG);
        default:
            return 0;
    }
}

static
ULONGLONG
BtrfsDataRefHash(
    _In_ ULONGLONG Root,
    _In_ ULONGLONG Owner,
    _In_ ULONGLONG Offset)
{
    ULONG High = BtrfsCrc32c(~0UL, &Root, sizeof(Root));
    ULONG Low = BtrfsCrc32c(~0UL, &Owner, sizeof(Owner));

    Low = BtrfsCrc32c(Low, &Offset, sizeof(Offset));
    return ((ULONGLONG)High << 31) ^ (ULONGLONG)Low;
}

/* The inline reference of a data extent that names this file range, or the offset where the inline references end */
static
BOOLEAN
BtrfsFindInlineDataRef(
    _In_reads_bytes_(Size) PUCHAR Item,
    _In_ ULONG Size,
    _In_ const BTRFS_PENDING_REF *Ref,
    _Out_ PULONG RefOffset,
    _Out_ PBOOLEAN Corrupt)
{
    ULONG Offset = sizeof(BTRFS_EXTENT_ITEM);

    *Corrupt = FALSE;
    *RefOffset = 0;

    while (Offset < Size)
    {
        UCHAR Type = Item[Offset];
        ULONG Length = BtrfsInlineRefSize(Type);

        if (Length == 0 || Length > Size - Offset)
        {
            *Corrupt = TRUE;
            return FALSE;
        }

        if (Type == BTRFS_EXTENT_DATA_REF_KEY)
        {
            PBTRFS_EXTENT_DATA_REF Data = (PBTRFS_EXTENT_DATA_REF)(Item + Offset + 1);

            if (Data->Root == Ref->Root && Data->ObjectId == Ref->ObjectId && Data->Offset == Ref->Offset)
            {
                *RefOffset = Offset;
                return TRUE;
            }
        }
        Offset += Length;
    }

    return FALSE;
}

static
NTSTATUS
BtrfsFreeDataExtent(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG ByteNr,
    _In_ ULONGLONG Length)
{
    NTSTATUS Status = BtrfsDeleteChecksums(Vcb, ByteNr, Length);

    if (!NT_SUCCESS(Status)) return Status;
    return BtrfsPin(Vcb, ByteNr, Length);
}

static
NTSTATUS
BtrfsDropKeyedDataRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Extents,
    _In_ const BTRFS_PENDING_REF *Ref)
{
    BTRFS_KEY Key;
    ULONG Tries;
    NTSTATUS Status;

    Key.ObjectId = Ref->ByteNr;
    Key.Type = BTRFS_EXTENT_DATA_REF_KEY;
    Key.Offset = BtrfsDataRefHash(Ref->Root, Ref->ObjectId, Ref->Offset);

    for (Tries = 0; Tries < 16; Tries++, Key.Offset++)
    {
        BTRFS_EXTENT_DATA_REF Data;
        ULONG Size;

        Status = BtrfsFindItem(Vcb, &Extents->Root, &Key, &Data, sizeof(Data), &Size);
        if (Status == STATUS_NOT_FOUND) return STATUS_NOT_SUPPORTED;
        if (!NT_SUCCESS(Status)) return Status;
        if (Size != sizeof(Data)) return STATUS_FILE_CORRUPT_ERROR;
        if (Data.Root != Ref->Root || Data.ObjectId != Ref->ObjectId || Data.Offset != Ref->Offset) continue;

        if (--Data.Count == 0) return BtrfsDeleteItem(Vcb, Extents, &Key);
        return BtrfsUpdateItem(Vcb, Extents, &Key, &Data, sizeof(Data));
    }

    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
BtrfsRunDataRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Extents,
    _In_ const BTRFS_PENDING_REF *Ref)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    ULONG RefOffset, Size;
    BOOLEAN Found, Corrupt, Keyed = FALSE, Free = FALSE;
    PUCHAR Item;
    PBTRFS_EXTENT_ITEM Extent;
    NTSTATUS Status;

    Key.ObjectId = Ref->ByteNr;
    Key.Type = BTRFS_EXTENT_ITEM_KEY;
    Key.Offset = Ref->Length;

    Status = BtrfsSearchForWrite(Vcb, Extents, &Key, BTRFS_INLINE_DATA_REF_SIZE + sizeof(BTRFS_ITEM), &Path);
    if (!NT_SUCCESS(Status))
    {
        BtrfsReleasePath(Vcb, &Path);
        return Status;
    }

    if (Path.Slots[0] >= Path.Nodes[0]->Header->ItemCount || BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) != 0)
    {
        BtrfsReleasePath(Vcb, &Path);
        if (Ref->Type == BTRFS_REF_DATA_DROP) return STATUS_FILE_CORRUPT_ERROR;

        {
            UCHAR Buffer[sizeof(BTRFS_EXTENT_ITEM) + BTRFS_INLINE_DATA_REF_SIZE];
            PBTRFS_EXTENT_DATA_REF Data = (PBTRFS_EXTENT_DATA_REF)(Buffer + sizeof(BTRFS_EXTENT_ITEM) + 1);

            Extent = (PBTRFS_EXTENT_ITEM)Buffer;
            Extent->Refs = 1;
            Extent->Generation = Vcb->TransId;
            Extent->Flags = BTRFS_EXTENT_FLAG_DATA;
            Buffer[sizeof(BTRFS_EXTENT_ITEM)] = BTRFS_EXTENT_DATA_REF_KEY;
            Data->Root = Ref->Root;
            Data->ObjectId = Ref->ObjectId;
            Data->Offset = Ref->Offset;
            Data->Count = 1;
            return BtrfsInsertItem(Vcb, Extents, &Key, Buffer, sizeof(Buffer), NULL);
        }
    }

    Item = BtrfsPathItemData(Vcb, &Path, &Size);
    if (Item == NULL || Size < sizeof(BTRFS_EXTENT_ITEM))
    {
        BtrfsReleasePath(Vcb, &Path);
        return STATUS_FILE_CORRUPT_ERROR;
    }
    Extent = (PBTRFS_EXTENT_ITEM)Item;
    if (!(Extent->Flags & BTRFS_EXTENT_FLAG_DATA) || Extent->Refs == 0)
    {
        BtrfsReleasePath(Vcb, &Path);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Found = BtrfsFindInlineDataRef(Item, Size, Ref, &RefOffset, &Corrupt);
    if (Corrupt)
    {
        BtrfsReleasePath(Vcb, &Path);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Ref->Type == BTRFS_REF_DATA_ADD)
    {
        if (!Found)
        {
            BtrfsReleasePath(Vcb, &Path);
            return STATUS_NOT_SUPPORTED;
        }
        ((PBTRFS_EXTENT_DATA_REF)(Item + RefOffset + 1))->Count++;
        Extent->Refs++;
        BtrfsReleasePath(Vcb, &Path);
        return STATUS_SUCCESS;
    }

    Extent->Refs--;
    Free = (Extent->Refs == 0);
    if (Found)
    {
        PBTRFS_EXTENT_DATA_REF Data = (PBTRFS_EXTENT_DATA_REF)(Item + RefOffset + 1);

        ASSERT(Data->Count != 0);
        if (--Data->Count == 0 && !Free)
        {
            RtlMoveMemory(Item + RefOffset, Item + RefOffset + BTRFS_INLINE_DATA_REF_SIZE,
                          Size - RefOffset - BTRFS_INLINE_DATA_REF_SIZE);
            Status = BtrfsResizeItemAt(Vcb, &Path, Size - BTRFS_INLINE_DATA_REF_SIZE);
            if (!NT_SUCCESS(Status))
            {
                BtrfsReleasePath(Vcb, &Path);
                return Status;
            }
        }
    }
    else
    {
        Keyed = TRUE;
    }

    if (Free)
        Status = BtrfsDeleteItemAt(Vcb, Extents, &Path);
    BtrfsReleasePath(Vcb, &Path);
    if (!NT_SUCCESS(Status)) return Status;

    if (Keyed)
    {
        Status = BtrfsDropKeyedDataRef(Vcb, Extents, Ref);
        if (!NT_SUCCESS(Status)) return Status;
    }

    return Free ? BtrfsFreeDataExtent(Vcb, Ref->ByteNr, Ref->Length) : STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsRunMetaRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Extents,
    _In_ const BTRFS_PENDING_REF *Ref)
{
    BTRFS_KEY Key;
    NTSTATUS Status;

    Key.ObjectId = Ref->ByteNr;
    Key.Type = BTRFS_METADATA_ITEM_KEY;
    Key.Offset = Ref->ObjectId;

    if (Ref->Type == BTRFS_REF_META_ADD)
    {
        UCHAR Buffer[sizeof(BTRFS_EXTENT_ITEM) + BTRFS_INLINE_TREE_REF_SIZE];
        PBTRFS_EXTENT_ITEM Extent = (PBTRFS_EXTENT_ITEM)Buffer;

        Extent->Refs = 1;
        Extent->Generation = Vcb->TransId;
        Extent->Flags = BTRFS_EXTENT_FLAG_TREE_BLOCK;
        Buffer[sizeof(BTRFS_EXTENT_ITEM)] = BTRFS_TREE_BLOCK_REF_KEY;
        RtlCopyMemory(Buffer + sizeof(BTRFS_EXTENT_ITEM) + 1, &Ref->Root, sizeof(ULONGLONG));
        return BtrfsInsertItem(Vcb, Extents, &Key, Buffer, sizeof(Buffer), NULL);
    }

    {
        BTRFS_EXTENT_ITEM Extent;

        Status = BtrfsFindItem(Vcb, &Extents->Root, &Key, &Extent, sizeof(Extent), NULL);
        if (Status == STATUS_NOT_FOUND) return STATUS_FILE_CORRUPT_ERROR;
        if (!NT_SUCCESS(Status)) return Status;
        if (Extent.Refs != 1) return STATUS_NOT_SUPPORTED;
    }

    Status = BtrfsDeleteItem(Vcb, Extents, &Key);
    if (!NT_SUCCESS(Status)) return Status;
    return BtrfsPin(Vcb, Ref->ByteNr, Ref->Length);
}

/* Applying a reference changes the extent tree, which may queue references of its own: the caller repeats */
NTSTATUS
BtrfsRunPendingRefs(
    _In_ PBTRFS_VCB Vcb,
    _Out_ PBOOLEAN Ran)
{
    PBTRFS_LIVE_ROOT Extents = BtrfsLiveRoot(Vcb, BTRFS_EXTENT_TREE_OBJECTID);
    NTSTATUS Status = STATUS_SUCCESS;

    *Ran = FALSE;
    if (Extents == NULL) return STATUS_NOT_SUPPORTED;

    while (!IsListEmpty(&Vcb->PendingRefs) && NT_SUCCESS(Status))
    {
        PBTRFS_PENDING_REF Ref = CONTAINING_RECORD(RemoveHeadList(&Vcb->PendingRefs), BTRFS_PENDING_REF, Links);

        *Ran = TRUE;
        if (Ref->Type == BTRFS_REF_META_ADD || Ref->Type == BTRFS_REF_META_DROP)
            Status = BtrfsRunMetaRef(Vcb, Extents, Ref);
        else
            Status = BtrfsRunDataRef(Vcb, Extents, Ref);

        if (!NT_SUCCESS(Status))
            DPRINT1("Btrfs: reference %u of %I64x could not be applied (%lx)\n", Ref->Type, Ref->ByteNr, Status);
        ExFreePoolWithTag(Ref, TAG_BTRFS);
    }

    return Status;
}

static
ULONG
BtrfsChecksumsPerItem(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Room = (Vcb->NodeSize - sizeof(BTRFS_HEADER)) / 2 - sizeof(BTRFS_ITEM);

    return max(Room / Vcb->ChecksumSize, 1);
}

static
NTSTATUS
BtrfsInsertChecksumRun(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Sums,
    _In_ ULONGLONG Logical,
    _In_reads_bytes_(Sectors * Vcb->ChecksumSize) const UCHAR *Checksums,
    _In_ ULONG Sectors)
{
    ULONG PerItem = BtrfsChecksumsPerItem(Vcb);
    NTSTATUS Status = STATUS_SUCCESS;

    while (Sectors != 0 && NT_SUCCESS(Status))
    {
        ULONG Count = min(Sectors, PerItem);
        BTRFS_KEY Key;

        Key.ObjectId = BTRFS_EXTENT_CSUM_OBJECTID;
        Key.Type = BTRFS_EXTENT_CSUM_KEY;
        Key.Offset = Logical;
        Status = BtrfsInsertItem(Vcb, Sums, &Key, Checksums, Count * Vcb->ChecksumSize, NULL);

        Logical += (ULONGLONG)Count * Vcb->SectorSize;
        Checksums += Count * Vcb->ChecksumSize;
        Sectors -= Count;
    }
    return Status;
}

NTSTATUS
BtrfsInsertChecksums(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTRFS_LIVE_ROOT Sums = BtrfsLiveRoot(Vcb, BTRFS_CSUM_TREE_OBJECTID);
    ULONG Sectors = Length / Vcb->SectorSize, Index;
    PUCHAR Checksums;
    NTSTATUS Status;

    ASSERT((Length % Vcb->SectorSize) == 0 && (Logical % Vcb->SectorSize) == 0);
    if (Sums == NULL) return STATUS_NOT_SUPPORTED;

    Checksums = ExAllocatePoolUninitialized(PagedPool, (SIZE_T)Sectors * Vcb->ChecksumSize, TAG_BTRFS_IO);
    if (Checksums == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Sectors; Index++)
    {
        UCHAR Digest[BTRFS_CSUM_SIZE];

        BtrfsComputeChecksum(Vcb->ChecksumType, Data + (SIZE_T)Index * Vcb->SectorSize, Vcb->SectorSize, Digest);
        RtlCopyMemory(Checksums + (SIZE_T)Index * Vcb->ChecksumSize, Digest, Vcb->ChecksumSize);
    }

    Status = BtrfsInsertChecksumRun(Vcb, Sums, Logical, Checksums, Sectors);
    ExFreePoolWithTag(Checksums, TAG_BTRFS_IO);
    return Status;
}

static
NTSTATUS
BtrfsFindChecksumOverlap(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Sums,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _Out_ PBTRFS_KEY Found,
    _Out_ PULONG Sectors)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;
    ULONG Round;

    Key.ObjectId = BTRFS_EXTENT_CSUM_OBJECTID;
    Key.Type = BTRFS_EXTENT_CSUM_KEY;
    Key.Offset = Start;

    Status = BtrfsSearch(Vcb, &Sums->Root, &Key, &Path);
    if (Status == STATUS_NOT_FOUND) Status = STATUS_SUCCESS;
    if (NT_SUCCESS(Status) && (Path.Slots[0] >= Path.Nodes[0]->Header->ItemCount ||
                               BtrfsPathItem(&Path)->Key.Offset != Start))
    {
        NTSTATUS Back = BtrfsPreviousItem(Vcb, &Path);
        if (!NT_SUCCESS(Back) && Back != STATUS_NO_MORE_ENTRIES) Status = Back;
    }

    for (Round = 0; Round < 2 && NT_SUCCESS(Status); Round++)
    {
        PBTRFS_ITEM Item;
        ULONGLONG ItemEnd;

        if (Path.Slots[0] >= Path.Nodes[0]->Header->ItemCount)
        {
            Status = BtrfsNextItem(Vcb, &Path);
            if (!NT_SUCCESS(Status)) break;
        }

        Item = BtrfsPathItem(&Path);
        if (Item->Key.ObjectId == BTRFS_EXTENT_CSUM_OBJECTID && Item->Key.Type == BTRFS_EXTENT_CSUM_KEY)
        {
            ItemEnd = Item->Key.Offset + (ULONGLONG)(Item->Size / Vcb->ChecksumSize) * Vcb->SectorSize;
            if (Item->Key.Offset < End && ItemEnd > Start)
            {
                *Found = Item->Key;
                *Sectors = Item->Size / Vcb->ChecksumSize;
                BtrfsReleasePath(Vcb, &Path);
                return STATUS_SUCCESS;
            }
            if (Item->Key.Offset >= End) break;
        }
        Status = BtrfsNextItem(Vcb, &Path);
    }

    BtrfsReleasePath(Vcb, &Path);
    return (NT_SUCCESS(Status) || Status == STATUS_NO_MORE_ENTRIES) ? STATUS_NOT_FOUND : Status;
}

NTSTATUS
BtrfsDeleteChecksums(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_ ULONGLONG Length)
{
    PBTRFS_LIVE_ROOT Sums = BtrfsLiveRoot(Vcb, BTRFS_CSUM_TREE_OBJECTID);
    ULONGLONG End = Logical + Length;
    NTSTATUS Status;

    if (Sums == NULL) return STATUS_NOT_SUPPORTED;

    for (;;)
    {
        BTRFS_KEY Key;
        ULONG Sectors, Size;
        ULONGLONG ItemStart, ItemEnd;
        PUCHAR Copy;

        Status = BtrfsFindChecksumOverlap(Vcb, Sums, Logical, End, &Key, &Sectors);
        if (Status == STATUS_NOT_FOUND) return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;

        ItemStart = Key.Offset;
        ItemEnd = ItemStart + (ULONGLONG)Sectors * Vcb->SectorSize;
        Size = Sectors * Vcb->ChecksumSize;

        Copy = ExAllocatePoolUninitialized(PagedPool, Size, TAG_BTRFS_IO);
        if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        Status = BtrfsFindItem(Vcb, &Sums->Root, &Key, Copy, Size, NULL);
        if (NT_SUCCESS(Status)) Status = BtrfsDeleteItem(Vcb, Sums, &Key);

        if (NT_SUCCESS(Status) && ItemStart < Logical)
        {
            ULONG Keep = (ULONG)((Logical - ItemStart) / Vcb->SectorSize);
            Status = BtrfsInsertChecksumRun(Vcb, Sums, ItemStart, Copy, Keep);
        }
        if (NT_SUCCESS(Status) && ItemEnd > End)
        {
            ULONG Skip = (ULONG)((End - ItemStart) / Vcb->SectorSize);
            Status = BtrfsInsertChecksumRun(Vcb, Sums, End, Copy + Skip * Vcb->ChecksumSize, Sectors - Skip);
        }

        ExFreePoolWithTag(Copy, TAG_BTRFS_IO);
        if (!NT_SUCCESS(Status)) return Status;
    }
}
