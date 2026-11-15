/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Handle cleanup, deletion of a name and the closing of file objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

/* Takes one name of the file away. When it was the last one the inode lives on until the FCB goes */
NTSTATUS
Ext4UnlinkName(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONG ParentInode,
    _In_reads_bytes_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    bool LastLink = false;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));
    ASSERT(!Fcb->Volume);

    if (ParentInode == 0 || NameLength == 0) return STATUS_CANNOT_DELETE;
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4Unlink(&Vcb->Core, ParentInode, Name, NameLength, Fcb->Inode, &LastLink));
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (LastLink)
    {
        Fcb->Orphaned = TRUE;
        Fcb->Attr.Links = 0;
    }
    else if (Fcb->Attr.Links > 1)
    {
        Fcb->Attr.Links--;
    }
    Ext4MarkCoreDirty(Vcb);
    return STATUS_SUCCESS;
}

/* The last handle of a file whose deletion is pending takes its name away, and the file with its last name */
static
VOID
Ext4DeleteOnCleanup(
    _In_ PEXT4_FCB Fcb,
    _In_ PEXT4_CCB Ccb,
    _In_ PFILE_OBJECT FileObject)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    LARGE_INTEGER Zero = { 0 };
    NTSTATUS Status;

    ASSERT(Fcb->DeletePending);
    ASSERT(Fcb->OpenHandles == 0);
    ASSERT(!Fcb->Orphaned);

    Status = Ext4UnlinkName(Fcb, Fcb->DeleteParent, Fcb->DeleteName, Fcb->DeleteNameLength);
    Fcb->DeletePending = FALSE;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ext4: inode %lu could not be deleted (%lx)\n", Fcb->Inode, Status);
        return;
    }

    if (Fcb->Orphaned && !Fcb->Directory)
    {
        /* Nothing of the data is worth writing any more */
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        Fcb->Header.FileSize.QuadPart = 0;
        Fcb->Header.ValidDataLength.QuadPart = 0;
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        CcUninitializeCacheMap(FileObject, &Zero, NULL);
        MmFlushImageSection(&Fcb->SectionObjectPointers, MmFlushForDelete);
    }

    Ext4NotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                     FILE_ACTION_REMOVED);
}

NTSTATUS
NTAPI
Ext4DispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject->FsContext;
    PEXT4_CCB Ccb = FileObject->FsContext2;
    BOOLEAN Uninitialized = FALSE;
    PEXT4_VCB Vcb;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);

    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    ASSERT(Ccb != NULL);
    ASSERT(Ccb->Signature == EXT4_CCB_SIGNATURE);
    Vcb = Fcb->Vcb;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    if (Fcb->Directory) FsRtlNotifyCleanup(Vcb->NotifySync, &Vcb->DirNotifyList, Ccb);
    if (!Fcb->Directory && !Fcb->Volume) FsRtlFastUnlockAll(&Fcb->FileLock, FileObject, IoGetRequestorProcess(Irp), NULL);

    if (Ccb->DeleteOnClose && !Fcb->Volume && !Fcb->Orphaned && !Vcb->Dismounted)
        Ext4MarkDeletePending(Fcb, Ccb, FileObject, TRUE);

    IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);

    if (Vcb->Locked && Vcb->LockFileObject == FileObject)
    {
        Vcb->Locked = FALSE;
        Vcb->LockFileObject = NULL;
        Vcb->Vpb->Flags &= ~VPB_LOCKED;
    }

    ASSERT(Fcb->OpenHandles > 0);
    ASSERT(Vcb->OpenHandles > 0);
    Fcb->OpenHandles--;
    Vcb->OpenHandles--;

    if (!Fcb->Volume && !Vcb->Dismounted)
    {
        if (Fcb->OpenHandles == 0 && Fcb->DeletePending && !Fcb->Orphaned)
        {
            Ext4DeleteOnCleanup(Fcb, Ccb, FileObject);
            Uninitialized = Fcb->Orphaned && !Fcb->Directory;
        }
        if (!Fcb->Orphaned) Ext4WriteTimes(Fcb);
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    ExReleaseResourceLite(&Vcb->Resource);

    if (!Uninitialized) CcUninitializeCacheMap(FileObject, NULL, NULL);
    KeLeaveCriticalRegion();

    FileObject->Flags |= FO_CLEANUP_COMPLETE;
    return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
Ext4DispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject->FsContext;
    PEXT4_CCB Ccb = FileObject->FsContext2;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);

    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);

    if (Ccb != NULL)
    {
        ASSERT(Ccb->Signature == EXT4_CCB_SIGNATURE);
        if (Ccb->SearchPattern.Buffer != NULL) ExFreePoolWithTag(Ccb->SearchPattern.Buffer, TAG_EXT4);
        if (Ccb->AllocatedName != NULL) ExFreePoolWithTag(Ccb->AllocatedName, TAG_EXT4);
        Ccb->Signature = 0;
        ExFreePoolWithTag(Ccb, TAG_EXT4);
        FileObject->FsContext2 = NULL;
    }

    Ext4DereferenceFcb(Fcb);
    return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
}
