/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Directory enumeration and change notification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

typedef struct _BTRFS_LISTED
{
    UNICODE_STRING Name;
    ULONGLONG Index;
    ULONGLONG FileId;
    BTRFS_INODE_ITEM Inode;
    ULONG Attributes;
    ULONG ReparseTag;
    BOOLEAN Directory;
} BTRFS_LISTED, *PBTRFS_LISTED;

static
ULONG
BtrfsEntryFixedSize(
    _In_ FILE_INFORMATION_CLASS Class)
{
    switch (Class)
    {
        case FileDirectoryInformation: return FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
        case FileFullDirectoryInformation: return FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
        case FileBothDirectoryInformation: return FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
        case FileNamesInformation: return FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
        case FileIdBothDirectoryInformation: return FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName);
        case FileIdFullDirectoryInformation: return FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName);
        default: return 0;
    }
}

static
VOID
BtrfsFillEntry(
    _In_ PBTRFS_VCB Vcb,
    _In_ FILE_INFORMATION_CLASS Class,
    _In_ PBTRFS_LISTED Listed,
    _Out_writes_bytes_(Fixed + Copied) PUCHAR Entry,
    _In_ ULONG Fixed,
    _In_ ULONG Copied)
{
    LARGE_INTEGER Creation, Access, Write, Change, EndOfFile, Allocation;
    ULONG EaSize = (Listed->Attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? Listed->ReparseTag : 0;
    ULONG Attributes = Listed->Attributes ? Listed->Attributes : FILE_ATTRIBUTE_NORMAL;

    RtlZeroMemory(Entry, Fixed);

    Creation = BtrfsTime(Listed->Inode.CreateTime.Seconds ? &Listed->Inode.CreateTime : &Listed->Inode.ChangeTime);
    Access = BtrfsTime(&Listed->Inode.AccessTime);
    Write = BtrfsTime(&Listed->Inode.ModifyTime);
    Change = BtrfsTime(&Listed->Inode.ChangeTime);
    EndOfFile.QuadPart = Listed->Directory ? 0 : (LONGLONG)Listed->Inode.Size;
    Allocation.QuadPart = Listed->Directory ? 0 :
        (LONGLONG)((Listed->Inode.Size + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1));

#define BTRFS_FILL_COMMON(Info) \
    do { \
        (Info)->FileIndex = (ULONG)Listed->Index; \
        (Info)->CreationTime = Creation; \
        (Info)->LastAccessTime = Access; \
        (Info)->LastWriteTime = Write; \
        (Info)->ChangeTime = Change; \
        (Info)->EndOfFile = EndOfFile; \
        (Info)->AllocationSize = Allocation; \
        (Info)->FileAttributes = Attributes; \
        (Info)->FileNameLength = Listed->Name.Length; \
        RtlCopyMemory((Info)->FileName, Listed->Name.Buffer, Copied); \
    } while (0)

    switch (Class)
    {
        case FileDirectoryInformation:
            BTRFS_FILL_COMMON((PFILE_DIRECTORY_INFORMATION)Entry);
            break;
        case FileFullDirectoryInformation:
            BTRFS_FILL_COMMON((PFILE_FULL_DIR_INFORMATION)Entry);
            ((PFILE_FULL_DIR_INFORMATION)Entry)->EaSize = EaSize;
            break;
        case FileBothDirectoryInformation:
            BTRFS_FILL_COMMON((PFILE_BOTH_DIR_INFORMATION)Entry);
            ((PFILE_BOTH_DIR_INFORMATION)Entry)->EaSize = EaSize;
            break;
        case FileIdBothDirectoryInformation:
            BTRFS_FILL_COMMON((PFILE_ID_BOTH_DIR_INFORMATION)Entry);
            ((PFILE_ID_BOTH_DIR_INFORMATION)Entry)->EaSize = EaSize;
            ((PFILE_ID_BOTH_DIR_INFORMATION)Entry)->FileId.QuadPart = (LONGLONG)Listed->FileId;
            break;
        case FileIdFullDirectoryInformation:
            BTRFS_FILL_COMMON((PFILE_ID_FULL_DIR_INFORMATION)Entry);
            ((PFILE_ID_FULL_DIR_INFORMATION)Entry)->EaSize = EaSize;
            ((PFILE_ID_FULL_DIR_INFORMATION)Entry)->FileId.QuadPart = (LONGLONG)Listed->FileId;
            break;
        case FileNamesInformation:
            ((PFILE_NAMES_INFORMATION)Entry)->FileIndex = (ULONG)Listed->Index;
            ((PFILE_NAMES_INFORMATION)Entry)->FileNameLength = Listed->Name.Length;
            RtlCopyMemory(((PFILE_NAMES_INFORMATION)Entry)->FileName, Listed->Name.Buffer, Copied);
            break;
        default:
            ASSERT(FALSE);
            break;
    }

#undef BTRFS_FILL_COMMON
}

static
BOOLEAN
BtrfsPatternMatches(
    _In_ PBTRFS_CCB Ccb,
    _In_ PCUNICODE_STRING Name)
{
    if (Ccb->SearchPattern.Length == 0) return TRUE;
    if (Ccb->PatternHasWildcards) return FsRtlIsNameInExpression(&Ccb->SearchPattern, (PUNICODE_STRING)Name, TRUE, NULL);
    return RtlEqualUnicodeString(&Ccb->SearchPattern, Name, TRUE);
}

/* The next entry at or after the cursor: "." and ".." first except in the root, then the directory's index order */
static
NTSTATUS
BtrfsNextListed(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Cursor,
    _Out_ PBTRFS_LISTED Listed)
{
    static const WCHAR Dot[] = L"..";
    PBTRFS_VCB Vcb = Fcb->Vcb;
    BOOLEAN IsRoot = (Fcb->Root.Id == Vcb->DefaultRoot.Id && Fcb->Inode == Vcb->DefaultRoot.RootDirId);
    BTRFS_DIR_ENTRY Entry;
    BTRFS_ROOT Root;
    ULONGLONG Inode;
    NTSTATUS Status;

    RtlZeroMemory(Listed, sizeof(*Listed));

    if (Cursor < BTRFS_DIR_CURSOR_FIRST && !IsRoot)
    {
        Listed->Index = Cursor;
        Listed->Inode = Fcb->InodeItem;
        Listed->FileId = BtrfsFileId(&Fcb->Root, Fcb->Inode);
        Listed->Attributes = FILE_ATTRIBUTE_DIRECTORY;
        Listed->Directory = TRUE;
        Listed->Name.Buffer = ExAllocatePoolUninitialized(PagedPool, sizeof(Dot), TAG_BTRFS);
        if (Listed->Name.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(Listed->Name.Buffer, Dot, sizeof(Dot));
        Listed->Name.Length = (USHORT)((Cursor + 1) * sizeof(WCHAR));
        Listed->Name.MaximumLength = sizeof(Dot);
        return STATUS_SUCCESS;
    }

    Status = BtrfsReadDirEntry(Vcb, &Fcb->Root, Fcb->Inode, max(Cursor, BTRFS_DIR_CURSOR_FIRST), &Entry);
    if (!NT_SUCCESS(Status)) return Status;

    Listed->Index = Entry.Index;
    Listed->Name = Entry.Name;
    Entry.Name.Buffer = NULL;

    Status = BtrfsResolveLocation(Vcb, &Fcb->Root, &Entry.Location, &Root, &Inode);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, &Root, Inode, &Listed->Inode);
    BtrfsFreeDirEntry(&Entry);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Listed->Name.Buffer, TAG_BTRFS);
        Listed->Name.Buffer = NULL;
        return (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? STATUS_FILE_CORRUPT_ERROR : Status;
    }

    Listed->FileId = BtrfsFileId(&Root, Inode);
    Listed->Directory = ((Listed->Inode.Mode & BTRFS_S_IFMT) == BTRFS_S_IFDIR);
    Listed->Attributes = BtrfsInodeAttributes(&Listed->Inode, &Listed->Name, &Listed->ReparseTag);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsQueryDirectory(
    _In_ PBTRFS_FCB Fcb,
    _In_ PBTRFS_CCB Ccb,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    FILE_INFORMATION_CLASS Class = Stack->Parameters.QueryDirectory.FileInformationClass;
    PUNICODE_STRING Pattern = Stack->Parameters.QueryDirectory.FileName;
    ULONG Length = Stack->Parameters.QueryDirectory.Length;
    BOOLEAN Restart = BooleanFlagOn(Stack->Flags, SL_RESTART_SCAN);
    BOOLEAN Single = BooleanFlagOn(Stack->Flags, SL_RETURN_SINGLE_ENTRY);
    ULONG Fixed = BtrfsEntryFixedSize(Class);
    PUCHAR Buffer, Previous = NULL;
    ULONG Used = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    *Information = 0;
    if (Fixed == 0) return STATUS_INVALID_INFO_CLASS;
    if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;

    Buffer = (Irp->MdlAddress != NULL) ? MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : Irp->UserBuffer;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!Ccb->SearchStarted || (Restart && Pattern != NULL && Pattern->Length != 0))
    {
        if (Ccb->SearchPattern.Buffer != NULL) ExFreePoolWithTag(Ccb->SearchPattern.Buffer, TAG_BTRFS);
        RtlInitEmptyUnicodeString(&Ccb->SearchPattern, NULL, 0);
        Ccb->PatternHasWildcards = FALSE;

        if (Pattern != NULL && Pattern->Length != 0 &&
            !(Pattern->Length == sizeof(WCHAR) && Pattern->Buffer[0] == L'*'))
        {
            Ccb->SearchPattern.Buffer = ExAllocatePoolUninitialized(PagedPool, Pattern->Length, TAG_BTRFS);
            if (Ccb->SearchPattern.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
            Ccb->SearchPattern.MaximumLength = Pattern->Length;
            RtlUpcaseUnicodeString(&Ccb->SearchPattern, Pattern, FALSE);
            Ccb->PatternHasWildcards = FsRtlDoesNameContainWildCards(&Ccb->SearchPattern);
        }

        Ccb->SearchStarted = TRUE;
        Ccb->ReturnedAny = FALSE;
        Ccb->Cursor = BTRFS_DIR_CURSOR_DOT;
    }
    else if (Restart)
    {
        Ccb->ReturnedAny = FALSE;
        Ccb->Cursor = BTRFS_DIR_CURSOR_DOT;
    }

    if (BooleanFlagOn(Stack->Flags, SL_INDEX_SPECIFIED)) Ccb->Cursor = (ULONGLONG)Stack->Parameters.QueryDirectory.FileIndex + 1;

    for (;;)
    {
        BTRFS_LISTED Listed;
        ULONG Aligned, Needed, Copied;

        Status = BtrfsNextListed(Fcb, Ccb->Cursor, &Listed);
        if (!NT_SUCCESS(Status)) break;

        if (!BtrfsPatternMatches(Ccb, &Listed.Name))
        {
            Ccb->Cursor = Listed.Index + 1;
            ExFreePoolWithTag(Listed.Name.Buffer, TAG_BTRFS);
            continue;
        }

        Aligned = ALIGN_UP_BY(Used, sizeof(LONGLONG));
        Needed = Fixed + Listed.Name.Length;

        if (Used != 0 && (Aligned > Length || Needed > Length - Aligned))
        {
            ExFreePoolWithTag(Listed.Name.Buffer, TAG_BTRFS);
            break;
        }

        if (Used == 0 && Needed > Length)
        {
            Copied = Length - Fixed;
            BtrfsFillEntry(Fcb->Vcb, Class, &Listed, Buffer, Fixed, Copied & ~(sizeof(WCHAR) - 1));
            ExFreePoolWithTag(Listed.Name.Buffer, TAG_BTRFS);
            *Information = Length;
            return STATUS_BUFFER_OVERFLOW;
        }

        if (Previous != NULL) *(PULONG)Previous = Aligned - (ULONG)(Previous - Buffer);
        BtrfsFillEntry(Fcb->Vcb, Class, &Listed, Buffer + Aligned, Fixed, Listed.Name.Length);
        Previous = Buffer + Aligned;
        Used = Aligned + Needed;

        Ccb->Cursor = Listed.Index + 1;
        Ccb->ReturnedAny = TRUE;
        ExFreePoolWithTag(Listed.Name.Buffer, TAG_BTRFS);

        if (Single) break;
    }

    if (Used != 0)
    {
        *Information = Used;
        return STATUS_SUCCESS;
    }

    if (Status == STATUS_NO_MORE_ENTRIES) return Ccb->ReturnedAny ? STATUS_NO_MORE_FILES : STATUS_NO_SUCH_FILE;
    return NT_SUCCESS(Status) ? STATUS_NO_MORE_FILES : Status;
}

NTSTATUS
NTAPI
BtrfsDispatchDirectoryControl(
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
    if (!Fcb->Directory) return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Fcb->Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_DIRECTORY:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
            Status = BtrfsQueryDirectory(Fcb, Ccb, Irp, &Information);
            ExReleaseResourceLite(&Fcb->MainResource);
            KeLeaveCriticalRegion();
            return BtrfsCompleteRequest(Irp, Status, Information);

        case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
            FsRtlNotifyFullChangeDirectory(Fcb->Vcb->NotifySync,
                                           &Fcb->Vcb->DirNotifyList,
                                           Ccb,
                                           (PSTRING)&Ccb->FullName,
                                           BooleanFlagOn(Stack->Flags, SL_WATCH_TREE),
                                           FALSE,
                                           Stack->Parameters.NotifyDirectory.CompletionFilter,
                                           Irp,
                                           NULL,
                                           NULL);
            return STATUS_PENDING;

        default:
            return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}
