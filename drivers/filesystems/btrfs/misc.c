/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Byte-range locks, device control, Plug and Play and requests the volume does not serve
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

NTSTATUS
NTAPI
BtrfsDispatchUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PBTRFS_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_QUERY_EA:
            return BtrfsCompleteRequest(Irp, STATUS_EAS_NOT_SUPPORTED, 0);
        case IRP_MJ_SET_EA:
        case IRP_MJ_SET_SECURITY:
            if (!BtrfsIsFsDevice(DeviceObject) && Fcb != NULL && !Fcb->Vcb->Writable)
                return BtrfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);
            return BtrfsCompleteRequest(Irp, (Stack->MajorFunction == IRP_MJ_SET_EA) ? STATUS_EAS_NOT_SUPPORTED : STATUS_INVALID_DEVICE_REQUEST, 0);
        default:
            return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

NTSTATUS
NTAPI
BtrfsDispatchLockControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PBTRFS_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL || Fcb->Directory || Fcb->Volume)
        return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    Status = FsRtlProcessFileLock(&Fcb->FileLock, Irp, NULL);
    Fcb->Header.IsFastIoPossible = FsRtlAreThereCurrentFileLocks(&Fcb->FileLock) ? FastIoIsQuestionable : FastIoIsPossible;
    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PBTRFS_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Fcb->Vcb->TargetDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
BtrfsPnpDone(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
BtrfsForwardAndWait(
    _In_ PBTRFS_VCB Vcb,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, BtrfsPnpDone, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PBTRFS_VCB Vcb = BtrfsDeviceVcb(DeviceObject);
    PBTRFS_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Vcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb != NULL) Vcb = Fcb->Vcb;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_REMOVE_DEVICE:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            Status = (Vcb->OpenHandles != 0 || Vcb->Locked) ? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
            if (NT_SUCCESS(Status))
            {
                Vcb->Locked = TRUE;
                Vcb->Vpb->Flags |= VPB_LOCKED;
            }
            ExReleaseResourceLite(&Vcb->Resource);
            KeLeaveCriticalRegion();
            if (!NT_SUCCESS(Status)) return BtrfsCompleteRequest(Irp, Status, 0);

            Status = BtrfsForwardAndWait(Vcb, Irp);
            if (!NT_SUCCESS(Status))
            {
                KeEnterCriticalRegion();
                ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
                Vcb->Locked = FALSE;
                Vcb->Vpb->Flags &= ~VPB_LOCKED;
                ExReleaseResourceLite(&Vcb->Resource);
                KeLeaveCriticalRegion();
            }
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            if (Vcb->Locked && Vcb->LockFileObject == NULL)
            {
                Vcb->Locked = FALSE;
                Vcb->Vpb->Flags &= ~VPB_LOCKED;
            }
            ExReleaseResourceLite(&Vcb->Resource);
            KeLeaveCriticalRegion();
            break;

        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = BtrfsForwardAndWait(Vcb, Irp);
            BtrfsDismountVolume(Vcb);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Vcb->TargetDeviceObject, Irp);
}
