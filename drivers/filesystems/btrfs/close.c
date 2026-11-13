/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cleanup and close of file objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

/* The last handle of a file whose deletion is pending takes its name away, and the file with its last name */
static
VOID
BtrfsDeleteOnCleanup(
    _In_ PBTRFS_FCB Fcb,
    _In_ PBTRFS_CCB Ccb,
    _In_ PFILE_OBJECT FileObject)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    LARGE_INTEGER Zero = { 0 };
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(Fcb->DeletePending && Fcb->OpenHandles == 0 && !Fcb->Removed);

    if (!Fcb->Directory)
    {
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        Fcb->Header.FileSize.QuadPart = 0;
        Fcb->Header.ValidDataLength.QuadPart = 0;
        Fcb->Header.AllocationSize.QuadPart = 0;
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        CcUninitializeCacheMap(FileObject, &Zero, NULL);
        MmFlushImageSection(&Fcb->SectionObjectPointers, MmFlushForDelete);
    }

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, &Fcb->Root, Fcb->Inode, &Fcb->InodeItem);
    if (NT_SUCCESS(Status) && Fcb->Directory && Fcb->InodeItem.Size != 0) Status = STATUS_DIRECTORY_NOT_EMPTY;
    if (NT_SUCCESS(Status))
        Status = BtrfsDeleteLink(Vcb, &Fcb->Root, Fcb->ParentInode, &Fcb->LinkName, Fcb->Inode, &Fcb->InodeItem);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);

    Fcb->DeletePending = FALSE;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Btrfs: inode %I64u of subvolume %I64u could not be deleted (%lx)\n", Fcb->Inode, Fcb->Root.Id, Status);
        return;
    }

    Fcb->Removed = (Fcb->InodeItem.LinkCount == 0);
    if (Fcb->Removed) BtrfsReleaseAllReservations(Fcb);
    BtrfsNotifyChange(Vcb, &Ccb->FullName, Fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                      FILE_ACTION_REMOVED);
}

NTSTATUS
NTAPI
BtrfsDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject->FsContext;
    PBTRFS_CCB Ccb = FileObject->FsContext2;
    BOOLEAN Uninitialized = FALSE;
    PBTRFS_VCB Vcb;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);
    ASSERT(Ccb != NULL && Ccb->Signature == BTRFS_CCB_SIGNATURE);
    Vcb = Fcb->Vcb;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    if (Fcb->Directory) FsRtlNotifyCleanup(Vcb->NotifySync, &Vcb->DirNotifyList, Ccb);
    if (!Fcb->Directory && !Fcb->Volume) FsRtlFastUnlockAll(&Fcb->FileLock, FileObject, IoGetRequestorProcess(Irp), NULL);

    if (Ccb->DeleteOnClose && !Fcb->Volume && !Fcb->Removed && !Vcb->Dismounted)
        BtrfsMarkDeletePending(Fcb, Ccb, FileObject, TRUE);

    IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);

    if (Vcb->Locked && Vcb->LockFileObject == FileObject)
    {
        Vcb->Locked = FALSE;
        Vcb->LockFileObject = NULL;
        Vcb->Vpb->Flags &= ~VPB_LOCKED;
    }

    ASSERT(Fcb->OpenHandles > 0 && Vcb->OpenHandles > 0);
    Fcb->OpenHandles--;
    Vcb->OpenHandles--;

    if (Fcb->OpenHandles == 0 && Fcb->DeletePending && !Fcb->Removed && !Vcb->Dismounted)
    {
        BtrfsDeleteOnCleanup(Fcb, Ccb, FileObject);
        Uninitialized = !Fcb->Directory;
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    ExReleaseResourceLite(&Vcb->Resource);

    if (!Uninitialized) CcUninitializeCacheMap(FileObject, NULL, NULL);
    KeLeaveCriticalRegion();

    FileObject->Flags |= FO_CLEANUP_COMPLETE;
    return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
BtrfsDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject->FsContext;
    PBTRFS_CCB Ccb = FileObject->FsContext2;

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);

    if (Ccb != NULL)
    {
        ASSERT(Ccb->Signature == BTRFS_CCB_SIGNATURE);
        if (Ccb->SearchPattern.Buffer != NULL) ExFreePoolWithTag(Ccb->SearchPattern.Buffer, TAG_BTRFS);
        if (Ccb->AllocatedName != NULL) ExFreePoolWithTag(Ccb->AllocatedName, TAG_BTRFS);
        Ccb->Signature = 0;
        ExFreePoolWithTag(Ccb, TAG_BTRFS);
        FileObject->FsContext2 = NULL;
    }

    KeEnterCriticalRegion();
    BtrfsDereferenceFcb(Fcb);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}
