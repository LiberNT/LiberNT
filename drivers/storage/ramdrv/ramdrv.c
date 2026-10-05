/*
 * PROJECT:     LiberNT Storage
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writable RAM drive formatted as FAT32 at load
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddstor.h>

#define NDEBUG
#include <debug.h>

#define RAMDRV_TAG 'vrdR'
#define RAMDRV_SECTOR_SIZE 512
#define RAMDRV_CHUNK_SIZE (64 * 1024)
#define RAMDRV_DEFAULT_SIZE_MB 256
#define RAMDRV_MIN_SIZE_MB 64
#define RAMDRV_MAX_SIZE_MB 1024
#define RAMDRV_FAT32_MIN_CLUSTERS 65525
#define RAMDRV_MAX_SECTORS_PER_CLUSTER 8
#define RAMDRV_RESERVED_SECTORS 32
#define RAMDRV_FSINFO_SECTOR 1
#define RAMDRV_BACKUP_BOOT_SECTOR 6
#define RAMDRV_ROOT_CLUSTER 2
#define RAMDRV_SECTORS_PER_TRACK 63
#define RAMDRV_TRACKS_PER_CYLINDER 255

typedef struct _RAMDRV_EXTENSION
{
    ULONGLONG Size;
    ULONG ChunkCount;
    PUCHAR *Chunks;
} RAMDRV_EXTENSION, *PRAMDRV_EXTENSION;

static UNICODE_STRING RamDrvLinkName = RTL_CONSTANT_STRING(L"\\DosDevices\\R:");

static
PUCHAR
RamDrvChunk(
    _In_ PRAMDRV_EXTENSION Extension,
    _In_ ULONG Index,
    _In_ BOOLEAN Allocate)
{
    PUCHAR Chunk = Extension->Chunks[Index];
    PUCHAR New;

    if (Chunk != NULL || !Allocate)
        return Chunk;
    New = ExAllocatePoolZero(NonPagedPool, RAMDRV_CHUNK_SIZE, RAMDRV_TAG);
    if (New == NULL)
        return NULL;
    Chunk = InterlockedCompareExchangePointer((PVOID *)&Extension->Chunks[Index], New, NULL);
    if (Chunk != NULL)
    {
        ExFreePoolWithTag(New, RAMDRV_TAG);
        return Chunk;
    }
    return New;
}

static
NTSTATUS
RamDrvTransfer(
    _In_ PRAMDRV_EXTENSION Extension,
    _In_ ULONGLONG Offset,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Write)
{
    while (Length != 0)
    {
        ULONG Index = (ULONG)(Offset / RAMDRV_CHUNK_SIZE);
        ULONG Within = (ULONG)(Offset % RAMDRV_CHUNK_SIZE);
        ULONG Bytes = min(Length, RAMDRV_CHUNK_SIZE - Within);
        PUCHAR Chunk = RamDrvChunk(Extension, Index, Write);

        if (Write)
        {
            if (Chunk == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
            RtlCopyMemory(Chunk + Within, Buffer, Bytes);
        }
        else if (Chunk != NULL)
        {
            RtlCopyMemory(Buffer, Chunk + Within, Bytes);
        }
        else
        {
            RtlZeroMemory(Buffer, Bytes);
        }
        Offset += Bytes;
        Buffer += Bytes;
        Length -= Bytes;
    }
    return STATUS_SUCCESS;
}

static
VOID
RamDrvPut16(
    _Out_writes_bytes_(2) PUCHAR Target,
    _In_ ULONG Value)
{
    Target[0] = (UCHAR)Value;
    Target[1] = (UCHAR)(Value >> 8);
}

static
VOID
RamDrvPut32(
    _Out_writes_bytes_(4) PUCHAR Target,
    _In_ ULONG Value)
{
    RamDrvPut16(Target, Value);
    RamDrvPut16(Target + 2, Value >> 16);
}

static
NTSTATUS
RamDrvWriteSector(
    _In_ PRAMDRV_EXTENSION Extension,
    _In_ ULONG SectorNumber,
    _In_reads_bytes_(RAMDRV_SECTOR_SIZE) PUCHAR Sector)
{
    return RamDrvTransfer(Extension,
                          (ULONGLONG)SectorNumber * RAMDRV_SECTOR_SIZE,
                          Sector,
                          RAMDRV_SECTOR_SIZE,
                          TRUE);
}

static
NTSTATUS
RamDrvFormat(
    _In_ PRAMDRV_EXTENSION Extension)
{
    UCHAR Sector[RAMDRV_SECTOR_SIZE];
    ULONG TotalSectors = (ULONG)(Extension->Size / RAMDRV_SECTOR_SIZE);
    ULONG SectorsPerCluster = RAMDRV_MAX_SECTORS_PER_CLUSTER;
    ULONG FatSectors;
    ULONG Clusters;
    ULONG Fat;
    NTSTATUS Status;

    for (;;)
    {
        FatSectors = (TotalSectors - RAMDRV_RESERVED_SECTORS + 128 * SectorsPerCluster) /
                     (128 * SectorsPerCluster + 1);
        Clusters = (TotalSectors - RAMDRV_RESERVED_SECTORS - 2 * FatSectors) / SectorsPerCluster;
        if (Clusters >= RAMDRV_FAT32_MIN_CLUSTERS || SectorsPerCluster == 1)
            break;
        SectorsPerCluster /= 2;
    }
    if (Clusters < RAMDRV_FAT32_MIN_CLUSTERS)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Sector, sizeof(Sector));
    Sector[0] = 0xEB;
    Sector[1] = 0x58;
    Sector[2] = 0x90;
    RtlCopyMemory(&Sector[3], "LIBERNT ", 8);
    RamDrvPut16(&Sector[11], RAMDRV_SECTOR_SIZE);
    Sector[13] = (UCHAR)SectorsPerCluster;
    RamDrvPut16(&Sector[14], RAMDRV_RESERVED_SECTORS);
    Sector[16] = 2;
    Sector[21] = 0xF8;
    RamDrvPut16(&Sector[24], RAMDRV_SECTORS_PER_TRACK);
    RamDrvPut16(&Sector[26], RAMDRV_TRACKS_PER_CYLINDER);
    RamDrvPut32(&Sector[32], TotalSectors);
    RamDrvPut32(&Sector[36], FatSectors);
    RamDrvPut32(&Sector[44], RAMDRV_ROOT_CLUSTER);
    RamDrvPut16(&Sector[48], RAMDRV_FSINFO_SECTOR);
    RamDrvPut16(&Sector[50], RAMDRV_BACKUP_BOOT_SECTOR);
    Sector[64] = 0x80;
    Sector[66] = 0x29;
    RamDrvPut32(&Sector[67], (ULONG)KeQueryInterruptTime());
    RtlCopyMemory(&Sector[71], "RAMDRIVE   ", 11);
    RtlCopyMemory(&Sector[82], "FAT32   ", 8);
    Sector[510] = 0x55;
    Sector[511] = 0xAA;
    Status = RamDrvWriteSector(Extension, 0, Sector);
    if (NT_SUCCESS(Status))
        Status = RamDrvWriteSector(Extension, RAMDRV_BACKUP_BOOT_SECTOR, Sector);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Sector, sizeof(Sector));
    RamDrvPut32(&Sector[0], 0x41615252);
    RamDrvPut32(&Sector[484], 0x61417272);
    RamDrvPut32(&Sector[488], Clusters - 1);
    RamDrvPut32(&Sector[492], RAMDRV_ROOT_CLUSTER + 1);
    RamDrvPut32(&Sector[508], 0xAA550000);
    Status = RamDrvWriteSector(Extension, RAMDRV_FSINFO_SECTOR, Sector);
    if (NT_SUCCESS(Status))
        Status = RamDrvWriteSector(Extension, RAMDRV_BACKUP_BOOT_SECTOR + RAMDRV_FSINFO_SECTOR, Sector);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Sector, sizeof(Sector));
    RamDrvPut32(&Sector[0], 0x0FFFFFF8);
    RamDrvPut32(&Sector[4], 0x0FFFFFFF);
    RamDrvPut32(&Sector[8], 0x0FFFFFFF);
    for (Fat = 0; Fat < 2; ++Fat)
    {
        Status = RamDrvWriteSector(Extension, RAMDRV_RESERVED_SECTORS + Fat * FatSectors, Sector);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
RamDrvComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
RamDrvCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return RamDrvComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
RamDrvReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRAMDRV_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN Write = Stack->MajorFunction == IRP_MJ_WRITE;
    ULONGLONG Offset = (ULONGLONG)Stack->Parameters.Read.ByteOffset.QuadPart;
    ULONG Length = Stack->Parameters.Read.Length;
    PUCHAR Buffer;
    NTSTATUS Status;

    if ((Offset | Length) & (RAMDRV_SECTOR_SIZE - 1) ||
        Offset > Extension->Size || Length > Extension->Size - Offset)
    {
        return RamDrvComplete(Irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (Length == 0)
        return RamDrvComplete(Irp, STATUS_SUCCESS, 0);
    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer == NULL)
        return RamDrvComplete(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    Status = RamDrvTransfer(Extension, Offset, Buffer, Length, Write);
    return RamDrvComplete(Irp, Status, NT_SUCCESS(Status) ? Length : 0);
}

static
VOID
RamDrvGeometry(
    _In_ PRAMDRV_EXTENSION Extension,
    _Out_ PDISK_GEOMETRY Geometry)
{
    Geometry->Cylinders.QuadPart = (LONGLONG)(Extension->Size / RAMDRV_SECTOR_SIZE /
                                   (RAMDRV_SECTORS_PER_TRACK * RAMDRV_TRACKS_PER_CYLINDER));
    Geometry->MediaType = FixedMedia;
    Geometry->TracksPerCylinder = RAMDRV_TRACKS_PER_CYLINDER;
    Geometry->SectorsPerTrack = RAMDRV_SECTORS_PER_TRACK;
    Geometry->BytesPerSector = RAMDRV_SECTOR_SIZE;
}

static
NTSTATUS
NTAPI
RamDrvDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRAMDRV_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Output = Irp->AssociatedIrp.SystemBuffer;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
            if (OutputLength < sizeof(DISK_GEOMETRY))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RamDrvGeometry(Extension, Output);
            return RamDrvComplete(Irp, STATUS_SUCCESS, sizeof(DISK_GEOMETRY));

        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
        {
            PDISK_GEOMETRY_EX GeometryEx = Output;

            if (OutputLength < FIELD_OFFSET(DISK_GEOMETRY_EX, Data))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RamDrvGeometry(Extension, &GeometryEx->Geometry);
            GeometryEx->DiskSize.QuadPart = (LONGLONG)Extension->Size;
            return RamDrvComplete(Irp, STATUS_SUCCESS, FIELD_OFFSET(DISK_GEOMETRY_EX, Data));
        }

        case IOCTL_DISK_GET_LENGTH_INFO:
            if (OutputLength < sizeof(GET_LENGTH_INFORMATION))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            ((PGET_LENGTH_INFORMATION)Output)->Length.QuadPart = (LONGLONG)Extension->Size;
            return RamDrvComplete(Irp, STATUS_SUCCESS, sizeof(GET_LENGTH_INFORMATION));

        case IOCTL_DISK_GET_PARTITION_INFO:
        {
            PPARTITION_INFORMATION Partition = Output;

            if (OutputLength < sizeof(PARTITION_INFORMATION))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlZeroMemory(Partition, sizeof(*Partition));
            Partition->PartitionLength.QuadPart = (LONGLONG)Extension->Size;
            Partition->PartitionNumber = 1;
            Partition->PartitionType = PARTITION_FAT32;
            Partition->RecognizedPartition = TRUE;
            return RamDrvComplete(Irp, STATUS_SUCCESS, sizeof(PARTITION_INFORMATION));
        }

        case IOCTL_DISK_GET_PARTITION_INFO_EX:
        {
            PPARTITION_INFORMATION_EX Partition = Output;

            if (OutputLength < sizeof(PARTITION_INFORMATION_EX))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlZeroMemory(Partition, sizeof(*Partition));
            Partition->PartitionStyle = PARTITION_STYLE_MBR;
            Partition->PartitionLength.QuadPart = (LONGLONG)Extension->Size;
            Partition->PartitionNumber = 1;
            Partition->Mbr.PartitionType = PARTITION_FAT32;
            Partition->Mbr.RecognizedPartition = TRUE;
            return RamDrvComplete(Irp, STATUS_SUCCESS, sizeof(PARTITION_INFORMATION_EX));
        }

        case IOCTL_STORAGE_GET_HOTPLUG_INFO:
        {
            PSTORAGE_HOTPLUG_INFO Hotplug = Output;

            if (OutputLength < sizeof(STORAGE_HOTPLUG_INFO))
                return RamDrvComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlZeroMemory(Hotplug, sizeof(*Hotplug));
            Hotplug->Size = sizeof(*Hotplug);
            return RamDrvComplete(Irp, STATUS_SUCCESS, sizeof(STORAGE_HOTPLUG_INFO));
        }

        case IOCTL_DISK_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY2:
        case IOCTL_DISK_IS_WRITABLE:
        case IOCTL_DISK_MEDIA_REMOVAL:
        case IOCTL_STORAGE_MEDIA_REMOVAL:
        case IOCTL_DISK_VERIFY:
            return RamDrvComplete(Irp, STATUS_SUCCESS, 0);

        default:
            return RamDrvComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static
ULONG
RamDrvQuerySizeMb(
    _In_ PUNICODE_STRING RegistryPath)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];
    ULONG SizeMb = RAMDRV_DEFAULT_SIZE_MB;
    ULONG Default = RAMDRV_DEFAULT_SIZE_MB;
    UNICODE_STRING Parameters;
    WCHAR Buffer[260];

    RtlInitEmptyUnicodeString(&Parameters, Buffer, sizeof(Buffer));
    if (!NT_SUCCESS(RtlAppendUnicodeStringToString(&Parameters, RegistryPath)) ||
        !NT_SUCCESS(RtlAppendUnicodeToString(&Parameters, L"\\Parameters")))
    {
        return SizeMb;
    }
    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = L"SizeMB";
    Table[0].EntryContext = &SizeMb;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD;
    Table[0].DefaultData = &Default;
    Table[0].DefaultLength = sizeof(Default);
    (VOID)RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, Parameters.Buffer, Table, NULL, NULL);
    return min(max(SizeMb, RAMDRV_MIN_SIZE_MB), RAMDRV_MAX_SIZE_MB);
}

static
VOID
NTAPI
RamDrvUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
    PRAMDRV_EXTENSION Extension;
    ULONG Index;

    IoDeleteSymbolicLink(&RamDrvLinkName);
    if (DeviceObject == NULL)
        return;
    Extension = DeviceObject->DeviceExtension;
    if (Extension->Chunks != NULL)
    {
        for (Index = 0; Index < Extension->ChunkCount; ++Index)
        {
            if (Extension->Chunks[Index] != NULL)
                ExFreePoolWithTag(Extension->Chunks[Index], RAMDRV_TAG);
        }
        ExFreePoolWithTag(Extension->Chunks, RAMDRV_TAG);
    }
    IoDeleteDevice(DeviceObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\RamDrive0");
    PDEVICE_OBJECT DeviceObject;
    PRAMDRV_EXTENSION Extension;
    NTSTATUS Status;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = RamDrvCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = RamDrvCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = RamDrvReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = RamDrvReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RamDrvDeviceControl;
    DriverObject->DriverUnload = RamDrvUnload;

    Status = IoCreateDevice(DriverObject,
                            sizeof(RAMDRV_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_DISK,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Size = (ULONGLONG)RamDrvQuerySizeMb(RegistryPath) * 1024 * 1024;
    Extension->ChunkCount = (ULONG)(Extension->Size / RAMDRV_CHUNK_SIZE);
    Extension->Chunks = ExAllocatePoolZero(NonPagedPool,
                                           Extension->ChunkCount * sizeof(PUCHAR),
                                           RAMDRV_TAG);
    if (Extension->Chunks == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DeviceObject->Flags |= DO_DIRECT_IO;
    DeviceObject->AlignmentRequirement = FILE_WORD_ALIGNMENT;

    Status = RamDrvFormat(Extension);
    if (NT_SUCCESS(Status))
        Status = IoCreateSymbolicLink(&RamDrvLinkName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        RamDrvUnload(DriverObject);
        return Status;
    }

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DPRINT1("RAMDRV: %I64u MB FAT32 drive at R:\n", Extension->Size / (1024 * 1024));
    return STATUS_SUCCESS;
}
