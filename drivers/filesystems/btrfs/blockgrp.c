/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     New block groups in the device space no chunk uses yet
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_DEVICE_RESERVED      0x100000ULL
#define BTRFS_MAX_DATA_STRIPE      0x40000000ULL
#define BTRFS_MAX_METADATA_STRIPE  0x10000000ULL
#define BTRFS_LARGE_VOLUME         (50ULL * 0x40000000ULL)
#define BTRFS_METADATA_RESERVE     128

typedef struct _BTRFS_HOLE_SEARCH
{
    ULONGLONG Cursor;
    ULONGLONG BestStart;
    ULONGLONG BestLength;
    ULONGLONG Wanted;
} BTRFS_HOLE_SEARCH, *PBTRFS_HOLE_SEARCH;

static
VOID
BtrfsConsiderHole(
    _Inout_ PBTRFS_HOLE_SEARCH Search,
    _In_ ULONGLONG End)
{
    ULONGLONG Start = (Search->Cursor + BTRFS_STRIPE_LEN - 1) & ~(BTRFS_STRIPE_LEN - 1);

    if (End > Start && End - Start > Search->BestLength && Search->BestLength < Search->Wanted)
    {
        Search->BestStart = Start;
        Search->BestLength = End - Start;
    }
}

static
VOID
BtrfsPassUsed(
    _Inout_ PBTRFS_HOLE_SEARCH Search,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    if (Start > Search->Cursor) BtrfsConsiderHole(Search, Start);
    if (Start + Length > Search->Cursor) Search->Cursor = Start + Length;
}

/* The device space past the first megabyte that no device extent and no superblock copy takes */
static
NTSTATUS
BtrfsFindDeviceHole(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Wanted,
    _Out_ PULONGLONG Start,
    _Out_ PULONGLONG Length)
{
    static const ULONGLONG Mirrors[] = { 0x4000000ULL, 0x4000000000ULL };
    PBTRFS_LIVE_ROOT DevTree = BtrfsLiveRoot(Vcb, BTRFS_DEV_TREE_OBJECTID);
    ULONGLONG DeviceEnd = Vcb->Super.DevItem.TotalBytes;
    BTRFS_HOLE_SEARCH Search;
    ULONG Mirror = 0;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    *Start = 0;
    *Length = 0;
    if (DevTree == NULL) return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->PartitionLength != 0) DeviceEnd = min(DeviceEnd, Vcb->PartitionLength);

    RtlZeroMemory(&Search, sizeof(Search));
    Search.Cursor = BTRFS_DEVICE_RESERVED;
    Search.Wanted = Wanted;

    Key.ObjectId = Vcb->DeviceId;
    Key.Type = BTRFS_DEV_EXTENT_KEY;
    Key.Offset = 0;

    Status = BtrfsSearch(Vcb, &DevTree->Root, &Key, &Path);
    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        PBTRFS_DEV_EXTENT Extent;
        ULONG Size;

        if (Item->Key.ObjectId != Vcb->DeviceId || Item->Key.Type != BTRFS_DEV_EXTENT_KEY) break;
        Extent = BtrfsPathItemData(Vcb, &Path, &Size);
        if (Extent == NULL || Size < sizeof(*Extent))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        while (Mirror < RTL_NUMBER_OF(Mirrors) && Mirrors[Mirror] < Item->Key.Offset)
        {
            BtrfsPassUsed(&Search, Mirrors[Mirror], BTRFS_STRIPE_LEN);
            Mirror++;
        }
        BtrfsPassUsed(&Search, Item->Key.Offset, Extent->Length);
        Status = BtrfsNextItem(Vcb, &Path);
    }
    BtrfsReleasePath(Vcb, &Path);
    if (Status != STATUS_NOT_FOUND && Status != STATUS_NO_MORE_ENTRIES && !NT_SUCCESS(Status)) return Status;

    for (; Mirror < RTL_NUMBER_OF(Mirrors) && Mirrors[Mirror] < DeviceEnd; Mirror++)
        BtrfsPassUsed(&Search, Mirrors[Mirror], BTRFS_STRIPE_LEN);
    BtrfsConsiderHole(&Search, DeviceEnd);

    *Start = Search.BestStart;
    *Length = Search.BestLength & ~(BTRFS_STRIPE_LEN - 1);
    return STATUS_SUCCESS;
}

/* New chunks of a kind take the profile of the chunks of that kind already there */
static
ULONGLONG
BtrfsProfileOf(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Type)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->ChunkCount; Index++)
    {
        if ((Vcb->Chunks[Index]->Type & BTRFS_BLOCK_GROUP_TYPES) == Type)
            return Vcb->Chunks[Index]->Type & BTRFS_BLOCK_GROUP_PROFILES;
    }
    return 0;
}

static
NTSTATUS
BtrfsInsertChunkItems(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_ const BTRFS_CHUNK *Chunk,
    _In_ ULONG ChunkSize,
    _In_ ULONGLONG StripeLength)
{
    PBTRFS_LIVE_ROOT ChunkTree = BtrfsLiveRoot(Vcb, BTRFS_CHUNK_TREE_OBJECTID);
    PBTRFS_LIVE_ROOT DevTree = BtrfsLiveRoot(Vcb, BTRFS_DEV_TREE_OBJECTID);
    PBTRFS_LIVE_ROOT FreeTree = BtrfsLiveRoot(Vcb, BTRFS_FREE_SPACE_TREE_OBJECTID);
    PBTRFS_LIVE_ROOT Groups = BtrfsLiveRoot(Vcb, (Vcb->Super.CompatRoFlags & BTRFS_COMPAT_RO_BLOCK_GROUP_TREE) ?
                                                 BTRFS_BLOCK_GROUP_TREE_OBJECTID : BTRFS_EXTENT_TREE_OBJECTID);
    ULONGLONG DeviceBytes = StripeLength * Chunk->StripeCount;
    BTRFS_BLOCK_GROUP_ITEM Group;
    BTRFS_FREE_SPACE_INFO Info;
    BTRFS_DEV_EXTENT Extent;
    BTRFS_DEV_ITEM Device;
    BTRFS_KEY Key;
    USHORT Stripe;
    NTSTATUS Status;

    if (ChunkTree == NULL || DevTree == NULL || FreeTree == NULL || Groups == NULL) return STATUS_FILE_CORRUPT_ERROR;

    Key.ObjectId = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
    Key.Type = BTRFS_CHUNK_ITEM_KEY;
    Key.Offset = Logical;
    Status = BtrfsInsertItem(Vcb, ChunkTree, &Key, Chunk, ChunkSize, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Key.ObjectId = BTRFS_DEV_ITEMS_OBJECTID;
    Key.Type = BTRFS_DEV_ITEM_KEY;
    Key.Offset = Vcb->DeviceId;
    Status = BtrfsFindItem(Vcb, &ChunkTree->Root, &Key, &Device, sizeof(Device), NULL);
    if (!NT_SUCCESS(Status)) return (Status == STATUS_NOT_FOUND) ? STATUS_FILE_CORRUPT_ERROR : Status;
    Device.BytesUsed += DeviceBytes;
    Status = BtrfsUpdateItem(Vcb, ChunkTree, &Key, &Device, sizeof(Device));
    if (!NT_SUCCESS(Status)) return Status;
    Vcb->Super.DevItem.BytesUsed = Device.BytesUsed;

    for (Stripe = 0; Stripe < Chunk->StripeCount; Stripe++)
    {
        Extent.ChunkTree = BTRFS_CHUNK_TREE_OBJECTID;
        Extent.ChunkObjectId = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
        Extent.ChunkOffset = Logical;
        Extent.Length = StripeLength;
        RtlCopyMemory(Extent.ChunkTreeUuid, Vcb->ChunkTreeUuid, BTRFS_UUID_SIZE);

        Key.ObjectId = Vcb->DeviceId;
        Key.Type = BTRFS_DEV_EXTENT_KEY;
        Key.Offset = Chunk->Stripes[Stripe].Offset;
        Status = BtrfsInsertItem(Vcb, DevTree, &Key, &Extent, sizeof(Extent), NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Group.Used = 0;
    Group.ChunkObjectId = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
    Group.Flags = Chunk->Type;
    Key.ObjectId = Logical;
    Key.Type = BTRFS_BLOCK_GROUP_ITEM_KEY;
    Key.Offset = Chunk->Length;
    Status = BtrfsInsertItem(Vcb, Groups, &Key, &Group, sizeof(Group), NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Info.ExtentCount = 0;
    Info.Flags = 0;
    Key.Type = BTRFS_FREE_SPACE_INFO_KEY;
    return BtrfsInsertItem(Vcb, FreeTree, &Key, &Info, sizeof(Info), NULL);
}

/* A chunk the size Linux gives it: a tenth of the volume, within the limit of its kind */
static
NTSTATUS
BtrfsCreateBlockGroup(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Type)
{
    UCHAR Buffer[BTRFS_CHUNK_FIXED_SIZE + 2 * sizeof(BTRFS_STRIPE)];
    PBTRFS_CHUNK Chunk = (PBTRFS_CHUNK)Buffer;
    ULONGLONG Profile = BtrfsProfileOf(Vcb, Type);
    ULONGLONG MaxStripe, Stripe, HoleStart, HoleLength, Logical = 0;
    USHORT Copies, Index;
    ULONG ChunkSize;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    ASSERT(Vcb->TransActive && Vcb->SpaceLoaded);

    /* TODO: system chunks, which also go into the superblock's chunk array, are not created */
    if (Type & BTRFS_BLOCK_GROUP_SYSTEM) return STATUS_DISK_FULL;
    if (Profile & ~BTRFS_BLOCK_GROUP_DUP) return STATUS_DISK_FULL;
    Copies = (Profile & BTRFS_BLOCK_GROUP_DUP) ? 2 : 1;

    if (Type & BTRFS_BLOCK_GROUP_DATA)
        MaxStripe = BTRFS_MAX_DATA_STRIPE;
    else
        MaxStripe = (Vcb->Super.TotalBytes > BTRFS_LARGE_VOLUME) ? BTRFS_MAX_DATA_STRIPE : BTRFS_MAX_METADATA_STRIPE;
    MaxStripe = min(MaxStripe, Vcb->Super.TotalBytes / 10) & ~(BTRFS_STRIPE_LEN - 1);
    if (MaxStripe < BTRFS_STRIPE_LEN) MaxStripe = BTRFS_STRIPE_LEN;

    Status = BtrfsFindDeviceHole(Vcb, MaxStripe * Copies, &HoleStart, &HoleLength);
    if (!NT_SUCCESS(Status)) return Status;
    Stripe = min(MaxStripe, (HoleLength / Copies) & ~(BTRFS_STRIPE_LEN - 1));
    if (Stripe < BTRFS_STRIPE_LEN) return STATUS_DISK_FULL;

    for (Index = 0; Index < Vcb->ChunkCount; Index++)
        Logical = max(Logical, Vcb->Chunks[Index]->Logical + Vcb->Chunks[Index]->Length);

    ChunkSize = BTRFS_CHUNK_FIXED_SIZE + Copies * sizeof(BTRFS_STRIPE);
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Chunk->Length = Stripe;
    Chunk->Owner = BTRFS_EXTENT_TREE_OBJECTID;
    Chunk->StripeLength = BTRFS_STRIPE_LEN;
    Chunk->Type = Type | Profile;
    Chunk->IoAlign = (ULONG)BTRFS_STRIPE_LEN;
    Chunk->IoWidth = (ULONG)BTRFS_STRIPE_LEN;
    Chunk->SectorSize = Vcb->SectorSize;
    Chunk->StripeCount = Copies;
    Chunk->SubStripes = 1;
    for (Index = 0; Index < Copies; Index++)
    {
        Chunk->Stripes[Index].DeviceId = Vcb->DeviceId;
        Chunk->Stripes[Index].Offset = HoleStart + Index * Stripe;
        RtlCopyMemory(Chunk->Stripes[Index].DeviceUuid, Vcb->Super.DevItem.Uuid, BTRFS_UUID_SIZE);
    }

    Status = BtrfsAddChunk(Vcb, Logical, Chunk, ChunkSize);
    if (NT_SUCCESS(Status)) Status = BtrfsAddSpace(Vcb, Logical, Stripe, Chunk->Type);
    if (NT_SUCCESS(Status)) Status = BtrfsInsertChunkItems(Vcb, Logical, Chunk, ChunkSize, Stripe);
    return Status;
}

/* Space for a change and for the trees its commit rewrites is found before the change starts, not halfway through */
NTSTATUS
BtrfsReserveSpace(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Want,
    _In_ ULONGLONG Bytes)
{
    BOOLEAN Mixed = BooleanFlagOn(Vcb->Super.IncompatFlags, BTRFS_INCOMPAT_MIXED_GROUPS);
    ULONGLONG Reserve = (ULONGLONG)BTRFS_METADATA_RESERVE * Vcb->NodeSize;
    ULONGLONG Type = Mixed ? (BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA) : Want;
    ULONGLONG Needed = Bytes + ((Mixed || Want == BTRFS_BLOCK_GROUP_METADATA) ? Reserve : 0);
    ULONG Sequence = Vcb->ChangeSequence;
    NTSTATUS Status;

    ASSERT(Want == BTRFS_BLOCK_GROUP_DATA || Want == BTRFS_BLOCK_GROUP_METADATA);
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    ASSERT(Vcb->TransActive && Vcb->SpaceLoaded);

    while (BtrfsFreeBytes(Vcb, Want) < Needed)
    {
        Status = BtrfsCreateBlockGroup(Vcb, Type);
        if (Status == STATUS_DISK_FULL) break;
        if (!NT_SUCCESS(Status)) return Status;
    }

    /* A block group is a whole change of its own: a change that gives up after it does not take it back */
    Vcb->ChangeSequence = Sequence;
    if (Want == BTRFS_BLOCK_GROUP_DATA && BtrfsFreeBytes(Vcb, Want) < Bytes + (Mixed ? Reserve / 2 : 0)) return STATUS_DISK_FULL;
    return STATUS_SUCCESS;
}
