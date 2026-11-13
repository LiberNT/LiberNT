/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transactions: when a volume may be changed, committing the changes and giving them up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_WRITABLE_INCOMPAT_REQUIRED (BTRFS_INCOMPAT_SKINNY_METADATA | BTRFS_INCOMPAT_NO_HOLES)
#define BTRFS_WRITABLE_COMPAT_RO (BTRFS_COMPAT_RO_FREE_SPACE_TREE | BTRFS_COMPAT_RO_FREE_SPACE_TREE_VALID | \
                                  BTRFS_COMPAT_RO_BLOCK_GROUP_TREE)
#define BTRFS_COMMIT_DELAY (-5LL * 1000 * 1000 * 10)

static
BOOLEAN
BtrfsSnapshotsExist(
    _In_ PBTRFS_VCB Vcb)
{
    PBTRFS_LIVE_ROOT RootTree = BtrfsLiveRoot(Vcb, BTRFS_ROOT_TREE_OBJECTID);
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    BOOLEAN Found = TRUE;
    NTSTATUS Status;

    if (RootTree == NULL) return TRUE;

    Key.ObjectId = BTRFS_FS_TREE_OBJECTID;
    Key.Type = BTRFS_ROOT_ITEM_KEY;
    Key.Offset = 0;

    BtrfsAcquireTreeShared(Vcb);
    Status = BtrfsSearch(Vcb, &RootTree->Root, &Key, &Path);
    if (Status == STATUS_NOT_FOUND) Found = FALSE;
    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);

        if (Item->Key.Type == BTRFS_ROOT_ITEM_KEY &&
            (Item->Key.ObjectId == BTRFS_FS_TREE_OBJECTID ||
             (Item->Key.ObjectId >= BTRFS_FIRST_FREE_OBJECTID && Item->Key.ObjectId <= BTRFS_LAST_FREE_OBJECTID)))
        {
            ULONG Size;
            PBTRFS_ROOT_ITEM Root = BtrfsPathItemData(Vcb, &Path, &Size);

            if (Root == NULL || Size < sizeof(BTRFS_ROOT_ITEM) || Root->LastSnapshot != 0) break;
        }
        Status = BtrfsNextItem(Vcb, &Path);
    }
    if (Status == STATUS_NO_MORE_ENTRIES) Found = FALSE;
    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return Found;
}

NTSTATUS
BtrfsCheckWritable(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Index;

    if (Vcb->Super.LogRoot != 0)
    {
        DPRINT1("Btrfs: the volume has a log that Linux has not replayed, it stays read-only\n");
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    if ((Vcb->Super.IncompatFlags & BTRFS_WRITABLE_INCOMPAT_REQUIRED) != BTRFS_WRITABLE_INCOMPAT_REQUIRED ||
        (Vcb->Super.CompatRoFlags & ~BTRFS_WRITABLE_COMPAT_RO) != 0 ||
        (Vcb->Super.CompatRoFlags & (BTRFS_COMPAT_RO_FREE_SPACE_TREE | BTRFS_COMPAT_RO_FREE_SPACE_TREE_VALID)) !=
            (BTRFS_COMPAT_RO_FREE_SPACE_TREE | BTRFS_COMPAT_RO_FREE_SPACE_TREE_VALID))
    {
        DPRINT1("Btrfs: the volume uses features (%I64x, read-only %I64x) this driver does not change, it stays read-only\n",
                Vcb->Super.IncompatFlags, Vcb->Super.CompatRoFlags);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }

    for (Index = 0; Index < Vcb->ChunkCount; Index++)
    {
        if (Vcb->Chunks[Index]->Type & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10))
            return STATUS_MEDIA_WRITE_PROTECTED;
    }

    if (BtrfsSnapshotsExist(Vcb))
    {
        DPRINT1("Btrfs: the volume has snapshots, it stays read-only\n");
        return STATUS_MEDIA_WRITE_PROTECTED;
    }

    if (BtrfsDeviceControl(Vcb->TargetDeviceObject, IOCTL_DISK_IS_WRITABLE, NULL, 0, TRUE) == STATUS_MEDIA_WRITE_PROTECTED)
        return STATUS_MEDIA_WRITE_PROTECTED;

    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsStartTransaction(
    _In_ PBTRFS_VCB Vcb)
{
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));

    if (!Vcb->Writable || Vcb->Dismounted) return STATUS_MEDIA_WRITE_PROTECTED;
    if (!Vcb->TransActive)
    {
        Status = BtrfsLoadSpace(Vcb);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Btrfs: the free space of the volume cannot be followed (%lx), it stays read-only\n", Status);
            Vcb->Writable = FALSE;
            return STATUS_MEDIA_WRITE_PROTECTED;
        }

        Vcb->TransId = Vcb->Super.Generation + 1;
        Vcb->BytesUsedDelta = 0;
        Vcb->TransActive = TRUE;
    }

    return BtrfsReserveSpace(Vcb, BTRFS_BLOCK_GROUP_METADATA, 0);
}

static
NTSTATUS
BtrfsUpdateRootItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live)
{
    PBTRFS_LIVE_ROOT RootTree = BtrfsLiveRoot(Vcb, BTRFS_ROOT_TREE_OBJECTID);
    BTRFS_ROOT_ITEM_V2 Item;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    ULONG Size = 0;
    NTSTATUS Status;

    if (RootTree == NULL) return STATUS_NOT_SUPPORTED;

    Key.ObjectId = Live->Root.Id;
    Key.Type = BTRFS_ROOT_ITEM_KEY;
    Key.Offset = 0;

    Status = BtrfsSearch(Vcb, &RootTree->Root, &Key, &Path);
    if (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Found = BtrfsPathItem(&Path);
        PVOID Data;

        if (Found->Key.ObjectId != Live->Root.Id || Found->Key.Type != BTRFS_ROOT_ITEM_KEY)
        {
            Status = STATUS_NOT_FOUND;
        }
        else
        {
            Key = Found->Key;
            Data = BtrfsPathItemData(Vcb, &Path, &Size);
            if (Data == NULL || Size < sizeof(BTRFS_ROOT_ITEM) || Size > sizeof(Item))
                Status = STATUS_FILE_CORRUPT_ERROR;
            else
                RtlCopyMemory(&Item, Data, Size);
        }
    }
    BtrfsReleasePath(Vcb, &Path);
    if (!NT_SUCCESS(Status)) return Status;

    Item.Legacy.ByteNr = Live->Root.ByteNr;
    Item.Legacy.Level = Live->Root.Level;
    Item.Legacy.Generation = Vcb->TransId;
    Item.Legacy.BytesUsed = (ULONGLONG)((LONGLONG)Item.Legacy.BytesUsed + Live->BytesUsedDelta);
    if (Size >= sizeof(BTRFS_ROOT_ITEM_V2))
    {
        Item.GenerationV2 = Vcb->TransId;
        if (Live->Root.Id == BTRFS_FS_TREE_OBJECTID || Live->Root.Id >= BTRFS_FIRST_FREE_OBJECTID)
        {
            Item.CTransId = Vcb->TransId;
            Item.CTime = BtrfsNow();
        }
    }

    Status = BtrfsUpdateItem(Vcb, RootTree, &Key, &Item, Size);
    if (NT_SUCCESS(Status))
    {
        Live->BytesUsedDelta = 0;
        Live->Dirty = FALSE;
    }
    return Status;
}

static
NTSTATUS
BtrfsUpdateRootItems(
    _In_ PBTRFS_VCB Vcb,
    _Out_ PBOOLEAN Changed)
{
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    *Changed = FALSE;
    for (Entry = Vcb->LiveRoots.Flink; Entry != &Vcb->LiveRoots; Entry = Entry->Flink)
    {
        PBTRFS_LIVE_ROOT Live = CONTAINING_RECORD(Entry, BTRFS_LIVE_ROOT, Links);

        if (Live->Root.Id == BTRFS_ROOT_TREE_OBJECTID || Live->Root.Id == BTRFS_CHUNK_TREE_OBJECTID) continue;
        if (!Live->Dirty && Live->BytesUsedDelta == 0) continue;

        *Changed = TRUE;
        Status = BtrfsUpdateRootItem(Vcb, Live);
        if (!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
}

/* Where a tree stands as of this commit, as its root item says */
static
VOID
BtrfsBackupTree(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Id,
    _Out_ PULONGLONG ByteNr,
    _Out_ PULONGLONG Generation,
    _Out_ PUCHAR Level)
{
    PBTRFS_LIVE_ROOT RootTree = BtrfsLiveRoot(Vcb, BTRFS_ROOT_TREE_OBJECTID);
    BTRFS_ROOT_ITEM Item;
    BTRFS_KEY Key;

    *ByteNr = 0;
    *Generation = 0;
    *Level = 0;

    Key.ObjectId = Id;
    Key.Type = BTRFS_ROOT_ITEM_KEY;
    Key.Offset = 0;
    if (RootTree != NULL && NT_SUCCESS(BtrfsFindItem(Vcb, &RootTree->Root, &Key, &Item, sizeof(Item), NULL)))
    {
        *ByteNr = Item.ByteNr;
        *Generation = Item.Generation;
        *Level = Item.Level;
    }
}

/* The superblock keeps the roots of its last commits, the newest after the one it held before */
static
VOID
BtrfsBackupRoots(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_SUPERBLOCK Super)
{
    PBTRFS_ROOT_BACKUP Backups = (PBTRFS_ROOT_BACKUP)Super->SuperRoots;
    PBTRFS_ROOT_BACKUP Backup;
    ULONG Index, Next = 0;

    for (Index = 0; Index < BTRFS_NUM_BACKUP_ROOTS; Index++)
    {
        if (Backups[Index].TreeRootGeneration == Vcb->Super.Generation)
        {
            Next = (Index + 1) % BTRFS_NUM_BACKUP_ROOTS;
            break;
        }
    }

    Backup = &Backups[Next];
    RtlZeroMemory(Backup, sizeof(*Backup));
    Backup->TreeRoot = Super->Root;
    Backup->TreeRootGeneration = Super->Generation;
    Backup->TreeRootLevel = Super->RootLevel;
    Backup->ChunkRoot = Super->ChunkRoot;
    Backup->ChunkRootGeneration = Super->ChunkRootGeneration;
    Backup->ChunkRootLevel = Super->ChunkRootLevel;
    BtrfsBackupTree(Vcb, BTRFS_EXTENT_TREE_OBJECTID, &Backup->ExtentRoot, &Backup->ExtentRootGeneration, &Backup->ExtentRootLevel);
    BtrfsBackupTree(Vcb, BTRFS_FS_TREE_OBJECTID, &Backup->FsRoot, &Backup->FsRootGeneration, &Backup->FsRootLevel);
    BtrfsBackupTree(Vcb, BTRFS_DEV_TREE_OBJECTID, &Backup->DevRoot, &Backup->DevRootGeneration, &Backup->DevRootLevel);
    BtrfsBackupTree(Vcb, BTRFS_CSUM_TREE_OBJECTID, &Backup->CsumRoot, &Backup->CsumRootGeneration, &Backup->CsumRootLevel);
    Backup->TotalBytes = Super->TotalBytes;
    Backup->BytesUsed = Super->BytesUsed;
    Backup->DeviceCount = Super->DeviceCount;
}

static
NTSTATUS
BtrfsWriteSuperblocks(
    _In_ PBTRFS_VCB Vcb)
{
    static const ULONGLONG Mirrors[] = { BTRFS_SUPERBLOCK_OFFSET, 0x4000000ULL, 0x4000000000ULL };
    PBTRFS_LIVE_ROOT RootTree = BtrfsLiveRoot(Vcb, BTRFS_ROOT_TREE_OBJECTID);
    PBTRFS_LIVE_ROOT ChunkTree = BtrfsLiveRoot(Vcb, BTRFS_CHUNK_TREE_OBJECTID);
    PBTRFS_SUPERBLOCK Super;
    UCHAR Digest[BTRFS_CSUM_SIZE];
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    if (RootTree == NULL || ChunkTree == NULL) return STATUS_NOT_SUPPORTED;

    Super = ExAllocatePoolUninitialized(PagedPool, sizeof(BTRFS_SUPERBLOCK), TAG_BTRFS);
    if (Super == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Super, &Vcb->Super, sizeof(*Super));
    Super->Generation = Vcb->TransId;
    Super->Root = RootTree->Root.ByteNr;
    Super->RootLevel = RootTree->Root.Level;
    if (ChunkTree->Root.ByteNr != Vcb->Super.ChunkRoot)
    {
        Super->ChunkRoot = ChunkTree->Root.ByteNr;
        Super->ChunkRootLevel = ChunkTree->Root.Level;
        Super->ChunkRootGeneration = Vcb->TransId;
    }
    Super->BytesUsed = (ULONGLONG)((LONGLONG)Super->BytesUsed + Vcb->BytesUsedDelta);
    BtrfsBackupRoots(Vcb, Super);

    for (Index = 0; Index < RTL_NUMBER_OF(Mirrors) && NT_SUCCESS(Status); Index++)
    {
        if (Index != 0 && (Vcb->PartitionLength == 0 || Mirrors[Index] + sizeof(*Super) > Vcb->PartitionLength)) break;

        Super->ByteNr = Mirrors[Index];
        BtrfsComputeChecksum(Super->ChecksumType, Super->FsId, BTRFS_SUPERBLOCK_SIZE - BTRFS_CSUM_SIZE, Digest);
        RtlCopyMemory(Super->Checksum, Digest, BTRFS_CSUM_SIZE);
        Status = BtrfsWriteDevice(Vcb->TargetDeviceObject, Mirrors[Index], Super, sizeof(*Super));
    }

    if (NT_SUCCESS(Status))
    {
        Super->ByteNr = BTRFS_SUPERBLOCK_OFFSET;
        RtlCopyMemory(&Vcb->Super, Super, sizeof(*Super));
    }
    ExFreePoolWithTag(Super, TAG_BTRFS);
    return Status;
}

NTSTATUS
BtrfsCommit(
    _In_ PBTRFS_VCB Vcb)
{
    ULONG Round;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    if (!Vcb->TransActive) return STATUS_SUCCESS;

    for (Round = 0; Round < 64; Round++)
    {
        BOOLEAN Refs = FALSE, Space = FALSE, Roots = FALSE;

        Status = BtrfsRunPendingRefs(Vcb, &Refs);
        if (NT_SUCCESS(Status)) Status = BtrfsWriteFreeSpaceTree(Vcb, &Space);
        if (NT_SUCCESS(Status)) Status = BtrfsWriteBlockGroups(Vcb);
        if (NT_SUCCESS(Status) && IsListEmpty(&Vcb->PendingRefs)) Status = BtrfsUpdateRootItems(Vcb, &Roots);
        if (!NT_SUCCESS(Status)) break;

        if (!Refs && !Space && !Roots && IsListEmpty(&Vcb->PendingRefs)) break;
    }
    if (NT_SUCCESS(Status) && Round == 64) Status = STATUS_INTERNAL_ERROR;

    if (NT_SUCCESS(Status)) Status = BtrfsWriteNodes(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsFlushDevice(Vcb->TargetDeviceObject);
    if (NT_SUCCESS(Status)) Status = BtrfsWriteSuperblocks(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsFlushDevice(Vcb->TargetDeviceObject);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Btrfs: the transaction %I64u could not be committed (%lx)\n", Vcb->TransId, Status);
        BtrfsAbortTransaction(Vcb);
        return Status;
    }

    BtrfsUnpinAll(Vcb);
    Vcb->BytesUsedDelta = 0;
    Vcb->TransActive = FALSE;
    return STATUS_SUCCESS;
}

/* The root tree and the chunk tree hang from the superblock rather than from a root item */
static
NTSTATUS
BtrfsAddSuperblockRoots(
    _Inout_ PBTRFS_VCB Vcb)
{
    PBTRFS_LIVE_ROOT RootTree, ChunkTree;

    RootTree = ExAllocatePoolZero(NonPagedPool, sizeof(BTRFS_LIVE_ROOT), TAG_BTRFS);
    ChunkTree = ExAllocatePoolZero(NonPagedPool, sizeof(BTRFS_LIVE_ROOT), TAG_BTRFS);
    if (RootTree == NULL || ChunkTree == NULL)
    {
        if (RootTree != NULL) ExFreePoolWithTag(RootTree, TAG_BTRFS);
        if (ChunkTree != NULL) ExFreePoolWithTag(ChunkTree, TAG_BTRFS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RootTree->Root.Id = BTRFS_ROOT_TREE_OBJECTID;
    RootTree->Root.ByteNr = Vcb->Super.Root;
    RootTree->Root.Level = Vcb->Super.RootLevel;
    ChunkTree->Root.Id = BTRFS_CHUNK_TREE_OBJECTID;
    ChunkTree->Root.ByteNr = Vcb->Super.ChunkRoot;
    ChunkTree->Root.Level = Vcb->Super.ChunkRootLevel;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    InsertTailList(&Vcb->LiveRoots, &RootTree->Links);
    InsertTailList(&Vcb->LiveRoots, &ChunkTree->Links);
    ExReleaseFastMutex(&Vcb->NodeCacheLock);
    return STATUS_SUCCESS;
}

/* A change that failed halfway leaves the trees inconsistent: the transaction is given up rather than committed */
NTSTATUS
BtrfsFinishChange(
    _In_ PBTRFS_VCB Vcb,
    _In_ NTSTATUS Status,
    _In_ ULONG Sequence)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));

    if (NT_SUCCESS(Status))
    {
        if (Vcb->TransActive && Vcb->ChangeSequence != Sequence) BtrfsQueueCommit(Vcb);
        return Status;
    }

    if (Vcb->TransActive && Vcb->ChangeSequence != Sequence)
    {
        DPRINT1("Btrfs: a change failed (%lx), the volume goes back to its last commit and stays read-only\n", Status);
        BtrfsAbortTransaction(Vcb);
    }
    return Status;
}

/* A transaction that went wrong leaves the volume as it was at the last commit, and read-only */
VOID
BtrfsAbortTransaction(
    _In_ PBTRFS_VCB Vcb)
{
    LIST_ENTRY Nodes, Roots;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    InitializeListHead(&Nodes);
    InitializeListHead(&Roots);

    while (!IsListEmpty(&Vcb->PendingRefs))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Vcb->PendingRefs), BTRFS_PENDING_REF, Links), TAG_BTRFS);

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    while (!IsListEmpty(&Vcb->DirtyNodes))
    {
        PBTRFS_NODE Node = CONTAINING_RECORD(RemoveHeadList(&Vcb->DirtyNodes), BTRFS_NODE, LruLinks);

        Node->Deleted = TRUE;
        if (--Node->References == 0) InsertTailList(&Nodes, &Node->LruLinks);
    }
    while (!IsListEmpty(&Vcb->LiveRoots))
        InsertTailList(&Roots, RemoveHeadList(&Vcb->LiveRoots));
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    while (!IsListEmpty(&Nodes))
    {
        PBTRFS_NODE Node = CONTAINING_RECORD(RemoveHeadList(&Nodes), BTRFS_NODE, LruLinks);

        ExFreePoolWithTag(Node->Header, TAG_BTRFS_NODE);
        ExFreePoolWithTag(Node, TAG_BTRFS_NODE);
    }
    while (!IsListEmpty(&Roots))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Roots), BTRFS_LIVE_ROOT, Links), TAG_BTRFS);

    if (!NT_SUCCESS(BtrfsAddSuperblockRoots(Vcb))) DPRINT1("Btrfs: the trees of the last commit could not be followed again\n");

    BtrfsFreeSpaceMaps(Vcb);
    Vcb->BytesUsedDelta = 0;
    Vcb->TransActive = FALSE;
    Vcb->Writable = FALSE;
}

static
VOID
NTAPI
BtrfsCommitWorker(
    _In_ PVOID Context)
{
    PBTRFS_VCB Vcb = Context;

    InterlockedExchange(&Vcb->CommitQueued, 0);

    BtrfsAcquireTreeExclusive(Vcb);
    if (Vcb->TransActive && !Vcb->Dismounted) BtrfsCommit(Vcb);
    BtrfsReleaseTree(Vcb);

    BtrfsDereferenceVcb(Vcb);
}

static
VOID
NTAPI
BtrfsCommitDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PBTRFS_VCB Vcb = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    ExQueueWorkItem(&Vcb->CommitWork, DelayedWorkQueue);
}

/* Changes made through mapped views and the lazy writer reach the disk a few seconds later */
VOID
BtrfsQueueCommit(
    _In_ PBTRFS_VCB Vcb)
{
    LARGE_INTEGER Delay;

    if (InterlockedCompareExchange(&Vcb->CommitQueued, 1, 0) != 0) return;

    if (Vcb->CommitWork.WorkerRoutine == NULL)
    {
        KeInitializeTimer(&Vcb->CommitTimer);
        KeInitializeDpc(&Vcb->CommitDpc, BtrfsCommitDpc, Vcb);
        ExInitializeWorkItem(&Vcb->CommitWork, BtrfsCommitWorker, Vcb);
    }

    BtrfsReferenceVcb(Vcb);
    Delay.QuadPart = BTRFS_COMMIT_DELAY;
    KeSetTimer(&Vcb->CommitTimer, Delay, &Vcb->CommitDpc);
}

NTSTATUS
BtrfsInitializeTransactions(
    _Inout_ PBTRFS_VCB Vcb)
{
    PBTRFS_NODE Node;
    NTSTATUS Status;

    ASSERT(IsListEmpty(&Vcb->LiveRoots));

    Status = BtrfsAddSuperblockRoots(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    Status = BtrfsReadNode(Vcb, Vcb->RootTree.ByteNr, Vcb->RootTree.Level, &Node);
    if (!NT_SUCCESS(Status)) return Status;
    RtlCopyMemory(Vcb->ChunkTreeUuid, Node->Header->ChunkTreeUuid, BTRFS_UUID_SIZE);
    BtrfsReleaseNode(Vcb, Node);
    return STATUS_SUCCESS;
}

VOID
BtrfsUninitializeTransactions(
    _Inout_ PBTRFS_VCB Vcb)
{
    BtrfsAcquireTreeExclusive(Vcb);
    if (Vcb->TransActive)
    {
        DPRINT1("Btrfs: the volume goes away with transaction %I64u not committed\n", Vcb->TransId);
        BtrfsAbortTransaction(Vcb);
    }
    ASSERT(IsListEmpty(&Vcb->DirtyNodes) && IsListEmpty(&Vcb->PendingRefs));

    while (!IsListEmpty(&Vcb->LiveRoots))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Vcb->LiveRoots), BTRFS_LIVE_ROOT, Links), TAG_BTRFS);
    BtrfsFreeSpaceMaps(Vcb);
    BtrfsReleaseTree(Vcb);
}

/* The data the cache holds for every file of the volume goes to the disk first, then the trees that describe it */
NTSTATUS
BtrfsFlushVolume(
    _In_ PBTRFS_VCB Vcb)
{
    IO_STATUS_BLOCK IoStatus;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));

    for (Entry = Vcb->FcbList.Flink; Entry != &Vcb->FcbList; Entry = Entry->Flink)
    {
        PBTRFS_FCB Fcb = CONTAINING_RECORD(Entry, BTRFS_FCB, VcbLinks);

        if (Fcb->Directory || Fcb->SectionObjectPointers.DataSectionObject == NULL) continue;
        CcFlushCache(&Fcb->SectionObjectPointers, NULL, 0, &IoStatus);
        if (!NT_SUCCESS(IoStatus.Status) && NT_SUCCESS(Status)) Status = IoStatus.Status;
    }

    BtrfsAcquireTreeExclusive(Vcb);
    if (Vcb->TransActive)
    {
        NTSTATUS Commit = BtrfsCommit(Vcb);

        if (NT_SUCCESS(Status)) Status = Commit;
    }
    BtrfsReleaseTree(Vcb);
    return Status;
}
