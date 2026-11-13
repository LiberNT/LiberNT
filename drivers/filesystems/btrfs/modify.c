/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Changing files and directories: names, inodes and file data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_UNIX_EPOCH_100NS 116444736000000000LL
#define BTRFS_MAX_EXTENT       0x4000000ULL

BTRFS_TIME
BtrfsFromNtTime(
    _In_ LARGE_INTEGER Time)
{
    BTRFS_TIME Result;
    LONGLONG Relative = Time.QuadPart - BTRFS_UNIX_EPOCH_100NS;
    LONGLONG Seconds = Relative / 10000000;
    LONGLONG Rest = Relative % 10000000;

    if (Rest < 0)
    {
        Rest += 10000000;
        Seconds--;
    }
    Result.Seconds = Seconds;
    Result.Nanoseconds = (ULONG)(Rest * 100);
    return Result;
}

BTRFS_TIME
BtrfsNow(VOID)
{
    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now);
    return BtrfsFromNtTime(Now);
}

static
NTSTATUS
BtrfsNameUtf8(
    _In_ PCUNICODE_STRING Name,
    _Out_writes_bytes_(BTRFS_NAME_LENGTH) PCHAR Utf8,
    _Out_ PULONG Length)
{
    NTSTATUS Status = BtrfsUnicodeToUtf8(Name, Utf8, BTRFS_NAME_LENGTH, Length);

    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW) return STATUS_OBJECT_NAME_INVALID;
    if (NT_SUCCESS(Status) && (*Length == 0 || *Length > BTRFS_NAME_LENGTH)) return STATUS_OBJECT_NAME_INVALID;
    return Status;
}

NTSTATUS
BtrfsWriteInode(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Inode,
    _In_ const BTRFS_INODE_ITEM *Item)
{
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Root->Id);
    BTRFS_KEY Key;

    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;
    Key.ObjectId = Inode;
    Key.Type = BTRFS_INODE_ITEM_KEY;
    Key.Offset = 0;
    return BtrfsUpdateItem(Vcb, Live, &Key, Item, sizeof(*Item));
}

static
NTSTATUS
BtrfsLastKeyBelow(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ const BTRFS_KEY *Limit,
    _Out_ PBTRFS_KEY Found)
{
    BTRFS_PATH Path;
    NTSTATUS Status;

    RtlZeroMemory(Found, sizeof(*Found));
    Status = BtrfsSearch(Vcb, &Live->Root, Limit, &Path);
    if (NT_SUCCESS(Status) || Status == STATUS_NOT_FOUND)
    {
        Status = BtrfsPreviousItem(Vcb, &Path);
        if (NT_SUCCESS(Status)) *Found = BtrfsPathItem(&Path)->Key;
    }
    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

static
NTSTATUS
BtrfsNextInodeNumber(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _Out_ PULONGLONG Inode)
{
    if (Live->HighestInode == 0)
    {
        BTRFS_KEY Limit, Found;
        NTSTATUS Status;

        Limit.ObjectId = BTRFS_LAST_FREE_OBJECTID;
        Limit.Type = 0;
        Limit.Offset = 0;
        Status = BtrfsLastKeyBelow(Vcb, Live, &Limit, &Found);
        if (!NT_SUCCESS(Status) && Status != STATUS_NO_MORE_ENTRIES) return Status;
        Live->HighestInode = max(Found.ObjectId, BTRFS_FIRST_FREE_OBJECTID);
    }

    if (Live->HighestInode >= BTRFS_LAST_FREE_OBJECTID) return STATUS_DISK_FULL;
    *Inode = ++Live->HighestInode;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsNextDirIndex(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ ULONGLONG Directory,
    _Out_ PULONGLONG Index)
{
    BTRFS_KEY Limit, Found;
    NTSTATUS Status;

    Limit.ObjectId = Directory;
    Limit.Type = BTRFS_DIR_INDEX_KEY;
    Limit.Offset = MAXULONGLONG;
    Status = BtrfsLastKeyBelow(Vcb, Live, &Limit, &Found);
    if (!NT_SUCCESS(Status) && Status != STATUS_NO_MORE_ENTRIES) return Status;

    if (Found.ObjectId == Directory && Found.Type == BTRFS_DIR_INDEX_KEY && Found.Offset >= BTRFS_DIR_CURSOR_FIRST)
        *Index = Found.Offset + 1;
    else
        *Index = BTRFS_DIR_CURSOR_FIRST;
    return STATUS_SUCCESS;
}

static
VOID
BtrfsFillDirItem(
    _Out_ PBTRFS_DIR_ITEM Entry,
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Inode,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length)
{
    Entry->Location.ObjectId = Inode;
    Entry->Location.Type = BTRFS_INODE_ITEM_KEY;
    Entry->Location.Offset = 0;
    Entry->TransId = Vcb->TransId;
    Entry->DataLength = 0;
    Entry->NameLength = (USHORT)Length;
    Entry->Type = Type;
    RtlCopyMemory(Entry->Name, Utf8, Length);
}

static
NTSTATUS
BtrfsAddDirEntries(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ ULONGLONG Directory,
    _In_ ULONGLONG Index,
    _In_ ULONGLONG Inode,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length)
{
    UCHAR Buffer[BTRFS_DIR_ITEM_FIXED_SIZE + BTRFS_NAME_LENGTH];
    ULONG EntrySize = BTRFS_DIR_ITEM_FIXED_SIZE + Length;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    BtrfsFillDirItem((PBTRFS_DIR_ITEM)Buffer, Vcb, Inode, Type, Utf8, Length);

    Key.ObjectId = Directory;
    Key.Type = BTRFS_DIR_ITEM_KEY;
    Key.Offset = BtrfsNameHash(Utf8, Length);

    Status = BtrfsSearchForWrite(Vcb, Live, &Key, (LONG)(EntrySize + sizeof(BTRFS_ITEM)), &Path);
    if (NT_SUCCESS(Status) && Path.Slots[0] < Path.Nodes[0]->Header->ItemCount &&
        BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) == 0)
    {
        ULONG Size;

        BtrfsPathItemData(Vcb, &Path, &Size);
        Status = BtrfsResizeItemAt(Vcb, &Path, Size + EntrySize);
        if (NT_SUCCESS(Status))
        {
            PUCHAR Data = BtrfsPathItemData(Vcb, &Path, &Size);
            RtlCopyMemory(Data + Size - EntrySize, Buffer, EntrySize);
        }
        BtrfsReleasePath(Vcb, &Path);
    }
    else
    {
        BtrfsReleasePath(Vcb, &Path);
        if (NT_SUCCESS(Status)) Status = BtrfsInsertItem(Vcb, Live, &Key, Buffer, EntrySize, NULL);
    }
    if (!NT_SUCCESS(Status)) return Status;

    Key.Type = BTRFS_DIR_INDEX_KEY;
    Key.Offset = Index;
    return BtrfsInsertItem(Vcb, Live, &Key, Buffer, EntrySize, NULL);
}

static
NTSTATUS
BtrfsRemoveDirItem(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ ULONGLONG Directory,
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    Key.ObjectId = Directory;
    Key.Type = BTRFS_DIR_ITEM_KEY;
    Key.Offset = BtrfsNameHash(Utf8, Length);

    Status = BtrfsSearchForWrite(Vcb, Live, &Key, -1, &Path);
    if (NT_SUCCESS(Status))
    {
        ULONG Size, Offset = 0;
        PUCHAR Data;

        Status = STATUS_FILE_CORRUPT_ERROR;
        if (Path.Slots[0] < Path.Nodes[0]->Header->ItemCount && BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) == 0)
        {
            Data = BtrfsPathItemData(Vcb, &Path, &Size);
            while (Data != NULL && Size - Offset >= BTRFS_DIR_ITEM_FIXED_SIZE)
            {
                PBTRFS_DIR_ITEM Entry = (PBTRFS_DIR_ITEM)(Data + Offset);
                ULONG EntrySize = BTRFS_DIR_ITEM_FIXED_SIZE + Entry->NameLength + Entry->DataLength;

                if (EntrySize > Size - Offset) break;
                if (Entry->NameLength == Length && RtlCompareMemory(Entry->Name, Utf8, Length) == Length)
                {
                    if (EntrySize == Size)
                    {
                        Status = BtrfsDeleteItemAt(Vcb, Live, &Path);
                    }
                    else
                    {
                        RtlMoveMemory(Data + Offset, Data + Offset + EntrySize, Size - Offset - EntrySize);
                        Status = BtrfsResizeItemAt(Vcb, &Path, Size - EntrySize);
                    }
                    break;
                }
                Offset += EntrySize;
            }
        }
    }

    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

static
NTSTATUS
BtrfsAddInodeRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ ULONGLONG Inode,
    _In_ ULONGLONG Directory,
    _In_ ULONGLONG Index,
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length)
{
    UCHAR Buffer[FIELD_OFFSET(BTRFS_INODE_REF, Name) + BTRFS_NAME_LENGTH];
    PBTRFS_INODE_REF Ref = (PBTRFS_INODE_REF)Buffer;
    ULONG RefSize = FIELD_OFFSET(BTRFS_INODE_REF, Name) + Length;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    Ref->Index = Index;
    Ref->NameLength = (USHORT)Length;
    RtlCopyMemory(Ref->Name, Utf8, Length);

    Key.ObjectId = Inode;
    Key.Type = BTRFS_INODE_REF_KEY;
    Key.Offset = Directory;

    Status = BtrfsSearchForWrite(Vcb, Live, &Key, (LONG)(RefSize + sizeof(BTRFS_ITEM)), &Path);
    if (NT_SUCCESS(Status) && Path.Slots[0] < Path.Nodes[0]->Header->ItemCount &&
        BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) == 0)
    {
        ULONG Size;

        BtrfsPathItemData(Vcb, &Path, &Size);
        Status = BtrfsResizeItemAt(Vcb, &Path, Size + RefSize);
        if (NT_SUCCESS(Status))
        {
            PUCHAR Data = BtrfsPathItemData(Vcb, &Path, &Size);
            RtlCopyMemory(Data + Size - RefSize, Buffer, RefSize);
        }
        BtrfsReleasePath(Vcb, &Path);
        return Status;
    }

    BtrfsReleasePath(Vcb, &Path);
    if (!NT_SUCCESS(Status)) return Status;
    return BtrfsInsertItem(Vcb, Live, &Key, Buffer, RefSize, NULL);
}

/* The link is found by the name it has on the disk; a name opened without regard to case finds it too */
static
NTSTATUS
BtrfsRemoveInodeRef(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ ULONGLONG Inode,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _Out_writes_bytes_(BTRFS_NAME_LENGTH) PCHAR DiskName,
    _Out_ PULONG DiskLength,
    _Out_ PULONGLONG Index)
{
    CHAR Utf8[BTRFS_NAME_LENGTH];
    ULONG Length;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    *Index = 0;
    *DiskLength = 0;
    Status = BtrfsNameUtf8(Name, Utf8, &Length);
    if (!NT_SUCCESS(Status)) return Status;

    Key.ObjectId = Inode;
    Key.Type = BTRFS_INODE_REF_KEY;
    Key.Offset = Directory;

    Status = BtrfsSearchForWrite(Vcb, Live, &Key, -1, &Path);
    if (NT_SUCCESS(Status))
    {
        ULONG Size, Offset = 0, Match = MAXULONG;
        PUCHAR Data = NULL;

        /* TODO: links kept in extended inode refs are not found */
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        if (Path.Slots[0] < Path.Nodes[0]->Header->ItemCount && BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) == 0)
        {
            Data = BtrfsPathItemData(Vcb, &Path, &Size);
            while (Data != NULL && Size - Offset >= FIELD_OFFSET(BTRFS_INODE_REF, Name))
            {
                PBTRFS_INODE_REF Ref = (PBTRFS_INODE_REF)(Data + Offset);
                ULONG RefSize = FIELD_OFFSET(BTRFS_INODE_REF, Name) + Ref->NameLength;
                UNICODE_STRING Other;

                if (RefSize > Size - Offset)
                {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    Match = MAXULONG;
                    break;
                }
                if (Ref->NameLength == Length && RtlCompareMemory(Ref->Name, Utf8, Length) == Length)
                {
                    Match = Offset;
                    break;
                }
                if (Match == MAXULONG && NT_SUCCESS(BtrfsUtf8ToUnicode(Ref->Name, Ref->NameLength, &Other)))
                {
                    if (RtlEqualUnicodeString(&Other, Name, TRUE)) Match = Offset;
                    ExFreePoolWithTag(Other.Buffer, TAG_BTRFS);
                }
                Offset += RefSize;
            }
        }

        if (Match != MAXULONG)
        {
            PBTRFS_INODE_REF Ref = (PBTRFS_INODE_REF)(Data + Match);
            ULONG RefSize = FIELD_OFFSET(BTRFS_INODE_REF, Name) + Ref->NameLength;

            ASSERT(Ref->NameLength != 0 && Ref->NameLength <= BTRFS_NAME_LENGTH);
            *Index = Ref->Index;
            *DiskLength = Ref->NameLength;
            RtlCopyMemory(DiskName, Ref->Name, Ref->NameLength);

            if (RefSize == Size)
            {
                Status = BtrfsDeleteItemAt(Vcb, Live, &Path);
            }
            else
            {
                RtlMoveMemory(Data + Match, Data + Match + RefSize, Size - Match - RefSize);
                Status = BtrfsResizeItemAt(Vcb, &Path, Size - RefSize);
            }
        }
    }

    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

static
NTSTATUS
BtrfsTouchDirectory(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ LONGLONG SizeChange)
{
    BTRFS_INODE_ITEM Item;
    NTSTATUS Status = BtrfsLoadInode(Vcb, Root, Directory, &Item);

    if (!NT_SUCCESS(Status)) return Status;
    if (SizeChange < 0 && Item.Size < (ULONGLONG)-SizeChange) return STATUS_FILE_CORRUPT_ERROR;

    Item.Size = (ULONGLONG)((LONGLONG)Item.Size + SizeChange);
    Item.TransId = Vcb->TransId;
    Item.Sequence++;
    Item.ModifyTime = Item.ChangeTime = BtrfsNow();
    return BtrfsWriteInode(Vcb, Root, Directory, &Item);
}

NTSTATUS
BtrfsLinkEntry(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONGLONG Inode,
    _In_ UCHAR Type)
{
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Root->Id);
    CHAR Utf8[BTRFS_NAME_LENGTH];
    ULONGLONG Index;
    ULONG Length;
    NTSTATUS Status;

    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;
    Status = BtrfsNameUtf8(Name, Utf8, &Length);
    if (NT_SUCCESS(Status)) Status = BtrfsNextDirIndex(Vcb, Live, Directory, &Index);
    if (NT_SUCCESS(Status)) Status = BtrfsAddInodeRef(Vcb, Live, Inode, Directory, Index, Utf8, Length);
    if (NT_SUCCESS(Status)) Status = BtrfsAddDirEntries(Vcb, Live, Directory, Index, Inode, Type, Utf8, Length);
    if (NT_SUCCESS(Status)) Status = BtrfsTouchDirectory(Vcb, Root, Directory, 2LL * Length);
    return Status;
}

NTSTATUS
BtrfsUnlinkEntry(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONGLONG Inode)
{
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Root->Id);
    CHAR Utf8[BTRFS_NAME_LENGTH];
    ULONGLONG Index;
    ULONG Length;
    BTRFS_KEY Key;
    NTSTATUS Status;

    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;
    Status = BtrfsRemoveInodeRef(Vcb, Live, Inode, Directory, Name, Utf8, &Length, &Index);
    if (NT_SUCCESS(Status)) Status = BtrfsRemoveDirItem(Vcb, Live, Directory, Utf8, Length);
    if (NT_SUCCESS(Status))
    {
        Key.ObjectId = Directory;
        Key.Type = BTRFS_DIR_INDEX_KEY;
        Key.Offset = Index;
        Status = BtrfsDeleteItem(Vcb, Live, &Key);
    }
    if (NT_SUCCESS(Status)) Status = BtrfsTouchDirectory(Vcb, Root, Directory, -2LL * Length);
    return Status;
}

NTSTATUS
BtrfsDeleteLink(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONGLONG Inode,
    _Inout_ PBTRFS_INODE_ITEM Item)
{
    BOOLEAN IsDirectory = ((Item->Mode & BTRFS_S_IFMT) == BTRFS_S_IFDIR);
    NTSTATUS Status;

    ASSERT(!IsDirectory || Item->Size == 0);

    Status = BtrfsUnlinkEntry(Vcb, Root, Directory, Name, Inode);
    if (!NT_SUCCESS(Status)) return Status;

    if (Item->LinkCount != 0) Item->LinkCount--;
    if (Item->LinkCount == 0 || IsDirectory)
    {
        Item->LinkCount = 0;
        return BtrfsRemoveInode(Vcb, Root, Inode);
    }

    Item->TransId = Vcb->TransId;
    Item->Sequence++;
    Item->ChangeTime = BtrfsNow();
    return BtrfsWriteInode(Vcb, Root, Inode, Item);
}

NTSTATUS
BtrfsCreateEntry(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IsDirectory,
    _Out_ PULONGLONG Inode)
{
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Root->Id);
    BTRFS_INODE_ITEM Parent, Item;
    CHAR Utf8[BTRFS_NAME_LENGTH];
    ULONG Length;
    BTRFS_KEY Key;
    NTSTATUS Status;

    *Inode = 0;
    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;

    Status = BtrfsNameUtf8(Name, Utf8, &Length);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, Root, Directory, &Parent);
    if (NT_SUCCESS(Status)) Status = BtrfsNextInodeNumber(Vcb, Live, Inode);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Item, sizeof(Item));
    Item.Generation = Vcb->TransId;
    Item.TransId = Vcb->TransId;
    Item.LinkCount = 1;
    Item.Uid = Parent.Uid;
    Item.Gid = Parent.Gid;
    Item.Mode = IsDirectory ? (BTRFS_S_IFDIR | 0755) : (BTRFS_S_IFREG | 0644);
    Item.Sequence = 1;
    Item.AccessTime = Item.ChangeTime = Item.ModifyTime = Item.CreateTime = BtrfsNow();

    Key.ObjectId = *Inode;
    Key.Type = BTRFS_INODE_ITEM_KEY;
    Key.Offset = 0;
    Status = BtrfsInsertItem(Vcb, Live, &Key, &Item, sizeof(Item), NULL);
    if (!NT_SUCCESS(Status)) return Status;

    return BtrfsLinkEntry(Vcb, Root, Directory, Name, *Inode, IsDirectory ? BTRFS_FT_DIR : BTRFS_FT_REG_FILE);
}

static
NTSTATUS
BtrfsFirstItemOf(
    _In_ PBTRFS_VCB Vcb,
    _In_ PBTRFS_LIVE_ROOT Live,
    _In_ const BTRFS_KEY *From,
    _Out_ PBTRFS_KEY Key,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG Size)
{
    BTRFS_PATH Path;
    NTSTATUS Status;

    *Size = 0;
    Status = BtrfsSearch(Vcb, &Live->Root, From, &Path);
    if (NT_SUCCESS(Status))
    {
        PVOID Data = BtrfsPathItemData(Vcb, &Path, Size);

        *Key = BtrfsPathItem(&Path)->Key;
        if (Data == NULL) Status = STATUS_FILE_CORRUPT_ERROR;
        else RtlCopyMemory(Buffer, Data, min(*Size, BufferSize));
    }
    BtrfsReleasePath(Vcb, &Path);
    return Status;
}

NTSTATUS
BtrfsRemoveInode(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Inode)
{
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Root->Id);
    NTSTATUS Status;

    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;

    for (;;)
    {
        BTRFS_FILE_EXTENT_ITEM Extent;
        BTRFS_KEY From, Key;
        ULONG Size;

        From.ObjectId = Inode;
        From.Type = 0;
        From.Offset = 0;
        Status = BtrfsFirstItemOf(Vcb, Live, &From, &Key, &Extent, sizeof(Extent), &Size);
        if (Status == STATUS_NOT_FOUND) return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;
        if (Key.ObjectId != Inode) return STATUS_SUCCESS;

        if (Key.Type == BTRFS_EXTENT_DATA_KEY && Size >= sizeof(Extent) && Extent.Type != BTRFS_FILE_EXTENT_INLINE &&
            Extent.DiskByteNr != 0)
        {
            Status = BtrfsQueueRef(Vcb, BTRFS_REF_DATA_DROP, Extent.DiskByteNr, Extent.DiskNumBytes, Root->Id, Inode,
                                   Key.Offset - Extent.Offset);
            if (!NT_SUCCESS(Status)) return Status;
        }

        Status = BtrfsDeleteItem(Vcb, Live, &Key);
        if (!NT_SUCCESS(Status)) return Status;
    }
}

/* The first extent item of the file that overlaps [Start, End), and the range of the file it covers */
static
NTSTATUS
BtrfsFindExtentOverlap(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _Out_ PBTRFS_KEY Found,
    _Out_ PBTRFS_FILE_EXTENT_ITEM Extent,
    _Out_ PULONG Size,
    _Out_ PULONGLONG ItemEnd)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    Key.ObjectId = Fcb->Inode;
    Key.Type = BTRFS_EXTENT_DATA_KEY;
    Key.Offset = Start;

    Status = BtrfsSearch(Vcb, &Fcb->Root, &Key, &Path);
    if (Status == STATUS_NOT_FOUND || (NT_SUCCESS(Status) && BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) != 0))
    {
        NTSTATUS Back = BtrfsPreviousItem(Vcb, &Path);

        if (NT_SUCCESS(Back))
        {
            PBTRFS_ITEM Previous = BtrfsPathItem(&Path);

            if (Previous->Key.ObjectId == Fcb->Inode && Previous->Key.Type == BTRFS_EXTENT_DATA_KEY)
                Status = STATUS_SUCCESS;
            else
                Status = BtrfsNextItem(Vcb, &Path);
        }
        else if (Back != STATUS_NO_MORE_ENTRIES)
        {
            Status = Back;
        }
    }

    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        PBTRFS_FILE_EXTENT_ITEM Data;
        ULONGLONG Length;

        if (Item->Key.ObjectId != Fcb->Inode || Item->Key.Type != BTRFS_EXTENT_DATA_KEY || Item->Key.Offset >= End)
        {
            Status = STATUS_NOT_FOUND;
            break;
        }

        Data = BtrfsPathItemData(Vcb, &Path, Size);
        if (Data == NULL || *Size < BTRFS_FILE_EXTENT_INLINE_DATA)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        RtlZeroMemory(Extent, sizeof(*Extent));
        RtlCopyMemory(Extent, Data, min(*Size, sizeof(*Extent)));
        Length = (Extent->Type == BTRFS_FILE_EXTENT_INLINE) ? Extent->RamBytes : Extent->NumBytes;

        if (Item->Key.Offset + Length > Start)
        {
            *Found = Item->Key;
            *ItemEnd = Item->Key.Offset + Length;
            break;
        }
        Status = BtrfsNextItem(Vcb, &Path);
    }

    BtrfsReleasePath(Vcb, &Path);
    if (Status == STATUS_NO_MORE_ENTRIES) Status = STATUS_NOT_FOUND;
    return Status;
}

/* File data in [Start, End) stops belonging to the file; pieces outside the range stay, as trimmed or split items */
static
NTSTATUS
BtrfsDropExtents(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _Out_ PULONGLONG Dropped)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Fcb->Root.Id);
    NTSTATUS Status;

    *Dropped = 0;
    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;

    for (;;)
    {
        BTRFS_FILE_EXTENT_ITEM Extent;
        BTRFS_KEY Key, Right;
        ULONGLONG ItemEnd, BackrefOffset;
        BOOLEAN OnDisk;
        ULONG Size;

        Status = BtrfsFindExtentOverlap(Fcb, Start, End, &Key, &Extent, &Size, &ItemEnd);
        if (Status == STATUS_NOT_FOUND) return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;

        if (Extent.Type == BTRFS_FILE_EXTENT_INLINE)
        {
            Status = BtrfsDeleteItem(Vcb, Live, &Key);
            if (!NT_SUCCESS(Status)) return Status;
            *Dropped += Extent.RamBytes;
            continue;
        }
        if (Size < sizeof(Extent)) return STATUS_FILE_CORRUPT_ERROR;

        OnDisk = (Extent.DiskByteNr != 0);
        BackrefOffset = Key.Offset - Extent.Offset;

        if (Key.Offset >= Start && ItemEnd <= End)
        {
            Status = BtrfsDeleteItem(Vcb, Live, &Key);
            if (NT_SUCCESS(Status) && OnDisk)
                Status = BtrfsQueueRef(Vcb, BTRFS_REF_DATA_DROP, Extent.DiskByteNr, Extent.DiskNumBytes, Fcb->Root.Id, Fcb->Inode,
                                       BackrefOffset);
            if (OnDisk) *Dropped += Extent.NumBytes;
        }
        else if (Key.Offset < Start && ItemEnd > End)
        {
            BTRFS_FILE_EXTENT_ITEM Tail = Extent;

            Extent.NumBytes = Start - Key.Offset;
            Status = BtrfsUpdateItem(Vcb, Live, &Key, &Extent, sizeof(Extent));

            Tail.Offset += End - Key.Offset;
            Tail.NumBytes = ItemEnd - End;
            Right = Key;
            Right.Offset = End;
            if (NT_SUCCESS(Status)) Status = BtrfsInsertItem(Vcb, Live, &Right, &Tail, sizeof(Tail), NULL);
            if (NT_SUCCESS(Status) && OnDisk)
                Status = BtrfsQueueRef(Vcb, BTRFS_REF_DATA_ADD, Extent.DiskByteNr, Extent.DiskNumBytes, Fcb->Root.Id, Fcb->Inode,
                                       BackrefOffset);
            if (OnDisk) *Dropped += End - Start;
        }
        else if (Key.Offset < Start)
        {
            if (OnDisk) *Dropped += ItemEnd - Start;
            Extent.NumBytes = Start - Key.Offset;
            Status = BtrfsUpdateItem(Vcb, Live, &Key, &Extent, sizeof(Extent));
        }
        else
        {
            if (OnDisk) *Dropped += End - Key.Offset;
            Extent.Offset += End - Key.Offset;
            Extent.NumBytes = ItemEnd - End;
            Right = Key;
            Right.Offset = End;
            Status = BtrfsDeleteItem(Vcb, Live, &Key);
            if (NT_SUCCESS(Status)) Status = BtrfsInsertItem(Vcb, Live, &Right, &Extent, sizeof(Extent), NULL);
        }

        if (!NT_SUCCESS(Status)) return Status;
    }
}

static
NTSTATUS
BtrfsWriteExtents(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Start,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONGLONG Length)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    PBTRFS_LIVE_ROOT Live = BtrfsLiveRoot(Vcb, Fcb->Root.Id);
    NTSTATUS Status;

    if (Live == NULL) return STATUS_FILE_CORRUPT_ERROR;
    ASSERT((Start % Vcb->SectorSize) == 0 && (Length % Vcb->SectorSize) == 0);

    while (Length != 0)
    {
        ULONGLONG Piece = min(Length, BTRFS_MAX_EXTENT), Logical;
        BTRFS_FILE_EXTENT_ITEM Extent;
        BTRFS_KEY Key;

        for (;;)
        {
            Status = BtrfsAllocate(Vcb, BTRFS_BLOCK_GROUP_DATA, Piece, &Logical);
            if (Status != STATUS_DISK_FULL || Piece == Vcb->SectorSize) break;
            Piece = max(((Piece / 2) + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1), Vcb->SectorSize);
        }
        if (!NT_SUCCESS(Status)) return Status;

        Status = BtrfsWriteLogical(Vcb, Logical, Data, (ULONG)Piece);
        if (NT_SUCCESS(Status) && !(Fcb->InodeItem.Flags & 1)) Status = BtrfsInsertChecksums(Vcb, Logical, Data, (ULONG)Piece);
        if (!NT_SUCCESS(Status))
        {
            BtrfsPin(Vcb, Logical, Piece);
            return Status;
        }

        RtlZeroMemory(&Extent, sizeof(Extent));
        Extent.Generation = Vcb->TransId;
        Extent.RamBytes = Piece;
        Extent.Type = BTRFS_FILE_EXTENT_REG;
        Extent.DiskByteNr = Logical;
        Extent.DiskNumBytes = Piece;
        Extent.NumBytes = Piece;

        Key.ObjectId = Fcb->Inode;
        Key.Type = BTRFS_EXTENT_DATA_KEY;
        Key.Offset = Start;
        Status = BtrfsInsertItem(Vcb, Live, &Key, &Extent, sizeof(Extent), NULL);
        if (NT_SUCCESS(Status))
            Status = BtrfsQueueRef(Vcb, BTRFS_REF_DATA_ADD, Logical, Piece, Fcb->Root.Id, Fcb->Inode, Start);
        if (!NT_SUCCESS(Status)) return Status;

        Fcb->InodeItem.Bytes += Piece;
        Start += Piece;
        Data += Piece;
        Length -= Piece;
    }

    return STATUS_SUCCESS;
}

static
VOID
BtrfsForgetExtentCache(
    _In_ PBTRFS_FCB Fcb)
{
    ExAcquireFastMutex(&Fcb->ExtentCacheLock);
    if (Fcb->CachedExtent != NULL) ExFreePoolWithTag(Fcb->CachedExtent, TAG_BTRFS_IO);
    Fcb->CachedExtent = NULL;
    Fcb->CachedExtentLength = 0;
    ExReleaseFastMutex(&Fcb->ExtentCacheLock);
}

/* Rewriting [Start, Start + Length) of the file: sectors are written whole, keeping what was around the change */
static
NTSTATUS
BtrfsRewriteRange(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_opt_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN ZeroFromOffset)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Start = Offset & ~((ULONGLONG)Vcb->SectorSize - 1);
    ULONGLONG End = (Offset + Length + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1);
    ULONGLONG Dropped = 0;
    PUCHAR Bounce;
    NTSTATUS Status;

    if (End - Start > MAXULONG) return STATUS_INVALID_PARAMETER;
    Bounce = ExAllocatePoolUninitialized(PagedPool, (SIZE_T)(End - Start), TAG_BTRFS_IO);
    if (Bounce == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Start != Offset || End != Offset + Length || ZeroFromOffset || Buffer == NULL)
        Status = BtrfsReadFileData(Fcb, Start, Bounce, (ULONG)(End - Start));
    else
        Status = STATUS_SUCCESS;

    if (NT_SUCCESS(Status))
    {
        if (ZeroFromOffset)
            RtlZeroMemory(Bounce + (Offset - Start), (SIZE_T)(End - Offset));
        else if (Buffer != NULL)
            RtlCopyMemory(Bounce + (Offset - Start), Buffer, Length);

        Status = BtrfsDropExtents(Fcb, Start, End, &Dropped);
    }
    if (NT_SUCCESS(Status))
    {
        ASSERT(Fcb->InodeItem.Bytes >= Dropped);
        Fcb->InodeItem.Bytes -= min(Dropped, Fcb->InodeItem.Bytes);
        Status = BtrfsWriteExtents(Fcb, Start, Bounce, End - Start);
    }

    ExFreePoolWithTag(Bounce, TAG_BTRFS_IO);
    BtrfsForgetExtentCache(Fcb);
    return Status;
}

/* Linux keeps small files inline in the tree; a change to one turns it into a regular extent first */
static
NTSTATUS
BtrfsUninline(
    _In_ PBTRFS_FCB Fcb)
{
    BTRFS_FILE_EXTENT_ITEM Extent;
    BTRFS_KEY Key;
    ULONGLONG ItemEnd;
    ULONG Size;
    NTSTATUS Status;

    Status = BtrfsFindExtentOverlap(Fcb, 0, 1, &Key, &Extent, &Size, &ItemEnd);
    if (Status == STATUS_NOT_FOUND || (NT_SUCCESS(Status) && Extent.Type != BTRFS_FILE_EXTENT_INLINE)) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    if (Fcb->InodeItem.Size == 0)
    {
        Status = BtrfsDropExtents(Fcb, 0, Fcb->Vcb->SectorSize, &ItemEnd);
        if (NT_SUCCESS(Status)) Fcb->InodeItem.Bytes -= min(ItemEnd, Fcb->InodeItem.Bytes);
        return Status;
    }

    return BtrfsRewriteRange(Fcb, 0, NULL, (ULONG)min(Fcb->InodeItem.Size, Fcb->Vcb->SectorSize), FALSE);
}

static
NTSTATUS
BtrfsFinishInodeChange(
    _In_ PBTRFS_FCB Fcb)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;

    Fcb->InodeItem.TransId = Vcb->TransId;
    Fcb->InodeItem.Sequence++;
    return BtrfsWriteInode(Vcb, &Fcb->Root, Fcb->Inode, &Fcb->InodeItem);
}

/* Data past the end of the file reads as zeros once the file grows over it */
static
NTSTATUS
BtrfsZeroTail(
    _In_ PBTRFS_FCB Fcb)
{
    ULONG SectorSize = Fcb->Vcb->SectorSize;
    ULONGLONG Size = Fcb->InodeItem.Size;

    if ((Size % SectorSize) == 0) return STATUS_SUCCESS;
    return BtrfsRewriteRange(Fcb, Size, NULL, (ULONG)(SectorSize - (Size % SectorSize)), TRUE);
}

NTSTATUS
BtrfsWriteFileData(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->Vcb->TreeLock));
    ASSERT(!Fcb->Directory && !Fcb->Volume);

    if (Fcb->ReparseTag != 0) return STATUS_ACCESS_DENIED;
    Status = BtrfsStartTransaction(Fcb->Vcb);
    if (NT_SUCCESS(Status))
    {
        ULONG SectorSize = Fcb->Vcb->SectorSize;

        BtrfsReleaseReservation(Fcb, Offset, Offset + Length);
        Status = BtrfsReserveSpace(Fcb->Vcb, BTRFS_BLOCK_GROUP_DATA,
                                   Fcb->Vcb->DelallocBytes +
                                   (((ULONGLONG)Length + SectorSize - 1) & ~((ULONGLONG)SectorSize - 1)) + 3ULL * SectorSize);
    }
    if (NT_SUCCESS(Status)) Status = BtrfsUninline(Fcb);
    if (NT_SUCCESS(Status) && Offset > Fcb->InodeItem.Size) Status = BtrfsZeroTail(Fcb);
    if (NT_SUCCESS(Status)) Status = BtrfsRewriteRange(Fcb, Offset, Buffer, Length, FALSE);
    if (NT_SUCCESS(Status))
    {
        if (Offset + Length > Fcb->InodeItem.Size) Fcb->InodeItem.Size = Offset + Length;
        Status = BtrfsFinishInodeChange(Fcb);
    }
    return Status;
}

NTSTATUS
BtrfsTruncate(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG NewSize)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Dropped = 0, Boundary;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));

    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status) && NewSize < Fcb->InodeItem.Size)
        BtrfsReleaseReservation(Fcb, (NewSize + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1), MAXULONGLONG);
    if (NT_SUCCESS(Status)) Status = BtrfsReserveSpace(Vcb, BTRFS_BLOCK_GROUP_DATA, Vcb->DelallocBytes + 2ULL * Vcb->SectorSize);
    if (NT_SUCCESS(Status)) Status = BtrfsUninline(Fcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (NewSize < Fcb->InodeItem.Size)
    {
        if ((NewSize % Vcb->SectorSize) != 0)
        {
            Status = BtrfsRewriteRange(Fcb, NewSize, NULL, (ULONG)(Vcb->SectorSize - (NewSize % Vcb->SectorSize)), TRUE);
            if (!NT_SUCCESS(Status)) return Status;
        }

        Boundary = (NewSize + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1);
        Status = BtrfsDropExtents(Fcb, Boundary, MAXULONGLONG, &Dropped);
        if (!NT_SUCCESS(Status)) return Status;
        Fcb->InodeItem.Bytes -= min(Dropped, Fcb->InodeItem.Bytes);
        BtrfsForgetExtentCache(Fcb);
    }
    else if (NewSize > Fcb->InodeItem.Size)
    {
        Status = BtrfsZeroTail(Fcb);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Fcb->InodeItem.Size = NewSize;
    return BtrfsFinishInodeChange(Fcb);
}

UCHAR
BtrfsFileType(
    _In_ ULONG Mode)
{
    switch (Mode & BTRFS_S_IFMT)
    {
        case BTRFS_S_IFDIR: return BTRFS_FT_DIR;
        case BTRFS_S_IFLNK: return BTRFS_FT_SYMLINK;
        case BTRFS_S_IFCHR: return BTRFS_FT_CHRDEV;
        case BTRFS_S_IFBLK: return BTRFS_FT_BLKDEV;
        case BTRFS_S_IFIFO: return BTRFS_FT_FIFO;
        case BTRFS_S_IFSOCK: return BTRFS_FT_SOCK;
        default: return BTRFS_FT_REG_FILE;
    }
}

/* A directory has one name; following it up from Directory reaches the top of the subvolume */
NTSTATUS
BtrfsIsWithinDirectory(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Ancestor,
    _In_ ULONGLONG Directory,
    _Out_ PBOOLEAN Within)
{
    ULONG Depth;

    *Within = FALSE;
    for (Depth = 0; Depth < 4096; Depth++)
    {
        BTRFS_PATH Path;
        BTRFS_KEY Key;
        NTSTATUS Status;
        ULONGLONG Parent = 0;

        if (Directory == Ancestor)
        {
            *Within = TRUE;
            return STATUS_SUCCESS;
        }
        if (Directory == Root->RootDirId) return STATUS_SUCCESS;

        Key.ObjectId = Directory;
        Key.Type = BTRFS_INODE_REF_KEY;
        Key.Offset = 0;

        BtrfsAcquireTreeShared(Vcb);
        Status = BtrfsSearch(Vcb, Root, &Key, &Path);
        if (NT_SUCCESS(Status))
        {
            PBTRFS_ITEM Item = BtrfsPathItem(&Path);

            if (Item->Key.ObjectId == Directory && Item->Key.Type == BTRFS_INODE_REF_KEY)
                Parent = Item->Key.Offset;
            else
                Status = STATUS_FILE_CORRUPT_ERROR;
        }
        BtrfsReleasePath(Vcb, &Path);
        BtrfsReleaseTree(Vcb);

        if (Status == STATUS_NOT_FOUND) Status = STATUS_FILE_CORRUPT_ERROR;
        if (!NT_SUCCESS(Status)) return Status;
        if (Parent == Directory) return STATUS_SUCCESS;
        Directory = Parent;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}
