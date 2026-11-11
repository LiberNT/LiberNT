/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite partition table reads on a disk waiting for verification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntdddisk.h>

#define NDEBUG
#include <debug.h>

NTKERNELAPI
NTSTATUS
NTAPI
IoCreateDriver(
    _In_opt_ PUNICODE_STRING DriverName,
    _In_ PDRIVER_INITIALIZE InitializationFunction);

NTKERNELAPI
VOID
NTAPI
IoDeleteDriver(
    _In_ PDRIVER_OBJECT DriverObject);

#define TEST_SECTOR_SIZE        512
#define TEST_DISK_SECTORS       2048
#define TEST_DISK_SIGNATURE     0x4B54504CUL
#define TEST_PARTITION_START    63
#define TEST_PARTITION_SECTORS  1000

static LONG ReadsWithoutOverride;
static LONG Reads;

static
NTSTATUS
TestComplete(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
TestDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDISK_GEOMETRY_EX Geometry;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DISK_GEOMETRY_EX))
                return TestComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            Geometry = Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(Geometry, sizeof(*Geometry));
            Geometry->Geometry.Cylinders.QuadPart = 1;
            Geometry->Geometry.MediaType = RemovableMedia;
            Geometry->Geometry.TracksPerCylinder = 32;
            Geometry->Geometry.SectorsPerTrack = 64;
            Geometry->Geometry.BytesPerSector = TEST_SECTOR_SIZE;
            Geometry->DiskSize.QuadPart = (LONGLONG)TEST_DISK_SECTORS * TEST_SECTOR_SIZE;
            return TestComplete(Irp, STATUS_SUCCESS, sizeof(DISK_GEOMETRY_EX));

        case IOCTL_DISK_IS_WRITABLE:
            return TestComplete(Irp, STATUS_SUCCESS, 0);

        default:
            return TestComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static
NTSTATUS
NTAPI
TestRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Length = Stack->Parameters.Read.Length;
    PUCHAR Buffer;

    InterlockedIncrement(&Reads);

    /* What a class driver does with a removable disk whose media may have changed */
    if ((DeviceObject->Flags & DO_VERIFY_VOLUME) && !(Stack->Flags & SL_OVERRIDE_VERIFY_VOLUME))
    {
        InterlockedIncrement(&ReadsWithoutOverride);
        return TestComplete(Irp, STATUS_VERIFY_REQUIRED, 0);
    }

    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer == NULL) return TestComplete(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    RtlZeroMemory(Buffer, Length);
    if (Stack->Parameters.Read.ByteOffset.QuadPart == 0 && Length >= TEST_SECTOR_SIZE)
    {
        *(PULONG)&Buffer[0x1B8] = TEST_DISK_SIGNATURE;
        Buffer[0x1BE + 4] = PARTITION_IFS;
        *(PULONG)&Buffer[0x1BE + 8] = TEST_PARTITION_START;
        *(PULONG)&Buffer[0x1BE + 12] = TEST_PARTITION_SECTORS;
        Buffer[510] = 0x55;
        Buffer[511] = 0xAA;
    }

    return TestComplete(Irp, STATUS_SUCCESS, Length);
}

static
NTSTATUS
NTAPI
TestDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = TestDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = TestRead;
    return STATUS_SUCCESS;
}

START_TEST(IoReadPartitionTable)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\KmtestPartitionTable");
    PDRIVE_LAYOUT_INFORMATION_EX Layout = NULL;
    PDRIVER_OBJECT DriverObject = NULL;
    PDEVICE_OBJECT Disk = NULL;
    NTSTATUS Status;

    Status = IoCreateDriver(&DriverName, TestDriverEntry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByName(&DriverName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType,
                                     KernelMode, NULL, (PVOID *)&DriverObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_DISK, FILE_REMOVABLE_MEDIA, FALSE, &Disk);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Disk->Flags |= DO_DIRECT_IO | DO_VERIFY_VOLUME;
        Disk->Flags &= ~DO_DEVICE_INITIALIZING;

        Status = IoReadPartitionTableEx(Disk, &Layout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Reads != 0, "the partition table was not read\n");
        ok_eq_long(ReadsWithoutOverride, 0L);
        ok((Disk->Flags & DO_VERIFY_VOLUME) != 0, "reading the partition table cleared the verify request\n");

        if (Layout != NULL)
        {
            ok_eq_ulong((ULONG)Layout->PartitionStyle, (ULONG)PARTITION_STYLE_MBR);
            ok_eq_hex(Layout->Mbr.Signature, TEST_DISK_SIGNATURE);
            ok(Layout->PartitionCount >= 1, "no partition entries\n");
            if (Layout->PartitionCount >= 1)
            {
                ok_eq_longlong(Layout->PartitionEntry[0].StartingOffset.QuadPart,
                               (LONGLONG)TEST_PARTITION_START * TEST_SECTOR_SIZE);
                ok_eq_longlong(Layout->PartitionEntry[0].PartitionLength.QuadPart,
                               (LONGLONG)TEST_PARTITION_SECTORS * TEST_SECTOR_SIZE);
                ok_eq_uint(Layout->PartitionEntry[0].Mbr.PartitionType, PARTITION_IFS);
            }
            ExFreePool(Layout);
        }

        IoDeleteDevice(Disk);
    }

    ObDereferenceObject(DriverObject);
    IoDeleteDriver(DriverObject);
}
