/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Extended attributes: the Linux owner, group, mode and device of a file, and its user attributes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_XATTR_USER         "user."
#define EXT4_XATTR_USER_LENGTH  5
#define EXT4_EA_ENTRY_SIZE(NameLength, ValueLength) \
    ALIGN_UP_BY(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + (NameLength) + 1 + (ValueLength), sizeof(ULONG))

static
BOOLEAN
Ext4EaNameIs(
    _In_reads_bytes_(Length) const CHAR *Name,
    _In_ ULONG Length,
    _In_ PCSTR Wanted)
{
    return Length == strlen(Wanted) && _strnicmp(Name, Wanted, Length) == 0;
}

static
PFILE_FULL_EA_INFORMATION
Ext4AppendEa(
    _Inout_ PUCHAR Buffer,
    _Inout_ PULONG Used,
    _Inout_ PFILE_FULL_EA_INFORMATION *Previous,
    _In_reads_bytes_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength,
    _In_reads_bytes_opt_(ValueLength) const VOID *Value,
    _In_ ULONG ValueLength)
{
    PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)(Buffer + *Used);

    Entry->NextEntryOffset = 0;
    Entry->Flags = 0;
    Entry->EaNameLength = (UCHAR)NameLength;
    Entry->EaValueLength = (USHORT)ValueLength;
    RtlCopyMemory(Entry->EaName, Name, NameLength);
    Entry->EaName[NameLength] = 0;
    if (Value != NULL && ValueLength != 0) RtlCopyMemory(Entry->EaName + NameLength + 1, Value, ValueLength);

    if (*Previous != NULL) (*Previous)->NextEntryOffset = (ULONG)((PUCHAR)Entry - (PUCHAR)*Previous);
    *Previous = Entry;
    *Used += EXT4_EA_ENTRY_SIZE(NameLength, ValueLength);
    return Entry;
}

/* Every attribute of the file as one list: the four Linux metadata attributes first, then the user attributes */
static
NTSTATUS
Ext4CollectEas(
    _In_ PEXT4_FCB Fcb,
    _Out_ PUCHAR *List,
    _Out_ PULONG Length)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONG Type = Fcb->Attr.Mode & E4_S_IFMT;
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    PCHAR Names = NULL;
    uint32_t NamesLength = 0, Offset;
    ULONG Size, Used = 0;
    PUCHAR Buffer;
    int Error;

    *List = NULL;
    *Length = 0;

    Ext4LockCore(Vcb);
    Error = E4XattrList(&Vcb->Core, Fcb->Inode, NULL, 0, &NamesLength);
    if (Error == EOK && NamesLength != 0)
    {
        Names = ExAllocatePoolUninitialized(PagedPool, NamesLength, TAG_EXT4);
        if (Names == NULL)
            Error = ENOMEM;
        else
            Error = E4XattrList(&Vcb->Core, Fcb->Inode, Names, NamesLength, &NamesLength);
    }

    Size = 4 * EXT4_EA_ENTRY_SIZE(6, 8);
    for (Offset = 0; Error == EOK && Offset < NamesLength; Offset += (uint32_t)strlen(Names + Offset) + 1)
    {
        ULONG NameLength = (ULONG)strlen(Names + Offset);
        uint32_t ValueLength = 0;

        if (NameLength <= EXT4_XATTR_USER_LENGTH || NameLength - EXT4_XATTR_USER_LENGTH > 255 ||
            RtlCompareMemory(Names + Offset, EXT4_XATTR_USER, EXT4_XATTR_USER_LENGTH) != EXT4_XATTR_USER_LENGTH)
        {
            continue;
        }
        if (E4XattrGet(&Vcb->Core, Fcb->Inode, Names + Offset, NameLength, NULL, 0, &ValueLength) != EOK) continue;
        if (ValueLength > MAXUSHORT) continue;
        Size += EXT4_EA_ENTRY_SIZE(NameLength - EXT4_XATTR_USER_LENGTH, ValueLength);
    }

    Buffer = NULL;
    if (Error == EOK)
    {
        Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_EXT4);
        if (Buffer == NULL) Error = ENOMEM;
    }
    if (Error == EOK)
    {
        ULONG Device[2] = { Fcb->Attr.DeviceMajor, Fcb->Attr.DeviceMinor };

        Ext4AppendEa(Buffer, &Used, &Previous, LX_FILE_METADATA_UID_EA_NAME, 6, &Fcb->Attr.Uid, sizeof(ULONG));
        Ext4AppendEa(Buffer, &Used, &Previous, LX_FILE_METADATA_GID_EA_NAME, 6, &Fcb->Attr.Gid, sizeof(ULONG));
        Ext4AppendEa(Buffer, &Used, &Previous, LX_FILE_METADATA_MODE_EA_NAME, 6, &Fcb->Attr.Mode, sizeof(ULONG));
        if (Type == E4_S_IFCHR || Type == E4_S_IFBLK)
            Ext4AppendEa(Buffer, &Used, &Previous, LX_FILE_METADATA_DEVICE_ID_EA_NAME, 6, Device, sizeof(Device));

        for (Offset = 0; Offset < NamesLength; Offset += (uint32_t)strlen(Names + Offset) + 1)
        {
            ULONG NameLength = (ULONG)strlen(Names + Offset);
            PFILE_FULL_EA_INFORMATION Entry;
            uint32_t ValueLength = 0;

            if (NameLength <= EXT4_XATTR_USER_LENGTH || NameLength - EXT4_XATTR_USER_LENGTH > 255 ||
                RtlCompareMemory(Names + Offset, EXT4_XATTR_USER, EXT4_XATTR_USER_LENGTH) != EXT4_XATTR_USER_LENGTH)
            {
                continue;
            }
            if (E4XattrGet(&Vcb->Core, Fcb->Inode, Names + Offset, NameLength, NULL, 0, &ValueLength) != EOK) continue;
            if (ValueLength > MAXUSHORT) continue;
            if (Used + EXT4_EA_ENTRY_SIZE(NameLength - EXT4_XATTR_USER_LENGTH, ValueLength) > Size) break;

            Entry = Ext4AppendEa(Buffer, &Used, &Previous, Names + Offset + EXT4_XATTR_USER_LENGTH,
                                 NameLength - EXT4_XATTR_USER_LENGTH, NULL, ValueLength);
            E4XattrGet(&Vcb->Core, Fcb->Inode, Names + Offset, NameLength,
                       Entry->EaName + Entry->EaNameLength + 1, ValueLength, &ValueLength);
        }
    }
    Ext4UnlockCore(Vcb);

    if (Names != NULL) ExFreePoolWithTag(Names, TAG_EXT4);
    if (Error != EOK)
    {
        if (Buffer != NULL) ExFreePoolWithTag(Buffer, TAG_EXT4);
        return Ext4Status(Vcb, Error);
    }

    /* The Linux metadata attributes are always there, so the list is never empty */
    ASSERT(Buffer != NULL);
    ASSERT(Used != 0);
    ASSERT(Used <= Size);
    *List = Buffer;
    *Length = Used;
    return STATUS_SUCCESS;
}

ULONG
Ext4EaSize(
    _In_ PEXT4_FCB Fcb)
{
    PFILE_FULL_EA_INFORMATION Entry;
    ULONG Length, Size = sizeof(ULONG);
    PUCHAR List;

    if (!NT_SUCCESS(Ext4CollectEas(Fcb, &List, &Length))) return 0;

    for (Entry = (PFILE_FULL_EA_INFORMATION)List;; Entry = (PFILE_FULL_EA_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset))
    {
        Size += 5 + Entry->EaNameLength + Entry->EaValueLength;
        if (Entry->NextEntryOffset == 0) break;
    }
    ExFreePoolWithTag(List, TAG_EXT4);
    return Size;
}

NTSTATUS
Ext4ApplyEaList(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_reads_bytes_(Length) PFILE_FULL_EA_INFORMATION List,
    _In_ ULONG Length)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    PFILE_FULL_EA_INFORMATION Entry = List;
    E4_ATTR Attr = Fcb->Attr;
    ULONG Mask = 0, NewType = 0, ErrorOffset;
    BOOLEAN DeferSymlink = FALSE;
    NTSTATUS Status;
    int Error = EOK;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));

    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    Status = IoCheckEaBufferValidity(List, Length, &ErrorOffset);
    if (!NT_SUCCESS(Status)) return Status;

    Ext4LockCore(Vcb);
    Error = E4Begin(&Vcb->Core);
    while (Error == EOK)
    {
        const CHAR *Name = Entry->EaName;
        const UCHAR *Value = (const UCHAR *)Entry->EaName + Entry->EaNameLength + 1;
        ULONG NameLength = Entry->EaNameLength;
        ULONG ValueLength = Entry->EaValueLength;

        if (Ext4EaNameIs(Name, NameLength, LX_FILE_METADATA_UID_EA_NAME))
        {
            if (ValueLength >= sizeof(ULONG))
            {
                RtlCopyMemory(&Attr.Uid, Value, sizeof(ULONG));
                Mask |= E4_SET_UID;
            }
        }
        else if (Ext4EaNameIs(Name, NameLength, LX_FILE_METADATA_GID_EA_NAME))
        {
            if (ValueLength >= sizeof(ULONG))
            {
                RtlCopyMemory(&Attr.Gid, Value, sizeof(ULONG));
                Mask |= E4_SET_GID;
            }
        }
        else if (Ext4EaNameIs(Name, NameLength, LX_FILE_METADATA_MODE_EA_NAME))
        {
            if (ValueLength >= sizeof(ULONG))
            {
                ULONG Mode;

                RtlCopyMemory(&Mode, Value, sizeof(ULONG));
                Attr.Mode = (Attr.Mode & E4_S_IFMT) | (Mode & 07777);
                Mask |= E4_SET_MODE;
                if ((Mode & E4_S_IFMT) != 0 && (Mode & E4_S_IFMT) != (Fcb->Attr.Mode & E4_S_IFMT)) NewType = Mode & E4_S_IFMT;
            }
        }
        else if (Ext4EaNameIs(Name, NameLength, LX_FILE_METADATA_DEVICE_ID_EA_NAME))
        {
            if (ValueLength >= 2 * sizeof(ULONG))
            {
                RtlCopyMemory(&Attr.DeviceMajor, Value, sizeof(ULONG));
                RtlCopyMemory(&Attr.DeviceMinor, Value + sizeof(ULONG), sizeof(ULONG));
                Mask |= E4_SET_DEVICE;
            }
        }
        else if (NameLength != 0)
        {
            /* TODO: the attribute lxcore keeps Linux extended attributes in is stored as one user attribute, so
             * Linux itself does not see them as security.* or trusted.* attributes */
            CHAR Full[EXT4_XATTR_USER_LENGTH + 256];

            RtlCopyMemory(Full, EXT4_XATTR_USER, EXT4_XATTR_USER_LENGTH);
            RtlCopyMemory(Full + EXT4_XATTR_USER_LENGTH, Name, NameLength);
            if (ValueLength == 0)
            {
                Error = E4XattrRemove(&Vcb->Core, Fcb->Inode, Full, EXT4_XATTR_USER_LENGTH + NameLength);
                if (Error == ENODATA) Error = EOK;
            }
            else
            {
                Error = E4XattrSet(&Vcb->Core, Fcb->Inode, Full, EXT4_XATTR_USER_LENGTH + NameLength, Value, ValueLength);
            }
        }

        if (Entry->NextEntryOffset == 0) break;
        Entry = (PFILE_FULL_EA_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset);
    }

    /* The mode attribute is how a file becomes a symbolic link, a device node, a fifo or a socket */
    if (Error == EOK && NewType == E4_S_IFLNK)
    {
        if (Ccb->ParentInode == 0)
            Error = ENOTSUP;
        else if ((Fcb->Attr.Mode & E4_S_IFMT) != E4_S_IFREG || Fcb->Attr.Size != 0 || Fcb->Attr.Links != 1)
            Error = EINVAL;
        else
            DeferSymlink = TRUE;
    }
    else if (Error == EOK && NewType != 0)
    {
        Error = E4SetType(&Vcb->Core, Ccb->ParentInode, Ccb->Name, Ccb->NameLength, Fcb->Inode, NewType | (Attr.Mode & 07777));
    }
    if (Error == EOK && Mask != 0) Error = E4SetAttr(&Vcb->Core, Fcb->Inode, Mask, &Attr);
    if (Vcb->Core.TransDepth != 0) Error = E4End(&Vcb->Core, Error);
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (DeferSymlink)
    {
        Fcb->PendingSymlink = TRUE;
        Fcb->PendingSymlinkMode = NewType | (Attr.Mode & 07777);
    }

    Ext4MarkCoreDirty(Vcb);
    Status = Ext4RefreshFcb(Fcb);
    if (NT_SUCCESS(Status) && NewType != 0 && !Fcb->Directory)
    {
        /* Whatever the handle did as a regular file is over: the size is the one of the new node */
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        Fcb->Header.FileSize.QuadPart = 0;
        Fcb->Header.ValidDataLength.QuadPart = 0;
        Fcb->Header.AllocationSize.QuadPart = 0;
        ExReleaseResourceLite(&Fcb->PagingIoResource);
    }
    return Status;
}

static
PVOID
Ext4EaBuffer(
    _In_ PIRP Irp)
{
    if (Irp->AssociatedIrp.SystemBuffer != NULL) return Irp->AssociatedIrp.SystemBuffer;
    if (Irp->MdlAddress != NULL) return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    return Irp->UserBuffer;
}

static
PFILE_FULL_EA_INFORMATION
Ext4FindEa(
    _In_ PUCHAR List,
    _In_reads_bytes_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength)
{
    PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)List;

    for (;;)
    {
        if (Entry->EaNameLength == NameLength && _strnicmp(Entry->EaName, Name, NameLength) == 0) return Entry;
        if (Entry->NextEntryOffset == 0) return NULL;
        Entry = (PFILE_FULL_EA_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset);
    }
}

static
NTSTATUS
Ext4QueryEaWorker(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PUCHAR Output = Ext4EaBuffer(Irp);
    ULONG OutputLength = Stack->Parameters.QueryEa.Length;
    PFILE_GET_EA_INFORMATION Wanted = Stack->Parameters.QueryEa.EaList;
    ULONG WantedLength = Stack->Parameters.QueryEa.EaListLength;
    BOOLEAN Single = BooleanFlagOn(Stack->Flags, SL_RETURN_SINGLE_ENTRY);
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    ULONG ListLength, Used = 0;
    NTSTATUS Status;
    PUCHAR List;

    *Information = 0;
    if (Fcb->Volume) return STATUS_INVALID_PARAMETER;
    if (Output == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = Ext4CollectEas(Fcb, &List, &ListLength);
    if (!NT_SUCCESS(Status)) return Status;

    if (Wanted != NULL && WantedLength != 0)
    {
        /* Each name asked for gets an entry; one the file lacks comes back with no value */
        ULONG Offset = 0;

        for (;;)
        {
            PFILE_GET_EA_INFORMATION Get = (PFILE_GET_EA_INFORMATION)((PUCHAR)Wanted + Offset);
            PFILE_FULL_EA_INFORMATION Found;
            ULONG ValueLength, Needed;

            if (Offset + FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) > WantedLength ||
                Offset + FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + Get->EaNameLength > WantedLength)
            {
                Status = STATUS_EA_LIST_INCONSISTENT;
                break;
            }
            Found = Ext4FindEa(List, Get->EaName, Get->EaNameLength);
            ValueLength = Found ? Found->EaValueLength : 0;
            Needed = EXT4_EA_ENTRY_SIZE(Get->EaNameLength, ValueLength);
            if (Used + Needed > OutputLength)
            {
                Status = (Used == 0) ? STATUS_BUFFER_TOO_SMALL : STATUS_BUFFER_OVERFLOW;
                break;
            }
            Ext4AppendEa(Output, &Used, &Previous, Get->EaName, Get->EaNameLength,
                         Found ? Found->EaName + Found->EaNameLength + 1 : NULL, ValueLength);
            if (Get->NextEntryOffset == 0 || Single) break;
            Offset += Get->NextEntryOffset;
        }
    }
    else
    {
        PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)List;
        ULONG Index = 0, Start;

        if (Stack->Flags & SL_INDEX_SPECIFIED)
            Start = Stack->Parameters.QueryEa.EaIndex ? Stack->Parameters.QueryEa.EaIndex - 1 : 0;
        else if (Stack->Flags & SL_RESTART_SCAN)
            Start = 0;
        else
            Start = Ccb->EaIndex;

        Status = STATUS_NO_MORE_EAS;
        for (;; Index++)
        {
            if (Index >= Start)
            {
                ULONG Needed = EXT4_EA_ENTRY_SIZE(Entry->EaNameLength, Entry->EaValueLength);

                if (Used + Needed > OutputLength)
                {
                    Status = (Used == 0) ? STATUS_BUFFER_TOO_SMALL : STATUS_BUFFER_OVERFLOW;
                    break;
                }
                Ext4AppendEa(Output, &Used, &Previous, Entry->EaName, Entry->EaNameLength,
                             Entry->EaName + Entry->EaNameLength + 1, Entry->EaValueLength);
                Status = STATUS_SUCCESS;
                Ccb->EaIndex = Index + 1;
                if (Single) break;
            }
            if (Entry->NextEntryOffset == 0) break;
            Entry = (PFILE_FULL_EA_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset);
        }
    }

    ExFreePoolWithTag(List, TAG_EXT4);
    *Information = Used;
    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchQueryEa(
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
    if (Fcb->Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    Status = Ext4QueryEaWorker(Fcb, Ccb, Irp, &Information);
    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, NT_ERROR(Status) ? 0 : Information);
}

NTSTATUS
NTAPI
Ext4DispatchSetEa(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PEXT4_CCB Ccb = FileObject ? FileObject->FsContext2 : NULL;
    PVOID Buffer = Ext4EaBuffer(Irp);
    PEXT4_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL || Ccb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (Buffer == NULL) return Ext4CompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Vcb->Resource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
    Status = Ext4ApplyEaList(Fcb, Ccb, Buffer, Stack->Parameters.SetEa.Length);
    if (NT_SUCCESS(Status)) Ext4NotifyChange(Vcb, &Ccb->FullName, FILE_NOTIFY_CHANGE_EA | FILE_NOTIFY_CHANGE_ATTRIBUTES, FILE_ACTION_MODIFIED);
    ExReleaseResourceLite(&Fcb->MainResource);
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, 0);
}
