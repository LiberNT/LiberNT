/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Copy-on-write changes to the trees: searching for a change, inserting, deleting and resizing items
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_LEAF_DATA(Vcb) ((Vcb)->NodeSize - (ULONG)sizeof(BTRFS_HEADER))

static
PBTRFS_ITEM
BtrfsLeafItems(
    _In_ PBTRFS_NODE Node)
{
    ASSERT(Node->Header->Level == 0);
    return (PBTRFS_ITEM)(Node->Header + 1);
}

static
PUCHAR
BtrfsLeafData(
    _In_ PBTRFS_NODE Node)
{
    return (PUCHAR)(Node->Header + 1);
}

static
PBTRFS_KEY_POINTER
BtrfsNodePointers(
    _In_ PBTRFS_NODE Node)
{
    ASSERT(Node->Header->Level != 0);
    return (PBTRFS_KEY_POINTER)(Node->Header + 1);
}

static
ULONG
BtrfsLeafDataEnd(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_NODE Node)
{
    ULONG Count = Node->Header->ItemCount;

    return Count ? BtrfsLeafItems(Node)[Count - 1].Offset : BTRFS_LEAF_DATA(Vcb);
}

static
ULONG
BtrfsLeafFree(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_NODE Node)
{
    ULONG Count = Node->Header->ItemCount;
    ULONG Used = Count * sizeof(BTRFS_ITEM) + (BTRFS_LEAF_DATA(Vcb) - BtrfsLeafDataEnd(Vcb, Node));

    ASSERT(Used <= BTRFS_LEAF_DATA(Vcb));
    return BTRFS_LEAF_DATA(Vcb) - Used;
}

static
ULONG
BtrfsNodeCapacity(
    _In_ PBTRFS_VCB Vcb)
{
    return (Vcb->NodeSize - sizeof(BTRFS_HEADER)) / sizeof(BTRFS_KEY_POINTER);
}

VOID
BtrfsMarkNodeDirty(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_NODE Node)
{
    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    ASSERT(!Node->Cached && !Node->Dirty);
    Node->Dirty = TRUE;
    Node->References++;
    InsertTailList(&Vcb->DirtyNodes, &Node->LruLinks);
    ExReleaseFastMutex(&Vcb->NodeCacheLock);
}

/* A node that left the tree is no longer found by its address: its space returns once the change is committed */
static
NTSTATUS
BtrfsRetireNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ PBTRFS_NODE Node)
{
    BOOLEAN Free = FALSE;
    UCHAR Level = Node->Header->Level;
    ULONGLONG Logical = Node->Logical;

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    if (Node->Cached)
    {
        RemoveEntryList(&Node->LruLinks);
        Node->Cached = FALSE;
        Vcb->NodeCount--;
        Free = (--Node->References == 0);
    }
    else if (Node->Dirty && !Node->Deleted)
    {
        Node->Deleted = TRUE;
        RemoveEntryList(&Node->LruLinks);
        Free = (--Node->References == 0);
    }
    ExReleaseFastMutex(&Vcb->NodeCacheLock);
    ASSERT(!Free);
    UNREFERENCED_PARAMETER(Free);

    Root->BytesUsedDelta -= Vcb->NodeSize;
    return BtrfsQueueRef(Vcb, BTRFS_REF_META_DROP, Logical, Vcb->NodeSize, Root->Root.Id, Level, 0);
}

static
NTSTATUS
BtrfsNewNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ UCHAR Level,
    _In_opt_ PBTRFS_NODE Template,
    _Out_ PBTRFS_NODE *Result)
{
    PBTRFS_NODE Node;
    ULONGLONG Logical;
    NTSTATUS Status;

    *Result = NULL;
    Status = BtrfsAllocate(Vcb, (Root->Root.Id == BTRFS_CHUNK_TREE_OBJECTID) ? BTRFS_BLOCK_GROUP_SYSTEM : BTRFS_BLOCK_GROUP_METADATA,
                           Vcb->NodeSize, &Logical);
    if (!NT_SUCCESS(Status)) return Status;

    Node = ExAllocatePoolZero(PagedPool, sizeof(BTRFS_NODE), TAG_BTRFS_NODE);
    if (Node != NULL) Node->Header = ExAllocatePoolZero(PagedPool, Vcb->NodeSize, TAG_BTRFS_NODE);
    if (Node == NULL || Node->Header == NULL)
    {
        if (Node != NULL) ExFreePoolWithTag(Node, TAG_BTRFS_NODE);
        BtrfsPin(Vcb, Logical, Vcb->NodeSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Template != NULL)
    {
        RtlCopyMemory(Node->Header, Template->Header, Vcb->NodeSize);
    }
    else
    {
        RtlCopyMemory(Node->Header->FsId, Vcb->HeaderFsId, BTRFS_UUID_SIZE);
        RtlCopyMemory(Node->Header->ChunkTreeUuid, Vcb->ChunkTreeUuid, BTRFS_UUID_SIZE);
        Node->Header->Level = Level;
    }

    Node->Logical = Logical;
    Node->References = 1;
    Node->Header->ByteNr = Logical;
    Node->Header->Generation = Vcb->TransId;
    Node->Header->Owner = Root->Root.Id;
    Node->Header->Flags = BTRFS_MIXED_BACKREF_REV << BTRFS_BACKREF_REV_SHIFT;
    RtlZeroMemory(Node->Header->Checksum, BTRFS_CSUM_SIZE);
    BtrfsMarkNodeDirty(Vcb, Node);

    Root->BytesUsedDelta += Vcb->NodeSize;
    Status = BtrfsQueueRef(Vcb, BTRFS_REF_META_ADD, Logical, Vcb->NodeSize, Root->Root.Id, Node->Header->Level, 0);
    if (!NT_SUCCESS(Status))
    {
        BtrfsReleaseNode(Vcb, Node);
        return Status;
    }

    *Result = Node;
    return STATUS_SUCCESS;
}

/* The caller's reference to Node becomes a reference to the copy that may be changed in this transaction */
static
NTSTATUS
BtrfsCowNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_NODE *Node)
{
    PBTRFS_NODE Old = *Node, New;
    NTSTATUS Status;

    ASSERT(Vcb->TransActive);
    if (Old->Dirty)
    {
        ASSERT(!Old->Deleted && Old->Header->Generation == Vcb->TransId);
        return STATUS_SUCCESS;
    }

    Status = BtrfsNewNode(Vcb, Root, Old->Header->Level, Old, &New);
    if (!NT_SUCCESS(Status)) return Status;

    Status = BtrfsRetireNode(Vcb, Root, Old);
    BtrfsReleaseNode(Vcb, Old);
    *Node = New;
    return Status;
}

static
VOID
BtrfsFixupKeys(
    _Inout_ PBTRFS_PATH Path,
    _In_ ULONG FromLevel,
    _In_ const BTRFS_KEY *Key)
{
    ULONG Level;

    for (Level = FromLevel + 1; Level < Path->Levels; Level++)
    {
        ULONG Slot = Path->Slots[Level];

        BtrfsNodePointers(Path->Nodes[Level])[Slot].Key = *Key;
        if (Slot != 0) break;
    }
}

static
NTSTATUS
BtrfsInsertPointer(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path,
    _In_ ULONG Level,
    _In_ ULONG Slot,
    _In_ const BTRFS_KEY *Key,
    _In_ ULONGLONG Logical)
{
    PBTRFS_NODE Node = Path->Nodes[Level];
    PBTRFS_KEY_POINTER Pointers = BtrfsNodePointers(Node);
    ULONG Count = Node->Header->ItemCount;

    ASSERT(Node->Dirty && Count < BtrfsNodeCapacity(Vcb) && Slot <= Count);
    RtlMoveMemory(&Pointers[Slot + 1], &Pointers[Slot], (Count - Slot) * sizeof(BTRFS_KEY_POINTER));
    Pointers[Slot].Key = *Key;
    Pointers[Slot].BlockPointer = Logical;
    Pointers[Slot].Generation = Vcb->TransId;
    Node->Header->ItemCount = Count + 1;
    if (Slot == 0) BtrfsFixupKeys(Path, Level, Key);
    return STATUS_SUCCESS;
}

/* The root of a tree that splits gets a parent: the tree grows by one level */
static
NTSTATUS
BtrfsGrowRoot(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_PATH Path)
{
    ULONG Level = Path->Levels - 1;
    PBTRFS_NODE Child = Path->Nodes[Level], NewRoot;
    PBTRFS_KEY_POINTER Pointer;
    NTSTATUS Status;

    if (Path->Levels >= BTRFS_MAX_LEVEL) return STATUS_DISK_FULL;

    Status = BtrfsNewNode(Vcb, Root, (UCHAR)(Level + 1), NULL, &NewRoot);
    if (!NT_SUCCESS(Status)) return Status;

    Pointer = BtrfsNodePointers(NewRoot);
    Pointer->Key = (Child->Header->Level == 0) ? BtrfsLeafItems(Child)[0].Key : BtrfsNodePointers(Child)[0].Key;
    if (Child->Header->ItemCount == 0) RtlZeroMemory(&Pointer->Key, sizeof(Pointer->Key));
    Pointer->BlockPointer = Child->Logical;
    Pointer->Generation = Vcb->TransId;
    NewRoot->Header->ItemCount = 1;

    Path->Nodes[Level + 1] = NewRoot;
    Path->Slots[Level + 1] = 0;
    Path->Levels++;

    Root->Root.ByteNr = NewRoot->Logical;
    Root->Root.Level = (UCHAR)(Level + 1);
    Root->Dirty = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsSplitNode(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_PATH Path,
    _In_ ULONG Level)
{
    PBTRFS_NODE Node = Path->Nodes[Level], Right;
    ULONG Count = Node->Header->ItemCount, Middle = Count / 2;
    NTSTATUS Status;

    if (Level + 1 == Path->Levels)
    {
        Status = BtrfsGrowRoot(Vcb, Root, Path);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = BtrfsNewNode(Vcb, Root, (UCHAR)Level, NULL, &Right);
    if (!NT_SUCCESS(Status)) return Status;

    RtlCopyMemory(BtrfsNodePointers(Right), &BtrfsNodePointers(Node)[Middle], (Count - Middle) * sizeof(BTRFS_KEY_POINTER));
    Right->Header->ItemCount = Count - Middle;
    Node->Header->ItemCount = Middle;

    Status = BtrfsInsertPointer(Vcb, Path, Level + 1, Path->Slots[Level + 1] + 1, &BtrfsNodePointers(Right)[0].Key, Right->Logical);
    if (!NT_SUCCESS(Status))
    {
        BtrfsReleaseNode(Vcb, Right);
        return Status;
    }

    if (Path->Slots[Level] >= Middle)
    {
        Path->Slots[Level] -= Middle;
        Path->Slots[Level + 1]++;
        BtrfsReleaseNode(Vcb, Node);
        Path->Nodes[Level] = Right;
    }
    else
    {
        BtrfsReleaseNode(Vcb, Right);
    }

    return STATUS_SUCCESS;
}

static
VOID
BtrfsCopyLeafItems(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_NODE From,
    _In_ ULONG First,
    _In_ ULONG Count,
    _Inout_ PBTRFS_NODE To)
{
    PBTRFS_ITEM Source = BtrfsLeafItems(From), Target = BtrfsLeafItems(To);
    ULONG Index, End = BtrfsLeafDataEnd(Vcb, To);
    ULONG Existing = To->Header->ItemCount;

    for (Index = 0; Index < Count; Index++)
    {
        const BTRFS_ITEM *Item = &Source[First + Index];

        End -= Item->Size;
        RtlCopyMemory(BtrfsLeafData(To) + End, BtrfsLeafData(From) + Item->Offset, Item->Size);
        Target[Existing + Index].Key = Item->Key;
        Target[Existing + Index].Offset = End;
        Target[Existing + Index].Size = Item->Size;
    }
    To->Header->ItemCount = Existing + Count;
}

/* A leaf without room for Needed bytes at the path's slot is cut in two, and the path follows the slot */
static
NTSTATUS
BtrfsSplitLeaf(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_PATH Path,
    _In_ const BTRFS_KEY *Key,
    _In_ ULONG Needed)
{
    PBTRFS_NODE Leaf = Path->Nodes[0], Right;
    ULONG Count = Leaf->Header->ItemCount, Slot = Path->Slots[0], Middle;
    BOOLEAN ToRight, Exists;
    BTRFS_KEY First;
    NTSTATUS Status;

    if (Needed > BTRFS_LEAF_DATA(Vcb)) return STATUS_INVALID_PARAMETER;
    ASSERT(Count != 0);

    Exists = (Slot < Count && BtrfsCompareKeys(&BtrfsLeafItems(Leaf)[Slot].Key, Key) == 0);
    if (Exists && Count == 1) return STATUS_MORE_PROCESSING_REQUIRED;

    if (Path->Levels == 1)
    {
        Status = BtrfsGrowRoot(Vcb, Root, Path);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (Slot == Count)
    {
        Middle = Count;
        ToRight = TRUE;
    }
    else if (Slot == 0)
    {
        Middle = Exists ? 1 : 0;
        ToRight = FALSE;
    }
    else
    {
        Middle = Count / 2;
        ToRight = (Slot >= Middle);
    }

    Status = BtrfsNewNode(Vcb, Root, 0, NULL, &Right);
    if (!NT_SUCCESS(Status)) return Status;

    BtrfsCopyLeafItems(Vcb, Leaf, Middle, Count - Middle, Right);
    Leaf->Header->ItemCount = Middle;
    First = (Right->Header->ItemCount != 0) ? BtrfsLeafItems(Right)[0].Key : *Key;

    Status = BtrfsInsertPointer(Vcb, Path, 1, Path->Slots[1] + 1, &First, Right->Logical);
    if (!NT_SUCCESS(Status))
    {
        BtrfsReleaseNode(Vcb, Right);
        return Status;
    }

    if (ToRight)
    {
        Path->Slots[0] = Slot - Middle;
        Path->Slots[1]++;
        BtrfsReleaseNode(Vcb, Leaf);
        Path->Nodes[0] = Right;
    }
    else
    {
        BtrfsReleaseNode(Vcb, Right);
    }

    return (BtrfsLeafFree(Vcb, Path->Nodes[0]) >= Needed) ? STATUS_SUCCESS : STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
BtrfsSearchForWrite(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ const BTRFS_KEY *Key,
    _In_ LONG InsertLength,
    _Out_ PBTRFS_PATH Path)
{
    ULONG Level = Root->Root.Level;
    PBTRFS_NODE Node;
    NTSTATUS Status;
    ULONG Attempts;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    Vcb->ChangeSequence++;
    ASSERT(Vcb->TransActive);

    BtrfsInitPath(Path);
    if (Level >= BTRFS_MAX_LEVEL) return STATUS_FILE_CORRUPT_ERROR;

    Status = BtrfsReadNode(Vcb, Root->Root.ByteNr, (UCHAR)Level, &Node);
    if (!NT_SUCCESS(Status)) return Status;
    Status = BtrfsCowNode(Vcb, Root, &Node);
    if (!NT_SUCCESS(Status))
    {
        BtrfsReleaseNode(Vcb, Node);
        return Status;
    }
    if (Root->Root.ByteNr != Node->Logical)
    {
        Root->Root.ByteNr = Node->Logical;
        Root->Dirty = TRUE;
    }

    Path->Levels = (UCHAR)(Level + 1);
    Path->Nodes[Level] = Node;

    for (;;)
    {
        ULONG Low = 0, High;

        Node = Path->Nodes[Level];
        High = Node->Header->ItemCount;

        if (Level != 0)
        {
            PBTRFS_KEY_POINTER Pointers;
            PBTRFS_NODE Child;

            while (Low < High)
            {
                ULONG Middle = Low + (High - Low) / 2;

                if (BtrfsCompareKeys(&BtrfsNodePointers(Node)[Middle].Key, Key) <= 0)
                    Low = Middle + 1;
                else
                    High = Middle;
            }
            Path->Slots[Level] = Low ? Low - 1 : 0;

            if (InsertLength > 0 && Node->Header->ItemCount >= BtrfsNodeCapacity(Vcb) - 3)
            {
                Status = BtrfsSplitNode(Vcb, Root, Path, Level);
                if (!NT_SUCCESS(Status)) return Status;
                Node = Path->Nodes[Level];
            }

            Pointers = BtrfsNodePointers(Node);
            Status = BtrfsReadNode(Vcb, Pointers[Path->Slots[Level]].BlockPointer, (UCHAR)(Level - 1), &Child);
            if (!NT_SUCCESS(Status)) return Status;
            Status = BtrfsCowNode(Vcb, Root, &Child);
            if (!NT_SUCCESS(Status))
            {
                BtrfsReleaseNode(Vcb, Child);
                return Status;
            }
            if (Pointers[Path->Slots[Level]].BlockPointer != Child->Logical)
            {
                Pointers[Path->Slots[Level]].BlockPointer = Child->Logical;
                Pointers[Path->Slots[Level]].Generation = Vcb->TransId;
            }

            Level--;
            Path->Nodes[Level] = Child;
            continue;
        }

        while (Low < High)
        {
            ULONG Middle = Low + (High - Low) / 2;

            if (BtrfsCompareKeys(&BtrfsLeafItems(Node)[Middle].Key, Key) < 0)
                Low = Middle + 1;
            else
                High = Middle;
        }
        Path->Slots[0] = Low;

        if (InsertLength <= 0) return STATUS_SUCCESS;

        for (Attempts = 0; Attempts < 3; Attempts++)
        {
            if (BtrfsLeafFree(Vcb, Path->Nodes[0]) >= (ULONG)InsertLength) return STATUS_SUCCESS;
            Status = BtrfsSplitLeaf(Vcb, Root, Path, Key, (ULONG)InsertLength);
            if (NT_SUCCESS(Status)) return STATUS_SUCCESS;
            if (Status != STATUS_MORE_PROCESSING_REQUIRED) return Status;
        }
        return STATUS_DISK_FULL;
    }
}

static
NTSTATUS
BtrfsInsertEmpty(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path,
    _In_ const BTRFS_KEY *Key,
    _In_ ULONG Size,
    _Out_ PVOID *Data)
{
    PBTRFS_NODE Leaf = Path->Nodes[0];
    PBTRFS_ITEM Items = BtrfsLeafItems(Leaf);
    ULONG Count = Leaf->Header->ItemCount, Slot = Path->Slots[0];
    ULONG DataEnd = BtrfsLeafDataEnd(Vcb, Leaf), Offset, Index;

    ASSERT(Leaf->Dirty);
    ASSERT(BtrfsLeafFree(Vcb, Leaf) >= Size + sizeof(BTRFS_ITEM));
    ASSERT(Slot == 0 || BtrfsCompareKeys(&Items[Slot - 1].Key, Key) < 0);
    ASSERT(Slot == Count || BtrfsCompareKeys(&Items[Slot].Key, Key) > 0);

    if (Slot < Count)
    {
        ULONG OldData = Items[Slot].Offset + Items[Slot].Size;

        RtlMoveMemory(BtrfsLeafData(Leaf) + DataEnd - Size, BtrfsLeafData(Leaf) + DataEnd, OldData - DataEnd);
        for (Index = Slot; Index < Count; Index++)
            Items[Index].Offset -= Size;
        RtlMoveMemory(&Items[Slot + 1], &Items[Slot], (Count - Slot) * sizeof(BTRFS_ITEM));
        Offset = OldData - Size;
    }
    else
    {
        Offset = DataEnd - Size;
    }

    Items[Slot].Key = *Key;
    Items[Slot].Offset = Offset;
    Items[Slot].Size = Size;
    Leaf->Header->ItemCount = Count + 1;
    if (Slot == 0) BtrfsFixupKeys(Path, 0, Key);

    *Data = BtrfsLeafData(Leaf) + Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsInsertItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ const BTRFS_KEY *Key,
    _In_reads_bytes_opt_(Size) const VOID *Data,
    _In_ ULONG Size,
    _Out_opt_ PVOID *Written)
{
    BTRFS_PATH Path;
    PVOID Target = NULL;
    NTSTATUS Status;

    if (Written != NULL) *Written = NULL;
    Status = BtrfsSearchForWrite(Vcb, Root, Key, (LONG)(Size + sizeof(BTRFS_ITEM)), &Path);
    if (NT_SUCCESS(Status) && Path.Slots[0] < Path.Nodes[0]->Header->ItemCount &&
        BtrfsCompareKeys(&BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Key, Key) == 0)
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
    }
    if (NT_SUCCESS(Status)) Status = BtrfsInsertEmpty(Vcb, &Path, Key, Size, &Target);
    if (NT_SUCCESS(Status))
    {
        if (Data != NULL)
            RtlCopyMemory(Target, Data, Size);
        else
            RtlZeroMemory(Target, Size);
        if (Written != NULL) *Written = Target;
    }

    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

static
NTSTATUS
BtrfsDeletePointer(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_PATH Path,
    _In_ ULONG Level)
{
    PBTRFS_NODE Node = Path->Nodes[Level];
    PBTRFS_KEY_POINTER Pointers = BtrfsNodePointers(Node);
    ULONG Count = Node->Header->ItemCount, Slot = Path->Slots[Level];
    NTSTATUS Status;

    ASSERT(Node->Dirty && Slot < Count);
    RtlMoveMemory(&Pointers[Slot], &Pointers[Slot + 1], (Count - Slot - 1) * sizeof(BTRFS_KEY_POINTER));
    Node->Header->ItemCount = --Count;

    if (Count == 0 && Level + 1 < Path->Levels)
    {
        Status = BtrfsRetireNode(Vcb, Root, Node);
        if (!NT_SUCCESS(Status)) return Status;
        return BtrfsDeletePointer(Vcb, Root, Path, Level + 1);
    }

    if (Count == 0)
    {
        PBTRFS_NODE Leaf;

        Status = BtrfsNewNode(Vcb, Root, 0, NULL, &Leaf);
        if (!NT_SUCCESS(Status)) return Status;
        Status = BtrfsRetireNode(Vcb, Root, Node);
        Root->Root.ByteNr = Leaf->Logical;
        Root->Root.Level = 0;
        Root->Dirty = TRUE;
        BtrfsReleaseNode(Vcb, Leaf);
        return Status;
    }

    if (Slot == 0) BtrfsFixupKeys(Path, Level, &Pointers[0].Key);
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsDeleteItemAt(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _Inout_ PBTRFS_PATH Path)
{
    PBTRFS_NODE Leaf = Path->Nodes[0];
    PBTRFS_ITEM Items = BtrfsLeafItems(Leaf);
    ULONG Count = Leaf->Header->ItemCount, Slot = Path->Slots[0], Size, Index;
    ULONG DataEnd = BtrfsLeafDataEnd(Vcb, Leaf);

    ASSERT(Leaf->Dirty && Slot < Count);

    Size = Items[Slot].Size;
    RtlMoveMemory(BtrfsLeafData(Leaf) + DataEnd + Size, BtrfsLeafData(Leaf) + DataEnd, Items[Slot].Offset - DataEnd);
    for (Index = Slot + 1; Index < Count; Index++)
        Items[Index].Offset += Size;
    RtlMoveMemory(&Items[Slot], &Items[Slot + 1], (Count - Slot - 1) * sizeof(BTRFS_ITEM));
    Leaf->Header->ItemCount = --Count;

    if (Count == 0 && Path->Levels > 1)
    {
        NTSTATUS Status = BtrfsRetireNode(Vcb, Root, Leaf);

        if (!NT_SUCCESS(Status)) return Status;
        return BtrfsDeletePointer(Vcb, Root, Path, 1);
    }

    if (Slot == 0 && Count != 0) BtrfsFixupKeys(Path, 0, &Items[0].Key);
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsDeleteItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ const BTRFS_KEY *Key)
{
    BTRFS_PATH Path;
    NTSTATUS Status;

    Status = BtrfsSearchForWrite(Vcb, Root, Key, -1, &Path);
    if (NT_SUCCESS(Status))
    {
        if (Path.Slots[0] < Path.Nodes[0]->Header->ItemCount &&
            BtrfsCompareKeys(&BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Key, Key) == 0)
            Status = BtrfsDeleteItemAt(Vcb, Root, &Path);
        else
            Status = STATUS_NOT_FOUND;
    }

    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

/* Items keep their start and grow or shrink at their end; the caller searched with room for the growth */
NTSTATUS
BtrfsResizeItemAt(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PBTRFS_PATH Path,
    _In_ ULONG NewSize)
{
    PBTRFS_NODE Leaf = Path->Nodes[0];
    PBTRFS_ITEM Items = BtrfsLeafItems(Leaf);
    ULONG Count = Leaf->Header->ItemCount, Slot = Path->Slots[0], Index;
    ULONG DataEnd = BtrfsLeafDataEnd(Vcb, Leaf), OldSize = Items[Slot].Size, OldEnd = Items[Slot].Offset + OldSize;

    ASSERT(Leaf->Dirty && Slot < Count);

    if (NewSize > OldSize)
    {
        ULONG Grow = NewSize - OldSize;

        if (BtrfsLeafFree(Vcb, Leaf) < Grow) return STATUS_DISK_FULL;
        RtlMoveMemory(BtrfsLeafData(Leaf) + DataEnd - Grow, BtrfsLeafData(Leaf) + DataEnd, OldEnd - DataEnd);
        for (Index = Slot; Index < Count; Index++)
            Items[Index].Offset -= Grow;
        RtlZeroMemory(BtrfsLeafData(Leaf) + OldEnd - Grow, Grow);
    }
    else if (NewSize < OldSize)
    {
        ULONG Shrink = OldSize - NewSize;

        RtlMoveMemory(BtrfsLeafData(Leaf) + DataEnd + Shrink, BtrfsLeafData(Leaf) + DataEnd, OldEnd - Shrink - DataEnd);
        for (Index = Slot; Index < Count; Index++)
            Items[Index].Offset += Shrink;
    }

    Items[Slot].Size = NewSize;
    return STATUS_SUCCESS;
}

VOID
BtrfsSetKeyAt(
    _Inout_ PBTRFS_PATH Path,
    _In_ const BTRFS_KEY *Key)
{
    PBTRFS_NODE Leaf = Path->Nodes[0];
    PBTRFS_ITEM Items = BtrfsLeafItems(Leaf);
    ULONG Slot = Path->Slots[0];

    ASSERT(Leaf->Dirty && Slot < Leaf->Header->ItemCount);
    ASSERT(Slot == 0 || BtrfsCompareKeys(&Items[Slot - 1].Key, Key) < 0);
    ASSERT(Slot + 1 == Leaf->Header->ItemCount || BtrfsCompareKeys(&Items[Slot + 1].Key, Key) > 0);

    Items[Slot].Key = *Key;
    if (Slot == 0) BtrfsFixupKeys(Path, 0, Key);
}

NTSTATUS
BtrfsUpdateItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Root,
    _In_ const BTRFS_KEY *Key,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ ULONG Size)
{
    BTRFS_PATH Path;
    LONG Growth = -1;
    NTSTATUS Status;

    for (;;)
    {
        ULONG OldSize;

        Status = BtrfsSearchForWrite(Vcb, Root, Key, Growth, &Path);
        if (!NT_SUCCESS(Status) || Path.Slots[0] >= Path.Nodes[0]->Header->ItemCount ||
            BtrfsCompareKeys(&BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Key, Key) != 0)
        {
            break;
        }

        OldSize = BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Size;
        if (Size <= OldSize || BtrfsLeafFree(Vcb, Path.Nodes[0]) >= Size - OldSize || Growth > 0) break;

        Growth = (LONG)(Size - OldSize);
        BtrfsReleasePath(Vcb, &Path);
    }

    if (NT_SUCCESS(Status))
    {
        if (Path.Slots[0] < Path.Nodes[0]->Header->ItemCount &&
            BtrfsCompareKeys(&BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Key, Key) == 0)
        {
            if (BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Size != Size) Status = BtrfsResizeItemAt(Vcb, &Path, Size);
            if (NT_SUCCESS(Status))
                RtlCopyMemory(BtrfsLeafData(Path.Nodes[0]) + BtrfsLeafItems(Path.Nodes[0])[Path.Slots[0]].Offset, Data, Size);
        }
        else
        {
            Status = STATUS_NOT_FOUND;
        }
    }

    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

/* Every block changed in the transaction goes to the device with its checksum, then serves later reads as clean */
NTSTATUS
BtrfsWriteNodes(
    _In_ PBTRFS_VCB Vcb)
{
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Entry = Vcb->DirtyNodes.Flink; Entry != &Vcb->DirtyNodes; Entry = Entry->Flink)
    {
        PBTRFS_NODE Node = CONTAINING_RECORD(Entry, BTRFS_NODE, LruLinks);
        UCHAR Digest[BTRFS_CSUM_SIZE];

        ASSERT(Node->Dirty && !Node->Deleted && Node->Header->Generation == Vcb->TransId);
        ASSERT(Node->Header->ItemCount != 0 || Node->Header->Level == 0);

        Node->Header->Flags |= BTRFS_HEADER_FLAG_WRITTEN;
        BtrfsComputeChecksum(Vcb->ChecksumType, Node->Header->FsId, Vcb->NodeSize - BTRFS_CSUM_SIZE, Digest);
        RtlCopyMemory(Node->Header->Checksum, Digest, BTRFS_CSUM_SIZE);

        Status = BtrfsWriteLogical(Vcb, Node->Logical, Node->Header, Vcb->NodeSize);
        if (!NT_SUCCESS(Status)) return Status;
    }

    ExAcquireFastMutex(&Vcb->NodeCacheLock);
    while (!IsListEmpty(&Vcb->DirtyNodes))
    {
        PBTRFS_NODE Node = CONTAINING_RECORD(RemoveHeadList(&Vcb->DirtyNodes), BTRFS_NODE, LruLinks);

        Node->Dirty = FALSE;
        Node->Cached = TRUE;
        InsertHeadList(&Vcb->NodeLru, &Node->LruLinks);
        Vcb->NodeCount++;
    }
    ExReleaseFastMutex(&Vcb->NodeCacheLock);

    return Status;
}
