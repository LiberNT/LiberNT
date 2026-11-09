/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The file system of last resort, which hands the volume through
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PDEVICE_OBJECT RawDiskDeviceObject = NULL;
PDEVICE_OBJECT RawCdromDeviceObject = NULL;
PDEVICE_OBJECT RawTapeDeviceObject = NULL;

/* PRIVATE DATA ***************************************************************/

typedef struct _RAW_VCB
{
    PDEVICE_OBJECT TargetDeviceObject;
    PVPB Vpb;
    ULONG OpenCount;
    SHARE_ACCESS ShareAccess;
    LARGE_INTEGER VolumeSize;
    ULONG BytesPerSector;
    BOOLEAN Dismounted;
} RAW_VCB, *PRAW_VCB;

static PDRIVER_OBJECT RawFsDriverObject = NULL;

static const UNICODE_STRING RawFsName = RTL_CONSTANT_STRING(L"RAW");

NTSTATUS NTAPI RawDispatch(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS NTAPI RawCompletionRoutine(_In_ PDEVICE_OBJECT DeviceObject,
                                    _In_ PIRP Irp,
                                    _In_ PVOID Context);
NTSTATUS RawInitializeVcb(_In_ PRAW_VCB Vcb,
                          _In_ PDEVICE_OBJECT TargetDeviceObject,
                          _In_ PVPB Vpb);
BOOLEAN RawCheckForDismount(_In_ PRAW_VCB Vcb, _In_ BOOLEAN Force);
NTSTATUS RawCreate(_In_ PVOID Vcb, _In_ PDEVICE_OBJECT Dev, _In_ PIRP Irp);
NTSTATUS RawClose(_In_ PVOID Vcb, _In_ PDEVICE_OBJECT Dev, _In_ PIRP Irp);
NTSTATUS RawCleanup(_In_ PVOID Vcb, _In_ PDEVICE_OBJECT Dev, _In_ PIRP Irp);
NTSTATUS RawReadWriteDeviceControl(_In_ PVOID Vcb,
                                   _In_ PDEVICE_OBJECT Dev,
                                   _In_ PIRP Irp);
NTSTATUS RawQueryInformation(_In_ PVOID Vcb,
                             _In_ PDEVICE_OBJECT Dev,
                             _In_ PIRP Irp);
NTSTATUS RawSetInformation(_In_ PVOID Vcb,
                           _In_ PDEVICE_OBJECT Dev,
                           _In_ PIRP Irp);
NTSTATUS RawQueryVolumeInformation(_In_ PVOID Vcb,
                                   _In_ PDEVICE_OBJECT Dev,
                                   _In_ PIRP Irp);
NTSTATUS RawFileSystemControl(_In_ PVOID Vcb,
                              _In_ PDEVICE_OBJECT Dev,
                              _In_ PIRP Irp);
NTSTATUS RawShutdown(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
VOID RawUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS RawMountVolume(_In_ PIO_STACK_LOCATION StackPtr);
NTSTATUS RawUserFsCtrl(_In_ PIO_STACK_LOCATION StackPtr, _In_ PRAW_VCB Vcb);
NTSTATUS RawQueryFsVolumeInfo(_In_ PRAW_VCB Vcb,
                              _Out_ PFILE_FS_VOLUME_INFORMATION Buffer,
                              _Inout_ PULONG Length);
NTSTATUS RawQueryFsSizeInfo(_In_ PRAW_VCB Vcb,
                            _Out_ PFILE_FS_SIZE_INFORMATION Buffer,
                            _Inout_ PULONG Length);
NTSTATUS RawQueryFsDeviceInfo(_In_ PRAW_VCB Vcb,
                              _Out_ PFILE_FS_DEVICE_INFORMATION Buffer,
                              _Inout_ PULONG Length);
NTSTATUS RawQueryFsAttributeInfo(_In_ PRAW_VCB Vcb,
                                 _Out_ PFILE_FS_ATTRIBUTE_INFORMATION Buffer,
                                 _Inout_ PULONG Length);

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
RawFsIsRawFileSystemDeviceObject(
    IN PDEVICE_OBJECT DeviceObject)
{
    if (DeviceObject == RawDiskDeviceObject) return TRUE;
    if (DeviceObject == RawCdromDeviceObject) return TRUE;
    if (DeviceObject == RawTapeDeviceObject) return TRUE;

    return FALSE;
}

static
VOID
RawCompleteRequest(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS
NTAPI
RawCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    if (Irp->PendingReturned) IoMarkIrpPending(Irp);

    return STATUS_SUCCESS;
}

NTSTATUS
RawInitializeVcb(
    _In_ PRAW_VCB Vcb,
    _In_ PDEVICE_OBJECT TargetDeviceObject,
    _In_ PVPB Vpb)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    DISK_GEOMETRY Geometry;
    PARTITION_INFORMATION Partition;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();

    RtlZeroMemory(Vcb, sizeof(RAW_VCB));

    Vcb->TargetDeviceObject = TargetDeviceObject;
    Vcb->Vpb = Vpb;
    Vcb->BytesPerSector = 512;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                        TargetDeviceObject,
                                        NULL,
                                        0,
                                        &Geometry,
                                        sizeof(Geometry),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = NULL;

    Status = IoCallDriver(TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (NT_SUCCESS(Status) && Geometry.BytesPerSector != 0)
    {
        Vcb->BytesPerSector = Geometry.BytesPerSector;
        Vcb->VolumeSize.QuadPart = Geometry.Cylinders.QuadPart *
                                   Geometry.TracksPerCylinder *
                                   Geometry.SectorsPerTrack *
                                   Geometry.BytesPerSector;
    }

    KeClearEvent(&Event);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_PARTITION_INFO,
                                        TargetDeviceObject,
                                        NULL,
                                        0,
                                        &Partition,
                                        sizeof(Partition),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL) return STATUS_SUCCESS;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = NULL;

    Status = IoCallDriver(TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (NT_SUCCESS(Status))
    {
        Vcb->VolumeSize = Partition.PartitionLength;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
RawCheckForDismount(
    _In_ PRAW_VCB Vcb,
    _In_ BOOLEAN Force)
{
    PVPB Vpb;
    KIRQL OldIrql;

    IoAcquireVpbSpinLock(&OldIrql);

    Vpb = Vcb->Vpb;

    if (!Force && Vcb->OpenCount != 0)
    {
        IoReleaseVpbSpinLock(OldIrql);
        return FALSE;
    }

    if (Vpb != NULL && Vpb->ReferenceCount != 0 && !Force)
    {
        IoReleaseVpbSpinLock(OldIrql);
        return FALSE;
    }

    if (Vpb != NULL)
    {
        Vpb->Flags &= ~VPB_MOUNTED;
        Vpb->DeviceObject = NULL;
    }

    Vcb->Dismounted = TRUE;

    IoReleaseVpbSpinLock(OldIrql);

    return TRUE;
}

NTSTATUS
RawCreate(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;
    NTSTATUS Status;

    PAGED_CODE();

    if (Vcb == NULL || Vcb->Dismounted)
    {
        RawCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
        return STATUS_VOLUME_DISMOUNTED;
    }

    if (StackPtr->FileObject->FileName.Length != 0)
    {
        RawCompleteRequest(Irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (Vcb->OpenCount == 0)
    {
        IoSetShareAccess(StackPtr->Parameters.Create.SecurityContext
                             ->DesiredAccess,
                         StackPtr->Parameters.Create.ShareAccess,
                         StackPtr->FileObject,
                         &Vcb->ShareAccess);
    }
    else
    {
        Status = IoCheckShareAccess(StackPtr->Parameters.Create.SecurityContext
                                        ->DesiredAccess,
                                    StackPtr->Parameters.Create.ShareAccess,
                                    StackPtr->FileObject,
                                    &Vcb->ShareAccess,
                                    TRUE);
        if (!NT_SUCCESS(Status))
        {
            RawCompleteRequest(Irp, Status, 0);
            return Status;
        }
    }

    Vcb->OpenCount++;

    StackPtr->FileObject->FsContext = Vcb;
    StackPtr->FileObject->SectionObjectPointer = NULL;

    UNREFERENCED_PARAMETER(DeviceObject);

    RawCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

    return STATUS_SUCCESS;
}

NTSTATUS
RawClose(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PRAW_VCB Vcb = VcbVoid;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Vcb != NULL && Vcb->OpenCount != 0) Vcb->OpenCount--;

    if (Vcb != NULL && Vcb->OpenCount == 0) RawCheckForDismount(Vcb, FALSE);

    RawCompleteRequest(Irp, STATUS_SUCCESS, 0);

    return STATUS_SUCCESS;
}

NTSTATUS
RawCleanup(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Vcb != NULL)
    {
        IoRemoveShareAccess(StackPtr->FileObject, &Vcb->ShareAccess);
    }

    RawCompleteRequest(Irp, STATUS_SUCCESS, 0);

    return STATUS_SUCCESS;
}

NTSTATUS
RawReadWriteDeviceControl(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PRAW_VCB Vcb = VcbVoid;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Vcb == NULL || Vcb->Dismounted)
    {
        RawCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
        return STATUS_VOLUME_DISMOUNTED;
    }

    IoSkipCurrentIrpStackLocation(Irp);

    return IoCallDriver(Vcb->TargetDeviceObject, Irp);
}

NTSTATUS
RawQueryFsVolumeInfo(
    _In_ PRAW_VCB Vcb,
    _Out_ PFILE_FS_VOLUME_INFORMATION Buffer,
    _Inout_ PULONG Length)
{
    if (*Length < sizeof(FILE_FS_VOLUME_INFORMATION))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Buffer, sizeof(FILE_FS_VOLUME_INFORMATION));

    Buffer->VolumeSerialNumber =
        (Vcb->Vpb != NULL) ? Vcb->Vpb->SerialNumber : 0;
    Buffer->SupportsObjects = FALSE;
    Buffer->VolumeLabelLength = 0;

    *Length -= sizeof(FILE_FS_VOLUME_INFORMATION);

    return STATUS_SUCCESS;
}

NTSTATUS
RawQueryFsSizeInfo(
    _In_ PRAW_VCB Vcb,
    _Out_ PFILE_FS_SIZE_INFORMATION Buffer,
    _Inout_ PULONG Length)
{
    if (*Length < sizeof(FILE_FS_SIZE_INFORMATION))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Buffer, sizeof(FILE_FS_SIZE_INFORMATION));

    Buffer->BytesPerSector = Vcb->BytesPerSector;
    Buffer->SectorsPerAllocationUnit = 1;
    Buffer->TotalAllocationUnits.QuadPart =
        (Vcb->BytesPerSector != 0)
            ? Vcb->VolumeSize.QuadPart / Vcb->BytesPerSector
            : 0;
    Buffer->AvailableAllocationUnits.QuadPart = 0;

    *Length -= sizeof(FILE_FS_SIZE_INFORMATION);

    return STATUS_SUCCESS;
}

NTSTATUS
RawQueryFsDeviceInfo(
    _In_ PRAW_VCB Vcb,
    _Out_ PFILE_FS_DEVICE_INFORMATION Buffer,
    _Inout_ PULONG Length)
{
    if (*Length < sizeof(FILE_FS_DEVICE_INFORMATION))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Buffer->DeviceType = Vcb->TargetDeviceObject->DeviceType;
    Buffer->Characteristics = Vcb->TargetDeviceObject->Characteristics;

    *Length -= sizeof(FILE_FS_DEVICE_INFORMATION);

    return STATUS_SUCCESS;
}

NTSTATUS
RawQueryFsAttributeInfo(
    _In_ PRAW_VCB Vcb,
    _Out_ PFILE_FS_ATTRIBUTE_INFORMATION Buffer,
    _Inout_ PULONG Length)
{
    ULONG Required;

    UNREFERENCED_PARAMETER(Vcb);

    Required = (ULONG)FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION,
                                   FileSystemName) +
               RawFsName.Length;

    if (*Length < (ULONG)FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION,
                                      FileSystemName))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Buffer->FileSystemAttributes = 0;
    Buffer->MaximumComponentNameLength = 0;
    Buffer->FileSystemNameLength = RawFsName.Length;

    if (*Length < Required)
    {
        RtlCopyMemory(Buffer->FileSystemName,
                      RawFsName.Buffer,
                      *Length - FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION,
                                             FileSystemName));
        *Length = 0;
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(Buffer->FileSystemName,
                  RawFsName.Buffer,
                  RawFsName.Length);

    *Length -= Required;

    return STATUS_SUCCESS;
}

NTSTATUS
RawQueryVolumeInformation(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;
    NTSTATUS Status;
    ULONG Length;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Vcb == NULL)
    {
        RawCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
        return STATUS_VOLUME_DISMOUNTED;
    }

    Length = StackPtr->Parameters.QueryVolume.Length;

    switch (StackPtr->Parameters.QueryVolume.FsInformationClass)
    {
        case FileFsVolumeInformation:
            Status = RawQueryFsVolumeInfo(Vcb,
                                          Irp->AssociatedIrp.SystemBuffer,
                                          &Length);
            break;

        case FileFsSizeInformation:
            Status = RawQueryFsSizeInfo(Vcb,
                                        Irp->AssociatedIrp.SystemBuffer,
                                        &Length);
            break;

        case FileFsDeviceInformation:
            Status = RawQueryFsDeviceInfo(Vcb,
                                          Irp->AssociatedIrp.SystemBuffer,
                                          &Length);
            break;

        case FileFsAttributeInformation:
            Status = RawQueryFsAttributeInfo(Vcb,
                                             Irp->AssociatedIrp.SystemBuffer,
                                             &Length);
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    RawCompleteRequest(
        Irp,
        Status,
        NT_SUCCESS(Status)
            ? StackPtr->Parameters.QueryVolume.Length - Length
            : 0);

    return Status;
}

NTSTATUS
RawQueryInformation(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;
    ULONG Returned = 0;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Vcb == NULL)
    {
        RawCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
        return STATUS_VOLUME_DISMOUNTED;
    }

    switch (StackPtr->Parameters.QueryFile.FileInformationClass)
    {
        case FilePositionInformation:
        {
            PFILE_POSITION_INFORMATION Info =
                Irp->AssociatedIrp.SystemBuffer;

            if (StackPtr->Parameters.QueryFile.Length < sizeof(*Info))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Info->CurrentByteOffset =
                StackPtr->FileObject->CurrentByteOffset;
            Returned = sizeof(*Info);
            Status = STATUS_SUCCESS;
            break;
        }

        case FileStandardInformation:
        {
            PFILE_STANDARD_INFORMATION Info =
                Irp->AssociatedIrp.SystemBuffer;

            if (StackPtr->Parameters.QueryFile.Length < sizeof(*Info))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(Info, sizeof(*Info));
            Info->EndOfFile = Vcb->VolumeSize;
            Info->AllocationSize = Vcb->VolumeSize;
            Info->NumberOfLinks = 1;
            Returned = sizeof(*Info);
            Status = STATUS_SUCCESS;
            break;
        }

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    RawCompleteRequest(Irp, Status, Returned);

    return Status;
}

NTSTATUS
RawSetInformation(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (StackPtr->Parameters.SetFile.FileInformationClass !=
        FilePositionInformation)
    {
        RawCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }

    if (StackPtr->Parameters.SetFile.Length <
        sizeof(FILE_POSITION_INFORMATION))
    {
        RawCompleteRequest(Irp, STATUS_BUFFER_TOO_SMALL, 0);
        return STATUS_BUFFER_TOO_SMALL;
    }

    UNREFERENCED_PARAMETER(Vcb);

    StackPtr->FileObject->CurrentByteOffset =
        ((PFILE_POSITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer)
            ->CurrentByteOffset;

    Status = STATUS_SUCCESS;

    RawCompleteRequest(Irp, Status, 0);

    return Status;
}

NTSTATUS
RawMountVolume(
    _In_ PIO_STACK_LOCATION StackPtr)
{
    PDEVICE_OBJECT VolumeDeviceObject;
    PRAW_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoCreateDevice(RawFsDriverObject,
                            sizeof(RAW_VCB),
                            NULL,
                            StackPtr->Parameters.MountVolume.Vpb
                                ->RealDevice->DeviceType,
                            0,
                            FALSE,
                            &VolumeDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    VolumeDeviceObject->StackSize =
        (CCHAR)(StackPtr->Parameters.MountVolume.DeviceObject->StackSize + 1);
    VolumeDeviceObject->SectorSize =
        StackPtr->Parameters.MountVolume.DeviceObject->SectorSize;
    VolumeDeviceObject->AlignmentRequirement =
        StackPtr->Parameters.MountVolume.DeviceObject->AlignmentRequirement;
    VolumeDeviceObject->Flags |= DO_DIRECT_IO;

    Vcb = VolumeDeviceObject->DeviceExtension;

    Status = RawInitializeVcb(Vcb,
                              StackPtr->Parameters.MountVolume.DeviceObject,
                              StackPtr->Parameters.MountVolume.Vpb);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(VolumeDeviceObject);
        return Status;
    }

    StackPtr->Parameters.MountVolume.Vpb->DeviceObject = VolumeDeviceObject;
    StackPtr->Parameters.MountVolume.Vpb->SerialNumber = 0;
    StackPtr->Parameters.MountVolume.Vpb->VolumeLabelLength = 0;

    VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS
RawUserFsCtrl(
    _In_ PIO_STACK_LOCATION StackPtr,
    _In_ PRAW_VCB Vcb)
{
    PAGED_CODE();

    switch (StackPtr->Parameters.FileSystemControl.FsControlCode)
    {
        case FSCTL_DISMOUNT_VOLUME:
            return RawCheckForDismount(Vcb, TRUE) ? STATUS_SUCCESS
                                                  : STATUS_DEVICE_BUSY;

        case FSCTL_LOCK_VOLUME:
            if (Vcb->OpenCount > 1) return STATUS_ACCESS_DENIED;
            Vcb->Vpb->Flags |= VPB_LOCKED;
            return STATUS_SUCCESS;

        case FSCTL_UNLOCK_VOLUME:
            if (!(Vcb->Vpb->Flags & VPB_LOCKED))
            {
                return STATUS_NOT_LOCKED;
            }
            Vcb->Vpb->Flags &= ~VPB_LOCKED;
            return STATUS_SUCCESS;

        case FSCTL_IS_VOLUME_MOUNTED:
            return Vcb->Dismounted ? STATUS_VOLUME_DISMOUNTED
                                   : STATUS_SUCCESS;

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS
RawFileSystemControl(
    _In_ PVOID VcbVoid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PRAW_VCB Vcb = VcbVoid;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (StackPtr->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
            Status = RawMountVolume(StackPtr);
            break;

        case IRP_MN_VERIFY_VOLUME:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_USER_FS_REQUEST:
            if (Vcb == NULL)
            {
                Status = STATUS_VOLUME_DISMOUNTED;
                break;
            }

            Status = RawUserFsCtrl(StackPtr, Vcb);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    RawCompleteRequest(Irp, Status, 0);

    return Status;
}

NTSTATUS
RawShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    RawCompleteRequest(Irp, STATUS_SUCCESS, 0);

    return STATUS_SUCCESS;
}

VOID
RawUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (RawDiskDeviceObject != NULL)
    {
        IoDeleteDevice(RawDiskDeviceObject);
        RawDiskDeviceObject = NULL;
    }

    if (RawCdromDeviceObject != NULL)
    {
        IoDeleteDevice(RawCdromDeviceObject);
        RawCdromDeviceObject = NULL;
    }

    if (RawTapeDeviceObject != NULL)
    {
        IoDeleteDevice(RawTapeDeviceObject);
        RawTapeDeviceObject = NULL;
    }
}

NTSTATUS
NTAPI
RawDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PVOID Vcb = NULL;

    if (!RawFsIsRawFileSystemDeviceObject(DeviceObject))
    {
        Vcb = DeviceObject->DeviceExtension;
    }

    switch (StackPtr->MajorFunction)
    {
        case IRP_MJ_CREATE:
            return RawCreate(Vcb, DeviceObject, Irp);

        case IRP_MJ_CLOSE:
            return RawClose(Vcb, DeviceObject, Irp);

        case IRP_MJ_CLEANUP:
            return RawCleanup(Vcb, DeviceObject, Irp);

        case IRP_MJ_READ:
        case IRP_MJ_WRITE:
        case IRP_MJ_DEVICE_CONTROL:
            return RawReadWriteDeviceControl(Vcb, DeviceObject, Irp);

        case IRP_MJ_QUERY_INFORMATION:
            return RawQueryInformation(Vcb, DeviceObject, Irp);

        case IRP_MJ_SET_INFORMATION:
            return RawSetInformation(Vcb, DeviceObject, Irp);

        case IRP_MJ_QUERY_VOLUME_INFORMATION:
            return RawQueryVolumeInformation(Vcb, DeviceObject, Irp);

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            return RawFileSystemControl(Vcb, DeviceObject, Irp);

        case IRP_MJ_SHUTDOWN:
            return RawShutdown(DeviceObject, Irp);

        default:
            RawCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
RawFsDriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\FileSystem\\RAW");
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);

    if (DriverObject == NULL)
    {
        Status = IoCreateDriver(&DriverName, RawFsDriverEntry);
        return Status;
    }

    RawFsDriverObject = DriverObject;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = RawDispatch;
    }

    DriverObject->DriverUnload = RawUnload;

    Status = IoCreateDevice(DriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &RawDiskDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoCreateDevice(DriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_CD_ROM_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &RawCdromDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(RawDiskDeviceObject);
        RawDiskDeviceObject = NULL;
        return Status;
    }

    Status = IoCreateDevice(DriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_TAPE_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &RawTapeDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(RawCdromDeviceObject);
        IoDeleteDevice(RawDiskDeviceObject);
        RawCdromDeviceObject = NULL;
        RawDiskDeviceObject = NULL;
        return Status;
    }

    RawDiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    RawCdromDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    RawTapeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    IoRegisterFileSystem(RawDiskDeviceObject);
    IoRegisterFileSystem(RawCdromDeviceObject);
    IoRegisterFileSystem(RawTapeDeviceObject);

    return STATUS_SUCCESS;
}
