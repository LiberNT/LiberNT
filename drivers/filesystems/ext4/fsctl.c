/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mount requests, volume locking and dismount, reparse data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

typedef struct _EXT4_LX_REPARSE_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    ULONG Version;
    CHAR Target[1];
} EXT4_LX_REPARSE_BUFFER, *PEXT4_LX_REPARSE_BUFFER;

static
NTSTATUS
Ext4GetReparsePoint(
    _In_ PEXT4_FCB Fcb,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PEXT4_LX_REPARSE_BUFFER Reparse = Buffer;
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONG Header = REPARSE_DATA_BUFFER_HEADER_SIZE;
    uint32_t TargetLength = 0;
    NTSTATUS Status;
    PCHAR Target;

    *Information = 0;
    if (Fcb->ReparseTag == 0) return STATUS_NOT_A_REPARSE_POINT;
    if (Buffer == NULL || Length < Header) return STATUS_BUFFER_TOO_SMALL;

    if (Fcb->ReparseTag != IO_REPARSE_TAG_LX_SYMLINK)
    {
        Reparse->ReparseTag = Fcb->ReparseTag;
        Reparse->ReparseDataLength = 0;
        Reparse->Reserved = 0;
        *Information = Header;
        return STATUS_SUCCESS;
    }

    Target = ExAllocatePoolUninitialized(PagedPool, E4_SYMLINK_MAX + 1, TAG_EXT4);
    if (Target == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4ReadLink(&Vcb->Core, Fcb->Inode, Target, E4_SYMLINK_MAX, &TargetLength));
    Ext4UnlockCore(Vcb);

    if (NT_SUCCESS(Status) && Length < Header + sizeof(ULONG) + TargetLength) Status = STATUS_BUFFER_OVERFLOW;
    if (NT_SUCCESS(Status))
    {
        Reparse->ReparseTag = IO_REPARSE_TAG_LX_SYMLINK;
        Reparse->ReparseDataLength = (USHORT)(sizeof(ULONG) + TargetLength);
        Reparse->Reserved = 0;
        Reparse->Version = 2;
        RtlCopyMemory(Reparse->Target, Target, TargetLength);
        *Information = Header + sizeof(ULONG) + TargetLength;
    }
    ExFreePoolWithTag(Target, TAG_EXT4);
    return Status;
}

/* Writes out and drops the cached data of every file, so that nothing of the volume lives outside the core.
 * Fails when a file is still mapped or open. */
static
NTSTATUS
Ext4PurgeVolume(
    _In_ PEXT4_VCB Vcb,
    _In_ BOOLEAN Force)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Bucket;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));

    for (Bucket = 0; Bucket < EXT4_FCB_BUCKETS; Bucket++)
    {
        PLIST_ENTRY Entry;

        for (Entry = Vcb->FcbTable[Bucket].Flink; Entry != &Vcb->FcbTable[Bucket]; Entry = Entry->Flink)
        {
            PEXT4_FCB Fcb = CONTAINING_RECORD(Entry, EXT4_FCB, TableLinks);
            IO_STATUS_BLOCK IoStatus;

            if (Fcb->Directory || Fcb->SectionObjectPointers.DataSectionObject == NULL)
            {
                if (!Fcb->Directory) Ext4WriteTimes(Fcb);
                continue;
            }

            ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
            CcFlushCache(&Fcb->SectionObjectPointers, NULL, 0, &IoStatus);
            if (!NT_SUCCESS(IoStatus.Status) && !Force) Status = IoStatus.Status;
            Ext4WriteTimes(Fcb);
            if (!CcPurgeCacheSection(&Fcb->SectionObjectPointers, NULL, 0, FALSE) && !Force) Status = STATUS_ACCESS_DENIED;
            ExReleaseResourceLite(&Fcb->MainResource);
            if (!NT_SUCCESS(Status)) return Status;
        }
    }
    return Status;
}

static
NTSTATUS
Ext4LockVolume(
    _In_ PEXT4_VCB Vcb,
    _In_ PFILE_OBJECT FileObject)
{
    PEXT4_FCB Fcb = FileObject->FsContext;
    NTSTATUS Status = STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);

    if (Fcb == NULL || !Fcb->Volume)
        Status = STATUS_INVALID_PARAMETER;
    else if (Vcb->Locked || Vcb->OpenHandles > 1)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = Ext4PurgeVolume(Vcb, FALSE);
    if (NT_SUCCESS(Status)) Status = Ext4FlushVolume(Vcb);
    if (NT_SUCCESS(Status))
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
Ext4UnlockVolume(
    _In_ PEXT4_VCB Vcb,
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

/* A dismounted volume answers only cleanup and close; the next open of the device mounts it anew. The journal is
 * closed here and now: a second mount must find the volume at rest, whatever handles are still open on this one. */
NTSTATUS
Ext4DismountVolume(
    _In_ PEXT4_VCB Vcb)
{
    BOOLEAN Release = FALSE;
    KIRQL OldIrql;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (!Vcb->Dismounted)
    {
        Ext4PurgeVolume(Vcb, TRUE);
        Ext4StopCore(Vcb);
        Vcb->Dismounted = TRUE;
        Release = TRUE;
        IoAcquireVpbSpinLock(&OldIrql);
        Vcb->Vpb->Flags &= ~VPB_MOUNTED;
        IoReleaseVpbSpinLock(OldIrql);
    }
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    if (Release) Ext4DereferenceVcb(Vcb);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4UserFsRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    PEXT4_VCB Vcb = Ext4DeviceVcb(DeviceObject);

    *Information = 0;
    if (Vcb == NULL || Fcb == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    Vcb = Fcb->Vcb;

    switch (Stack->Parameters.FileSystemControl.FsControlCode)
    {
        case FSCTL_LOCK_VOLUME:
            return Ext4LockVolume(Vcb, FileObject);
        case FSCTL_UNLOCK_VOLUME:
            return Ext4UnlockVolume(Vcb, FileObject);
        case FSCTL_DISMOUNT_VOLUME:
            return Ext4DismountVolume(Vcb);
        case FSCTL_IS_VOLUME_MOUNTED:
            return Vcb->Dismounted ? STATUS_VOLUME_DISMOUNTED : STATUS_SUCCESS;
        case FSCTL_GET_REPARSE_POINT:
        {
            NTSTATUS Status;

            if (Vcb->Dismounted) return STATUS_VOLUME_DISMOUNTED;
            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
            Status = Ext4GetReparsePoint(Fcb, Irp->AssociatedIrp.SystemBuffer ? Irp->AssociatedIrp.SystemBuffer : Irp->UserBuffer,
                                         Stack->Parameters.FileSystemControl.OutputBufferLength, Information);
            ExReleaseResourceLite(&Fcb->MainResource);
            KeLeaveCriticalRegion();
            return Status;
        }
        case FSCTL_MARK_VOLUME_DIRTY:
            return Vcb->Writable ? STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED;
        case FSCTL_SET_SPARSE:
            /* Every file may have holes */
            return Vcb->Writable ? STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED;
        case FSCTL_SET_REPARSE_POINT:
        case FSCTL_DELETE_REPARSE_POINT:
        case FSCTL_SET_COMPRESSION:
        case FSCTL_SET_ZERO_DATA:
            /* TODO: reparse data is set through the Linux mode attribute only; compression and zeroed ranges are not offered */
            return Vcb->Writable ? STATUS_INVALID_DEVICE_REQUEST : STATUS_MEDIA_WRITE_PROTECTED;
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS
NTAPI
Ext4DispatchFileSystemControl(
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
            if (!Ext4IsFsDevice(DeviceObject))
                Status = STATUS_INVALID_DEVICE_REQUEST;
            else
                Status = Ext4MountVolume(Stack->Parameters.MountVolume.DeviceObject, Stack->Parameters.MountVolume.Vpb);
            break;

        case IRP_MN_VERIFY_VOLUME:
            Status = Ext4IsFsDevice(DeviceObject) ? STATUS_INVALID_DEVICE_REQUEST : Ext4VerifyVolume(Irp);
            break;

        case IRP_MN_USER_FS_REQUEST:
        case IRP_MN_KERNEL_CALL:
            Status = Ext4IsFsDevice(DeviceObject) ? STATUS_INVALID_DEVICE_REQUEST : Ext4UserFsRequest(DeviceObject, Irp, &Information);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return Ext4CompleteRequest(Irp, Status, Information);
}
