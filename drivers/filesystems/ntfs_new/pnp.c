/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdPnp)
#endif

/* FUNCTIONS ****************************************************************/

static
VOID
NtfsUnlockVolumeForPnp(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _In_ PVolumeContextBlock VolCB)
{
    KIRQL OldIrql;

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->VolumeLockOwner &&
        VolCB->VolumeLockOwner == VolCB->StreamFileObject)
    {
        VolCB->VolumeLockOwner = NULL;
        IoAcquireVpbSpinLock(&OldIrql);
        VolumeDeviceObject->Vpb->Flags &= ~VPB_LOCKED;
        IoReleaseVpbSpinLock(OldIrql);
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);
}

static
NTSTATUS
NtfsLockVolumeForPnp(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _In_ PVolumeContextBlock VolCB)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (VolCB->Dismounting ||
             VolCB->VolumeLockOwner ||
             VolCB->OpenHandleCount != 0 ||
             !VolCB->StreamFileObject)
        Status = STATUS_ACCESS_DENIED;
    else
    {
        VolCB->VolumeLockOwner = VolCB->StreamFileObject;
        IoAcquireVpbSpinLock(&OldIrql);
        VolumeDeviceObject->Vpb->Flags |= VPB_LOCKED;
        IoReleaseVpbSpinLock(OldIrql);
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    if (Status == STATUS_VOLUME_DISMOUNTED)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsFlushVolume(VolCB, TRUE);
    if (!NT_SUCCESS(Status))
        NtfsUnlockVolumeForPnp(VolumeDeviceObject, VolCB);
    return Status;
}

static
VOID
NtfsMarkVolumeRemoved(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _In_ PVolumeContextBlock VolCB)
{
    KIRQL OldIrql;

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    VolCB->Dismounted = TRUE;
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();

    IoAcquireVpbSpinLock(&OldIrql);
    VolumeDeviceObject->Vpb->Flags &= ~VPB_MOUNTED;
    IoReleaseVpbSpinLock(OldIrql);
}

VOID
NtfsReleaseDismountedVolume(
    _In_ PDEVICE_OBJECT VolumeDeviceObject)
{
    PVolumeContextBlock VolCB =
        (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    PFILE_OBJECT StreamFileObject = NULL;
    KIRQL OldIrql;

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounted && VolCB->OpenHandleCount == 0)
    {
        StreamFileObject = VolCB->StreamFileObject;
        VolCB->StreamFileObject = NULL;
        if (StreamFileObject && VolCB->VolumeLockOwner == StreamFileObject)
        {
            VolCB->VolumeLockOwner = NULL;
            IoAcquireVpbSpinLock(&OldIrql);
            VolumeDeviceObject->Vpb->Flags &= ~VPB_LOCKED;
            IoReleaseVpbSpinLock(OldIrql);
        }
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    if (StreamFileObject)
    {
        NtfsFlushVolume(VolCB, TRUE);
        ObDereferenceObject(StreamFileObject);
        /* FIXME: Delete the volume device and free the volume context once its last file object is closed. */
    }
}

_Function_class_(IRP_MJ_PNP)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdPnp(_In_ PDEVICE_OBJECT VolumeDeviceObject,
           _Inout_ PIRP Irp)
{
    /* Overview:
     * Handles PnP requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-pnp
     */
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PVolumeContextBlock VolCB;
    NTSTATUS Status;

    PAGED_CODE();

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    NtfsBindVolumeDisk(VolCB);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Status = NtfsLockVolumeForPnp(VolumeDeviceObject, VolCB);
            if (NT_SUCCESS(Status))
            {
                IoForwardIrpSynchronously(VolCB->StorageDevice, Irp);
                Status = Irp->IoStatus.Status;
                if (!NT_SUCCESS(Status))
                    NtfsUnlockVolumeForPnp(VolumeDeviceObject, VolCB);
            }
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            IoForwardIrpSynchronously(VolCB->StorageDevice, Irp);
            Status = Irp->IoStatus.Status;
            NtfsUnlockVolumeForPnp(VolumeDeviceObject, VolCB);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            NtfsMarkVolumeRemoved(VolumeDeviceObject, VolCB);
            IoForwardIrpSynchronously(VolCB->StorageDevice, Irp);
            Status = Irp->IoStatus.Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            NtfsReleaseDismountedVolume(VolumeDeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(VolCB->StorageDevice, Irp);
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
