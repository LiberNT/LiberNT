/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opening and creating files and directories
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_MAX_LINK_DEPTH 32

typedef struct _EXT4_WALK
{
    /* Where the walk stands: the directory, or the file the last component named */
    ULONG Inode;
    BOOLEAN Directory;
    /* The directory and on-disk name the current inode was reached through; ParentInode is 0 at the root */
    ULONG ParentInode;
    ULONG NameLength;
    CHAR Name[EXT4_NAME_BYTES];
    /* The component that names a file to create or a rename target, when the walk stops before it */
    UNICODE_STRING Missing;
    BOOLEAN LeafMissing;
    BOOLEAN LeafExists;
    /* The path still to walk; Expanded owns the buffer once a symbolic link was followed */
    UNICODE_STRING Remaining;
    PWCHAR Expanded;
    ULONG Links;
} EXT4_WALK, *PEXT4_WALK;

VOID
Ext4NotifyChange(
    _In_ PEXT4_VCB Vcb,
    _In_ PCUNICODE_STRING FullName,
    _In_ ULONG Filter,
    _In_ ULONG Action)
{
    USHORT Offset = FullName->Length;

    if (FullName->Length == 0 || FullName->Buffer[0] != L'\\') return;

    while (Offset > 0 && FullName->Buffer[Offset / sizeof(WCHAR) - 1] != L'\\') Offset -= sizeof(WCHAR);
    FsRtlNotifyFullReportChange(Vcb->NotifySync, &Vcb->DirNotifyList, (PSTRING)FullName, Offset, NULL, NULL, Filter, Action, NULL);
}

static
PEXT4_CCB
Ext4AllocateCcb(
    _In_ PCUNICODE_STRING Prefix,
    _In_ PCUNICODE_STRING Name)
{
    PEXT4_CCB Ccb;
    USHORT Length;
    BOOLEAN Separator;

    Separator = Prefix->Length != 0 && Name->Length != 0 &&
                Prefix->Buffer[Prefix->Length / sizeof(WCHAR) - 1] != L'\\' && Name->Buffer[0] != L'\\';
    if ((ULONG)Prefix->Length + Name->Length + sizeof(WCHAR) * 2 > MAXUSHORT) return NULL;
    Length = Prefix->Length + Name->Length + (Separator ? sizeof(WCHAR) : 0);

    Ccb = ExAllocatePoolZero(PagedPool, sizeof(EXT4_CCB) + Length + sizeof(WCHAR), TAG_EXT4);
    if (Ccb == NULL) return NULL;

    Ccb->Signature = EXT4_CCB_SIGNATURE;
    Ccb->FullName.Buffer = (PWCHAR)(Ccb + 1);
    Ccb->FullName.MaximumLength = Length + sizeof(WCHAR);
    RtlAppendUnicodeStringToString(&Ccb->FullName, Prefix);
    if (Separator) RtlAppendUnicodeToString(&Ccb->FullName, L"\\");
    RtlAppendUnicodeStringToString(&Ccb->FullName, Name);
    if (Ccb->FullName.Length == 0) RtlAppendUnicodeToString(&Ccb->FullName, L"\\");
    return Ccb;
}

static
BOOLEAN
Ext4StripDataStream(
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
BOOLEAN
Ext4ValidNewName(
    _In_ PCUNICODE_STRING Name)
{
    CHAR Utf8[EXT4_NAME_BYTES];
    ULONG Length;
    USHORT Index;

    for (Index = 0; Index < Name->Length / sizeof(WCHAR); Index++)
    {
        WCHAR Char = Name->Buffer[Index];

        if (Char == 0 || Char == L'/' || Char == L'\\') return FALSE;
    }
    if (Name->Length == sizeof(WCHAR) && Name->Buffer[0] == L'.') return FALSE;
    if (Name->Length == 2 * sizeof(WCHAR) && Name->Buffer[0] == L'.' && Name->Buffer[1] == L'.') return FALSE;

    return NT_SUCCESS(Ext4UnicodeToUtf8(Name, Utf8, sizeof(Utf8), &Length)) && Length != 0 && Length <= EXT4_NAME_BYTES;
}

/* Hands out the FCB of an open that failed after Ext4GetFcb: the last reference may free the volume, so the
 * caller's hold on the volume resource is dropped around it */
static
VOID
Ext4DropFcb(
    _In_ PEXT4_VCB Vcb,
    _In_ PEXT4_FCB Fcb)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    Ext4ReferenceVcb(Vcb);
    ExReleaseResourceLite(&Vcb->Resource);
    Ext4DereferenceFcb(Fcb);
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    Ext4DereferenceVcb(Vcb);
}

static
NTSTATUS
Ext4OpenVolume(
    _In_ PEXT4_VCB Vcb,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PULONG_PTR Information)
{
    UNICODE_STRING Empty = RTL_CONSTANT_STRING(L"");
    ULONG Disposition = Stack->Parameters.Create.Options >> 24;
    ACCESS_MASK Access = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    PEXT4_FCB Fcb;
    PEXT4_CCB Ccb;
    NTSTATUS Status;

    if (Disposition != FILE_OPEN && Disposition != FILE_OPEN_IF) return STATUS_ACCESS_DENIED;
    if (Stack->Parameters.Create.Options & FILE_DIRECTORY_FILE) return STATUS_NOT_A_DIRECTORY;

    if (Vcb->VolumeFcb == NULL)
    {
        Vcb->VolumeFcb = Ext4CreateVolumeFcb(Vcb);
        if (Vcb->VolumeFcb == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        Ext4ReferenceVcb(Vcb);
    }
    else
    {
        Ext4ReferenceFcb(Vcb->VolumeFcb);
    }
    Fcb = Vcb->VolumeFcb;

    Status = STATUS_SUCCESS;
    if (Fcb->OpenHandles != 0)
        Status = IoCheckShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess, TRUE);
    else
        IoSetShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess);
    if (!NT_SUCCESS(Status))
    {
        Ext4DropFcb(Vcb, Fcb);
        return Status;
    }

    Ccb = Ext4AllocateCcb(&Empty, &Empty);
    if (Ccb == NULL)
    {
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
        Ext4DropFcb(Vcb, Fcb);
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

/* Replaces what is left of the walk by the link target followed by the rest of the path */
static
NTSTATUS
Ext4FollowLink(
    _In_ PEXT4_VCB Vcb,
    _Inout_ PEXT4_WALK Walk,
    _In_ ULONG LinkInode)
{
    UNICODE_STRING Target;
    PWCHAR Buffer;
    PCHAR Utf8;
    uint32_t Length = 0;
    NTSTATUS Status;
    ULONG Total, Index;
    int Error;

    if (++Walk->Links > EXT4_MAX_LINK_DEPTH) return STATUS_REPARSE_POINT_NOT_RESOLVED;

    Utf8 = ExAllocatePoolUninitialized(PagedPool, E4_SYMLINK_MAX + 1, TAG_EXT4);
    if (Utf8 == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Ext4LockCore(Vcb);
    Error = E4ReadLink(&Vcb->Core, LinkInode, Utf8, E4_SYMLINK_MAX, &Length);
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);
    if (NT_SUCCESS(Status) && Length == 0) Status = STATUS_REPARSE_POINT_NOT_RESOLVED;
    if (NT_SUCCESS(Status)) Status = Ext4Utf8ToUnicode(Utf8, Length, &Target);
    ExFreePoolWithTag(Utf8, TAG_EXT4);
    if (!NT_SUCCESS(Status)) return Status;

    Total = Target.Length + sizeof(WCHAR) + Walk->Remaining.Length;
    if (Total > MAXUSHORT)
    {
        ExFreePoolWithTag(Target.Buffer, TAG_EXT4);
        return STATUS_NAME_TOO_LONG;
    }
    Buffer = ExAllocatePoolUninitialized(PagedPool, Total, TAG_EXT4);
    if (Buffer == NULL)
    {
        ExFreePoolWithTag(Target.Buffer, TAG_EXT4);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < Target.Length / sizeof(WCHAR); Index++)
        Buffer[Index] = (Target.Buffer[Index] == L'/') ? L'\\' : Target.Buffer[Index];
    Buffer[Index] = L'\\';
    RtlCopyMemory(Buffer + Index + 1, Walk->Remaining.Buffer, Walk->Remaining.Length);

    /* An absolute target is taken from the root of this volume; a relative one from the directory of the link */
    if (Target.Buffer[0] == L'/')
    {
        Walk->Inode = E4_ROOT_INODE;
        Walk->ParentInode = 0;
        Walk->NameLength = 0;
    }
    else
    {
        Walk->Inode = Walk->ParentInode;
    }
    Walk->Directory = TRUE;
    ExFreePoolWithTag(Target.Buffer, TAG_EXT4);

    if (Walk->Expanded != NULL) ExFreePoolWithTag(Walk->Expanded, TAG_EXT4);
    Walk->Expanded = Buffer;
    Walk->Remaining.Buffer = Buffer;
    Walk->Remaining.Length = Walk->Remaining.MaximumLength = (USHORT)Total;
    while (Walk->Remaining.Length != 0 && Walk->Remaining.Buffer[Walk->Remaining.Length / sizeof(WCHAR) - 1] == L'\\')
        Walk->Remaining.Length -= sizeof(WCHAR);
    return STATUS_SUCCESS;
}

/* Walks Walk->Remaining from Walk->Inode. With StopAtLeaf the last component is looked up but not entered: it is
 * reported in Missing with LeafExists. Otherwise a missing last component is reported with LeafMissing. */
static
NTSTATUS
Ext4WalkPath(
    _In_ PEXT4_VCB Vcb,
    _Inout_ PEXT4_WALK Walk,
    _In_ BOOLEAN IgnoreCase,
    _In_ BOOLEAN StopAtLeaf,
    _In_ BOOLEAN FollowLeaf)
{
    NTSTATUS Status;

    while (Walk->Remaining.Length != 0)
    {
        UNICODE_STRING Component;
        CHAR DiskName[EXT4_NAME_BYTES];
        ULONG DiskNameLength, Inode;
        BOOLEAN Last, Invalid;
        USHORT Index;
        UCHAR Type;

        while (Walk->Remaining.Length != 0 && Walk->Remaining.Buffer[0] == L'\\' && Walk->Links != 0)
        {
            Walk->Remaining.Buffer++;
            Walk->Remaining.Length -= sizeof(WCHAR);
        }
        if (Walk->Remaining.Length == 0) break;

        for (Index = 0; Index < Walk->Remaining.Length / sizeof(WCHAR) && Walk->Remaining.Buffer[Index] != L'\\'; Index++);
        Component.Buffer = Walk->Remaining.Buffer;
        Component.Length = Component.MaximumLength = Index * sizeof(WCHAR);
        Last = (Component.Length == Walk->Remaining.Length);
        Walk->Remaining.Buffer += Index + (Last ? 0 : 1);
        Walk->Remaining.Length -= Component.Length + (Last ? 0 : sizeof(WCHAR));

        if (Component.Length == 0) return STATUS_OBJECT_NAME_INVALID;
        if (!Walk->Directory) return STATUS_OBJECT_PATH_NOT_FOUND;

        if (Component.Length == sizeof(WCHAR) && Component.Buffer[0] == L'.')
        {
            /* Only a link target can bring "." and ".." this far */
            if (Walk->Links == 0) return STATUS_OBJECT_NAME_INVALID;
            continue;
        }
        if (Component.Length == 2 * sizeof(WCHAR) && Component.Buffer[0] == L'.' && Component.Buffer[1] == L'.')
        {
            uint32_t Parent = E4_ROOT_INODE;
            int Error;

            if (Walk->Links == 0) return STATUS_OBJECT_NAME_INVALID;
            Ext4LockCore(Vcb);
            Error = E4Parent(&Vcb->Core, Walk->Inode, &Parent);
            Status = Ext4Status(Vcb, Error);
            Ext4UnlockCore(Vcb);
            if (!NT_SUCCESS(Status)) return Status;
            Walk->Inode = Parent;
            Walk->ParentInode = 0;
            Walk->NameLength = 0;
            continue;
        }

        if (Last)
        {
            Ext4StripDataStream(&Component, &Invalid);
            if (Invalid) return STATUS_OBJECT_NAME_NOT_FOUND;
            if (Component.Length == 0) return STATUS_OBJECT_NAME_INVALID;
        }

        Status = Ext4LookupName(Vcb, Walk->Inode, &Component, IgnoreCase, &Inode, &Type, DiskName, &DiskNameLength);
        if (Last && StopAtLeaf)
        {
            if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND) return Status;
            Walk->Missing = Component;
            Walk->LeafExists = NT_SUCCESS(Status);
            return STATUS_SUCCESS;
        }
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            if (!Last) return STATUS_OBJECT_PATH_NOT_FOUND;
            Walk->Missing = Component;
            Walk->LeafMissing = TRUE;
            return STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(Status)) return Status;

        Walk->ParentInode = Walk->Inode;
        if (Type == EXT4_DE_SYMLINK && (!Last || FollowLeaf))
        {
            Status = Ext4FollowLink(Vcb, Walk, Inode);
            if (!NT_SUCCESS(Status)) return Status;
            continue;
        }
        if (!Last && Type != EXT4_DE_DIR) return STATUS_OBJECT_PATH_NOT_FOUND;

        Walk->Inode = Inode;
        Walk->Directory = (Type == EXT4_DE_DIR);
        Walk->NameLength = DiskNameLength;
        RtlCopyMemory(Walk->Name, DiskName, DiskNameLength);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4CreateNew(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PULONG Inode,
    _Out_writes_bytes_(EXT4_NAME_BYTES) PCHAR DiskName,
    _Out_ PULONG DiskNameLength)
{
    E4_ATTR Parent, Attr;
    uint32_t Created = 0;
    NTSTATUS Status;
    int Error;

    *Inode = 0;
    if (!Ext4ValidNewName(Name)) return STATUS_OBJECT_NAME_INVALID;
    Status = Ext4UnicodeToUtf8(Name, DiskName, EXT4_NAME_BYTES, DiskNameLength);
    if (!NT_SUCCESS(Status)) return STATUS_OBJECT_NAME_INVALID;

    RtlZeroMemory(&Attr, sizeof(Attr));
    Ext4LockCore(Vcb);
    Error = E4GetAttr(&Vcb->Core, Directory, &Parent);
    if (Error == EOK)
    {
        /* A file made from the Win32 side has no Linux owner to give: it takes the owner and group of its directory */
        Attr.Uid = Parent.Uid;
        Attr.Gid = Parent.Gid;
        if (IsDirectory)
            Attr.Mode = E4_S_IFDIR | 0755 | (Parent.Mode & 02000);
        else
            Attr.Mode = E4_S_IFREG | ((FileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644);
        Error = E4Create(&Vcb->Core, Directory, DiskName, *DiskNameLength, &Attr, &Created);
    }
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);

    if (NT_SUCCESS(Status))
    {
        *Inode = Created;
        Ext4MarkCoreDirty(Vcb);
    }
    return Status;
}

static
NTSTATUS
Ext4Overwrite(
    _In_ PEXT4_FCB Fcb,
    _In_ PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Zero = { 0 };
    NTSTATUS Status;

    if (!MmCanFileBeTruncated(&Fcb->SectionObjectPointers, &Zero)) return STATUS_USER_MAPPED_FILE;

    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
    FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
    Status = Ext4SetFileSize(Fcb, FileObject, 0);
    ExReleaseResourceLite(&Fcb->MainResource);
    return Status;
}

static
NTSTATUS
Ext4OpenById(
    _In_ PEXT4_VCB Vcb,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PULONG Inode)
{
    ULONGLONG Id;

    if (FileObject->FileName.Length != sizeof(ULONGLONG)) return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Id, FileObject->FileName.Buffer, sizeof(Id));
    if (Id == 0 || Id > MAXULONG) return STATUS_OBJECT_NAME_NOT_FOUND;

    UNREFERENCED_PARAMETER(Vcb);
    *Inode = (ULONG)Id;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4CreateWorker(
    _In_ PEXT4_VCB Vcb,
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
    /* Names differ by case on this file system, and a kernel caller such as the Linux subsystem gets exactly that.
     * A user-mode caller that asks for a case-insensitive open also finds a name that differs in case only. */
    BOOLEAN IgnoreCase = !(Stack->Flags & SL_CASE_SENSITIVE) && Irp->RequestorMode == UserMode;
    BOOLEAN TargetDirectory = BooleanFlagOn(Stack->Flags, SL_OPEN_TARGET_DIRECTORY);
    BOOLEAN ById = BooleanFlagOn(Options, FILE_OPEN_BY_FILE_ID);
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"");
    UNICODE_STRING OpenedName, LeafName = RTL_CONSTANT_STRING(L"");
    BOOLEAN TrailingSlash = FALSE, Created = FALSE;
    ULONG_PTR Result = FILE_OPENED;
    PEXT4_FCB Fcb = NULL;
    PEXT4_CCB Ccb;
    EXT4_WALK Walk;
    NTSTATUS Status;

    *Information = 0;
    RtlZeroMemory(&Walk, sizeof(Walk));
    Walk.Inode = E4_ROOT_INODE;
    Walk.Directory = TRUE;
    Walk.Remaining = FileObject->FileName;

    if (Stack->Flags & SL_OPEN_PAGING_FILE) return STATUS_ACCESS_DENIED;
    if (TargetDirectory && !Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    if (Related != NULL && Related->FsContext != NULL && !ById &&
        ((PEXT4_FCB)Related->FsContext)->Volume && Walk.Remaining.Length == 0)
    {
        return Ext4OpenVolume(Vcb, Stack, FileObject, Information);
    }
    if (Related == NULL && Walk.Remaining.Length == 0) return Ext4OpenVolume(Vcb, Stack, FileObject, Information);

    if (ById)
    {
        if (TargetDirectory || Disposition != FILE_OPEN) return STATUS_INVALID_PARAMETER;
        Status = Ext4OpenById(Vcb, FileObject, &Walk.Inode);
        if (!NT_SUCCESS(Status)) return Status;
        Walk.Remaining.Length = 0;
    }
    else if (Related != NULL)
    {
        PEXT4_FCB RelatedFcb = Related->FsContext;
        PEXT4_CCB RelatedCcb = Related->FsContext2;

        if (RelatedFcb == NULL || RelatedFcb->Signature != EXT4_FCB_SIGNATURE || RelatedCcb == NULL) return STATUS_INVALID_PARAMETER;
        if (RelatedFcb->Volume) return STATUS_INVALID_PARAMETER;
        if (Walk.Remaining.Length != 0 && Walk.Remaining.Buffer[0] == L'\\') return STATUS_OBJECT_NAME_INVALID;
        if (Walk.Remaining.Length != 0 && !RelatedFcb->Directory) return STATUS_OBJECT_PATH_NOT_FOUND;

        Walk.Inode = RelatedFcb->Inode;
        Walk.Directory = RelatedFcb->Directory;
        Walk.ParentInode = RelatedCcb->ParentInode;
        Walk.NameLength = RelatedCcb->NameLength;
        RtlCopyMemory(Walk.Name, RelatedCcb->Name, RelatedCcb->NameLength);
        Prefix = RelatedCcb->FullName;
    }
    else
    {
        if (Walk.Remaining.Buffer[0] != L'\\') return STATUS_OBJECT_NAME_INVALID;
        while (Walk.Remaining.Length != 0 && Walk.Remaining.Buffer[0] == L'\\')
        {
            Walk.Remaining.Buffer++;
            Walk.Remaining.Length -= sizeof(WCHAR);
        }
    }

    if (Walk.Remaining.Length >= sizeof(WCHAR) && Walk.Remaining.Buffer[Walk.Remaining.Length / sizeof(WCHAR) - 1] == L'\\')
    {
        TrailingSlash = TRUE;
        Walk.Remaining.Length -= sizeof(WCHAR);
        if (Walk.Remaining.Length != 0 && Walk.Remaining.Buffer[Walk.Remaining.Length / sizeof(WCHAR) - 1] == L'\\')
            return STATUS_OBJECT_NAME_INVALID;
    }
    if (TargetDirectory && Walk.Remaining.Length == 0) return STATUS_OBJECT_NAME_INVALID;

    Status = Ext4WalkPath(Vcb, &Walk, IgnoreCase, TargetDirectory, !(Options & FILE_OPEN_REPARSE_POINT));
    if (NT_SUCCESS(Status) && TargetDirectory && !Ext4ValidNewName(&Walk.Missing)) Status = STATUS_OBJECT_NAME_INVALID;
    if (NT_SUCCESS(Status) && Walk.LeafMissing)
    {
        ULONG Directory = Walk.Inode;

        if (Disposition == FILE_OPEN || Disposition == FILE_OVERWRITE)
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        else if (!Vcb->Writable)
            Status = STATUS_MEDIA_WRITE_PROTECTED;
        else if (TrailingSlash && !(Options & FILE_DIRECTORY_FILE))
            Status = STATUS_OBJECT_NAME_INVALID;
        else if ((Options & FILE_DIRECTORY_FILE) && Disposition != FILE_CREATE && Disposition != FILE_OPEN_IF)
            Status = STATUS_INVALID_PARAMETER;
        else
            Status = Ext4CreateNew(Vcb, Directory, &Walk.Missing, BooleanFlagOn(Options, FILE_DIRECTORY_FILE),
                                   Stack->Parameters.Create.FileAttributes, &Walk.Inode, Walk.Name, &Walk.NameLength);
        if (NT_SUCCESS(Status))
        {
            Walk.ParentInode = Directory;
            Walk.Directory = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
            Created = TRUE;
            Result = FILE_CREATED;
        }
    }
    if (NT_SUCCESS(Status)) LeafName = Walk.Missing;
    if (NT_SUCCESS(Status)) Status = Ext4GetFcb(Vcb, Walk.Inode, &Fcb);
    if (!NT_SUCCESS(Status))
    {
        if (Walk.Expanded != NULL) ExFreePoolWithTag(Walk.Expanded, TAG_EXT4);
        return Status;
    }

    Status = STATUS_SUCCESS;
    if (Fcb->DeletePending)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else if (TargetDirectory)
    {
        if (!Fcb->Directory) Status = STATUS_OBJECT_PATH_NOT_FOUND;
        Result = Walk.LeafExists ? FILE_EXISTS : FILE_DOES_NOT_EXIST;
    }
    else if ((Options & FILE_DIRECTORY_FILE) && !Fcb->Directory)
    {
        Status = STATUS_NOT_A_DIRECTORY;
    }
    else if ((Options & FILE_NON_DIRECTORY_FILE) && Fcb->Directory)
    {
        Status = STATUS_FILE_IS_A_DIRECTORY;
    }
    else if (TrailingSlash && !Fcb->Directory)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else if (Fcb->ReparseTag != 0 && !(Options & FILE_OPEN_REPARSE_POINT))
    {
        Status = STATUS_IO_REPARSE_TAG_NOT_HANDLED;
    }
    else if (!Created)
    {
        if (Disposition == FILE_CREATE)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
        else if (Disposition == FILE_OVERWRITE || Disposition == FILE_OVERWRITE_IF || Disposition == FILE_SUPERSEDE)
        {
            if (!Vcb->Writable)
                Status = STATUS_MEDIA_WRITE_PROTECTED;
            else if (Fcb->Directory)
                Status = (Options & FILE_DIRECTORY_FILE) ? STATUS_INVALID_PARAMETER : STATUS_FILE_IS_A_DIRECTORY;
            else if (Fcb->ReparseTag != 0)
                Status = STATUS_ACCESS_DENIED;
            else
                Result = (Disposition == FILE_SUPERSEDE) ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
        }
    }

    if (NT_SUCCESS(Status) && (Options & FILE_DELETE_ON_CLOSE))
    {
        if (!Vcb->Writable)
            Status = STATUS_MEDIA_WRITE_PROTECTED;
        else if (Walk.ParentInode == 0 || TargetDirectory)
            Status = STATUS_CANNOT_DELETE;
    }
    if (NT_SUCCESS(Status) && (Access & EXT4_WRITE_ACCESS) && !Vcb->Writable && !Fcb->Directory)
        Status = STATUS_MEDIA_WRITE_PROTECTED;

    if (NT_SUCCESS(Status) && (Access & MAXIMUM_ALLOWED))
    {
        ACCESS_MASK Granted = Vcb->Writable ? FILE_ALL_ACCESS : EXT4_READ_ACCESS;

        Access = (Access & ~MAXIMUM_ALLOWED) | Granted;
        if (AccessState != NULL)
        {
            AccessState->PreviouslyGrantedAccess |= Granted;
            AccessState->RemainingDesiredAccess &= ~(MAXIMUM_ALLOWED | Granted);
        }
    }

    if (NT_SUCCESS(Status) && Fcb->OpenHandles != 0)
    {
        ACCESS_MASK ShareCheck = Access | ((Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED) ? FILE_WRITE_DATA : 0);

        Status = IoCheckShareAccess(ShareCheck, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess, FALSE);
    }
    if (NT_SUCCESS(Status) && (Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED))
        Status = Ext4Overwrite(Fcb, FileObject);

    Ccb = NULL;
    if (NT_SUCCESS(Status))
    {
        OpenedName = FileObject->FileName;
        if (ById)
        {
            OpenedName.Length = 0;
            Prefix.Length = 0;
        }
        else if (TargetDirectory && LeafName.Buffer >= FileObject->FileName.Buffer &&
                 LeafName.Buffer < FileObject->FileName.Buffer + FileObject->FileName.Length / sizeof(WCHAR))
        {
            USHORT Keep = (USHORT)(LeafName.Buffer - FileObject->FileName.Buffer);

            while (Keep > 1 && OpenedName.Buffer[Keep - 1] == L'\\') Keep--;
            OpenedName.Length = Keep * sizeof(WCHAR);
        }
        Ccb = Ext4AllocateCcb(&Prefix, &OpenedName);
        if (Ccb == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!NT_SUCCESS(Status))
    {
        if (Walk.Expanded != NULL) ExFreePoolWithTag(Walk.Expanded, TAG_EXT4);
        Ext4DropFcb(Vcb, Fcb);
        return Status;
    }

    if (Fcb->OpenHandles != 0)
        IoUpdateShareAccess(FileObject, &Fcb->ShareAccess);
    else
        IoSetShareAccess(Access, Stack->Parameters.Create.ShareAccess, FileObject, &Fcb->ShareAccess);

    if (!TargetDirectory && !ById)
    {
        Ccb->ParentInode = Walk.ParentInode;
        Ccb->NameLength = Walk.NameLength;
        RtlCopyMemory(Ccb->Name, Walk.Name, Walk.NameLength);
    }
    else if (TargetDirectory)
    {
        /* The handle names the directory; where that directory hangs is of no use to a rename target */
        Ccb->ParentInode = 0;
    }
    Ccb->DeleteOnClose = BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);

    if (TargetDirectory && LeafName.Length != 0)
    {
        /* The I/O manager reads the last component of the new name from here */
        if (LeafName.Length <= FileObject->FileName.MaximumLength)
        {
            RtlMoveMemory(FileObject->FileName.Buffer, LeafName.Buffer, LeafName.Length);
            FileObject->FileName.Length = LeafName.Length;
        }
    }
    if (Walk.Expanded != NULL) ExFreePoolWithTag(Walk.Expanded, TAG_EXT4);

    FileObject->FsContext = Fcb;
    FileObject->FsContext2 = Ccb;
    FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
    FileObject->Vpb = Vcb->Vpb;
    if (!Fcb->Directory && Fcb->ReparseTag == 0 && !(Options & FILE_NO_INTERMEDIATE_BUFFERING))
        FileObject->Flags |= FO_CACHE_SUPPORTED;

    if (Vcb->RootFcb == NULL && Fcb->Inode == E4_ROOT_INODE) Vcb->RootFcb = Fcb;

    Fcb->OpenHandles++;
    Vcb->OpenHandles++;

    if (Created && Stack->Parameters.Create.EaLength != 0 && Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
        Ext4ApplyEaList(Fcb, Ccb, Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.Create.EaLength);
        ExReleaseResourceLite(&Fcb->MainResource);
    }

    if (Created)
        Ext4NotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                         FILE_ACTION_ADDED);
    else if (Result == FILE_OVERWRITTEN || Result == FILE_SUPERSEDED)
        Ext4NotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED);

    *Information = Result;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Ext4DispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PEXT4_VCB Vcb;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject)) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

    Vcb = Ext4DeviceVcb(DeviceObject);
    if (Vcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);

    if (Vcb->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (Vcb->Locked)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = Ext4CreateWorker(Vcb, Irp, &Information);

    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}
