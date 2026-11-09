/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading and writing whole sectors, and what a disk says about itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
FstubSendIrp(
    _In_ PIRP Irp,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEVENT Event,
    _In_ PIO_STATUS_BLOCK IoStatus)
{
    NTSTATUS Status;

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus->Status;
    }

    return Status;
}

/* Asks a disk a question that takes no input and gives a fixed answer back */
static
NTSTATUS
FstubDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputBufferLength,
                                        OutputBuffer,
                                        OutputBufferLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    return FstubSendIrp(Irp, DeviceObject, &Event, &IoStatus);
}

static
NTSTATUS
FstubTransfer(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG MajorFunction,
    _In_ ULONG SectorCount,
    _In_ ULONGLONG Lba,
    _In_ PVOID Buffer)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    KEVENT Event;
    PIRP Irp;
    ULONG Length;

    PAGED_CODE();

    if (Disk->BytesPerSector == 0 || SectorCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Length = SectorCount * Disk->BytesPerSector;
    Offset.QuadPart = (LONGLONG)(Lba * Disk->BytesPerSector);

    if (Disk->Length.QuadPart != 0 &&
        Offset.QuadPart + (LONGLONG)Length > Disk->Length.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(MajorFunction,
                                       Disk->DeviceObject,
                                       Buffer,
                                       Length,
                                       &Offset,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->Flags |= IRP_NOCACHE;

    return FstubSendIrp(Irp, Disk->DeviceObject, &Event, &IoStatus);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FstubGetDiskGeometry(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDISK_GEOMETRY_EX Geometry)
{
    GET_LENGTH_INFORMATION Length;
    DISK_GEOMETRY Plain;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(Geometry, sizeof(*Geometry));

    Status = FstubDeviceControl(DeviceObject,
                                IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                NULL,
                                0,
                                Geometry,
                                sizeof(*Geometry));
    if (NT_SUCCESS(Status) && Geometry->Geometry.BytesPerSector != 0)
    {
        return STATUS_SUCCESS;
    }

    Status = FstubDeviceControl(DeviceObject,
                                IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                NULL,
                                0,
                                &Plain,
                                sizeof(Plain));
    if (!NT_SUCCESS(Status)) return Status;

    if (Plain.BytesPerSector == 0) return STATUS_DEVICE_CONFIGURATION_ERROR;

    Geometry->Geometry = Plain;
    Geometry->DiskSize.QuadPart = Plain.Cylinders.QuadPart *
                                  Plain.TracksPerCylinder *
                                  Plain.SectorsPerTrack *
                                  Plain.BytesPerSector;

    Status = FstubDeviceControl(DeviceObject,
                                IOCTL_DISK_GET_LENGTH_INFO,
                                NULL,
                                0,
                                &Length,
                                sizeof(Length));
    if (NT_SUCCESS(Status) && Length.Length.QuadPart != 0)
    {
        Geometry->DiskSize = Length.Length;
    }

    return STATUS_SUCCESS;
}

/* Reads whole sectors off the disk */
NTSTATUS
NTAPI
FstubReadSector(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG SectorCount,
    _In_ ULONGLONG Lba,
    _Out_writes_bytes_(SectorCount * Disk->BytesPerSector) PVOID Buffer)
{
    return FstubTransfer(Disk, IRP_MJ_READ, SectorCount, Lba, Buffer);
}

/* Writes whole sectors to the disk */
NTSTATUS
NTAPI
FstubWriteSector(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG SectorCount,
    _In_ ULONGLONG Lba,
    _In_reads_bytes_(SectorCount * Disk->BytesPerSector) PVOID Buffer)
{
    if (!Disk->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    return FstubTransfer(Disk, IRP_MJ_WRITE, SectorCount, Lba, Buffer);
}

NTSTATUS
NTAPI
FstubAllocateDiskInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Outptr_ PFSTUB_DISK *Disk,
    _In_opt_ PDISK_GEOMETRY_EX Geometry)
{
    DISK_GEOMETRY_EX Own;
    PFSTUB_DISK Result;
    NTSTATUS Status;

    PAGED_CODE();

    *Disk = NULL;

    if (Geometry == NULL)
    {
        Status = FstubGetDiskGeometry(DeviceObject, &Own);
        if (!NT_SUCCESS(Status)) return Status;

        Geometry = &Own;
    }

    if (Geometry->Geometry.BytesPerSector == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Result = ExAllocatePoolZero(NonPagedPool, sizeof(FSTUB_DISK), TAG_FSTUB);
    if (Result == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Result->Sector = ExAllocatePoolZero(NonPagedPool,
                                        Geometry->Geometry.BytesPerSector,
                                        TAG_FSTUB);
    if (Result->Sector == NULL)
    {
        ExFreePoolWithTag(Result, TAG_FSTUB);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Result->DeviceObject = DeviceObject;
    Result->BytesPerSector = Geometry->Geometry.BytesPerSector;
    Result->SectorsPerTrack = Geometry->Geometry.SectorsPerTrack;
    Result->TracksPerCylinder = Geometry->Geometry.TracksPerCylinder;
    Result->MediaType = Geometry->Geometry.MediaType;
    Result->Cylinders = Geometry->Geometry.Cylinders;
    Result->Length = Geometry->DiskSize;
    Result->SectorCount = (ULONGLONG)Geometry->DiskSize.QuadPart /
                          Geometry->Geometry.BytesPerSector;

    Status = FstubDeviceControl(DeviceObject,
                                IOCTL_DISK_IS_WRITABLE,
                                NULL,
                                0,
                                NULL,
                                0);
    Result->Writable = (Status != STATUS_MEDIA_WRITE_PROTECTED);

    *Disk = Result;
    return STATUS_SUCCESS;
}

/* Lets the disk go once the operation is over */
VOID
NTAPI
FstubFreeDiskInformation(
    _In_ PFSTUB_DISK Disk)
{
    if (Disk == NULL) return;

    if (Disk->Sector != NULL) ExFreePoolWithTag(Disk->Sector, TAG_FSTUB);

    ExFreePoolWithTag(Disk, TAG_FSTUB);
}

VOID
FASTCALL
xHalGetPartialGeometry(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PULONG ConfigurationInformationSectors,
    _Out_ PULONGLONG MaxOffset)
{
    DISK_GEOMETRY_EX Geometry;
    ULONGLONG Sectors;

    PAGED_CODE();

    *ConfigurationInformationSectors = 0;
    *MaxOffset = 0;

    if (!NT_SUCCESS(FstubGetDiskGeometry(DeviceObject, &Geometry))) return;
    if (Geometry.Geometry.BytesPerSector == 0) return;

    Sectors = (ULONGLONG)Geometry.DiskSize.QuadPart /
              Geometry.Geometry.BytesPerSector;

    *ConfigurationInformationSectors =
        (Sectors > MAXULONG) ? MAXULONG : (ULONG)Sectors;
    *MaxOffset = Sectors;
}

/* EOF */
