/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opening files, directories and the volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_WRITE_ACCESS (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | \
                            DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD | ACCESS_SYSTEM_SECURITY)
#define BTRFS_READ_ACCESS  (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE)

static
PBTRFS_CCB
BtrfsAllocateCcb(
    _In_ PCUNICODE_STRING Prefix,
    _In_ PCUNICODE_STRING Name)
{
    PBTRFS_CCB Ccb;
    USHORT Length;
    BOOLEAN Separator;

    Separator = Prefix->Length != 0 && Name->Length != 0 &&
                Prefix->Buffer[Prefix->Length / sizeof(WCHAR) - 1] != L'\\' && Name->Buffer[0] != L'\\';
    if ((ULONG)Prefix->Length + Name->Length + sizeof(WCHAR) * 2 > MAXUSHORT) return NULL;
    Length = Prefix->Length + Name->Length + (Separator ? sizeof(WCHAR) : 0);

    Ccb = ExAllocatePoolZero(PagedPool, sizeof(BTRFS_CCB) + Length + sizeof(WCHAR), TAG_BTRFS);
    if (Ccb == NULL) return NULL;

    Ccb->Signature = BTRFS_CCB_SIGNATURE;
    Ccb->FullName.Buffer = (PWCHAR)(Ccb + 1);
    Ccb->FullName.MaximumLength = Length + sizeof(WCHAR);
    RtlAppendUnicodeStringToString(&Ccb->FullName, Prefix);
    if (Separator) RtlAppendUnicodeToString(&Ccb->FullName, L"\\");
    RtlAppendUnicodeStringToString(&Ccb->FullName, Name);
    if (Ccb->FullName.Length == 0) RtlAppendUnicodeToString(&Ccb->FullName, L"\\");
    Ccb->Cursor = BTRFS_DIR_CURSOR_DOT;
    return Ccb;
}

static
BOOLEAN
BtrfsStripDataStream(
    _Inout_ PUNICODE_STRING Component,
    _Out_ PBOOLEAN Invalid)
{
    static const UNICODE_STRING Data = RTL_CONSTANT_STRING(L"::$DATA");
    UNICODE_STRING Tail;
    USHORT Index;

    *Invalid = FALSE;

    for (Index = 0; Index < Component->Length / sizeof(WCHAR); Index++)
    {
        if (Component->Buffer[Index] != L':') continue;

        Tail.Buffer = Component->Buffer + Index;
        Tail.Length = Tail.MaximumLength = Component->Length - Index * sizeof(WCHAR);
        if (!RtlEqualUnicodeString(&Tail, &Data, TRUE))
        {
            *Invalid = TRUE;
            return FALSE;
        }
        Component->Length = Index * sizeof(WCHAR);
        return TRUE;
    }

    return FALSE;
}

static
NTSTATUS
BtrfsOpenVolume(
    _In_ PBTRFS_VCB Vcb,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PULONG_PTR Information)
{
    UNICODE_STRING Empty = RTL_CONSTANT_STRING(L"");
    ULONG Disposition = Stack->Parameters.Create.Options >> 24;
    ACCESS_MASK Access = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    PBTRFS_FCB Fcb;
    PBTRFS_CCB Ccb;
    NTSTATUS Status;

    if (Disposition != FILE_OPEN && Disposition != FILE_OPEN_IF) return STATUS_ACCESS_DENIED;
    if (Stack->Parameters.Create.Options & FILE_DIRECTORY_FILE) return STATUS_NOT_A_DIRECTORY;

    if (Vcb->VolumeFcb == NULL)
    {
        Vcb->VolumeFcb = BtrfsCreateVolumeFcb(Vcb);
        if (Vcb->VolumeFcb == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        BtrfsReferenceVcb(Vcb);
    }
    else
    {
        BtrfsReferenceFcb(Vcb->VolumeFcb);
    }
    Fcb = Vcb->VolumeFcb;

    if (Fcb->OpenHandles != 0)
    {
        Status = IoCheckShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess, TRUE);
        if (!NT_SUCCESS(Status))
        {
            ExReleaseResourceLite(&Vcb->Resource);
            BtrfsDereferenceFcb(Fcb);
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            return Status;
        }
    }
    else
    {
        IoSetShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess);
    }

    Ccb = BtrfsAllocateCcb(&Empty, &Empty);
    if (Ccb == NULL)
    {
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
        ExReleaseResourceLite(&Vcb->Resource);
        BtrfsDereferenceFcb(Fcb);
        ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FileObject->FsContext = Fcb;
    FileObject->FsContext2 = Ccb;
    FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
    FileObject->Vpb = Vcb->Vpb;
    FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;

    Fcb->OpenHandles++;
    Vcb->OpenHandles++;
    *Information = FILE_OPENED;
    return STATUS_SUCCESS;
}

static
BOOLEAN
BtrfsValidNewName(
    _In_ PCUNICODE_STRING Name)
{
    CHAR Utf8[BTRFS_NAME_LENGTH];
    ULONG Length;
    USHORT Index;

    for (Index = 0; Index < Name->Length / sizeof(WCHAR); Index++)
    {
        WCHAR Char = Name->Buffer[Index];

        if (Char < 0x20 || Char == L'"' || Char == L'*' || Char == L'/' || Char == L':' || Char == L'<' ||
            Char == L'>' || Char == L'?' || Char == L'\\' || Char == L'|')
        {
            return FALSE;
        }
    }

    return NT_SUCCESS(BtrfsUnicodeToUtf8(Name, Utf8, sizeof(Utf8), &Length)) && Length != 0 && Length <= BTRFS_NAME_LENGTH;
}

static
NTSTATUS
BtrfsCreateNew(
    _In_ PBTRFS_VCB Vcb,
    _In_ const BTRFS_ROOT *Root,
    _In_ ULONGLONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IsDirectory,
    _Out_ PULONGLONG Inode)
{
    ULONG Sequence;
    NTSTATUS Status;

    *Inode = 0;
    if (!BtrfsValidNewName(Name)) return STATUS_OBJECT_NAME_INVALID;

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsCreateEntry(Vcb, Root, Directory, Name, IsDirectory, Inode);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);
    return Status;
}

static
NTSTATUS
BtrfsOverwrite(
    _In_ PBTRFS_FCB Fcb,
    _In_ PFILE_OBJECT FileObject)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    LARGE_INTEGER Zero = { 0 };
    ULONG Sequence;
    NTSTATUS Status;

    if (!MmCanFileBeTruncated(&Fcb->SectionObjectPointers, &Zero)) return STATUS_USER_MAPPED_FILE;

    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsTruncate(Fcb, 0);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);

    if (NT_SUCCESS(Status))
    {
        Fcb->Header.FileSize.QuadPart = 0;
        Fcb->Header.ValidDataLength.QuadPart = 0;
        Fcb->Header.AllocationSize.QuadPart = 0;
        Fcb->Modified = TRUE;
    }

    ExReleaseResourceLite(&Fcb->PagingIoResource);
    if (NT_SUCCESS(Status) && Fcb->SectionObjectPointers.SharedCacheMap != NULL)
    {
        FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    }
    ExReleaseResourceLite(&Fcb->MainResource);
    return Status;
}

static
NTSTATUS
BtrfsCreateWorker(
    _In_ PBTRFS_VCB Vcb,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PFILE_OBJECT Related = FileObject->RelatedFileObject;
    PACCESS_STATE AccessState = Stack->Parameters.Create.SecurityContext->AccessState;
    ACCESS_MASK Access = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    ULONG Options = Stack->Parameters.Create.Options & FILE_VALID_OPTION_FLAGS;
    ULONG Disposition = Stack->Parameters.Create.Options >> 24;
    BOOLEAN IgnoreCase = !(Stack->Flags & SL_CASE_SENSITIVE);
    BOOLEAN TargetDirectory = BooleanFlagOn(Stack->Flags, SL_OPEN_TARGET_DIRECTORY);
    UNICODE_STRING Remaining = FileObject->FileName;
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"");
    UNICODE_STRING Component, LastName = RTL_CONSTANT_STRING(L""), OpenedName;
    BOOLEAN TrailingSlash = FALSE, Directory = TRUE, Invalid, Created = FALSE, TargetExists = FALSE;
    ULONGLONG ParentInode = 0, ParentRootId = 0;
    ULONG_PTR Result = FILE_OPENED;
    BTRFS_ROOT Root;
    ULONGLONG Inode;
    PBTRFS_FCB Fcb = NULL;
    PBTRFS_CCB Ccb;
    NTSTATUS Status;

    *Information = 0;

    if (Stack->Flags & SL_OPEN_PAGING_FILE) return STATUS_ACCESS_DENIED;
    if (Options & FILE_OPEN_BY_FILE_ID) return STATUS_NOT_IMPLEMENTED;
    if (TargetDirectory && !Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    if (Related != NULL && Related->FsContext != NULL &&
        ((PBTRFS_FCB)Related->FsContext)->Volume && Remaining.Length == 0)
    {
        return BtrfsOpenVolume(Vcb, Stack, FileObject, Information);
    }
    if (Related == NULL && Remaining.Length == 0) return BtrfsOpenVolume(Vcb, Stack, FileObject, Information);

    if (Related != NULL)
    {
        PBTRFS_FCB RelatedFcb = Related->FsContext;
        PBTRFS_CCB RelatedCcb = Related->FsContext2;

        if (RelatedFcb == NULL || RelatedFcb->Signature != BTRFS_FCB_SIGNATURE || RelatedCcb == NULL) return STATUS_INVALID_PARAMETER;
        if (RelatedFcb->Volume) return STATUS_INVALID_PARAMETER;
        if (Remaining.Length != 0 && Remaining.Buffer[0] == L'\\') return STATUS_OBJECT_NAME_INVALID;
        if (Remaining.Length != 0 && !RelatedFcb->Directory) return STATUS_OBJECT_PATH_NOT_FOUND;

        Root = RelatedFcb->Root;
        Inode = RelatedFcb->Inode;
        Directory = RelatedFcb->Directory;
        Prefix = RelatedCcb->FullName;
        ParentInode = RelatedCcb->ParentInode;
        ParentRootId = RelatedCcb->ParentRootId;
    }
    else
    {
        if (Remaining.Buffer[0] != L'\\') return STATUS_OBJECT_NAME_INVALID;
        Root = Vcb->DefaultRoot;
        Inode = Vcb->DefaultRoot.RootDirId;
    }

    while (Remaining.Length != 0 && Remaining.Buffer[0] == L'\\')
    {
        Remaining.Buffer++;
        Remaining.Length -= sizeof(WCHAR);
        if (Related != NULL) break;
    }
    if (Remaining.Length >= sizeof(WCHAR) && Remaining.Buffer[Remaining.Length / sizeof(WCHAR) - 1] == L'\\')
    {
        TrailingSlash = TRUE;
        Remaining.Length -= sizeof(WCHAR);
        if (Remaining.Length != 0 && Remaining.Buffer[Remaining.Length / sizeof(WCHAR) - 1] == L'\\')
            return STATUS_OBJECT_NAME_INVALID;
    }
    if (TargetDirectory && Remaining.Length == 0) return STATUS_OBJECT_NAME_INVALID;

    while (Remaining.Length != 0)
    {
        BTRFS_KEY Location;
        BOOLEAN Last;
        USHORT Index;
        UCHAR Type;

        for (Index = 0; Index < Remaining.Length / sizeof(WCHAR) && Remaining.Buffer[Index] != L'\\'; Index++);
        Component.Buffer = Remaining.Buffer;
        Component.Length = Component.MaximumLength = Index * sizeof(WCHAR);
        Last = (Component.Length == Remaining.Length);
        Remaining.Buffer += Index + (Last ? 0 : 1);
        Remaining.Length -= Component.Length + (Last ? 0 : sizeof(WCHAR));

        if (Component.Length == 0) return STATUS_OBJECT_NAME_INVALID;
        if (Component.Length > BTRFS_NAME_LENGTH * sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;
        if ((Component.Length == sizeof(WCHAR) && Component.Buffer[0] == L'.') ||
            (Component.Length == 2 * sizeof(WCHAR) && Component.Buffer[0] == L'.' && Component.Buffer[1] == L'.'))
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        if (Last)
        {
            BtrfsStripDataStream(&Component, &Invalid);
            if (Invalid) return STATUS_OBJECT_NAME_NOT_FOUND;
            if (Component.Length == 0) return STATUS_OBJECT_NAME_INVALID;
        }

        if (!Directory) return STATUS_OBJECT_PATH_NOT_FOUND;

        Status = BtrfsLookupName(Vcb, &Root, Inode, &Component, IgnoreCase, &Location, &Type);

        if (Last && TargetDirectory)
        {
            if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND) return Status;
            if (!BtrfsValidNewName(&Component)) return STATUS_OBJECT_NAME_INVALID;
            TargetExists = NT_SUCCESS(Status);
            LastName = Component;
            break;
        }

        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            if (!Last) return STATUS_OBJECT_PATH_NOT_FOUND;
            if (Disposition == FILE_OPEN || Disposition == FILE_OVERWRITE) return STATUS_OBJECT_NAME_NOT_FOUND;
            if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
            if (TrailingSlash && !(Options & FILE_DIRECTORY_FILE)) return STATUS_OBJECT_NAME_INVALID;
            if ((Options & FILE_DIRECTORY_FILE) && Disposition != FILE_CREATE && Disposition != FILE_OPEN_IF)
                return STATUS_INVALID_PARAMETER;

            ParentInode = Inode;
            ParentRootId = Root.Id;
            Status = BtrfsCreateNew(Vcb, &Root, Inode, &Component, BooleanFlagOn(Options, FILE_DIRECTORY_FILE), &Inode);
            if (!NT_SUCCESS(Status)) return Status;

            Directory = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
            LastName = Component;
            Created = TRUE;
            Result = FILE_CREATED;
            break;
        }
        if (!NT_SUCCESS(Status)) return Status;

        if (!Last && Type != BTRFS_FT_DIR)
            return (Type == BTRFS_FT_SYMLINK) ? STATUS_IO_REPARSE_TAG_NOT_HANDLED : STATUS_OBJECT_PATH_NOT_FOUND;

        ParentInode = Inode;
        ParentRootId = Root.Id;
        Status = BtrfsResolveLocation(Vcb, &Root, &Location, &Root, &Inode);
        if (!NT_SUCCESS(Status)) return Last ? Status : STATUS_OBJECT_PATH_NOT_FOUND;

        Directory = (Type == BTRFS_FT_DIR);
        LastName = Component;
    }

    Status = BtrfsGetFcb(Vcb, &Root, Inode, TargetDirectory ? NULL : &LastName, &Fcb);
    if (!NT_SUCCESS(Status)) return Status;

#define BTRFS_FAIL(s) do { Status = (s); goto Fail; } while (0)

    if (Fcb->DeletePending) BTRFS_FAIL(STATUS_DELETE_PENDING);

    if (TargetDirectory)
    {
        if (!Fcb->Directory) BTRFS_FAIL(STATUS_OBJECT_PATH_NOT_FOUND);
        Result = TargetExists ? FILE_EXISTS : FILE_DOES_NOT_EXIST;
    }
    else
    {
        if ((Options & FILE_DIRECTORY_FILE) && !Fcb->Directory) BTRFS_FAIL(STATUS_NOT_A_DIRECTORY);
        if ((Options & FILE_NON_DIRECTORY_FILE) && Fcb->Directory) BTRFS_FAIL(STATUS_FILE_IS_A_DIRECTORY);
        if (TrailingSlash && !Fcb->Directory) BTRFS_FAIL(STATUS_OBJECT_NAME_INVALID);
        if (Fcb->ReparseTag != 0 && !(Options & FILE_OPEN_REPARSE_POINT)) BTRFS_FAIL(STATUS_IO_REPARSE_TAG_NOT_HANDLED);
        if (!Created)
        {
            if (Disposition == FILE_CREATE) BTRFS_FAIL(STATUS_OBJECT_NAME_COLLISION);
            if (Disposition == FILE_OVERWRITE || Disposition == FILE_OVERWRITE_IF || Disposition == FILE_SUPERSEDE)
            {
                if (!Vcb->Writable) BTRFS_FAIL(STATUS_MEDIA_WRITE_PROTECTED);
                if (Fcb->Directory) BTRFS_FAIL((Options & FILE_DIRECTORY_FILE) ? STATUS_INVALID_PARAMETER : STATUS_FILE_IS_A_DIRECTORY);
                if (Fcb->ReparseTag != 0) BTRFS_FAIL(STATUS_ACCESS_DENIED);
                Result = (Disposition == FILE_SUPERSEDE) ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
            }
        }
    }

    if (Options & FILE_DELETE_ON_CLOSE)
    {
        if (!Vcb->Writable) BTRFS_FAIL(STATUS_MEDIA_WRITE_PROTECTED);
        if (ParentInode == 0) BTRFS_FAIL(STATUS_CANNOT_DELETE);
        if (Fcb->Root.Id != ParentRootId) BTRFS_FAIL(STATUS_ACCESS_DENIED);
    }
    if ((Access & BTRFS_WRITE_ACCESS) && !Vcb->Writable) BTRFS_FAIL(STATUS_MEDIA_WRITE_PROTECTED);

    if (Access & MAXIMUM_ALLOWED)
    {
        ACCESS_MASK Granted = Vcb->Writable ? FILE_ALL_ACCESS : BTRFS_READ_ACCESS;

        Access = (Access & ~MAXIMUM_ALLOWED) | Granted;
        if (AccessState != NULL)
        {
            AccessState->PreviouslyGrantedAccess |= Granted;
            AccessState->RemainingDesiredAccess &= ~(MAXIMUM_ALLOWED | Granted);
        }
    }

    if (Fcb->OpenHandles != 0)
    {
        ACCESS_MASK ShareCheck = Access | ((Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED) ? FILE_WRITE_DATA : 0);

        Status = IoCheckShareAccess(ShareCheck, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess, FALSE);
        if (!NT_SUCCESS(Status)) goto Fail;
    }

    if (Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED)
    {
        Status = BtrfsOverwrite(Fcb, FileObject);
        if (!NT_SUCCESS(Status)) goto Fail;
    }

    OpenedName = FileObject->FileName;
    if (TargetDirectory)
    {
        USHORT Keep = (USHORT)(LastName.Buffer - FileObject->FileName.Buffer);

        while (Keep > 1 && OpenedName.Buffer[Keep - 1] == L'\\') Keep--;
        OpenedName.Length = Keep * sizeof(WCHAR);
    }

    Ccb = BtrfsAllocateCcb(&Prefix, &OpenedName);
    if (Ccb == NULL) BTRFS_FAIL(STATUS_INSUFFICIENT_RESOURCES);

#undef BTRFS_FAIL

    if (Fcb->OpenHandles != 0)
        IoUpdateShareAccess(FileObject, &Fcb->ShareAccess);
    else
        IoSetShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess);

    Ccb->ParentInode = ParentInode;
    Ccb->ParentRootId = ParentRootId;
    Ccb->DeleteOnClose = BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);

    if (TargetDirectory)
    {
        RtlMoveMemory(FileObject->FileName.Buffer, LastName.Buffer, LastName.Length);
        FileObject->FileName.Length = LastName.Length;
    }

    FileObject->FsContext = Fcb;
    FileObject->FsContext2 = Ccb;
    FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
    FileObject->Vpb = Vcb->Vpb;
    if (!Fcb->Directory && !(Options & FILE_NO_INTERMEDIATE_BUFFERING)) FileObject->Flags |= FO_CACHE_SUPPORTED;

    if (Vcb->RootFcb == NULL && Fcb->Root.Id == Vcb->DefaultRoot.Id && Fcb->Inode == Vcb->DefaultRoot.RootDirId)
        Vcb->RootFcb = Fcb;

    Fcb->OpenHandles++;
    Vcb->OpenHandles++;

    if (Created)
        BtrfsNotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                          FILE_ACTION_ADDED);
    else if (Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED)
        BtrfsNotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED);

    *Information = Result;
    return STATUS_SUCCESS;

Fail:
    ExReleaseResourceLite(&Vcb->Resource);
    BtrfsDereferenceFcb(Fcb);
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PBTRFS_VCB Vcb;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject)) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

    Vcb = BtrfsDeviceVcb(DeviceObject);
    if (Vcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);

    if (Vcb->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (Vcb->Locked)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = BtrfsCreateWorker(Vcb, Irp, &Information);

    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}
