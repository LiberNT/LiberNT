/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Directory enumeration and change notification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

typedef struct _EXT4_DIR_ITEM
{
    ULONGLONG Start;
    ULONGLONG Next;
    ULONG Inode;
    ULONG NameLength;
    CHAR Name[EXT4_NAME_BYTES];
    E4_ATTR Attr;
    BOOLEAN AttrValid;
} EXT4_DIR_ITEM, *PEXT4_DIR_ITEM;

typedef struct _EXT4_DIR_BATCH_CONTEXT
{
    PEXT4_DIR_ITEM Items;
    ULONG Count;
    ULONGLONG Position;
} EXT4_DIR_BATCH_CONTEXT, *PEXT4_DIR_BATCH_CONTEXT;

static
bool
Ext4CollectEntry(
    void *Context,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    uint8_t Type,
    uint64_t Offset)
{
    PEXT4_DIR_BATCH_CONTEXT Batch = Context;
    PEXT4_DIR_ITEM Item;

    UNREFERENCED_PARAMETER(Type);

    if (Batch->Count == EXT4_DIR_BATCH) return false;
    if (NameLength == 0 || NameLength > EXT4_NAME_BYTES)
    {
        Batch->Position = Offset;
        return true;
    }

    Item = &Batch->Items[Batch->Count++];
    Item->Start = Batch->Position;
    Item->Next = Offset;
    Item->Inode = Inode;
    Item->NameLength = NameLength;
    RtlCopyMemory(Item->Name, Name, NameLength);
    Item->AttrValid = FALSE;
    Batch->Position = Offset;
    return true;
}

static
ULONG
Ext4EntryFixedSize(
    _In_ FILE_INFORMATION_CLASS Class)
{
    switch (Class)
    {
        case FileDirectoryInformation: return FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
        case FileFullDirectoryInformation: return FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
        case FileBothDirectoryInformation: return FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
        case FileNamesInformation: return FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
        case FileIdFullDirectoryInformation: return FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName);
        case FileIdBothDirectoryInformation: return FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName);
        default: return 0;
    }
}

static
VOID
Ext4FormatEntry(
    _In_ PEXT4_VCB Vcb,
    _In_ FILE_INFORMATION_CLASS Class,
    _Out_ PUCHAR Entry,
    _In_ const EXT4_DIR_ITEM *Item,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG NameBytes)
{
    ULONG ReparseTag = 0;
    ULONG Attributes = Ext4FileAttributes(&Item->Attr, Item->Name, Item->NameLength, &ReparseTag);
    BOOLEAN Directory = (Item->Attr.Mode & E4_S_IFMT) == E4_S_IFDIR;
    LARGE_INTEGER Size, Allocation;
    PWCHAR NameTarget;

    if (Attributes == 0) Attributes = FILE_ATTRIBUTE_NORMAL;
    Size.QuadPart = Directory ? 0 : (LONGLONG)Item->Attr.Size;
    Allocation.QuadPart = Directory ? 0 : (LONGLONG)ALIGN_UP_BY(Item->Attr.Size, (ULONGLONG)Vcb->BlockSize);

    if (Class == FileNamesInformation)
    {
        PFILE_NAMES_INFORMATION Info = (PFILE_NAMES_INFORMATION)Entry;

        Info->NextEntryOffset = 0;
        Info->FileIndex = 0;
        Info->FileNameLength = Name->Length;
        RtlCopyMemory(Info->FileName, Name->Buffer, NameBytes);
        return;
    }

    /* The classes share their layout up to the name length */
    {
        PFILE_DIRECTORY_INFORMATION Info = (PFILE_DIRECTORY_INFORMATION)Entry;

        Info->NextEntryOffset = 0;
        Info->FileIndex = 0;
        Info->CreationTime = Ext4TimeToNt(&Item->Attr.Create);
        Info->LastAccessTime = Ext4TimeToNt(&Item->Attr.Access);
        Info->LastWriteTime = Ext4TimeToNt(&Item->Attr.Modify);
        Info->ChangeTime = Ext4TimeToNt(&Item->Attr.Change);
        Info->EndOfFile = Size;
        Info->AllocationSize = Allocation;
        Info->FileAttributes = Attributes;
        Info->FileNameLength = Name->Length;
    }

    switch (Class)
    {
        case FileFullDirectoryInformation:
        {
            PFILE_FULL_DIR_INFORMATION Info = (PFILE_FULL_DIR_INFORMATION)Entry;

            Info->EaSize = ReparseTag;
            NameTarget = Info->FileName;
            break;
        }
        case FileBothDirectoryInformation:
        {
            PFILE_BOTH_DIR_INFORMATION Info = (PFILE_BOTH_DIR_INFORMATION)Entry;

            Info->EaSize = ReparseTag;
            Info->ShortNameLength = 0;
            RtlZeroMemory(Info->ShortName, sizeof(Info->ShortName));
            NameTarget = Info->FileName;
            break;
        }
        case FileIdFullDirectoryInformation:
        {
            PFILE_ID_FULL_DIR_INFORMATION Info = (PFILE_ID_FULL_DIR_INFORMATION)Entry;

            Info->EaSize = ReparseTag;
            Info->FileId.QuadPart = Item->Inode;
            NameTarget = Info->FileName;
            break;
        }
        case FileIdBothDirectoryInformation:
        {
            PFILE_ID_BOTH_DIR_INFORMATION Info = (PFILE_ID_BOTH_DIR_INFORMATION)Entry;

            Info->EaSize = ReparseTag;
            Info->ShortNameLength = 0;
            RtlZeroMemory(Info->ShortName, sizeof(Info->ShortName));
            Info->FileId.QuadPart = Item->Inode;
            NameTarget = Info->FileName;
            break;
        }
        default:
            NameTarget = ((PFILE_DIRECTORY_INFORMATION)Entry)->FileName;
            break;
    }
    RtlCopyMemory(NameTarget, Name->Buffer, NameBytes);
}

static
NTSTATUS
Ext4SetPattern(
    _Inout_ PEXT4_CCB Ccb,
    _In_opt_ PUNICODE_STRING Pattern)
{
    static const UNICODE_STRING Star = RTL_CONSTANT_STRING(L"*");
    NTSTATUS Status;

    if (Ccb->SearchPattern.Buffer != NULL)
    {
        ExFreePoolWithTag(Ccb->SearchPattern.Buffer, TAG_EXT4);
        Ccb->SearchPattern.Buffer = NULL;
        Ccb->SearchPattern.Length = 0;
    }

    Ccb->MatchAll = (Pattern == NULL || Pattern->Length == 0 || RtlEqualUnicodeString(Pattern, &Star, FALSE));
    Ccb->PatternHasWildcards = TRUE;
    if (Ccb->MatchAll) return STATUS_SUCCESS;

    Ccb->SearchPattern.Buffer = ExAllocatePoolUninitialized(PagedPool, Pattern->Length, TAG_EXT4);
    if (Ccb->SearchPattern.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    Ccb->SearchPattern.MaximumLength = Pattern->Length;

    Ccb->PatternHasWildcards = FsRtlDoesNameContainWildCards(Pattern);
    if (Ccb->PatternHasWildcards)
    {
        Status = RtlUpcaseUnicodeString(&Ccb->SearchPattern, Pattern, FALSE);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Ccb->SearchPattern.Buffer, TAG_EXT4);
            Ccb->SearchPattern.Buffer = NULL;
            return Status;
        }
    }
    else
    {
        RtlCopyMemory(Ccb->SearchPattern.Buffer, Pattern->Buffer, Pattern->Length);
        Ccb->SearchPattern.Length = Pattern->Length;
    }
    return STATUS_SUCCESS;
}

/* Reads the next entries of the directory with their attributes */
static
NTSTATUS
Ext4ReadBatch(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _Inout_ PEXT4_DIR_BATCH_CONTEXT Batch,
    _Inout_ PULONGLONG Offset)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    NTSTATUS Status;
    ULONG Index;
    int Error;

    Batch->Count = 0;
    Batch->Position = *Offset;

    Ext4LockCore(Vcb);
    Error = EOK;
    if (!Ccb->MatchAll && !Ccb->PatternHasWildcards && *Offset == 0)
    {
        /* One name is asked for: it is looked up first, and searched for only when no entry has exactly that name */
        uint32_t Inode = 0;
        uint8_t Type = 0;
        CHAR Utf8[EXT4_NAME_BYTES];
        ULONG Length = 0;

        if (NT_SUCCESS(Ext4UnicodeToUtf8(&Ccb->SearchPattern, Utf8, sizeof(Utf8), &Length)) && Length != 0 &&
            E4Lookup(&Vcb->Core, Fcb->Inode, Utf8, Length, &Inode, &Type) == EOK)
        {
            *Offset = MAXULONGLONG;
            Batch->Items[0].Start = 0;
            Batch->Items[0].Next = MAXULONGLONG;
            Batch->Items[0].Inode = Inode;
            Batch->Items[0].NameLength = Length;
            RtlCopyMemory(Batch->Items[0].Name, Utf8, Length);
            Batch->Items[0].AttrValid = FALSE;
            Batch->Count = 1;
        }
        else
        {
            RtlUpcaseUnicodeString(&Ccb->SearchPattern, &Ccb->SearchPattern, FALSE);
            Ccb->PatternHasWildcards = TRUE;
        }
    }
    if (Batch->Count == 0 && *Offset != MAXULONGLONG && (Ccb->MatchAll || Ccb->PatternHasWildcards))
        Error = E4ReadDir(&Vcb->Core, Fcb->Inode, Offset, Ext4CollectEntry, Batch);

    for (Index = 0; Error == EOK && Index < Batch->Count; Index++)
    {
        /* A name whose inode cannot be read is still listed, without attributes */
        if (E4GetAttr(&Vcb->Core, Batch->Items[Index].Inode, &Batch->Items[Index].Attr) == EOK)
            Batch->Items[Index].AttrValid = TRUE;
        else
            RtlZeroMemory(&Batch->Items[Index].Attr, sizeof(E4_ATTR));
    }
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);
    return Status;
}

static
NTSTATUS
Ext4QueryDirectory(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    FILE_INFORMATION_CLASS Class = Stack->Parameters.QueryDirectory.FileInformationClass;
    PUNICODE_STRING Pattern = Stack->Parameters.QueryDirectory.FileName;
    ULONG Length = Stack->Parameters.QueryDirectory.Length;
    BOOLEAN Restart = BooleanFlagOn(Stack->Flags, SL_RESTART_SCAN);
    BOOLEAN Single = BooleanFlagOn(Stack->Flags, SL_RETURN_SINGLE_ENTRY);
    BOOLEAN First = !Ccb->SearchStarted;
    BOOLEAN IsRoot = (Fcb->Inode == E4_ROOT_INODE);
    ULONG Fixed = Ext4EntryFixedSize(Class);
    EXT4_DIR_BATCH_CONTEXT Batch;
    PUCHAR Buffer, Previous = NULL;
    ULONGLONG Offset;
    ULONG Used = 0;
    BOOLEAN Done = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    *Information = 0;
    if (Fixed == 0) return STATUS_INVALID_INFO_CLASS;
    if (Length < Fixed + sizeof(WCHAR)) return STATUS_BUFFER_TOO_SMALL;

    Buffer = (Irp->MdlAddress != NULL) ? MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : Irp->UserBuffer;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (First || (Restart && Pattern != NULL && Pattern->Length != 0))
    {
        Status = Ext4SetPattern(Ccb, Pattern);
        if (!NT_SUCCESS(Status)) return Status;
        Ccb->SearchStarted = TRUE;
        Ccb->DirOffset = 0;
    }
    else if (Restart)
    {
        Ccb->DirOffset = 0;
    }

    Batch.Items = ExAllocatePoolUninitialized(PagedPool, sizeof(EXT4_DIR_ITEM) * EXT4_DIR_BATCH, TAG_EXT4);
    if (Batch.Items == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Offset = Ccb->DirOffset;
    while (!Done)
    {
        ULONG Index;

        Status = Ext4ReadBatch(Fcb, Ccb, &Batch, &Offset);
        if (!NT_SUCCESS(Status) || Batch.Count == 0) break;

        for (Index = 0; Index < Batch.Count && !Done; Index++)
        {
            PEXT4_DIR_ITEM Item = &Batch.Items[Index];
            WCHAR NameBuffer[EXT4_NAME_BYTES + 1];
            UNICODE_STRING Name;
            ULONG Bytes = 0, Needed, NameBytes;
            BOOLEAN Dots = (Item->NameLength == 1 && Item->Name[0] == '.') ||
                           (Item->NameLength == 2 && Item->Name[0] == '.' && Item->Name[1] == '.');
            NTSTATUS Converted;

            Ccb->DirOffset = Item->Next;
            if (Dots && IsRoot) continue;

            Converted = RtlUTF8ToUnicodeN(NameBuffer, EXT4_NAME_BYTES * sizeof(WCHAR), &Bytes, Item->Name, Item->NameLength);
            if ((!NT_SUCCESS(Converted) && Converted != STATUS_SOME_NOT_MAPPED) || Bytes == 0) continue;
            Name.Buffer = NameBuffer;
            Name.Length = Name.MaximumLength = (USHORT)Bytes;

            if (!Ccb->MatchAll && Ccb->PatternHasWildcards && !FsRtlIsNameInExpression(&Ccb->SearchPattern, &Name, TRUE, NULL))
                continue;

            Needed = Fixed + Name.Length;
            if (Used != 0) Used = ALIGN_UP_BY(Used, sizeof(LONGLONG));
            if (Used + Needed > Length)
            {
                if (Previous != NULL)
                {
                    /* The entry waits for the next request */
                    Ccb->DirOffset = Item->Start;
                    Done = TRUE;
                    break;
                }
                /* Not even one entry fits: as much of its name as there is room for goes out */
                NameBytes = (Length - Fixed) & ~(sizeof(WCHAR) - 1);
                Status = STATUS_BUFFER_OVERFLOW;
            }
            else
            {
                NameBytes = Name.Length;
            }

            RtlZeroMemory(Buffer + Used, Fixed);
            Ext4FormatEntry(Fcb->Vcb, Class, Buffer + Used, Item, &Name, NameBytes);
            if (Previous != NULL) *(PULONG)Previous = (ULONG)(Buffer + Used - Previous);
            Previous = Buffer + Used;
            Used += Fixed + NameBytes;

            if (Single || Status == STATUS_BUFFER_OVERFLOW) Done = TRUE;
        }
    }

    ExFreePoolWithTag(Batch.Items, TAG_EXT4);
    if (!NT_SUCCESS(Status)) return Status;

    if (Previous == NULL) return First ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
    *Information = Used;
    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchDirectoryControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PEXT4_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    ULONG_PTR Information = 0;
    PEXT4_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (!Fcb->Directory) return Ext4CompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_DIRECTORY:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
            Status = Ext4QueryDirectory(Fcb, Ccb, Irp, &Information);
            ExReleaseResourceLite(&Fcb->MainResource);
            KeLeaveCriticalRegion();
            return Ext4CompleteRequest(Irp, Status, Information);

        case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            FsRtlNotifyFullChangeDirectory(Vcb->NotifySync, &Vcb->DirNotifyList, Ccb, (PSTRING)&Ccb->FullName,
                                           BooleanFlagOn(Stack->Flags, SL_WATCH_TREE), FALSE,
                                           Stack->Parameters.NotifyDirectory.CompletionFilter, Irp, NULL, NULL);
            ExReleaseResourceLite(&Vcb->Resource);
            KeLeaveCriticalRegion();
            return STATUS_PENDING;

        default:
            return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}
