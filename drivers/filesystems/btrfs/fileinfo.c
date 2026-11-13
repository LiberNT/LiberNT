/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Querying and setting file information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
VOID
BtrfsBasicInformation(
    _In_ PBTRFS_FCB Fcb,
    _Out_ PFILE_BASIC_INFORMATION Info)
{
    const BTRFS_INODE_ITEM *Inode = &Fcb->InodeItem;

    RtlZeroMemory(Info, sizeof(*Info));
    if (Fcb->Volume) return;

    Info->CreationTime = BtrfsTime(Inode->CreateTime.Seconds ? &Inode->CreateTime : &Inode->ChangeTime);
    Info->LastAccessTime = BtrfsTime(&Inode->AccessTime);
    Info->LastWriteTime = BtrfsTime(&Inode->ModifyTime);
    Info->ChangeTime = BtrfsTime(&Inode->ChangeTime);
    Info->FileAttributes = Fcb->Attributes ? Fcb->Attributes : FILE_ATTRIBUTE_NORMAL;
}

static
VOID
BtrfsStandardInformation(
    _In_ PBTRFS_FCB Fcb,
    _Out_ PFILE_STANDARD_INFORMATION Info)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->AllocationSize = Fcb->Header.AllocationSize;
    Info->EndOfFile = Fcb->Header.FileSize;
    Info->NumberOfLinks = Fcb->Volume ? 1 : max(Fcb->InodeItem.LinkCount, 1);
    Info->DeletePending = Fcb->DeletePending;
    Info->Directory = Fcb->Directory;
}

static
NTSTATUS
BtrfsNameInformation(
    _In_ PBTRFS_CCB Ccb,
    _Out_writes_bytes_(Length) PFILE_NAME_INFORMATION Info,
    _In_ ULONG Length,
    _Out_ PULONG Used)
{
    ULONG Fixed = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
    ULONG Copied;

    *Used = 0;
    if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;

    Info->FileNameLength = Ccb->FullName.Length;
    Copied = min(Length - Fixed, (ULONG)Ccb->FullName.Length) & ~(sizeof(WCHAR) - 1);
    RtlCopyMemory(Info->FileName, Ccb->FullName.Buffer, Copied);
    *Used = Fixed + Copied;
    return (Copied < Ccb->FullName.Length) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsQueryInformationWorker(
    _In_ PBTRFS_FCB Fcb,
    _In_ PBTRFS_CCB Ccb,
    _In_ PFILE_OBJECT FileObject,
    _In_ FILE_INFORMATION_CLASS Class,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    ULONG Used = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    *Information = 0;

#define BTRFS_NEED(Type) do { if (Length < sizeof(Type)) return STATUS_INFO_LENGTH_MISMATCH; Used = sizeof(Type); } while (0)

    switch (Class)
    {
        case FileBasicInformation:
            BTRFS_NEED(FILE_BASIC_INFORMATION);
            BtrfsBasicInformation(Fcb, Buffer);
            break;

        case FileStandardInformation:
            BTRFS_NEED(FILE_STANDARD_INFORMATION);
            BtrfsStandardInformation(Fcb, Buffer);
            break;

        case FileInternalInformation:
            BTRFS_NEED(FILE_INTERNAL_INFORMATION);
            ((PFILE_INTERNAL_INFORMATION)Buffer)->IndexNumber.QuadPart = Fcb->Volume ? 0 : (LONGLONG)BtrfsFileId(&Fcb->Root, Fcb->Inode);
            break;

        case FileEaInformation:
            BTRFS_NEED(FILE_EA_INFORMATION);
            ((PFILE_EA_INFORMATION)Buffer)->EaSize = 0;
            break;

        case FilePositionInformation:
            BTRFS_NEED(FILE_POSITION_INFORMATION);
            ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset = FileObject->CurrentByteOffset;
            break;

        case FileNameInformation:
        case FileNormalizedNameInformation:
            Status = BtrfsNameInformation(Ccb, Buffer, Length, &Used);
            break;

        case FileAlternateNameInformation:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case FileAllInformation:
        {
            PFILE_ALL_INFORMATION All = Buffer;
            ULONG Fixed = FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation);

            if (Length < Fixed + FIELD_OFFSET(FILE_NAME_INFORMATION, FileName)) return STATUS_INFO_LENGTH_MISMATCH;
            RtlZeroMemory(All, Fixed);
            BtrfsBasicInformation(Fcb, &All->BasicInformation);
            BtrfsStandardInformation(Fcb, &All->StandardInformation);
            All->InternalInformation.IndexNumber.QuadPart = Fcb->Volume ? 0 : (LONGLONG)BtrfsFileId(&Fcb->Root, Fcb->Inode);
            All->EaInformation.EaSize = 0;
            All->PositionInformation.CurrentByteOffset = FileObject->CurrentByteOffset;
            Status = BtrfsNameInformation(Ccb, &All->NameInformation, Length - Fixed, &Used);
            Used += Fixed;
            break;
        }

        case FileNetworkOpenInformation:
        {
            PFILE_NETWORK_OPEN_INFORMATION Info = Buffer;
            FILE_BASIC_INFORMATION Basic;

            BTRFS_NEED(FILE_NETWORK_OPEN_INFORMATION);
            RtlZeroMemory(Info, sizeof(*Info));
            BtrfsBasicInformation(Fcb, &Basic);
            Info->CreationTime = Basic.CreationTime;
            Info->LastAccessTime = Basic.LastAccessTime;
            Info->LastWriteTime = Basic.LastWriteTime;
            Info->ChangeTime = Basic.ChangeTime;
            Info->AllocationSize = Fcb->Header.AllocationSize;
            Info->EndOfFile = Fcb->Header.FileSize;
            Info->FileAttributes = Basic.FileAttributes;
            break;
        }

        case FileAttributeTagInformation:
            BTRFS_NEED(FILE_ATTRIBUTE_TAG_INFORMATION);
            ((PFILE_ATTRIBUTE_TAG_INFORMATION)Buffer)->FileAttributes = Fcb->Attributes ? Fcb->Attributes : FILE_ATTRIBUTE_NORMAL;
            ((PFILE_ATTRIBUTE_TAG_INFORMATION)Buffer)->ReparseTag = Fcb->ReparseTag;
            break;

        case FileStreamInformation:
        {
            static const WCHAR DataName[] = L"::$DATA";
            PFILE_STREAM_INFORMATION Info = Buffer;
            ULONG Needed = FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) + sizeof(DataName) - sizeof(WCHAR);

            if (Fcb->Directory || Fcb->Volume) break;
            if (Length < Needed) return STATUS_BUFFER_OVERFLOW;
            RtlZeroMemory(Info, Needed);
            Info->StreamNameLength = sizeof(DataName) - sizeof(WCHAR);
            Info->StreamSize = Fcb->Header.FileSize;
            Info->StreamAllocationSize = Fcb->Header.AllocationSize;
            RtlCopyMemory(Info->StreamName, DataName, Info->StreamNameLength);
            Used = Needed;
            break;
        }

        case FileIdInformation:
        {
            PFILE_ID_INFORMATION Info = Buffer;

            BTRFS_NEED(FILE_ID_INFORMATION);
            RtlZeroMemory(Info, sizeof(*Info));
            Info->VolumeSerialNumber = Fcb->Vcb->Vpb->SerialNumber;
            RtlCopyMemory(&Info->FileId.Identifier[0], &Fcb->Inode, sizeof(ULONGLONG));
            RtlCopyMemory(&Info->FileId.Identifier[8], &Fcb->Root.Id, sizeof(ULONGLONG));
            break;
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }

#undef BTRFS_NEED

    *Information = Used;
    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchQueryInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PBTRFS_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL)
        return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb->Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    Status = BtrfsQueryInformationWorker(Fcb, Ccb, FileObject, Stack->Parameters.QueryFile.FileInformationClass,
                                         Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.QueryFile.Length, &Information);
    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, Status, Information);
}

static
NTSTATUS
BtrfsLastComponent(
    _In_ PCUNICODE_STRING FullName,
    _Out_ PUNICODE_STRING Name)
{
    USHORT End = FullName->Length / sizeof(WCHAR), Start, Index;

    while (End > 1 && FullName->Buffer[End - 1] == L'\\') End--;
    for (Start = End; Start > 0 && FullName->Buffer[Start - 1] != L'\\'; Start--);
    for (Index = Start; Index < End && FullName->Buffer[Index] != L':'; Index++);

    Name->Buffer = FullName->Buffer + Start;
    Name->Length = Name->MaximumLength = (USHORT)((Index - Start) * sizeof(WCHAR));
    return (Name->Length != 0) ? STATUS_SUCCESS : STATUS_OBJECT_NAME_INVALID;
}

static
NTSTATUS
BtrfsCopyName(
    _In_ PCUNICODE_STRING Source,
    _Out_ PUNICODE_STRING Copy)
{
    Copy->Buffer = ExAllocatePoolUninitialized(PagedPool, Source->Length + sizeof(WCHAR), TAG_BTRFS);
    if (Copy->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy->Buffer, Source->Buffer, Source->Length);
    Copy->Buffer[Source->Length / sizeof(WCHAR)] = UNICODE_NULL;
    Copy->Length = Source->Length;
    Copy->MaximumLength = Source->Length + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsMarkDeletePending(
    _In_ PBTRFS_FCB Fcb,
    _In_ PBTRFS_CCB Ccb,
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Delete)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    UNICODE_STRING Name, Copy;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Delete)
    {
        Fcb->DeletePending = FALSE;
        FileObject->DeletePending = FALSE;
        return STATUS_SUCCESS;
    }
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Removed) return STATUS_FILE_DELETED;
    if (Fcb->DeletePending)
    {
        FileObject->DeletePending = TRUE;
        return STATUS_SUCCESS;
    }
    if (Ccb->ParentInode == 0) return STATUS_CANNOT_DELETE;
    if (Fcb->Root.Id != Ccb->ParentRootId) return STATUS_ACCESS_DENIED;

    if (Fcb->Directory)
    {
        BTRFS_INODE_ITEM Item;

        Status = BtrfsLoadInode(Vcb, &Fcb->Root, Fcb->Inode, &Item);
        if (!NT_SUCCESS(Status)) return Status;
        if (Item.Size != 0) return STATUS_DIRECTORY_NOT_EMPTY;
    }
    else if (!MmFlushImageSection(&Fcb->SectionObjectPointers, MmFlushForDelete))
    {
        return STATUS_CANNOT_DELETE;
    }

    Status = BtrfsLastComponent(&Ccb->FullName, &Name);
    if (NT_SUCCESS(Status)) Status = BtrfsCopyName(&Name, &Copy);
    if (!NT_SUCCESS(Status)) return Status;

    if (Fcb->LinkName.Buffer != NULL) ExFreePoolWithTag(Fcb->LinkName.Buffer, TAG_BTRFS);
    Fcb->LinkName = Copy;
    Fcb->ParentInode = Ccb->ParentInode;
    Fcb->DeletePending = TRUE;
    FileObject->DeletePending = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsSetBasicInformation(
    _In_ PBTRFS_FCB Fcb,
    _In_ const FILE_BASIC_INFORMATION *Info)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    BTRFS_INODE_ITEM Item;
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if ((Info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !Fcb->Directory) return STATUS_INVALID_PARAMETER;
    if ((Info->FileAttributes & FILE_ATTRIBUTE_TEMPORARY) && Fcb->Directory) return STATUS_INVALID_PARAMETER;
    if (Info->CreationTime.QuadPart <= 0 && Info->LastAccessTime.QuadPart <= 0 &&
        Info->LastWriteTime.QuadPart <= 0 && Info->ChangeTime.QuadPart <= 0)
    {
        /* TODO: DOS attributes are not stored on the volume */
        return STATUS_SUCCESS;
    }
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Removed) return STATUS_FILE_DELETED;

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, &Fcb->Root, Fcb->Inode, &Item);
    if (NT_SUCCESS(Status))
    {
        if (Info->CreationTime.QuadPart > 0) Item.CreateTime = BtrfsFromNtTime(Info->CreationTime);
        if (Info->LastAccessTime.QuadPart > 0) Item.AccessTime = BtrfsFromNtTime(Info->LastAccessTime);
        if (Info->LastWriteTime.QuadPart > 0) Item.ModifyTime = BtrfsFromNtTime(Info->LastWriteTime);
        Item.ChangeTime = (Info->ChangeTime.QuadPart > 0) ? BtrfsFromNtTime(Info->ChangeTime) : BtrfsNow();
        Item.TransId = Vcb->TransId;
        Item.Sequence++;
        Status = BtrfsWriteInode(Vcb, &Fcb->Root, Fcb->Inode, &Item);
    }
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);

    if (NT_SUCCESS(Status))
    {
        Fcb->InodeItem = Item;
        if (Info->LastWriteTime.QuadPart > 0) Fcb->Modified = FALSE;
    }
    return Status;
}

/* The cache manager's own size updates only move the valid data length, which here always follows the file size */
static
NTSTATUS
BtrfsSetFileSize(
    _In_ PBTRFS_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONGLONG NewSize,
    _In_ BOOLEAN Allocation)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG OldSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    LARGE_INTEGER Size;
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Fcb->Volume || Fcb->Directory) return STATUS_INVALID_PARAMETER;
    if (NewSize > (ULONGLONG)MAXLONGLONG - Vcb->SectorSize) return STATUS_INVALID_PARAMETER;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Removed) return STATUS_FILE_DELETED;
    if (Allocation && NewSize >= OldSize)
    {
        ULONGLONG Growth = ((NewSize + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1)) -
                           (ULONGLONG)Fcb->Header.AllocationSize.QuadPart;

        if ((LONGLONG)Growth <= 0) return STATUS_SUCCESS;

        /* An allocation the volume has no room for fails here, as on NTFS, not when the data is written */
        BtrfsAcquireTreeExclusive(Vcb);
        Sequence = Vcb->ChangeSequence;
        Status = BtrfsStartTransaction(Vcb);
        if (NT_SUCCESS(Status)) Status = BtrfsReserveSpace(Vcb, BTRFS_BLOCK_GROUP_DATA, Vcb->DelallocBytes + Growth);
        Status = BtrfsFinishChange(Vcb, Status, Sequence);
        BtrfsReleaseTree(Vcb);
        return Status;
    }
    if (NewSize == OldSize) return STATUS_SUCCESS;

    Size.QuadPart = (LONGLONG)NewSize;
    if (NewSize < OldSize && !MmCanFileBeTruncated(&Fcb->SectionObjectPointers, &Size)) return STATUS_USER_MAPPED_FILE;

    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsTruncate(Fcb, NewSize);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);
    if (NT_SUCCESS(Status))
    {
        Fcb->Header.FileSize.QuadPart = (LONGLONG)NewSize;
        Fcb->Header.ValidDataLength.QuadPart = (LONGLONG)NewSize;
        Fcb->Header.AllocationSize.QuadPart = (LONGLONG)((NewSize + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1));
        Fcb->Modified = TRUE;
    }
    ExReleaseResourceLite(&Fcb->PagingIoResource);

    if (NT_SUCCESS(Status))
    {
        FileObject->Flags |= FO_FILE_SIZE_CHANGED;
        if (Fcb->SectionObjectPointers.SharedCacheMap != NULL)
            CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    }
    return Status;
}

static
PBTRFS_FCB
BtrfsFindOpenFcb(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG RootId,
    _In_ ULONGLONG Inode)
{
    PLIST_ENTRY Entry;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    for (Entry = Vcb->FcbList.Flink; Entry != &Vcb->FcbList; Entry = Entry->Flink)
    {
        PBTRFS_FCB Candidate = CONTAINING_RECORD(Entry, BTRFS_FCB, VcbLinks);

        if (Candidate->Root.Id == RootId && Candidate->Inode == Inode) return Candidate;
    }
    return NULL;
}

static
NTSTATUS
BtrfsBuildName(
    _In_ PCUNICODE_STRING Directory,
    _In_ PCUNICODE_STRING Name,
    _Out_ PUNICODE_STRING FullName)
{
    BOOLEAN Separator = Directory->Length == 0 || Directory->Buffer[Directory->Length / sizeof(WCHAR) - 1] != L'\\';
    ULONG Length = Directory->Length + (Separator ? sizeof(WCHAR) : 0) + Name->Length;

    if (Length + sizeof(WCHAR) > MAXUSHORT) return STATUS_NAME_TOO_LONG;
    FullName->Buffer = ExAllocatePoolUninitialized(PagedPool, Length + sizeof(WCHAR), TAG_BTRFS);
    if (FullName->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    FullName->Length = 0;
    FullName->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    RtlAppendUnicodeStringToString(FullName, Directory);
    if (Separator) RtlAppendUnicodeToString(FullName, L"\\");
    RtlAppendUnicodeStringToString(FullName, Name);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsRenameOrLink(
    _In_ PBTRFS_FCB Fcb,
    _In_ PBTRFS_CCB Ccb,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ PIRP Irp,
    _In_ BOOLEAN Link)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    FILE_INFORMATION_CLASS Class = Stack->Parameters.SetFile.FileInformationClass;
    PFILE_RENAME_INFORMATION Info = Irp->AssociatedIrp.SystemBuffer;
    PFILE_OBJECT Target = Stack->Parameters.SetFile.FileObject;
    UNICODE_STRING NewName, OldName, DirectoryName, FullName = { 0 };
    ULONGLONG NewParent;
    BOOLEAN Replace, Exists = FALSE, SameInode = FALSE;
    PBTRFS_FCB Victim = NULL;
    BTRFS_INODE_ITEM VictimItem, Item;
    BTRFS_KEY Location;
    ULONG Sequence;
    UCHAR Type;
    USHORT Index;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));
    RtlZeroMemory(&VictimItem, sizeof(VictimItem));
    RtlZeroMemory(&Item, sizeof(Item));

    if (Stack->Parameters.SetFile.Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) ||
        Info->FileNameLength > Stack->Parameters.SetFile.Length - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Removed || Fcb->DeletePending) return STATUS_DELETE_PENDING;
    if (Link && Fcb->Directory) return STATUS_FILE_IS_A_DIRECTORY;
    if (Ccb->ParentInode == 0) return STATUS_ACCESS_DENIED;
    if (Fcb->Root.Id != Ccb->ParentRootId) return STATUS_ACCESS_DENIED;

    Replace = (Class == FileRenameInformationEx || Class == FileLinkInformationEx) ?
              BooleanFlagOn(Info->Flags, FILE_RENAME_REPLACE_IF_EXISTS) : Stack->Parameters.SetFile.ReplaceIfExists;

    if (Target != NULL)
    {
        PBTRFS_FCB TargetFcb = Target->FsContext;
        PBTRFS_CCB TargetCcb = Target->FsContext2;

        if (TargetFcb == NULL || TargetFcb->Signature != BTRFS_FCB_SIGNATURE || TargetCcb == NULL) return STATUS_INVALID_PARAMETER;
        if (TargetFcb->Vcb != Vcb) return STATUS_NOT_SAME_DEVICE;
        if (!TargetFcb->Directory) return STATUS_INVALID_PARAMETER;
        if (TargetFcb->Root.Id != Fcb->Root.Id) return STATUS_NOT_SAME_DEVICE;

        NewName = Target->FileName;
        NewParent = TargetFcb->Inode;
        DirectoryName = TargetCcb->FullName;
    }
    else
    {
        NewName.Buffer = Info->FileName;
        NewName.Length = NewName.MaximumLength = (USHORT)Info->FileNameLength;
        for (Index = 0; Index < NewName.Length / sizeof(WCHAR); Index++)
        {
            if (NewName.Buffer[Index] == L'\\') return STATUS_INVALID_PARAMETER;
        }
        NewParent = Ccb->ParentInode;
        DirectoryName = Ccb->FullName;
        while (DirectoryName.Length > sizeof(WCHAR) && DirectoryName.Buffer[DirectoryName.Length / sizeof(WCHAR) - 1] == L'\\')
            DirectoryName.Length -= sizeof(WCHAR);
        while (DirectoryName.Length != 0 && DirectoryName.Buffer[DirectoryName.Length / sizeof(WCHAR) - 1] != L'\\')
            DirectoryName.Length -= sizeof(WCHAR);
        if (DirectoryName.Length > sizeof(WCHAR)) DirectoryName.Length -= sizeof(WCHAR);
    }

    if (NewName.Length == 0 || NewName.Length > BTRFS_NAME_LENGTH * sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;
    for (Index = 0; Index < NewName.Length / sizeof(WCHAR); Index++)
    {
        WCHAR Char = NewName.Buffer[Index];

        if (Char < 0x20 || Char == L'"' || Char == L'*' || Char == L'/' || Char == L':' || Char == L'<' ||
            Char == L'>' || Char == L'?' || Char == L'\\' || Char == L'|')
        {
            return STATUS_OBJECT_NAME_INVALID;
        }
    }

    Status = BtrfsLastComponent(&Ccb->FullName, &OldName);
    if (!NT_SUCCESS(Status)) return Status;

    if (Fcb->Directory && !Link)
    {
        BOOLEAN Within;

        Status = BtrfsIsWithinDirectory(Vcb, &Fcb->Root, Fcb->Inode, NewParent, &Within);
        if (!NT_SUCCESS(Status)) return Status;
        if (Within) return STATUS_INVALID_PARAMETER;
    }

    Status = BtrfsLookupName(Vcb, &Fcb->Root, NewParent, &NewName, TRUE, &Location, &Type);
    if (NT_SUCCESS(Status))
    {
        Exists = TRUE;
        SameInode = (Location.Type == BTRFS_INODE_ITEM_KEY && Location.ObjectId == Fcb->Inode);
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    if (Exists && SameInode)
    {
        if (Link) return Replace ? STATUS_SUCCESS : STATUS_OBJECT_NAME_COLLISION;
        if (NewParent == Ccb->ParentInode && RtlEqualUnicodeString(&NewName, &OldName, FALSE)) return STATUS_SUCCESS;
        if (!(NewParent == Ccb->ParentInode && RtlEqualUnicodeString(&NewName, &OldName, TRUE)))
        {
            if (!Replace) return STATUS_OBJECT_NAME_COLLISION;
            Exists = TRUE;
        }
        else
        {
            Exists = FALSE;
        }
    }

    if (Exists && !SameInode)
    {
        if (!Replace) return STATUS_OBJECT_NAME_COLLISION;
        if (Location.Type != BTRFS_INODE_ITEM_KEY || Type == BTRFS_FT_DIR) return STATUS_ACCESS_DENIED;
        Victim = BtrfsFindOpenFcb(Vcb, Fcb->Root.Id, Location.ObjectId);
        if (Victim != NULL && Victim->OpenHandles != 0) return STATUS_ACCESS_DENIED;
        if (Victim != NULL)
        {
            if (!MmFlushImageSection(&Victim->SectionObjectPointers, MmFlushForDelete)) return STATUS_ACCESS_DENIED;
            CcPurgeCacheSection(&Victim->SectionObjectPointers, NULL, 0, FALSE);
        }
    }

    Status = BtrfsBuildName(&DirectoryName, &NewName, &FullName);
    if (!NT_SUCCESS(Status)) return Status;

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status) && Exists)
    {
        Status = BtrfsLoadInode(Vcb, &Fcb->Root, Location.ObjectId, &VictimItem);
        if (NT_SUCCESS(Status)) Status = BtrfsDeleteLink(Vcb, &Fcb->Root, NewParent, &NewName, Location.ObjectId, &VictimItem);
    }
    if (NT_SUCCESS(Status) && !Link) Status = BtrfsUnlinkEntry(Vcb, &Fcb->Root, Ccb->ParentInode, &OldName, Fcb->Inode);
    if (NT_SUCCESS(Status)) Status = BtrfsLinkEntry(Vcb, &Fcb->Root, NewParent, &NewName, Fcb->Inode, BtrfsFileType(Fcb->InodeItem.Mode));
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, &Fcb->Root, Fcb->Inode, &Item);
    if (NT_SUCCESS(Status))
    {
        if (Link) Item.LinkCount++;
        Item.TransId = Vcb->TransId;
        Item.Sequence++;
        Item.ChangeTime = BtrfsNow();
        Status = BtrfsWriteInode(Vcb, &Fcb->Root, Fcb->Inode, &Item);
    }
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FullName.Buffer, TAG_BTRFS);
        return Status;
    }

    Fcb->InodeItem.LinkCount = Item.LinkCount;
    Fcb->InodeItem.ChangeTime = Item.ChangeTime;
    Fcb->InodeItem.TransId = Item.TransId;
    Fcb->InodeItem.Sequence = Item.Sequence;
    if (Victim != NULL && VictimItem.LinkCount == 0)
    {
        Victim->Removed = TRUE;
        BtrfsReleaseAllReservations(Victim);
    }

    if (Exists)
        BtrfsNotifyChange(Vcb, &FullName, FILE_NOTIFY_CHANGE_FILE_NAME, FILE_ACTION_REMOVED);

    if (Link)
    {
        BtrfsNotifyChange(Vcb, &FullName, FILE_NOTIFY_CHANGE_FILE_NAME, FILE_ACTION_ADDED);
        ExFreePoolWithTag(FullName.Buffer, TAG_BTRFS);
        return STATUS_SUCCESS;
    }

    BtrfsNotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                      FILE_ACTION_RENAMED_OLD_NAME);
    BtrfsNotifyChange(Vcb, &FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                      FILE_ACTION_RENAMED_NEW_NAME);

    if (Ccb->AllocatedName != NULL) ExFreePoolWithTag(Ccb->AllocatedName, TAG_BTRFS);
    Ccb->AllocatedName = FullName.Buffer;
    Ccb->FullName = FullName;
    Ccb->ParentInode = NewParent;
    Fcb->Attributes = BtrfsInodeAttributes(&Fcb->InodeItem, &NewName, NULL);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
BtrfsDispatchSetInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PBTRFS_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    FILE_INFORMATION_CLASS Class = Stack->Parameters.SetFile.FileInformationClass;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.SetFile.Length;
    PFILE_POSITION_INFORMATION Position;
    BOOLEAN Namespace;
    PBTRFS_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    switch (Class)
    {
        case FilePositionInformation:
            if (Length < sizeof(FILE_POSITION_INFORMATION)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            Position = Buffer;
            if (Position->CurrentByteOffset.QuadPart < 0) return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
            if ((FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING) &&
                (Position->CurrentByteOffset.QuadPart % Vcb->DeviceSectorSize) != 0)
            {
                return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
            }
            FileObject->CurrentByteOffset = Position->CurrentByteOffset;
            return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

        case FileEndOfFileInformation:
            if (Length < sizeof(FILE_END_OF_FILE_INFORMATION)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            if (Stack->Parameters.SetFile.AdvanceOnly)
                return BtrfsCompleteRequest(Irp, Vcb->Writable ? STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED, 0);
            break;

        case FileBasicInformation:
            if (Length < sizeof(FILE_BASIC_INFORMATION)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            break;

        case FileAllocationInformation:
            if (Length < sizeof(FILE_ALLOCATION_INFORMATION)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            break;

        case FileDispositionInformation:
            if (Length < sizeof(FILE_DISPOSITION_INFORMATION)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            break;

        case FileDispositionInformationEx:
            if (Length < sizeof(FILE_DISPOSITION_INFORMATION_EX)) return BtrfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            break;

        case FileRenameInformation:
        case FileRenameInformationEx:
        case FileLinkInformation:
        case FileLinkInformationEx:
            break;

        case FileValidDataLengthInformation:
            return BtrfsCompleteRequest(Irp, Vcb->Writable ? STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED, 0);

        case FileShortNameInformation:
            return BtrfsCompleteRequest(Irp, Vcb->Writable ? STATUS_SHORT_NAMES_NOT_ENABLED_ON_VOLUME : STATUS_MEDIA_WRITE_PROTECTED, 0);

        default:
            return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    }

    Namespace = (Class == FileRenameInformation || Class == FileRenameInformationEx ||
                 Class == FileLinkInformation || Class == FileLinkInformationEx);

    KeEnterCriticalRegion();
    if (Namespace) ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    switch (Class)
    {
        case FileBasicInformation:
            Status = BtrfsSetBasicInformation(Fcb, Buffer);
            if (NT_SUCCESS(Status))
                BtrfsNotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED);
            break;

        case FileEndOfFileInformation:
            Status = BtrfsSetFileSize(Fcb, FileObject, (ULONGLONG)((PFILE_END_OF_FILE_INFORMATION)Buffer)->EndOfFile.QuadPart, FALSE);
            if (NT_SUCCESS(Status)) BtrfsNotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_SIZE, FILE_ACTION_MODIFIED);
            break;

        case FileAllocationInformation:
            Status = BtrfsSetFileSize(Fcb, FileObject, (ULONGLONG)((PFILE_ALLOCATION_INFORMATION)Buffer)->AllocationSize.QuadPart, TRUE);
            break;

        case FileDispositionInformation:
            Status = BtrfsMarkDeletePending(Fcb, Ccb, FileObject, ((PFILE_DISPOSITION_INFORMATION)Buffer)->DeleteFile);
            break;

        case FileDispositionInformationEx:
        {
            ULONG Flags = ((PFILE_DISPOSITION_INFORMATION_EX)Buffer)->Flags;

            if (Flags & FILE_DISPOSITION_ON_CLOSE)
            {
                Ccb->DeleteOnClose = BooleanFlagOn(Flags, FILE_DISPOSITION_DELETE);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = BtrfsMarkDeletePending(Fcb, Ccb, FileObject, BooleanFlagOn(Flags, FILE_DISPOSITION_DELETE));
            }
            break;
        }

        default:
            Status = BtrfsRenameOrLink(Fcb, Ccb, Stack, Irp, (Class == FileLinkInformation || Class == FileLinkInformationEx));
            break;
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    if (Namespace) ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, Status, 0);
}
