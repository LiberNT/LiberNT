/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a file system tells the rest of the system about its volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntddstor.h>
#include <ioevent.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_VOLUME_TAG 'vRsF'

/* PRIVATE FUNCTIONS **********************************************************/

static
const GUID *
FsRtlpVolumeEventGuid(
    _In_ ULONG EventCode)
{
    switch (EventCode)
    {
        case FSRTL_VOLUME_DISMOUNT: return &GUID_IO_VOLUME_DISMOUNT;
        case FSRTL_VOLUME_DISMOUNT_FAILED:
            return &GUID_IO_VOLUME_DISMOUNT_FAILED;
        case FSRTL_VOLUME_LOCK: return &GUID_IO_VOLUME_LOCK;
        case FSRTL_VOLUME_LOCK_FAILED: return &GUID_IO_VOLUME_LOCK_FAILED;
        case FSRTL_VOLUME_UNLOCK: return &GUID_IO_VOLUME_UNLOCK;
        case FSRTL_VOLUME_MOUNT: return &GUID_IO_VOLUME_MOUNT;
        case FSRTL_VOLUME_PREPARING_EJECT:
            return &GUID_IO_VOLUME_PREPARING_EJECT;
        case FSRTL_VOLUME_CHANGE_SIZE: return &GUID_IO_VOLUME_CHANGE_SIZE;

        /* TODO: the published ioevent.h carries no GUID for NEEDS_CHKDSK, WORM_NEAR_FULL, WEARING_OUT, FORCED_CLOSED, INFO_MAKE_COMPAT or BACKGROUND_FORMAT */
        default: return NULL;
    }
}

static
PVPB
FsRtlpVpbOf(
    _In_ PFILE_OBJECT FileObject)
{
    if (FileObject->Vpb != NULL) return FileObject->Vpb;

    if (FileObject->DeviceObject != NULL) return FileObject->DeviceObject->Vpb;

    return NULL;
}

static
VOID
NTAPI
FsRtlpVolumeEventComplete(
    _Inout_opt_ PVOID Context)
{
    if (Context != NULL) ExFreePoolWithTag(Context, FSRTL_VOLUME_TAG);
}

static
NTSTATUS
FsRtlpQueryStorageProperty(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ STORAGE_PROPERTY_ID PropertyId,
    _Out_writes_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length)
{
    STORAGE_PROPERTY_QUERY Query;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.PropertyId = PropertyId;
    Query.QueryType = PropertyStandardQuery;

    RtlZeroMemory(Descriptor, Length);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_QUERY_PROPERTY,
                                        DeviceObject,
                                        &Query,
                                        sizeof(Query),
                                        Descriptor,
                                        Length,
                                        FALSE,
                                        &Event,
                                        &IoStatus);

    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlNotifyVolumeEvent(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG EventCode)
{
    PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification;
    const GUID *Event;
    PVPB Vpb;

    PAGED_CODE();

    Event = FsRtlpVolumeEventGuid(EventCode);
    if (Event == NULL) return STATUS_INVALID_PARAMETER;

    Vpb = FsRtlpVpbOf(FileObject);
    if ((Vpb == NULL) || (Vpb->RealDevice == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Notification = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*Notification),
                                         FSRTL_VOLUME_TAG);

    if (Notification == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Notification, sizeof(*Notification));
    Notification->Version = 1;
    Notification->Size = sizeof(*Notification);
    Notification->Event = *Event;
    Notification->FileObject = FileObject;
    Notification->NameBufferOffset = -1;

    IoReportTargetDeviceChangeAsynchronous(Vpb->RealDevice,
                                           Notification,
                                           FsRtlpVolumeEventComplete,
                                           Notification);

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlDismountComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ NTSTATUS DismountStatus)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(DismountStatus);

    /* TODO: what this routine is to do after a dismount is not published anywhere; doing nothing leaves every documented caller unaffected */
}

NTSTATUS
NTAPI
FsRtlGetSectorSizeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PFILE_FS_SECTOR_SIZE_INFORMATION SectorSizeInfo)
{
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR Alignment;
    DEVICE_SEEK_PENALTY_DESCRIPTOR SeekPenalty;
    DEVICE_TRIM_DESCRIPTOR Trim;
    NTSTATUS Status;

    PAGED_CODE();

    if ((DeviceObject == NULL) || (SectorSizeInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FsRtlpQueryStorageProperty(DeviceObject,
                                        StorageAccessAlignmentProperty,
                                        &Alignment,
                                        sizeof(Alignment));

    if (!NT_SUCCESS(Status)) return Status;

    if ((Alignment.BytesPerLogicalSector == 0) ||
        (Alignment.BytesPerPhysicalSector == 0))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    RtlZeroMemory(SectorSizeInfo, sizeof(*SectorSizeInfo));

    SectorSizeInfo->LogicalBytesPerSector = Alignment.BytesPerLogicalSector;
    SectorSizeInfo->PhysicalBytesPerSectorForAtomicity =
        Alignment.BytesPerPhysicalSector;
    SectorSizeInfo->PhysicalBytesPerSectorForPerformance =
        Alignment.BytesPerPhysicalSector;
    SectorSizeInfo->FileSystemEffectivePhysicalBytesPerSectorForAtomicity =
        Alignment.BytesPerPhysicalSector;
    SectorSizeInfo->ByteOffsetForSectorAlignment =
        Alignment.BytesOffsetForSectorAlignment;
    SectorSizeInfo->ByteOffsetForPartitionAlignment = SSINFO_OFFSET_UNKNOWN;

    if (Alignment.BytesOffsetForSectorAlignment == 0)
    {
        SectorSizeInfo->Flags |= SSINFO_FLAGS_ALIGNED_DEVICE;
    }

    if (NT_SUCCESS(FsRtlpQueryStorageProperty(DeviceObject,
                                              StorageDeviceSeekPenaltyProperty,
                                              &SeekPenalty,
                                              sizeof(SeekPenalty))) &&
        !SeekPenalty.IncursSeekPenalty)
    {
        SectorSizeInfo->Flags |= SSINFO_FLAGS_NO_SEEK_PENALTY;
    }

    if (NT_SUCCESS(FsRtlpQueryStorageProperty(DeviceObject,
                                              StorageDeviceTrimProperty,
                                              &Trim,
                                              sizeof(Trim))) &&
        Trim.TrimEnabled)
    {
        SectorSizeInfo->Flags |= SSINFO_FLAGS_TRIM_ENABLED;
    }

    return STATUS_SUCCESS;
}

/* EOF */
