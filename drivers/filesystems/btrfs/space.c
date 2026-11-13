/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Block group space: allocation, space freed by a transaction, the free space tree and block group items
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
NTSTATUS
BtrfsRangeAppend(
    _Inout_ PBTRFS_RANGE *Ranges,
    _Inout_ PULONG Count,
    _Inout_ PULONG Capacity,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    if (*Count == *Capacity)
    {
        ULONG Grown = *Capacity ? *Capacity * 2 : 16;
        PBTRFS_RANGE New = ExAllocatePoolZero(PagedPool, Grown * sizeof(BTRFS_RANGE), TAG_BTRFS);

        if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        if (*Ranges != NULL)
        {
            RtlCopyMemory(New, *Ranges, *Count * sizeof(BTRFS_RANGE));
            ExFreePoolWithTag(*Ranges, TAG_BTRFS);
        }
        *Ranges = New;
        *Capacity = Grown;
    }

    (*Ranges)[*Count].Start = Start;
    (*Ranges)[*Count].Length = Length;
    (*Count)++;
    return STATUS_SUCCESS;
}

/* Free space read from bitmaps comes a sector at a time: neighbours become one range */
static
NTSTATUS
BtrfsRangeAppendMerged(
    _Inout_ PBTRFS_RANGE *Ranges,
    _Inout_ PULONG Count,
    _Inout_ PULONG Capacity,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    if (*Count != 0 && (*Ranges)[*Count - 1].Start + (*Ranges)[*Count - 1].Length == Start)
    {
        (*Ranges)[*Count - 1].Length += Length;
        return STATUS_SUCCESS;
    }
    return BtrfsRangeAppend(Ranges, Count, Capacity, Start, Length);
}

static
NTSTATUS
BtrfsReadFreeBitmap(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_SPACE Space,
    _In_ const BTRFS_KEY *Key,
    _In_reads_bytes_(Size) const UCHAR *Bitmap,
    _In_ ULONG Size)
{
    ULONGLONG Sectors = Key->Offset / Vcb->SectorSize, Sector;
    NTSTATUS Status = STATUS_SUCCESS;

    if ((Key->Offset % Vcb->SectorSize) != 0 || Size < (Sectors + 7) / 8) return STATUS_FILE_CORRUPT_ERROR;

    for (Sector = 0; Sector < Sectors && NT_SUCCESS(Status); Sector++)
    {
        if (Bitmap[Sector / 8] & (1 << (Sector % 8)))
            Status = BtrfsRangeAppendMerged(&Space->Free, &Space->FreeCount, &Space->FreeCapacity,
                                            Key->ObjectId + Sector * Vcb->SectorSize, Vcb->SectorSize);
    }
    return Status;
}

static
VOID
BtrfsRangeRemoveAt(
    _Inout_ PBTRFS_RANGE Ranges,
    _Inout_ PULONG Count,
    _In_ ULONG Index)
{
    ASSERT(Index < *Count);
    RtlMoveMemory(&Ranges[Index], &Ranges[Index + 1], (*Count - Index - 1) * sizeof(BTRFS_RANGE));
    (*Count)--;
}

static
VOID
BtrfsSortRanges(
    _Inout_ PBTRFS_RANGE Ranges,
    _In_ ULONG Count)
{
    ULONG Index, Inner;

    for (Index = 1; Index < Count; Index++)
    {
        BTRFS_RANGE Item = Ranges[Index];

        for (Inner = Index; Inner > 0 && Ranges[Inner - 1].Start > Item.Start; Inner--)
            Ranges[Inner] = Ranges[Inner - 1];
        Ranges[Inner] = Item;
    }
}

static
PBTRFS_SPACE
BtrfsSpaceOf(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];

        if (Logical >= Space->Start && Logical - Space->Start < Space->Length) return Space;
    }
    return NULL;
}

static
NTSTATUS
BtrfsLoadOneSpace(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_SPACE Space)
{
    PBTRFS_LIVE_ROOT Groups, FreeTree;
    BTRFS_BLOCK_GROUP_ITEM Group;
    BTRFS_FREE_SPACE_INFO Info;
    BOOLEAN Bitmaps;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    Groups = BtrfsLiveRoot(Vcb, (Vcb->Super.CompatRoFlags & BTRFS_COMPAT_RO_BLOCK_GROUP_TREE) ? BTRFS_BLOCK_GROUP_TREE_OBJECTID
                                                                                               : BTRFS_EXTENT_TREE_OBJECTID);
    FreeTree = BtrfsLiveRoot(Vcb, BTRFS_FREE_SPACE_TREE_OBJECTID);
    if (Groups == NULL || FreeTree == NULL) return STATUS_NOT_SUPPORTED;

    Key.ObjectId = Space->Start;
    Key.Type = BTRFS_BLOCK_GROUP_ITEM_KEY;
    Key.Offset = Space->Length;
    Status = BtrfsFindItem(Vcb, &Groups->Root, &Key, &Group, sizeof(Group), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    Space->Used = Space->CommittedUsed = Group.Used;
    Space->Flags = Group.Flags;

    Key.Type = BTRFS_FREE_SPACE_INFO_KEY;
    Status = BtrfsFindItem(Vcb, &FreeTree->Root, &Key, &Info, sizeof(Info), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    Bitmaps = BooleanFlagOn(Info.Flags, BTRFS_FREE_SPACE_USING_BITMAPS);

    Key.Type = Bitmaps ? BTRFS_FREE_SPACE_BITMAP_KEY : BTRFS_FREE_SPACE_EXTENT_KEY;
    Key.Offset = 0;
    Status = BtrfsSearch(Vcb, &FreeTree->Root, &Key, &Path);
    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);

        if (Item->Key.ObjectId >= Space->Start + Space->Length) break;
        if (Item->Key.Type == BTRFS_FREE_SPACE_EXTENT_KEY || Item->Key.Type == BTRFS_FREE_SPACE_BITMAP_KEY)
        {
            if (Item->Key.ObjectId < Space->Start || Item->Key.ObjectId + Item->Key.Offset > Space->Start + Space->Length ||
                (Item->Key.Type == BTRFS_FREE_SPACE_BITMAP_KEY) != Bitmaps)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
            if (Bitmaps)
            {
                ULONG Size;
                PUCHAR Bitmap = BtrfsPathItemData(Vcb, &Path, &Size);

                Status = (Bitmap != NULL) ? BtrfsReadFreeBitmap(Vcb, Space, &Item->Key, Bitmap, Size) : STATUS_FILE_CORRUPT_ERROR;
            }
            else
            {
                Status = BtrfsRangeAppendMerged(&Space->Free, &Space->FreeCount, &Space->FreeCapacity,
                                                Item->Key.ObjectId, Item->Key.Offset);
            }
            if (!NT_SUCCESS(Status)) break;
        }
        Status = BtrfsNextItem(Vcb, &Path);
    }
    BtrfsReleasePath(Vcb, &Path);

    if (Status == STATUS_NO_MORE_ENTRIES || Status == STATUS_NOT_FOUND || NT_SUCCESS(Status))
    {
        ULONGLONG FreeBytes = 0;
        ULONG Index;

        for (Index = 0; Index < Space->FreeCount; Index++)
            FreeBytes += Space->Free[Index].Length;
        if (Space->FreeCount != Info.ExtentCount || FreeBytes + Space->Used != Space->Length)
        {
            DPRINT1("Btrfs: block group %I64x: free space tree and used bytes disagree, it stays read-only\n", Space->Start);
            return STATUS_NOT_SUPPORTED;
        }
        return STATUS_SUCCESS;
    }
    return Status;
}

NTSTATUS
BtrfsLoadSpace(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Index, Count = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Vcb->SpaceLoaded) return STATUS_SUCCESS;

    for (Index = 0; Index < Vcb->ChunkCount; Index++)
    {
        if (Vcb->Chunks[Index]->Type & BTRFS_BLOCK_GROUP_TYPES) Count++;
    }
    if (Count == 0) return STATUS_DISK_FULL;

    Vcb->Spaces = ExAllocatePoolZero(PagedPool, Count * sizeof(BTRFS_SPACE), TAG_BTRFS);
    if (Vcb->Spaces == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Vcb->ChunkCount && NT_SUCCESS(Status); Index++)
    {
        PBTRFS_CHUNK_MAP Chunk = Vcb->Chunks[Index];

        if (!(Chunk->Type & BTRFS_BLOCK_GROUP_TYPES)) continue;

        Vcb->Spaces[Vcb->SpaceCount].Start = Chunk->Logical;
        Vcb->Spaces[Vcb->SpaceCount].Length = Chunk->Length;
        Vcb->SpaceCount++;
        Status = BtrfsLoadOneSpace(Vcb, &Vcb->Spaces[Vcb->SpaceCount - 1]);
    }

    if (!NT_SUCCESS(Status))
    {
        BtrfsFreeSpaceMaps(Vcb);
        return Status;
    }

    Vcb->SpaceLoaded = TRUE;
    return STATUS_SUCCESS;
}

VOID
BtrfsFreeSpaceMaps(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        if (Vcb->Spaces[Index].Free != NULL) ExFreePoolWithTag(Vcb->Spaces[Index].Free, TAG_BTRFS);
        if (Vcb->Spaces[Index].Pinned != NULL) ExFreePoolWithTag(Vcb->Spaces[Index].Pinned, TAG_BTRFS);
    }
    if (Vcb->Spaces != NULL) ExFreePoolWithTag(Vcb->Spaces, TAG_BTRFS);
    Vcb->Spaces = NULL;
    Vcb->SpaceCount = 0;
    Vcb->SpaceLoaded = FALSE;
}

NTSTATUS
BtrfsAllocate(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Want,
    _In_ ULONGLONG Length,
    _Out_ PULONGLONG Logical)
{
    ULONGLONG Align = (Want & BTRFS_BLOCK_GROUP_DATA) ? Vcb->SectorSize : Vcb->NodeSize;
    ULONG Index, Slot;

    ASSERT(Want == BTRFS_BLOCK_GROUP_DATA || Want == BTRFS_BLOCK_GROUP_METADATA || Want == BTRFS_BLOCK_GROUP_SYSTEM);
    ASSERT(Vcb->TransActive && Vcb->SpaceLoaded);
    ASSERT(Length != 0 && (Length % Vcb->SectorSize) == 0);
    *Logical = 0;
    Vcb->ChangeSequence++;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];

        if (!(Space->Flags & Want)) continue;

        for (Slot = 0; Slot < Space->FreeCount; Slot++)
        {
            BTRFS_RANGE Range = Space->Free[Slot];
            ULONGLONG Start = (Range.Start + Align - 1) & ~(Align - 1);
            ULONGLONG End = Range.Start + Range.Length;
            NTSTATUS Status;

            if (Start < Range.Start || Start + Length > End) continue;

            BtrfsRangeRemoveAt(Space->Free, &Space->FreeCount, Slot);
            if (Start > Range.Start)
            {
                Status = BtrfsRangeAppend(&Space->Free, &Space->FreeCount, &Space->FreeCapacity, Range.Start, Start - Range.Start);
                if (!NT_SUCCESS(Status)) return Status;
            }
            if (Start + Length < End)
            {
                Status = BtrfsRangeAppend(&Space->Free, &Space->FreeCount, &Space->FreeCapacity, Start + Length, End - Start - Length);
                if (!NT_SUCCESS(Status)) return Status;
            }
            BtrfsSortRanges(Space->Free, Space->FreeCount);

            Space->Used += Length;
            Space->Changed = TRUE;
            Vcb->BytesUsedDelta += (LONGLONG)Length;
            *Logical = Start;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_DISK_FULL;
}

ULONGLONG
BtrfsFreeBytes(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Want)
{
    ULONGLONG Free = 0;
    ULONG Index, Slot;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];

        if (!(Space->Flags & Want)) continue;
        for (Slot = 0; Slot < Space->FreeCount; Slot++)
            Free += Space->Free[Slot].Length;
    }
    return Free;
}

/* A block group created in the running transaction starts all free */
NTSTATUS
BtrfsAddSpace(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG Flags)
{
    PBTRFS_SPACE Spaces, Space;

    ASSERT(Vcb->SpaceLoaded);
    Spaces = ExAllocatePoolZero(PagedPool, (Vcb->SpaceCount + 1) * sizeof(BTRFS_SPACE), TAG_BTRFS);
    if (Spaces == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Space = &Spaces[Vcb->SpaceCount];
    Space->Start = Start;
    Space->Length = Length;
    Space->Flags = Flags;
    Space->Changed = TRUE;
    if (!NT_SUCCESS(BtrfsRangeAppend(&Space->Free, &Space->FreeCount, &Space->FreeCapacity, Start, Length)))
    {
        ExFreePoolWithTag(Spaces, TAG_BTRFS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Vcb->Spaces != NULL)
    {
        RtlCopyMemory(Spaces, Vcb->Spaces, Vcb->SpaceCount * sizeof(BTRFS_SPACE));
        ExFreePoolWithTag(Vcb->Spaces, TAG_BTRFS);
    }
    Vcb->Spaces = Spaces;
    Vcb->SpaceCount++;
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsPin(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_ ULONGLONG Length)
{
    PBTRFS_SPACE Space = BtrfsSpaceOf(Vcb, Logical);

    Vcb->ChangeSequence++;
    if (Space == NULL || Logical + Length > Space->Start + Space->Length || Space->Used < Length) return STATUS_FILE_CORRUPT_ERROR;

    Space->Used -= Length;
    Space->Changed = TRUE;
    Vcb->BytesUsedDelta -= (LONGLONG)Length;
    return BtrfsRangeAppend(&Space->Pinned, &Space->PinnedCount, &Space->PinnedCapacity, Logical, Length);
}

/* The free space a block group will have once the transaction is on disk: what is free now and what it freed */
static
NTSTATUS
BtrfsFinalFree(
    _In_ PBTRFS_SPACE Space,
    _Out_ PBTRFS_RANGE *Ranges,
    _Out_ PULONG Count)
{
    PBTRFS_RANGE All = NULL;
    ULONG Total = 0, Capacity = 0, Index, Out = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    *Ranges = NULL;
    *Count = 0;

    for (Index = 0; Index < Space->FreeCount && NT_SUCCESS(Status); Index++)
        Status = BtrfsRangeAppend(&All, &Total, &Capacity, Space->Free[Index].Start, Space->Free[Index].Length);
    for (Index = 0; Index < Space->PinnedCount && NT_SUCCESS(Status); Index++)
        Status = BtrfsRangeAppend(&All, &Total, &Capacity, Space->Pinned[Index].Start, Space->Pinned[Index].Length);
    if (!NT_SUCCESS(Status))
    {
        if (All != NULL) ExFreePoolWithTag(All, TAG_BTRFS);
        return Status;
    }

    BtrfsSortRanges(All, Total);
    for (Index = 0; Index < Total; Index++)
    {
        if (Out != 0 && All[Out - 1].Start + All[Out - 1].Length >= All[Index].Start)
        {
            ULONGLONG End = max(All[Out - 1].Start + All[Out - 1].Length, All[Index].Start + All[Index].Length);

            ASSERT(All[Out - 1].Start + All[Out - 1].Length == All[Index].Start);
            All[Out - 1].Length = End - All[Out - 1].Start;
        }
        else
        {
            All[Out++] = All[Index];
        }
    }

    *Ranges = All;
    *Count = Out;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsRewriteFreeSpace(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_SPACE Space)
{
    PBTRFS_LIVE_ROOT FreeTree = BtrfsLiveRoot(Vcb, BTRFS_FREE_SPACE_TREE_OBJECTID);
    PBTRFS_RANGE Ranges;
    ULONG Count, Index;
    BTRFS_FREE_SPACE_INFO Info;
    BTRFS_KEY Key;
    NTSTATUS Status;

    if (FreeTree == NULL) return STATUS_NOT_SUPPORTED;

    for (;;)
    {
        BTRFS_PATH Path;
        BOOLEAN Found = FALSE;

        Key.ObjectId = Space->Start;
        Key.Type = BTRFS_FREE_SPACE_EXTENT_KEY;
        Key.Offset = 0;
        Status = BtrfsSearchForWrite(Vcb, FreeTree, &Key, -1, &Path);
        if (NT_SUCCESS(Status) && Path.Slots[0] >= Path.Nodes[0]->Header->ItemCount)
        {
            Status = BtrfsNextItem(Vcb, &Path);
            if (Status == STATUS_NO_MORE_ENTRIES)
            {
                BtrfsReleasePath(Vcb, &Path);
                break;
            }
        }
        if (NT_SUCCESS(Status))
        {
            PBTRFS_ITEM Item = BtrfsPathItem(&Path);

            if ((Item->Key.Type == BTRFS_FREE_SPACE_EXTENT_KEY || Item->Key.Type == BTRFS_FREE_SPACE_BITMAP_KEY) &&
                Item->Key.ObjectId >= Space->Start && Item->Key.ObjectId < Space->Start + Space->Length)
            {
                Key = Item->Key;
                Found = TRUE;
            }
        }
        BtrfsReleasePath(Vcb, &Path);
        if (!NT_SUCCESS(Status)) return Status;
        if (!Found) break;

        Status = BtrfsDeleteItem(Vcb, FreeTree, &Key);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = BtrfsFinalFree(Space, &Ranges, &Count);
    if (!NT_SUCCESS(Status)) return Status;

    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        Key.ObjectId = Ranges[Index].Start;
        Key.Type = BTRFS_FREE_SPACE_EXTENT_KEY;
        Key.Offset = Ranges[Index].Length;
        Status = BtrfsInsertItem(Vcb, FreeTree, &Key, NULL, 0, NULL);
    }
    if (Ranges != NULL) ExFreePoolWithTag(Ranges, TAG_BTRFS);
    if (!NT_SUCCESS(Status)) return Status;

    Key.ObjectId = Space->Start;
    Key.Type = BTRFS_FREE_SPACE_INFO_KEY;
    Key.Offset = Space->Length;
    Info.ExtentCount = Count;
    Info.Flags = 0;
    return BtrfsUpdateItem(Vcb, FreeTree, &Key, &Info, sizeof(Info));
}

NTSTATUS
BtrfsWriteFreeSpaceTree(
    _In_ PBTRFS_VCB Vcb,
    _Out_ PBOOLEAN Changed)
{
    ULONG Index;
    NTSTATUS Status;

    *Changed = FALSE;
    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];

        if (!Space->Changed) continue;
        Space->Changed = FALSE;
        *Changed = TRUE;

        Status = BtrfsRewriteFreeSpace(Vcb, Space);
        if (!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsWriteBlockGroups(
    _In_ PBTRFS_VCB Vcb)
{
    PBTRFS_LIVE_ROOT Groups;
    ULONG Index;
    NTSTATUS Status;

    Groups = BtrfsLiveRoot(Vcb, (Vcb->Super.CompatRoFlags & BTRFS_COMPAT_RO_BLOCK_GROUP_TREE) ? BTRFS_BLOCK_GROUP_TREE_OBJECTID
                                                                                               : BTRFS_EXTENT_TREE_OBJECTID);
    if (Groups == NULL) return STATUS_NOT_SUPPORTED;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];
        BTRFS_BLOCK_GROUP_ITEM Group;
        BTRFS_KEY Key;

        Key.ObjectId = Space->Start;
        Key.Type = BTRFS_BLOCK_GROUP_ITEM_KEY;
        Key.Offset = Space->Length;
        Status = BtrfsFindItem(Vcb, &Groups->Root, &Key, &Group, sizeof(Group), NULL);
        if (!NT_SUCCESS(Status)) return Status;
        if (Group.Used == Space->Used) continue;

        Group.Used = Space->Used;
        Status = BtrfsUpdateItem(Vcb, Groups, &Key, &Group, sizeof(Group));
        if (!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
}

VOID
BtrfsUnpinAll(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SpaceCount; Index++)
    {
        PBTRFS_SPACE Space = &Vcb->Spaces[Index];
        PBTRFS_RANGE Ranges;
        ULONG Count;

        if (Space->PinnedCount != 0 && NT_SUCCESS(BtrfsFinalFree(Space, &Ranges, &Count)))
        {
            if (Space->Free != NULL) ExFreePoolWithTag(Space->Free, TAG_BTRFS);
            Space->Free = Ranges;
            Space->FreeCount = Count;
            Space->FreeCapacity = Count;
        }
        Space->PinnedCount = 0;
        Space->CommittedUsed = Space->Used;
        Space->Changed = FALSE;
    }
}
