/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File information queries and changes: attributes, sizes, names, links and deletion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_MAX_DEPTH 4096

static
ULONG
Ext4HandleAttributes(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb)
{
    ULONG Attributes;

    if (Fcb->Volume) return FILE_ATTRIBUTE_NORMAL;
    Attributes = Ext4FileAttributes(&Fcb->Attr, Ccb->Name, Ccb->NameLength, NULL);
    return Attributes ? Attributes : FILE_ATTRIBUTE_NORMAL;
}

static
LONGLONG
Ext4AllocationSize(
    _In_ PEXT4_FCB Fcb)
{
    if (Fcb->Directory) return (LONGLONG)ALIGN_UP_BY(Fcb->Attr.Size, (ULONGLONG)Fcb->Vcb->BlockSize);
    return Fcb->Header.AllocationSize.QuadPart;
}

static
LONGLONG
Ext4EndOfFile(
    _In_ PEXT4_FCB Fcb)
{
    return Fcb->Directory ? 0 : Fcb->Header.FileSize.QuadPart;
}

static
VOID
Ext4BasicInformation(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _Out_ PFILE_BASIC_INFORMATION Info)
{
    Info->CreationTime = Ext4TimeToNt(&Fcb->Attr.Create);
    Info->LastAccessTime = Ext4TimeToNt(&Fcb->Attr.Access);
    Info->LastWriteTime = Ext4TimeToNt(&Fcb->Attr.Modify);
    Info->ChangeTime = Ext4TimeToNt(&Fcb->Attr.Change);
    Info->FileAttributes = Ext4HandleAttributes(Fcb, Ccb);
}

static
VOID
Ext4StandardInformation(
    _In_ PEXT4_FCB Fcb,
    _Out_ PFILE_STANDARD_INFORMATION Info)
{
    Info->AllocationSize.QuadPart = Ext4AllocationSize(Fcb);
    Info->EndOfFile.QuadPart = Ext4EndOfFile(Fcb);
    Info->NumberOfLinks = Fcb->Orphaned ? 0 : max(Fcb->Attr.Links, 1);
    Info->DeletePending = Fcb->DeletePending;
    Info->Directory = Fcb->Directory;
}

VOID
Ext4FillStatLx(
    _In_ PEXT4_FCB Fcb,
    _Out_ PFILE_STAT_LX_INFORMATION Stat)
{
    ULONG Type = Fcb->Attr.Mode & E4_S_IFMT;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->FileId.QuadPart = Fcb->Inode;
    Stat->CreationTime = Ext4TimeToNt(&Fcb->Attr.Create);
    Stat->LastAccessTime = Ext4TimeToNt(&Fcb->Attr.Access);
    Stat->LastWriteTime = Ext4TimeToNt(&Fcb->Attr.Modify);
    Stat->ChangeTime = Ext4TimeToNt(&Fcb->Attr.Change);
    Stat->AllocationSize.QuadPart = Ext4AllocationSize(Fcb);
    Stat->EndOfFile.QuadPart = Ext4EndOfFile(Fcb);
    Stat->FileAttributes = Ext4FileAttributes(&Fcb->Attr, NULL, 0, &Stat->ReparseTag);
    if (Stat->FileAttributes == 0) Stat->FileAttributes = FILE_ATTRIBUTE_NORMAL;
    Stat->NumberOfLinks = Fcb->Orphaned ? 0 : max(Fcb->Attr.Links, 1);
    Stat->EffectiveAccess = Fcb->Vcb->Writable ? FILE_ALL_ACCESS : EXT4_READ_ACCESS;
    Stat->LxFlags = LX_FILE_METADATA_HAS_UID | LX_FILE_METADATA_HAS_GID | LX_FILE_METADATA_HAS_MODE;
    if (Fcb->Directory) Stat->LxFlags |= LX_FILE_CASE_SENSITIVE_DIR;
    Stat->LxUid = Fcb->Attr.Uid;
    Stat->LxGid = Fcb->Attr.Gid;
    Stat->LxMode = Fcb->Attr.Mode;
    if (Type == E4_S_IFCHR || Type == E4_S_IFBLK)
    {
        Stat->LxFlags |= LX_FILE_METADATA_HAS_DEVICE_ID;
        Stat->LxDeviceIdMajor = Fcb->Attr.DeviceMajor;
        Stat->LxDeviceIdMinor = Fcb->Attr.DeviceMinor;
    }
}

static
NTSTATUS
Ext4NameInformation(
    _In_ PEXT4_CCB Ccb,
    _Out_writes_bytes_(Length) PFILE_NAME_INFORMATION Info,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    ULONG Available = Length - FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
    ULONG Copy = min(Available, Ccb->FullName.Length);

    Info->FileNameLength = Ccb->FullName.Length;
    RtlCopyMemory(Info->FileName, Ccb->FullName.Buffer, Copy);
    *Information = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + Copy;
    return (Copy < Ccb->FullName.Length) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

static
NTSTATUS
Ext4QueryInformationWorker(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PFILE_OBJECT FileObject,
    _In_ FILE_INFORMATION_CLASS Class,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    *Information = 0;

#define EXT4_NEED(Type) do { if (Length < sizeof(Type)) return STATUS_INFO_LENGTH_MISMATCH; RtlZeroMemory(Buffer, sizeof(Type)); } while (0)

    switch (Class)
    {
        case FileBasicInformation:
            EXT4_NEED(FILE_BASIC_INFORMATION);
            Ext4BasicInformation(Fcb, Ccb, Buffer);
            *Information = sizeof(FILE_BASIC_INFORMATION);
            return STATUS_SUCCESS;

        case FileStandardInformation:
            EXT4_NEED(FILE_STANDARD_INFORMATION);
            Ext4StandardInformation(Fcb, Buffer);
            *Information = sizeof(FILE_STANDARD_INFORMATION);
            return STATUS_SUCCESS;

        case FileInternalInformation:
            EXT4_NEED(FILE_INTERNAL_INFORMATION);
            ((PFILE_INTERNAL_INFORMATION)Buffer)->IndexNumber.QuadPart = Fcb->Inode;
            *Information = sizeof(FILE_INTERNAL_INFORMATION);
            return STATUS_SUCCESS;

        case FileEaInformation:
            EXT4_NEED(FILE_EA_INFORMATION);
            ((PFILE_EA_INFORMATION)Buffer)->EaSize = Fcb->Volume ? 0 : Ext4EaSize(Fcb);
            *Information = sizeof(FILE_EA_INFORMATION);
            return STATUS_SUCCESS;

        case FilePositionInformation:
            EXT4_NEED(FILE_POSITION_INFORMATION);
            ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset = FileObject->CurrentByteOffset;
            *Information = sizeof(FILE_POSITION_INFORMATION);
            return STATUS_SUCCESS;

        case FileNameInformation:
        case FileNormalizedNameInformation:
            if (Length < sizeof(FILE_NAME_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            return Ext4NameInformation(Ccb, Buffer, Length, Information);

        case FileAlternateNameInformation:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case FileAllInformation:
        {
            PFILE_ALL_INFORMATION All = Buffer;
            ULONG_PTR NameBytes;
            NTSTATUS Status;

            if (Length < sizeof(FILE_ALL_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            RtlZeroMemory(All, sizeof(*All));
            Ext4BasicInformation(Fcb, Ccb, &All->BasicInformation);
            Ext4StandardInformation(Fcb, &All->StandardInformation);
            All->InternalInformation.IndexNumber.QuadPart = Fcb->Inode;
            All->EaInformation.EaSize = Fcb->Volume ? 0 : Ext4EaSize(Fcb);
            All->PositionInformation.CurrentByteOffset = FileObject->CurrentByteOffset;
            Status = Ext4NameInformation(Ccb, &All->NameInformation, Length - FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation),
                                         &NameBytes);
            *Information = FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation) + NameBytes;
            return Status;
        }

        case FileNetworkOpenInformation:
        {
            PFILE_NETWORK_OPEN_INFORMATION Info = Buffer;

            EXT4_NEED(FILE_NETWORK_OPEN_INFORMATION);
            Info->CreationTime = Ext4TimeToNt(&Fcb->Attr.Create);
            Info->LastAccessTime = Ext4TimeToNt(&Fcb->Attr.Access);
            Info->LastWriteTime = Ext4TimeToNt(&Fcb->Attr.Modify);
            Info->ChangeTime = Ext4TimeToNt(&Fcb->Attr.Change);
            Info->AllocationSize.QuadPart = Ext4AllocationSize(Fcb);
            Info->EndOfFile.QuadPart = Ext4EndOfFile(Fcb);
            Info->FileAttributes = Ext4HandleAttributes(Fcb, Ccb);
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileAttributeTagInformation:
        {
            PFILE_ATTRIBUTE_TAG_INFORMATION Info = Buffer;

            EXT4_NEED(FILE_ATTRIBUTE_TAG_INFORMATION);
            Info->FileAttributes = Ext4HandleAttributes(Fcb, Ccb);
            Info->ReparseTag = Fcb->ReparseTag;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileStreamInformation:
        {
            static const WCHAR DataName[] = L"::$DATA";
            PFILE_STREAM_INFORMATION Info = Buffer;
            ULONG Needed = FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) + sizeof(DataName) - sizeof(WCHAR);

            if (Fcb->Directory || Fcb->Volume) return STATUS_SUCCESS;
            if (Length < Needed) return STATUS_BUFFER_OVERFLOW;
            Info->NextEntryOffset = 0;
            Info->StreamNameLength = sizeof(DataName) - sizeof(WCHAR);
            Info->StreamSize.QuadPart = Ext4EndOfFile(Fcb);
            Info->StreamAllocationSize.QuadPart = Ext4AllocationSize(Fcb);
            RtlCopyMemory(Info->StreamName, DataName, sizeof(DataName) - sizeof(WCHAR));
            *Information = Needed;
            return STATUS_SUCCESS;
        }

        case FileIdInformation:
        {
            PFILE_ID_INFORMATION Info = Buffer;
            ULONGLONG Inode = Fcb->Inode;

            EXT4_NEED(FILE_ID_INFORMATION);
            Info->VolumeSerialNumber = Fcb->Vcb->Vpb->SerialNumber;
            RtlCopyMemory(Info->FileId.Identifier, &Inode, sizeof(Inode));
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileStatInformation:
        {
            FILE_STAT_LX_INFORMATION Lx;

            EXT4_NEED(FILE_STAT_INFORMATION);
            Ext4FillStatLx(Fcb, &Lx);
            RtlCopyMemory(Buffer, &Lx, sizeof(FILE_STAT_INFORMATION));
            *Information = sizeof(FILE_STAT_INFORMATION);
            return STATUS_SUCCESS;
        }

        case FileStatLxInformation:
            EXT4_NEED(FILE_STAT_LX_INFORMATION);
            Ext4FillStatLx(Fcb, Buffer);
            *Information = sizeof(FILE_STAT_LX_INFORMATION);
            return STATUS_SUCCESS;

        case FileCaseSensitiveInformation:
            EXT4_NEED(FILE_CASE_SENSITIVE_INFORMATION);
            if (!Fcb->Directory) return STATUS_NOT_A_DIRECTORY;
            ((PFILE_CASE_SENSITIVE_INFORMATION)Buffer)->Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
            *Information = sizeof(FILE_CASE_SENSITIVE_INFORMATION);
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

#undef EXT4_NEED
}

NTSTATUS
NTAPI
Ext4DispatchQueryInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PEXT4_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    if (Fcb->Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    Status = Ext4QueryInformationWorker(Fcb, Ccb, FileObject, Stack->Parameters.QueryFile.FileInformationClass,
                                        Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.QueryFile.Length, &Information);
    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, Information);
}

NTSTATUS
Ext4MarkDeletePending(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Delete)
{
    PEXT4_VCB Vcb = Fcb->Vcb;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Delete)
    {
        Fcb->DeletePending = FALSE;
        FileObject->DeletePending = FALSE;
        return STATUS_SUCCESS;
    }
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Orphaned) return STATUS_FILE_DELETED;
    if (Ccb->ParentInode == 0 || Ccb->NameLength == 0) return STATUS_CANNOT_DELETE;

    if (Fcb->Directory)
    {
        bool Empty = false;
        NTSTATUS Status;

        Ext4LockCore(Vcb);
        Status = Ext4Status(Vcb, E4IsEmptyDir(&Vcb->Core, Fcb->Inode, &Empty));
        Ext4UnlockCore(Vcb);
        if (!NT_SUCCESS(Status)) return Status;
        if (!Empty) return STATUS_DIRECTORY_NOT_EMPTY;
    }
    else if (!MmFlushImageSection(&Fcb->SectionObjectPointers, MmFlushForDelete))
    {
        return STATUS_CANNOT_DELETE;
    }

    Fcb->DeleteParent = Ccb->ParentInode;
    Fcb->DeleteNameLength = Ccb->NameLength;
    RtlCopyMemory(Fcb->DeleteName, Ccb->Name, Ccb->NameLength);
    Fcb->DeletePending = TRUE;
    FileObject->DeletePending = TRUE;
    return STATUS_SUCCESS;
}

/* Removes the name now and leaves the file to its open handles */
static
NTSTATUS
Ext4PosixDelete(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (Fcb->Orphaned) return STATUS_FILE_DELETED;

    Status = Ext4UnlinkName(Fcb, Ccb->ParentInode, Ccb->Name, Ccb->NameLength);
    if (!NT_SUCCESS(Status)) return Status;

    Ext4NotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                     FILE_ACTION_REMOVED);
    Ccb->ParentInode = 0;
    Ccb->NameLength = 0;
    Ccb->DeleteOnClose = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4SetBasicInformation(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PFILE_BASIC_INFORMATION Info)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    E4_ATTR Attr = Fcb->Attr;
    ULONG Mask = 0;
    NTSTATUS Status;

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    if (Info->CreationTime.QuadPart > 0)
    {
        Ext4TimeFromNt(Info->CreationTime, &Attr.Create);
        Mask |= E4_SET_CREATE;
    }
    if (Info->LastAccessTime.QuadPart > 0)
    {
        Ext4TimeFromNt(Info->LastAccessTime, &Attr.Access);
        Mask |= E4_SET_ACCESS;
    }
    if (Info->LastWriteTime.QuadPart > 0)
    {
        Ext4TimeFromNt(Info->LastWriteTime, &Attr.Modify);
        Mask |= E4_SET_MODIFY;
    }
    if (Info->ChangeTime.QuadPart > 0)
    {
        Ext4TimeFromNt(Info->ChangeTime, &Attr.Change);
        Mask |= E4_SET_CHANGE;
    }
    if (Info->FileAttributes != 0 && (Attr.Mode & E4_S_IFMT) == E4_S_IFREG)
    {
        BOOLEAN ReadOnly = BooleanFlagOn(Info->FileAttributes, FILE_ATTRIBUTE_READONLY);
        BOOLEAN IsReadOnly = (Attr.Mode & 0222) == 0;

        /* The read-only attribute stands for a file nobody may write; clearing it lets the owner write again */
        if (ReadOnly != IsReadOnly)
        {
            Attr.Mode = ReadOnly ? (Attr.Mode & ~0222U) : (Attr.Mode | 0200);
            Mask |= E4_SET_MODE;
        }
    }
    if (Mask == 0) return STATUS_SUCCESS;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4SetAttr(&Vcb->Core, Fcb->Inode, Mask, &Attr));
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Mask & E4_SET_MODIFY) Fcb->TimesDirty = FALSE;
    Ext4MarkCoreDirty(Vcb);
    Status = Ext4RefreshFcb(Fcb);
    Ext4NotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED);
    return Status;
}

static
NTSTATUS
Ext4IsWithin(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Ancestor,
    _In_ ULONG Directory,
    _Out_ PBOOLEAN Within)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Depth;

    *Within = FALSE;
    Ext4LockCore(Vcb);
    for (Depth = 0; Depth < EXT4_MAX_DEPTH; Depth++)
    {
        uint32_t Parent = 0;

        if (Directory == Ancestor)
        {
            *Within = TRUE;
            break;
        }
        if (Directory == E4_ROOT_INODE) break;
        Status = Ext4Status(Vcb, E4Parent(&Vcb->Core, Directory, &Parent));
        if (!NT_SUCCESS(Status)) break;
        if (Parent == Directory) break;
        Directory = Parent;
    }
    Ext4UnlockCore(Vcb);
    if (NT_SUCCESS(Status) && Depth == EXT4_MAX_DEPTH) Status = STATUS_FILE_CORRUPT_ERROR;
    return Status;
}

static
NTSTATUS
Ext4BuildName(
    _In_ PCUNICODE_STRING Directory,
    _In_ PCUNICODE_STRING Name,
    _Out_ PUNICODE_STRING FullName)
{
    BOOLEAN Separator = Directory->Length == 0 || Directory->Buffer[Directory->Length / sizeof(WCHAR) - 1] != L'\\';
    ULONG Length = Directory->Length + (Separator ? sizeof(WCHAR) : 0) + Name->Length;

    if (Length + sizeof(WCHAR) > MAXUSHORT) return STATUS_NAME_TOO_LONG;
    FullName->Buffer = ExAllocatePoolUninitialized(PagedPool, Length + sizeof(WCHAR), TAG_EXT4);
    if (FullName->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    FullName->Length = 0;
    FullName->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    RtlAppendUnicodeStringToString(FullName, Directory);
    if (Separator) RtlAppendUnicodeToString(FullName, L"\\");
    RtlAppendUnicodeStringToString(FullName, Name);
    return STATUS_SUCCESS;
}

/* What became of a file whose name a rename or a link took over */
static
VOID
Ext4DisposeVictim(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Inode,
    _In_ BOOLEAN LastLink)
{
    PEXT4_FCB Victim = Ext4FindFcb(Vcb, Inode);

    if (Victim != NULL)
    {
        if (LastLink)
        {
            Victim->Orphaned = TRUE;
            Victim->Attr.Links = 0;
        }
        else if (Victim->Attr.Links > 1)
        {
            Victim->Attr.Links--;
        }
        return;
    }
    if (LastLink)
    {
        Ext4LockCore(Vcb);
        E4Release(&Vcb->Core, Inode);
        Ext4UnlockCore(Vcb);
    }
}

static
NTSTATUS
Ext4RenameOrLink(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ PIRP Irp,
    _In_ BOOLEAN Link)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    FILE_INFORMATION_CLASS Class = Stack->Parameters.SetFile.FileInformationClass;
    PFILE_RENAME_INFORMATION Info = Irp->AssociatedIrp.SystemBuffer;
    PFILE_OBJECT Target = Stack->Parameters.SetFile.FileObject;
    BOOLEAN Extended = (Class == FileRenameInformationEx || Class == FileLinkInformationEx);
    UNICODE_STRING NewName, DirectoryName, FullName = { 0 };
    CHAR NewDiskName[EXT4_NAME_BYTES], ExistingName[EXT4_NAME_BYTES];
    ULONG NewDiskLength, ExistingLength = 0, ExistingInode = 0, NewParent, Victim = 0;
    BOOLEAN Replace, Posix, Exists, SameInode, SameEntry;
    bool VictimLastLink = false, Ignored = false;
    UCHAR ExistingType = 0;
    USHORT Index;
    NTSTATUS Status;
    int Error;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Stack->Parameters.SetFile.Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) ||
        Info->FileNameLength > Stack->Parameters.SetFile.Length - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Fcb->Orphaned || Fcb->DeletePending) return STATUS_DELETE_PENDING;
    if (Link && Fcb->Directory) return STATUS_FILE_IS_A_DIRECTORY;
    if (!Link && (Ccb->ParentInode == 0 || Ccb->NameLength == 0)) return STATUS_ACCESS_DENIED;

    Replace = Extended ? BooleanFlagOn(Info->Flags, FILE_RENAME_REPLACE_IF_EXISTS) : Stack->Parameters.SetFile.ReplaceIfExists;
    Posix = Extended && BooleanFlagOn(Info->Flags, FILE_RENAME_POSIX_SEMANTICS);

    if (Target != NULL)
    {
        PEXT4_FCB TargetFcb = Target->FsContext;
        PEXT4_CCB TargetCcb = Target->FsContext2;

        if (TargetFcb == NULL || TargetFcb->Signature != EXT4_FCB_SIGNATURE || TargetCcb == NULL) return STATUS_INVALID_PARAMETER;
        if (TargetFcb->Vcb != Vcb) return STATUS_NOT_SAME_DEVICE;
        if (!TargetFcb->Directory) return STATUS_INVALID_PARAMETER;

        NewName = Target->FileName;
        NewParent = TargetFcb->Inode;
        DirectoryName = TargetCcb->FullName;
    }
    else
    {
        if (Ccb->ParentInode == 0) return STATUS_ACCESS_DENIED;
        NewName.Buffer = Info->FileName;
        NewName.Length = NewName.MaximumLength = (USHORT)Info->FileNameLength;
        NewParent = Ccb->ParentInode;
        DirectoryName = Ccb->FullName;
        while (DirectoryName.Length > sizeof(WCHAR) && DirectoryName.Buffer[DirectoryName.Length / sizeof(WCHAR) - 1] == L'\\')
            DirectoryName.Length -= sizeof(WCHAR);
        while (DirectoryName.Length != 0 && DirectoryName.Buffer[DirectoryName.Length / sizeof(WCHAR) - 1] != L'\\')
            DirectoryName.Length -= sizeof(WCHAR);
        if (DirectoryName.Length > sizeof(WCHAR)) DirectoryName.Length -= sizeof(WCHAR);
    }

    if (NewName.Length == 0) return STATUS_OBJECT_NAME_INVALID;
    for (Index = 0; Index < NewName.Length / sizeof(WCHAR); Index++)
    {
        WCHAR Char = NewName.Buffer[Index];

        if (Char == 0 || Char == L'/' || Char == L'\\') return STATUS_OBJECT_NAME_INVALID;
    }
    Status = Ext4UnicodeToUtf8(&NewName, NewDiskName, sizeof(NewDiskName), &NewDiskLength);
    if (!NT_SUCCESS(Status) || NewDiskLength == 0) return STATUS_OBJECT_NAME_INVALID;
    if ((NewDiskLength == 1 && NewDiskName[0] == '.') || (NewDiskLength == 2 && NewDiskName[0] == '.' && NewDiskName[1] == '.'))
        return STATUS_OBJECT_NAME_INVALID;

    if (Fcb->Directory)
    {
        BOOLEAN Within;

        Status = Ext4IsWithin(Vcb, Fcb->Inode, NewParent, &Within);
        if (!NT_SUCCESS(Status)) return Status;
        if (Within) return STATUS_INVALID_PARAMETER;
    }

    Status = Ext4LookupName(Vcb, NewParent, &NewName, !Posix && Irp->RequestorMode == UserMode, &ExistingInode, &ExistingType,
                            ExistingName, &ExistingLength);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND) return Status;
    Exists = NT_SUCCESS(Status);
    SameInode = Exists && ExistingInode == Fcb->Inode;
    SameEntry = SameInode && !Link && NewParent == Ccb->ParentInode && ExistingLength == Ccb->NameLength &&
                RtlCompareMemory(ExistingName, Ccb->Name, ExistingLength) == ExistingLength;

    if (SameInode)
    {
        if (Link) return Replace ? STATUS_SUCCESS : STATUS_OBJECT_NAME_COLLISION;
        if (SameEntry && NewDiskLength == ExistingLength && RtlCompareMemory(NewDiskName, ExistingName, ExistingLength) == ExistingLength)
            return STATUS_SUCCESS;
        /* Another name of the same file stands where the new name goes: POSIX leaves both alone */
        if (!SameEntry && Posix) return STATUS_SUCCESS;
        if (!SameEntry && !Replace) return STATUS_OBJECT_NAME_COLLISION;
    }
    else if (Exists)
    {
        PEXT4_FCB Open;

        if (!Replace) return STATUS_OBJECT_NAME_COLLISION;
        if (ExistingType == EXT4_DE_DIR && !Posix) return STATUS_ACCESS_DENIED;
        Open = Ext4FindFcb(Vcb, ExistingInode);
        if (Open != NULL)
        {
            if (Open->OpenHandles != 0 && !Posix) return STATUS_ACCESS_DENIED;
            if (!Open->Directory)
            {
                if (!MmFlushImageSection(&Open->SectionObjectPointers, MmFlushForDelete)) return STATUS_ACCESS_DENIED;
                if (Open->OpenHandles == 0) CcPurgeCacheSection(&Open->SectionObjectPointers, NULL, 0, FALSE);
            }
        }
        Victim = ExistingInode;
    }

    Status = Ext4BuildName(&DirectoryName, &NewName, &FullName);
    if (!NT_SUCCESS(Status)) return Status;

    Ext4LockCore(Vcb);
    Error = E4Begin(&Vcb->Core);
    if (Error == EOK)
    {
        if (SameInode && !SameEntry)
        {
            /* The target name is another link of this file: it goes, and the name of the handle takes its place */
            bool Last;

            Error = E4Unlink(&Vcb->Core, NewParent, ExistingName, ExistingLength, Fcb->Inode, &Last);
        }
        if (Error == EOK && Link)
        {
            if (Victim != 0) Error = E4Unlink(&Vcb->Core, NewParent, ExistingName, ExistingLength, Victim, &VictimLastLink);
            if (Error == EOK) Error = E4Link(&Vcb->Core, NewParent, NewDiskName, NewDiskLength, Fcb->Inode);
        }
        else if (Error == EOK)
        {
            /* When the victim was found under a name of another case, that name is the one that goes */
            if (Victim != 0 && (ExistingLength != NewDiskLength ||
                                RtlCompareMemory(ExistingName, NewDiskName, NewDiskLength) != NewDiskLength))
            {
                Error = E4Unlink(&Vcb->Core, NewParent, ExistingName, ExistingLength, Victim, &VictimLastLink);
                if (Error == EOK)
                    Error = E4Rename(&Vcb->Core, Ccb->ParentInode, Ccb->Name, Ccb->NameLength, NewParent, NewDiskName,
                                     NewDiskLength, Fcb->Inode, 0, &Ignored);
            }
            else
            {
                Error = E4Rename(&Vcb->Core, Ccb->ParentInode, Ccb->Name, Ccb->NameLength, NewParent, NewDiskName,
                                 NewDiskLength, Fcb->Inode, Victim, &VictimLastLink);
            }
        }
        Error = E4End(&Vcb->Core, Error);
    }
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FullName.Buffer, TAG_EXT4);
        return Status;
    }

    Ext4MarkCoreDirty(Vcb);
    if (Victim != 0) Ext4DisposeVictim(Vcb, Victim, VictimLastLink);
    Ext4RefreshFcb(Fcb);

    if (Exists && !SameEntry)
        Ext4NotifyChange(Vcb, &FullName, FILE_NOTIFY_CHANGE_FILE_NAME, FILE_ACTION_REMOVED);

    if (Link)
    {
        Ext4NotifyChange(Vcb, &FullName, FILE_NOTIFY_CHANGE_FILE_NAME, FILE_ACTION_ADDED);
        ExFreePoolWithTag(FullName.Buffer, TAG_EXT4);
        return STATUS_SUCCESS;
    }

    Ext4NotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                     FILE_ACTION_RENAMED_OLD_NAME);
    Ext4NotifyChange(Vcb, &FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                     FILE_ACTION_RENAMED_NEW_NAME);

    /* TODO: other handles of the file keep the old name; a delete through one of them finds no such name */
    if (Ccb->AllocatedName != NULL) ExFreePoolWithTag(Ccb->AllocatedName, TAG_EXT4);
    Ccb->AllocatedName = FullName.Buffer;
    Ccb->FullName = FullName;
    Ccb->ParentInode = NewParent;
    Ccb->NameLength = NewDiskLength;
    RtlCopyMemory(Ccb->Name, NewDiskName, NewDiskLength);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4SetInformationWorker(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ PIRP Irp)
{
    PFILE_OBJECT FileObject = Stack->FileObject;
    FILE_INFORMATION_CLASS Class = Stack->Parameters.SetFile.FileInformationClass;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.SetFile.Length;

    switch (Class)
    {
        case FilePositionInformation:
            if (Length < sizeof(FILE_POSITION_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            FileObject->CurrentByteOffset = ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset;
            return STATUS_SUCCESS;

        case FileBasicInformation:
            if (Length < sizeof(FILE_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            return Ext4SetBasicInformation(Fcb, Ccb, Buffer);

        case FileEndOfFileInformation:
        {
            NTSTATUS Status;

            if (Length < sizeof(FILE_END_OF_FILE_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            if (Fcb->Directory || Fcb->Volume || Fcb->ReparseTag != 0) return STATUS_INVALID_DEVICE_REQUEST;
            /* The lazy writer reports how far it wrote; the size on the disk already says so */
            if (Stack->Parameters.SetFile.AdvanceOnly) return STATUS_SUCCESS;
            if (((PFILE_END_OF_FILE_INFORMATION)Buffer)->EndOfFile.QuadPart < 0) return STATUS_INVALID_PARAMETER;
            Status = Ext4SetFileSize(Fcb, FileObject, (ULONGLONG)((PFILE_END_OF_FILE_INFORMATION)Buffer)->EndOfFile.QuadPart);
            if (NT_SUCCESS(Status))
                Ext4NotifyChange(Fcb->Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_SIZE, FILE_ACTION_MODIFIED);
            return Status;
        }

        case FileAllocationInformation:
        {
            LONGLONG Allocation;

            if (Length < sizeof(FILE_ALLOCATION_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            if (Fcb->Directory || Fcb->Volume || Fcb->ReparseTag != 0) return STATUS_INVALID_DEVICE_REQUEST;
            Allocation = ((PFILE_ALLOCATION_INFORMATION)Buffer)->AllocationSize.QuadPart;
            if (Allocation < 0) return STATUS_INVALID_PARAMETER;
            /* TODO: space is not reserved ahead of the data; only an allocation below the file size has an effect */
            if (Allocation < Fcb->Header.FileSize.QuadPart) return Ext4SetFileSize(Fcb, FileObject, (ULONGLONG)Allocation);
            return STATUS_SUCCESS;
        }

        case FileDispositionInformation:
            if (Length < sizeof(FILE_DISPOSITION_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            return Ext4MarkDeletePending(Fcb, Ccb, FileObject, ((PFILE_DISPOSITION_INFORMATION)Buffer)->DeleteFile);

        case FileDispositionInformationEx:
        {
            ULONG Flags;

            if (Length < sizeof(FILE_DISPOSITION_INFORMATION_EX)) return STATUS_INFO_LENGTH_MISMATCH;
            Flags = ((PFILE_DISPOSITION_INFORMATION_EX)Buffer)->Flags;
            if (Flags & FILE_DISPOSITION_ON_CLOSE)
            {
                Ccb->DeleteOnClose = BooleanFlagOn(Flags, FILE_DISPOSITION_DELETE);
                return STATUS_SUCCESS;
            }
            if ((Flags & FILE_DISPOSITION_DELETE) && (Flags & FILE_DISPOSITION_POSIX_SEMANTICS))
                return Ext4PosixDelete(Fcb, Ccb);
            return Ext4MarkDeletePending(Fcb, Ccb, FileObject, BooleanFlagOn(Flags, FILE_DISPOSITION_DELETE));
        }

        case FileRenameInformation:
        case FileRenameInformationEx:
            return Ext4RenameOrLink(Fcb, Ccb, Stack, Irp, FALSE);

        case FileLinkInformation:
        case FileLinkInformationEx:
            return Ext4RenameOrLink(Fcb, Ccb, Stack, Irp, TRUE);

        case FileCaseSensitiveInformation:
            if (Length < sizeof(FILE_CASE_SENSITIVE_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
            if (!Fcb->Directory) return STATUS_NOT_A_DIRECTORY;
            /* Every directory of this file system tells names apart by case */
            return (((PFILE_CASE_SENSITIVE_INFORMATION)Buffer)->Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) ?
                   STATUS_SUCCESS : STATUS_NOT_SUPPORTED;

        case FileValidDataLengthInformation:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS
NTAPI
Ext4DispatchSetInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PEXT4_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    FILE_INFORMATION_CLASS Class = Stack->Parameters.SetFile.FileInformationClass;
    BOOLEAN Names;
    PEXT4_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    Names = (Class == FileRenameInformation || Class == FileRenameInformationEx || Class == FileLinkInformation ||
             Class == FileLinkInformationEx || Class == FileDispositionInformationEx);

    KeEnterCriticalRegion();
    if (Names)
        ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    else
        ExAcquireResourceSharedLite(&Vcb->Resource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    Status = Ext4SetInformationWorker(Fcb, Ccb, Stack, Irp);

    ExReleaseResourceLite(&Fcb->MainResource);
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, 0);
}
