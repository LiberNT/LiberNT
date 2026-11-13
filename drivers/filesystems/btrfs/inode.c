/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Subvolumes, inodes, directory entries and file control blocks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_UNIX_EPOCH 116444736000000000LL

NTSTATUS
BtrfsUtf8ToUnicode(
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length,
    _Out_ PUNICODE_STRING Name)
{
    ULONG Bytes = 0;
    NTSTATUS Status;
    PWCHAR Buffer;

    RtlInitEmptyUnicodeString(Name, NULL, 0);

    Status = RtlUTF8ToUnicodeN(NULL, 0, &Bytes, Utf8, Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_SOME_NOT_MAPPED) return Status;
    if (Bytes == 0 || Bytes > MAXUSHORT - sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;

    Buffer = ExAllocatePoolUninitialized(PagedPool, Bytes + sizeof(WCHAR), TAG_BTRFS);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlUTF8ToUnicodeN(Buffer, Bytes, &Bytes, Utf8, Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_SOME_NOT_MAPPED)
    {
        ExFreePoolWithTag(Buffer, TAG_BTRFS);
        return Status;
    }

    Buffer[Bytes / sizeof(WCHAR)] = UNICODE_NULL;
    Name->Buffer = Buffer;
    Name->Length = (USHORT)Bytes;
    Name->MaximumLength = (USHORT)(Bytes + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsUnicodeToUtf8(
    _In_ PCUNICODE_STRING Name,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG Length)
{
    NTSTATUS Status;

    *Length = 0;
    Status = RtlUnicodeToUTF8N(Buffer, BufferSize, Length, Name->Buffer, Name->Length);
    if (Status == STATUS_SOME_NOT_MAPPED) return STATUS_OBJECT_NAME_INVALID;
    return Status;
}

NTSTATUS
BtrfsFindRoot(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Id,
    _Out_ PBTRFS_ROOT Root)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    RtlZeroMemory(Root, sizeof(*Root));

    Key.ObjectId = Id;
    Key.Type = BTRFS_ROOT_ITEM_KEY;
    Key.Offset = 0;

    BtrfsAcquireTreeShared(Vcb);
    Status = BtrfsSearch(Vcb, &Vcb->RootTree, &Key, &Path);
    if (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        ULONG Size;
        PBTRFS_ROOT_ITEM RootItem;

        if (Item->Key.ObjectId != Id || Item->Key.Type != BTRFS_ROOT_ITEM_KEY)
        {
            Status = STATUS_NOT_FOUND;
        }
        else
        {
            RootItem = BtrfsPathItemData(Vcb, &Path, &Size);
            if (RootItem == NULL || Size < sizeof(BTRFS_ROOT_ITEM) || RootItem->Level >= BTRFS_MAX_LEVEL)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            else
            {
                Root->Id = Id;
                Root->ByteNr = RootItem->ByteNr;
                Root->Level = RootItem->Level;
                Root->RootDirId = RootItem->RootDirId ? RootItem->RootDirId : BTRFS_FIRST_FREE_OBJECTID;
            }
        }
    }

    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return Status;
}

NTSTATUS
BtrfsLoadInode(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Inode,
    _Out_ PBTRFS_INODE_ITEM Item)
{
    BTRFS_KEY Key;
    ULONG Size;
    NTSTATUS Status;

    Key.ObjectId = Inode;
    Key.Type = BTRFS_INODE_ITEM_KEY;
    Key.Offset = 0;

    Status = BtrfsFindItem(Vcb, Root, &Key, Item, sizeof(*Item), &Size);
    if (Status == STATUS_NOT_FOUND) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (NT_SUCCESS(Status) && Size < sizeof(*Item)) return STATUS_FILE_CORRUPT_ERROR;
    return Status;
}

NTSTATUS
BtrfsResolveLocation(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Parent,
    _In_ const BTRFS_KEY *Location,
    _Out_ PBTRFS_ROOT Root,
    _Out_ PULONGLONG Inode)
{
    NTSTATUS Status;

    if (Location->Type == BTRFS_INODE_ITEM_KEY)
    {
        *Root = *Parent;
        *Inode = Location->ObjectId;
        return STATUS_SUCCESS;
    }

    if (Location->Type != BTRFS_ROOT_ITEM_KEY) return STATUS_FILE_CORRUPT_ERROR;

    Status = BtrfsFindRoot(Vcb, Location->ObjectId, Root);
    if (Status == STATUS_NOT_FOUND) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!NT_SUCCESS(Status)) return Status;

    *Inode = Root->RootDirId;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsLookupExact(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length,
    _Out_ PBTRFS_KEY Location,
    _Out_ PUCHAR Type)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    Key.ObjectId = Directory;
    Key.Type = BTRFS_DIR_ITEM_KEY;
    Key.Offset = BtrfsNameHash(Utf8, Length);

    BtrfsAcquireTreeShared(Vcb);
    Status = BtrfsSearch(Vcb, Root, &Key, &Path);
    if (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        PUCHAR Data;
        ULONG Size, Offset = 0;

        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        if (BtrfsCompareKeys(&Item->Key, &Key) == 0)
        {
            Data = BtrfsPathItemData(Vcb, &Path, &Size);
            if (Data == NULL) Status = STATUS_FILE_CORRUPT_ERROR;

            while (Data != NULL && Size - Offset >= BTRFS_DIR_ITEM_FIXED_SIZE)
            {
                PBTRFS_DIR_ITEM Entry = (PBTRFS_DIR_ITEM)(Data + Offset);
                ULONG EntrySize = BTRFS_DIR_ITEM_FIXED_SIZE + Entry->NameLength + Entry->DataLength;

                if (EntrySize > Size - Offset)
                {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }

                if (Entry->NameLength == Length && RtlCompareMemory(Entry->Name, Utf8, Length) == Length)
                {
                    *Location = Entry->Location;
                    *Type = Entry->Type;
                    Status = STATUS_SUCCESS;
                    break;
                }

                Offset += EntrySize;
            }
        }
    }
    else if (Status == STATUS_NOT_FOUND)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return Status;
}

NTSTATUS
BtrfsLookupName(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _Out_ PBTRFS_KEY Location,
    _Out_ PUCHAR Type)
{
    CHAR Utf8[BTRFS_NAME_LENGTH * 4];
    BTRFS_DIR_ENTRY Entry;
    ULONGLONG Cursor = BTRFS_DIR_CURSOR_FIRST;
    ULONG Length;
    NTSTATUS Status;

    Status = BtrfsUnicodeToUtf8(Name, Utf8, sizeof(Utf8), &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || (NT_SUCCESS(Status) && Length > BTRFS_NAME_LENGTH)) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!NT_SUCCESS(Status)) return Status;

    Status = BtrfsLookupExact(Vcb, Root, Directory, Utf8, Length, Location, Type);
    if (Status != STATUS_OBJECT_NAME_NOT_FOUND || !IgnoreCase) return Status;

    for (;;)
    {
        BOOLEAN Exact;

        Status = BtrfsReadDirEntry(Vcb, Root, Directory, Cursor, &Entry);
        if (!NT_SUCCESS(Status)) break;

        Cursor = Entry.Index + 1;
        if (RtlEqualUnicodeString(&Entry.Name, Name, TRUE))
        {
            Exact = RtlEqualUnicodeString(&Entry.Name, Name, FALSE);
            ASSERTMSG("Btrfs: the name hash missed an entry of the same name\n", !Exact);
            UNREFERENCED_PARAMETER(Exact);
            *Location = Entry.Location;
            *Type = Entry.Type;
            BtrfsFreeDirEntry(&Entry);
            return STATUS_SUCCESS;
        }
        BtrfsFreeDirEntry(&Entry);
    }

    return (Status == STATUS_NO_MORE_ENTRIES) ? STATUS_OBJECT_NAME_NOT_FOUND : Status;
}

NTSTATUS
BtrfsReadDirEntry(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ ULONGLONG FromIndex,
    _Out_ PBTRFS_DIR_ENTRY Entry)
{
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    RtlZeroMemory(Entry, sizeof(*Entry));

    Key.ObjectId = Directory;
    Key.Type = BTRFS_DIR_INDEX_KEY;
    Key.Offset = FromIndex;

    BtrfsAcquireTreeShared(Vcb);
    Status = BtrfsSearch(Vcb, Root, &Key, &Path);
    if (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);

        if (Item->Key.ObjectId != Directory || Item->Key.Type != BTRFS_DIR_INDEX_KEY)
        {
            Status = STATUS_NO_MORE_ENTRIES;
        }
        else
        {
            ULONG Size;
            PBTRFS_DIR_ITEM DirItem = BtrfsPathItemData(Vcb, &Path, &Size);

            if (DirItem == NULL || Size < BTRFS_DIR_ITEM_FIXED_SIZE ||
                Size < BTRFS_DIR_ITEM_FIXED_SIZE + (ULONG)DirItem->NameLength + DirItem->DataLength ||
                DirItem->NameLength == 0)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            else
            {
                Entry->Index = Item->Key.Offset;
                Entry->Location = DirItem->Location;
                Entry->Type = DirItem->Type;
                Status = BtrfsUtf8ToUnicode(DirItem->Name, DirItem->NameLength, &Entry->Name);
            }
        }
    }
    else if (Status == STATUS_NOT_FOUND)
    {
        Status = STATUS_NO_MORE_ENTRIES;
    }

    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return Status;
}

VOID
BtrfsFreeDirEntry(
    _Inout_ PBTRFS_DIR_ENTRY Entry)
{
    if (Entry->Name.Buffer != NULL) ExFreePoolWithTag(Entry->Name.Buffer, TAG_BTRFS);
    RtlZeroMemory(Entry, sizeof(*Entry));
}

ULONG
BtrfsInodeAttributes(
    _In_ const BTRFS_INODE_ITEM *Item,
    _In_opt_ PCUNICODE_STRING Name,
    _Out_opt_ PULONG ReparseTag)
{
    ULONG Attributes = 0, Tag = 0;

    switch (Item->Mode & BTRFS_S_IFMT)
    {
        case BTRFS_S_IFDIR: Attributes = FILE_ATTRIBUTE_DIRECTORY; break;
        case BTRFS_S_IFLNK: Tag = IO_REPARSE_TAG_LX_SYMLINK; break;
        case BTRFS_S_IFIFO: Tag = IO_REPARSE_TAG_LX_FIFO; break;
        case BTRFS_S_IFCHR: Tag = IO_REPARSE_TAG_LX_CHR; break;
        case BTRFS_S_IFBLK: Tag = IO_REPARSE_TAG_LX_BLK; break;
        case BTRFS_S_IFSOCK: Tag = IO_REPARSE_TAG_AF_UNIX; break;
        default: Attributes = FILE_ATTRIBUTE_ARCHIVE; break;
    }

    if (Tag != 0) Attributes |= FILE_ATTRIBUTE_REPARSE_POINT;
    if (Name != NULL && Name->Length > sizeof(WCHAR) && Name->Buffer[0] == L'.') Attributes |= FILE_ATTRIBUTE_HIDDEN;
    if (ReparseTag != NULL) *ReparseTag = Tag;
    return Attributes;
}

LARGE_INTEGER
BtrfsTime(
    _In_ const BTRFS_TIME *Time)
{
    LARGE_INTEGER Result;
    LONGLONG Limit = (MAXLONGLONG - BTRFS_UNIX_EPOCH) / 10000000;

    if (Time->Seconds < -(BTRFS_UNIX_EPOCH / 10000000) || Time->Seconds > Limit)
        Result.QuadPart = (Time->Seconds < 0) ? 0 : MAXLONGLONG;
    else
        Result.QuadPart = Time->Seconds * 10000000 + Time->Nanoseconds / 100 + BTRFS_UNIX_EPOCH;
    return Result;
}

ULONGLONG
BtrfsFileId(
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Inode)
{
    if (Root->Id == BTRFS_FS_TREE_OBJECTID) return Inode;
    return (Root->Id << 48) ^ Inode;
}

static
PBTRFS_FCB
BtrfsAllocateFcb(
    _In_ PBTRFS_VCB Vcb)
{
    PBTRFS_FCB Fcb = ExAllocatePoolZero(NonPagedPool, sizeof(BTRFS_FCB), TAG_BTRFS_FCB);

    if (Fcb == NULL) return NULL;

    Fcb->Signature = BTRFS_FCB_SIGNATURE;
    Fcb->Vcb = Vcb;
    Fcb->References = 1;
    ExInitializeFastMutex(&Fcb->HeaderMutex);
    ExInitializeFastMutex(&Fcb->ExtentCacheLock);
    ExInitializeResourceLite(&Fcb->MainResource);
    ExInitializeResourceLite(&Fcb->PagingIoResource);
    FsRtlSetupAdvancedHeader(&Fcb->Header, &Fcb->HeaderMutex);
    Fcb->Header.NodeTypeCode = (CSHORT)0xB7F1;
    Fcb->Header.NodeByteSize = sizeof(BTRFS_FCB);
    Fcb->Header.Resource = &Fcb->MainResource;
    Fcb->Header.PagingIoResource = &Fcb->PagingIoResource;
    Fcb->Header.IsFastIoPossible = FastIoIsPossible;
    FsRtlInitializeFileLock(&Fcb->FileLock, NULL, NULL);
    return Fcb;
}

static
VOID
BtrfsFreeFcb(
    _In_ PBTRFS_FCB Fcb)
{
    ASSERT(Fcb->References == 0 && Fcb->OpenHandles == 0);
    ASSERT(Fcb->SectionObjectPointers.DataSectionObject == NULL);
    ASSERT(Fcb->SectionObjectPointers.SharedCacheMap == NULL);

    if (!Fcb->Volume) BtrfsReleaseAllReservations(Fcb);
    FsRtlTeardownPerStreamContexts(&Fcb->Header);
    FsRtlUninitializeFileLock(&Fcb->FileLock);
    ExDeleteResourceLite(&Fcb->MainResource);
    ExDeleteResourceLite(&Fcb->PagingIoResource);
    if (Fcb->CachedExtent != NULL) ExFreePoolWithTag(Fcb->CachedExtent, TAG_BTRFS_IO);
    if (Fcb->LinkName.Buffer != NULL) ExFreePoolWithTag(Fcb->LinkName.Buffer, TAG_BTRFS);
    Fcb->Signature = 0;
    ExFreePoolWithTag(Fcb, TAG_BTRFS_FCB);
}

PBTRFS_FCB
BtrfsCreateVolumeFcb(
    _In_ PBTRFS_VCB Vcb)
{
    PBTRFS_FCB Fcb = BtrfsAllocateFcb(Vcb);

    if (Fcb == NULL) return NULL;

    Fcb->Volume = TRUE;
    Fcb->Header.AllocationSize.QuadPart = (LONGLONG)Vcb->PartitionLength;
    Fcb->Header.FileSize.QuadPart = (LONGLONG)Vcb->PartitionLength;
    Fcb->Header.ValidDataLength.QuadPart = (LONGLONG)Vcb->PartitionLength;
    return Fcb;
}

NTSTATUS
BtrfsGetFcb(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Inode,
    _In_opt_ PCUNICODE_STRING Name,
    _Out_ PBTRFS_FCB *Fcb)
{
    PBTRFS_FCB New;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    LONGLONG Rounded;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    *Fcb = NULL;

    for (Entry = Vcb->FcbList.Flink; Entry != &Vcb->FcbList; Entry = Entry->Flink)
    {
        PBTRFS_FCB Candidate = CONTAINING_RECORD(Entry, BTRFS_FCB, VcbLinks);

        if (Candidate->Root.Id == Root->Id && Candidate->Inode == Inode)
        {
            BtrfsReferenceFcb(Candidate);
            *Fcb = Candidate;
            return STATUS_SUCCESS;
        }
    }

    New = BtrfsAllocateFcb(Vcb);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    New->Root = *Root;
    New->Inode = Inode;
    Status = BtrfsLoadInode(Vcb, Root, Inode, &New->InodeItem);
    if (!NT_SUCCESS(Status))
    {
        New->References = 0;
        BtrfsFreeFcb(New);
        return Status;
    }

    New->Directory = ((New->InodeItem.Mode & BTRFS_S_IFMT) == BTRFS_S_IFDIR);
    New->Attributes = BtrfsInodeAttributes(&New->InodeItem, Name, &New->ReparseTag);

    if (!New->Directory)
    {
        if (New->InodeItem.Size > MAXLONGLONG - Vcb->SectorSize)
        {
            New->References = 0;
            BtrfsFreeFcb(New);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Rounded = (LONGLONG)((New->InodeItem.Size + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1));
        New->Header.AllocationSize.QuadPart = Rounded;
        New->Header.FileSize.QuadPart = (LONGLONG)New->InodeItem.Size;
        New->Header.ValidDataLength.QuadPart = (LONGLONG)New->InodeItem.Size;
    }

    InsertTailList(&Vcb->FcbList, &New->VcbLinks);
    BtrfsReferenceVcb(Vcb);
    *Fcb = New;
    return STATUS_SUCCESS;
}

VOID
BtrfsReferenceFcb(
    _In_ PBTRFS_FCB Fcb)
{
    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);
    ASSERT(Fcb->References > 0);
    InterlockedIncrement(&Fcb->References);
}

VOID
BtrfsDereferenceFcb(
    _In_ PBTRFS_FCB Fcb)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    BOOLEAN Volume = Fcb->Volume;

    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);

    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    ASSERT(Fcb->References > 0);
    if (InterlockedDecrement(&Fcb->References) != 0)
    {
        ExReleaseResourceLite(&Vcb->Resource);
        return;
    }

    if (!Volume)
    {
        RemoveEntryList(&Fcb->VcbLinks);
        if (Vcb->RootFcb == Fcb) Vcb->RootFcb = NULL;
    }
    else if (Vcb->VolumeFcb == Fcb)
    {
        Vcb->VolumeFcb = NULL;
    }
    ExReleaseResourceLite(&Vcb->Resource);

    BtrfsFreeFcb(Fcb);
    BtrfsDereferenceVcb(Vcb);
}
