/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tree nodes, their cache and the B-tree walk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
VOID
BtrfsFreeNode(
    _In_ PBTRFS_NODE Node)
{
    ASSERT(Node->References == 0 && !Node->Cached);
    ExFreePoolWithTag(Node->Header, TAG_BTRFS_NODE);
    ExFreePoolWithTag(Node, TAG_BTRFS_NODE);
}

static
BOOLEAN
BtrfsNodeValid(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_HEADER Header,
    _In_ ULONGLONG Logical,
    _In_ UCHAR ExpectedLevel)
{
    ULONG Capacity, Entry;

    if (!BtrfsVerifyChecksum(Vcb->ChecksumType, Header->FsId, Vcb->NodeSize - BTRFS_CSUM_SIZE, Header->Checksum)) return FALSE;
    if (Header->ByteNr != Logical) return FALSE;
    if (RtlCompareMemory(Header->FsId, Vcb->HeaderFsId, BTRFS_UUID_SIZE) != BTRFS_UUID_SIZE) return FALSE;
    if (Header->Level != ExpectedLevel) return FALSE;

    Entry = (Header->Level == 0) ? sizeof(BTRFS_ITEM) : sizeof(BTRFS_KEY_POINTER);
    Capacity = (Vcb->NodeSize - sizeof(BTRFS_HEADER)) / Entry;
    if (Header->ItemCount > Capacity) return FALSE;
    if (Header->Level != 0 && Header->ItemCount == 0) return FALSE;

    return TRUE;
}

NTSTATUS
BtrfsReadNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_ UCHAR ExpectedLevel,
    _Out_ PBTRFS_NODE *Node)
{
    PBTRFS_NODE Found = NULL, New;
    LIST_ENTRY Evicted;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    ULONG Mirror, Mirrors;

    *Node = NULL;
    if (ExpectedLevel >= BTRFS_MAX_LEVEL || (Logical % Vcb->SectorSize) != 0) return STATUS_FILE_CORRUPT_ERROR;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    for (Entry = Vcb->DirtyNodes.Flink; Entry != &Vcb->DirtyNodes; Entry = Entry->Flink)
    {
        PBTRFS_NODE Candidate = CONTAINING_RECORD(Entry, BTRFS_NODE, LruLinks);

        if (Candidate->Logical == Logical)
        {
            ASSERT(Candidate->Dirty && !Candidate->Deleted);
            Found = Candidate;
            Found->References++;
            break;
        }
    }
    for (Entry = Vcb->NodeLru.Flink; Found == NULL && Entry != &Vcb->NodeLru; Entry = Entry->Flink)
    {
        PBTRFS_NODE Candidate = CONTAINING_RECORD(Entry, BTRFS_NODE, LruLinks);

        if (Candidate->Logical == Logical)
        {
            Found = Candidate;
            RemoveEntryList(&Found->LruLinks);
            InsertHeadList(&Vcb->NodeLru, &Found->LruLinks);
            Found->References++;
            break;
        }
    }
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    if (Found != NULL)
    {
        if (Found->Header->Level != ExpectedLevel)
        {
            BtrfsReleaseNode(Vcb, Found);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        *Node = Found;
        return STATUS_SUCCESS;
    }

    New = ExAllocatePoolZero(PagedPool, sizeof(BTRFS_NODE), TAG_BTRFS_NODE);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    New->Header = ExAllocatePoolUninitialized(PagedPool, Vcb->NodeSize, TAG_BTRFS_NODE);
    if (New->Header == NULL)
    {
        ExFreePoolWithTag(New, TAG_BTRFS_NODE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    New->Logical = Logical;

    Mirrors = BtrfsMirrorCount(Vcb, Logical);
    for (Mirror = 0; Mirror < Mirrors; Mirror++)
    {
        Status = BtrfsReadLogical(Vcb, Logical, New->Header, Vcb->NodeSize, Mirror);
        if (NT_SUCCESS(Status) && BtrfsNodeValid(Vcb, New->Header, Logical, ExpectedLevel)) break;
        if (NT_SUCCESS(Status)) Status = STATUS_FILE_CORRUPT_ERROR;
        DPRINT1("Btrfs: tree block %I64x copy %lu cannot be used (%lx)\n", Logical, Mirror, Status);
    }

    if (!NT_SUCCESS(Status))
    {
        BtrfsFreeNode(New);
        return Status;
    }

    InitializeListHead(&Evicted);

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    for (Entry = Vcb->NodeLru.Flink; Entry != &Vcb->NodeLru; Entry = Entry->Flink)
    {
        PBTRFS_NODE Candidate = CONTAINING_RECORD(Entry, BTRFS_NODE, LruLinks);

        if (Candidate->Logical == Logical)
        {
            Found = Candidate;
            Found->References++;
            break;
        }
    }

    if (Found == NULL)
    {
        New->Cached = TRUE;
        New->References = 2;
        InsertHeadList(&Vcb->NodeLru, &New->LruLinks);
        Vcb->NodeCount++;

        while (Vcb->NodeCount > BTRFS_NODE_CACHE_LIMIT)
        {
            PBTRFS_NODE Oldest = CONTAINING_RECORD(Vcb->NodeLru.Blink, BTRFS_NODE, LruLinks);

            ASSERT(Oldest->Cached);
            RemoveEntryList(&Oldest->LruLinks);
            Vcb->NodeCount--;
            Oldest->Cached = FALSE;
            if (--Oldest->References == 0) InsertTailList(&Evicted, &Oldest->LruLinks);
        }
    }
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    while (!IsListEmpty(&Evicted))
        BtrfsFreeNode(CONTAINING_RECORD(RemoveHeadList(&Evicted), BTRFS_NODE, LruLinks));

    if (Found != NULL)
    {
        BtrfsFreeNode(New);
        *Node = Found;
    }
    else
    {
        *Node = New;
    }

    return STATUS_SUCCESS;
}

VOID
BtrfsReleaseNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_NODE Node)
{
    BOOLEAN Free;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    ASSERT(Node->References > 0);
    Free = (--Node->References == 0);
    ASSERT(!Free || !Node->Cached);
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    if (Free) BtrfsFreeNode(Node);
}

VOID
BtrfsFlushNodeCache(
    _Inout_ PBTRFS_VCB Vcb)
{
    LIST_ENTRY Evicted;

    InitializeListHead(&Evicted);

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    while (!IsListEmpty(&Vcb->NodeLru))
    {
        PBTRFS_NODE Node = CONTAINING_RECORD(RemoveHeadList(&Vcb->NodeLru), BTRFS_NODE, LruLinks);

        Node->Cached = FALSE;
        Vcb->NodeCount--;
        if (--Node->References == 0) InsertTailList(&Evicted, &Node->LruLinks);
    }
    ASSERT(Vcb->NodeCount == 0);
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    while (!IsListEmpty(&Evicted))
        BtrfsFreeNode(CONTAINING_RECORD(RemoveHeadList(&Evicted), BTRFS_NODE, LruLinks));
}

LONG
BtrfsCompareKeys(
    _In_ const BTRFS_KEY *First,
    _In_ const BTRFS_KEY *Second)
{
    if (First->ObjectId != Second->ObjectId) return First->ObjectId < Second->ObjectId ? -1 : 1;
    if (First->Type != Second->Type) return First->Type < Second->Type ? -1 : 1;
    if (First->Offset != Second->Offset) return First->Offset < Second->Offset ? -1 : 1;
    return 0;
}

static
PBTRFS_KEY_POINTER
BtrfsKeyPointers(
    _In_ PBTRFS_NODE Node)
{
    ASSERT(Node->Header->Level != 0);
    return (PBTRFS_KEY_POINTER)(Node->Header + 1);
}

static
PBTRFS_ITEM
BtrfsItems(
    _In_ PBTRFS_NODE Node)
{
    ASSERT(Node->Header->Level == 0);
    return (PBTRFS_ITEM)(Node->Header + 1);
}

VOID
BtrfsInitPath(
    _Out_ PBTRFS_PATH Path)
{
    RtlZeroMemory(Path, sizeof(*Path));
}

VOID
BtrfsReleasePath(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path)
{
    ULONG Level;

    for (Level = 0; Level < BTRFS_MAX_LEVEL; Level++)
    {
        if (Path->Nodes[Level] != NULL)
        {
            BtrfsReleaseNode(Vcb, Path->Nodes[Level]);
            Path->Nodes[Level] = NULL;
        }
    }
    Path->Levels = 0;
}

/* Moves the path to the first slot of the next leaf, or reports the end of the tree with the path left at its end */
static
NTSTATUS
BtrfsNextLeaf(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path)
{
    ULONG Level;
    NTSTATUS Status;

    for (Level = 1; Level < Path->Levels; Level++)
    {
        if (Path->Slots[Level] + 1 < Path->Nodes[Level]->Header->ItemCount) break;
    }
    if (Level >= Path->Levels) return STATUS_NO_MORE_ENTRIES;

    Path->Slots[Level]++;

    while (Level > 0)
    {
        PBTRFS_KEY_POINTER Pointer = &BtrfsKeyPointers(Path->Nodes[Level])[Path->Slots[Level]];
        PBTRFS_NODE Child;

        Status = BtrfsReadNode(Vcb, Pointer->BlockPointer, (UCHAR)(Level - 1), &Child);
        if (!NT_SUCCESS(Status)) return Status;

        BtrfsReleaseNode(Vcb, Path->Nodes[Level - 1]);
        Path->Nodes[Level - 1] = Child;
        Path->Slots[Level - 1] = 0;
        Level--;
    }

    if (Path->Nodes[0]->Header->ItemCount == 0) return STATUS_FILE_CORRUPT_ERROR;
    return STATUS_SUCCESS;
}

VOID
BtrfsAcquireTreeShared(
    _In_ PBTRFS_VCB Vcb)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Vcb->TreeLock, TRUE);
}

VOID
BtrfsAcquireTreeExclusive(
    _In_ PBTRFS_VCB Vcb)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->TreeLock, TRUE);
}

VOID
BtrfsReleaseTree(
    _In_ PBTRFS_VCB Vcb)
{
    ExReleaseResourceLite(&Vcb->TreeLock);
    KeLeaveCriticalRegion();
}

static
PBTRFS_LIVE_ROOT
BtrfsFindLiveRoot(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Id)
{
    PBTRFS_LIVE_ROOT Found = NULL;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    for (Entry = Vcb->LiveRoots.Flink; Entry != &Vcb->LiveRoots; Entry = Entry->Flink)
    {
        PBTRFS_LIVE_ROOT Candidate = CONTAINING_RECORD(Entry, BTRFS_LIVE_ROOT, Links);

        if (Candidate->Root.Id == Id)
        {
            Found = Candidate;
            break;
        }
    }
    ExReleaseFastMutex(&Vcb->NodeCacheLock);
    return Found;
}

/* The tree a root stands for as it is now: a change in the running transaction may have moved its top block */
PBTRFS_LIVE_ROOT
BtrfsLiveRoot(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Id)
{
    PBTRFS_LIVE_ROOT Live = BtrfsFindLiveRoot(Vcb, Id), New;
    BTRFS_ROOT Root;

    if (Live != NULL || Id == BTRFS_CHUNK_TREE_OBJECTID) return Live;
    if (!NT_SUCCESS(BtrfsFindRoot(Vcb, Id, &Root))) return NULL;

    New = ExAllocatePoolZero(NonPagedPool, sizeof(BTRFS_LIVE_ROOT), TAG_BTRFS);
    if (New == NULL) return NULL;
    New->Root = Root;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    Live = NULL;
    {
        PLIST_ENTRY Entry;

        for (Entry = Vcb->LiveRoots.Flink; Entry != &Vcb->LiveRoots; Entry = Entry->Flink)
        {
            PBTRFS_LIVE_ROOT Candidate = CONTAINING_RECORD(Entry, BTRFS_LIVE_ROOT, Links);

            if (Candidate->Root.Id == Id)
            {
                Live = Candidate;
                break;
            }
        }
    }
    if (Live == NULL) InsertTailList(&Vcb->LiveRoots, &New->Links);
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    if (Live != NULL)
    {
        ExFreePoolWithTag(New, TAG_BTRFS);
        return Live;
    }
    return New;
}

NTSTATUS
BtrfsSearch(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ const BTRFS_KEY *Key,
    _Out_ PBTRFS_PATH Path)
{
    PBTRFS_LIVE_ROOT Live = BtrfsFindLiveRoot(Vcb, Root->Id);
    ULONGLONG Logical = Live ? Live->Root.ByteNr : Root->ByteNr;
    ULONG Level = Live ? Live->Root.Level : Root->Level;
    NTSTATUS Status;

    BtrfsInitPath(Path);
    if (Level >= BTRFS_MAX_LEVEL) return STATUS_FILE_CORRUPT_ERROR;
    Path->Levels = (UCHAR)(Level + 1);

    for (;;)
    {
        PBTRFS_NODE Node;
        ULONG Low = 0, High;

        Status = BtrfsReadNode(Vcb, Logical, (UCHAR)Level, &Node);
        if (!NT_SUCCESS(Status)) return Status;
        Path->Nodes[Level] = Node;
        High = Node->Header->ItemCount;

        if (Level != 0)
        {
            PBTRFS_KEY_POINTER Pointers = BtrfsKeyPointers(Node);

            while (Low < High)
            {
                ULONG Middle = Low + (High - Low) / 2;

                if (BtrfsCompareKeys(&Pointers[Middle].Key, Key) <= 0)
                    Low = Middle + 1;
                else
                    High = Middle;
            }

            Path->Slots[Level] = Low ? Low - 1 : 0;
            Logical = Pointers[Path->Slots[Level]].BlockPointer;
            Level--;
            continue;
        }

        {
            PBTRFS_ITEM Items = BtrfsItems(Node);

            while (Low < High)
            {
                ULONG Middle = Low + (High - Low) / 2;

                if (BtrfsCompareKeys(&Items[Middle].Key, Key) < 0)
                    Low = Middle + 1;
                else
                    High = Middle;
            }

            Path->Slots[0] = Low;
            if (Low < Node->Header->ItemCount) return STATUS_SUCCESS;

            Status = BtrfsNextLeaf(Vcb, Path);
            if (Status == STATUS_NO_MORE_ENTRIES)
            {
                ASSERT(Path->Slots[0] == Path->Nodes[0]->Header->ItemCount);
                return STATUS_NOT_FOUND;
            }
            return Status;
        }
    }
}

NTSTATUS
BtrfsNextItem(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path)
{
    NTSTATUS Status;

    ASSERT(Path->Nodes[0] != NULL);

    if (Path->Slots[0] + 1 < Path->Nodes[0]->Header->ItemCount)
    {
        Path->Slots[0]++;
        return STATUS_SUCCESS;
    }

    Status = BtrfsNextLeaf(Vcb, Path);
    if (Status == STATUS_NO_MORE_ENTRIES) Path->Slots[0] = Path->Nodes[0]->Header->ItemCount;
    return Status;
}

NTSTATUS
BtrfsPreviousItem(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path)
{
    ULONG Level;
    NTSTATUS Status;

    ASSERT(Path->Nodes[0] != NULL);

    if (Path->Slots[0] > 0)
    {
        Path->Slots[0]--;
        return STATUS_SUCCESS;
    }

    for (Level = 1; Level < Path->Levels; Level++)
    {
        if (Path->Slots[Level] > 0) break;
    }
    if (Level >= Path->Levels) return STATUS_NO_MORE_ENTRIES;

    Path->Slots[Level]--;

    while (Level > 0)
    {
        PBTRFS_KEY_POINTER Pointer = &BtrfsKeyPointers(Path->Nodes[Level])[Path->Slots[Level]];
        PBTRFS_NODE Child;

        Status = BtrfsReadNode(Vcb, Pointer->BlockPointer, (UCHAR)(Level - 1), &Child);
        if (!NT_SUCCESS(Status)) return Status;
        if (Child->Header->ItemCount == 0)
        {
            BtrfsReleaseNode(Vcb, Child);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        BtrfsReleaseNode(Vcb, Path->Nodes[Level - 1]);
        Path->Nodes[Level - 1] = Child;
        Path->Slots[Level - 1] = Child->Header->ItemCount - 1;
        Level--;
    }

    return STATUS_SUCCESS;
}

PBTRFS_ITEM
BtrfsPathItem(
    _In_ PBTRFS_PATH Path)
{
    ASSERT(Path->Nodes[0] != NULL);
    ASSERT(Path->Slots[0] < Path->Nodes[0]->Header->ItemCount);
    return &BtrfsItems(Path->Nodes[0])[Path->Slots[0]];
}

PVOID
BtrfsPathItemData(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_PATH Path,
    _Out_ PULONG Size)
{
    PBTRFS_ITEM Item = BtrfsPathItem(Path);
    ULONG DataArea = Vcb->NodeSize - sizeof(BTRFS_HEADER);

    *Size = 0;
    if (Item->Offset > DataArea || Item->Size > DataArea - Item->Offset) return NULL;
    if (Item->Offset < Path->Nodes[0]->Header->ItemCount * sizeof(BTRFS_ITEM)) return NULL;

    *Size = Item->Size;
    return (PUCHAR)(Path->Nodes[0]->Header + 1) + Item->Offset;
}

NTSTATUS
BtrfsFindItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ const BTRFS_KEY *Key,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_opt_ PULONG ItemSize)
{
    BTRFS_PATH Path;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, BufferSize);
    if (ItemSize != NULL) *ItemSize = 0;

    BtrfsAcquireTreeShared(Vcb);
    Status = BtrfsSearch(Vcb, Root, Key, &Path);
    if (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);

        if (BtrfsCompareKeys(&Item->Key, Key) == 0)
        {
            ULONG Size;
            PVOID Data = BtrfsPathItemData(Vcb, &Path, &Size);

            if (Data == NULL)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            else
            {
                RtlCopyMemory(Buffer, Data, min(Size, BufferSize));
                if (ItemSize != NULL) *ItemSize = Size;
            }
        }
        else
        {
            Status = STATUS_NOT_FOUND;
        }
    }

    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return Status;
}
