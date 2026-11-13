/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File system control: mount, verify, volume locking and reparse data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

typedef struct _BTRFS_LX_REPARSE_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    ULONG Version;
    CHAR Target[1];
} BTRFS_LX_REPARSE_BUFFER, *PBTRFS_LX_REPARSE_BUFFER;

static
NTSTATUS
BtrfsGetReparsePoint(
    _In_ PBTRFS_FCB Fcb,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PBTRFS_LX_REPARSE_BUFFER Reparse = Buffer;
    ULONG Header = REPARSE_DATA_BUFFER_HEADER_SIZE;
    PCHAR Target = NULL;
    ULONG TargetLength = 0;
    NTSTATUS Status;

    *Information = 0;
    if (Fcb->ReparseTag == 0) return STATUS_NOT_A_REPARSE_POINT;
    if (Buffer == NULL || Length < Header) return STATUS_BUFFER_TOO_SMALL;

    if (Fcb->ReparseTag == IO_REPARSE_TAG_LX_SYMLINK)
    {
        Status = BtrfsReadSymlink(Fcb, &Target, &TargetLength);
        if (!NT_SUCCESS(Status)) return Status;

        if (Length < Header + sizeof(ULONG) + TargetLength)
        {
            ExFreePoolWithTag(Target, TAG_BTRFS);
            return STATUS_BUFFER_OVERFLOW;
        }

        Reparse->ReparseTag = IO_REPARSE_TAG_LX_SYMLINK;
        Reparse->ReparseDataLength = (USHORT)(sizeof(ULONG) + TargetLength);
        Reparse->Reserved = 0;
        Reparse->Version = 2;
        RtlCopyMemory(Reparse->Target, Target, TargetLength);
        ExFreePoolWithTag(Target, TAG_BTRFS);
        *Information = Header + sizeof(ULONG) + TargetLength;
        return STATUS_SUCCESS;
    }

    Reparse->ReparseTag = Fcb->ReparseTag;
    Reparse->ReparseDataLength = 0;
    Reparse->Reserved = 0;
    *Information = Header;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsLockVolume(
    _In_ PBTRFS_VCB Vcb,
    _In_ PFILE_OBJECT FileObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;
    NTSTATUS Status = STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);

    if (Fcb == NULL || !Fcb->Volume)
        Status = STATUS_INVALID_PARAMETER;
    else if (Vcb->Locked)
        Status = STATUS_ACCESS_DENIED;
    else if (Vcb->OpenHandles > 1)
        Status = STATUS_ACCESS_DENIED;
    else if (Vcb->Writable && !NT_SUCCESS(Status = BtrfsFlushVolume(Vcb)))
        NOTHING;
    else
    {
        Vcb->Locked = TRUE;
        Vcb->LockFileObject = FileObject;
        Vcb->Vpb->Flags |= VPB_LOCKED;
    }

    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();
    return Status;
}

static
NTSTATUS
BtrfsUnlockVolume(
    _In_ PBTRFS_VCB Vcb,
    _In_ PFILE_OBJECT FileObject)
{
    NTSTATUS Status = STATUS_NOT_LOCKED;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (Vcb->Locked && Vcb->LockFileObject == FileObject)
    {
        Vcb->Locked = FALSE;
        Vcb->LockFileObject = NULL;
        Vcb->Vpb->Flags &= ~VPB_LOCKED;
        Status = STATUS_SUCCESS;
    }
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();
    return Status;
}

/* A dismounted volume answers only cleanup and close; the next open of the device mounts it anew */
NTSTATUS
BtrfsDismountVolume(
    _In_ PBTRFS_VCB Vcb)
{
    BOOLEAN Release = FALSE;
    KIRQL OldIrql;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (!Vcb->Dismounted)
    {
        if (Vcb->Writable)
        {
            NTSTATUS Status = BtrfsFlushVolume(Vcb);

            if (!NT_SUCCESS(Status)) DPRINT1("Btrfs: the volume could not be written out before dismounting (%lx)\n", Status);
        }
        Vcb->Dismounted = TRUE;
        Release = TRUE;
        IoAcquireVpbSpinLock(&OldIrql);
        Vcb->Vpb->Flags &= ~VPB_MOUNTED;
        IoReleaseVpbSpinLock(OldIrql);
    }
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    if (Release) BtrfsDereferenceVcb(Vcb);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsUserFsRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PBTRFS_VCB Vcb = BtrfsDeviceVcb(DeviceObject);

    *Information = 0;
    if (Vcb == NULL || Fcb == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    Vcb = Fcb->Vcb;

    switch (Stack->Parameters.FileSystemControl.FsControlCode)
    {
        case FSCTL_LOCK_VOLUME:
            return BtrfsLockVolume(Vcb, FileObject);
        case FSCTL_UNLOCK_VOLUME:
            return BtrfsUnlockVolume(Vcb, FileObject);
        case FSCTL_DISMOUNT_VOLUME:
            return BtrfsDismountVolume(Vcb);
        case FSCTL_IS_VOLUME_MOUNTED:
            return Vcb->Dismounted ? STATUS_VOLUME_DISMOUNTED : STATUS_SUCCESS;
        case FSCTL_GET_REPARSE_POINT:
        {
            NTSTATUS Status;

            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
            Status = BtrfsGetReparsePoint(Fcb, Irp->AssociatedIrp.SystemBuffer ? Irp->AssociatedIrp.SystemBuffer : Irp->UserBuffer,
                                          Stack->Parameters.FileSystemControl.OutputBufferLength, Information);
            ExReleaseResourceLite(&Fcb->MainResource);
            KeLeaveCriticalRegion();
            return Status;
        }
        case FSCTL_MARK_VOLUME_DIRTY:
            return Vcb->Writable ? STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED;
        case FSCTL_SET_REPARSE_POINT:
        case FSCTL_DELETE_REPARSE_POINT:
        case FSCTL_SET_COMPRESSION:
        case FSCTL_SET_SPARSE:
        case FSCTL_SET_ZERO_DATA:
            /* TODO: reparse points, compression, sparse ranges and zeroing are not changed by this driver */
            return Vcb->Writable ? STATUS_INVALID_DEVICE_REQUEST : STATUS_MEDIA_WRITE_PROTECTED;
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS
NTAPI
BtrfsDispatchFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
            if (!BtrfsIsFsDevice(DeviceObject))
                Status = STATUS_INVALID_DEVICE_REQUEST;
            else
                Status = BtrfsMountVolume(Stack->Parameters.MountVolume.DeviceObject, Stack->Parameters.MountVolume.Vpb);
            break;

        case IRP_MN_VERIFY_VOLUME:
            Status = BtrfsIsFsDevice(DeviceObject) ? STATUS_INVALID_DEVICE_REQUEST : BtrfsVerifyVolume(Irp);
            break;

        case IRP_MN_USER_FS_REQUEST:
        case IRP_MN_KERNEL_CALL:
            Status = BtrfsIsFsDevice(DeviceObject) ? STATUS_INVALID_DEVICE_REQUEST : BtrfsUserFsRequest(DeviceObject, Irp, &Information);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return BtrfsCompleteRequest(Irp, Status, Information);
}
