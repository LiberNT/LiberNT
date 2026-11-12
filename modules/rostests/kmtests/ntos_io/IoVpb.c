/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the VPB of a disk device across attachment and deletion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntdddisk.h>

#define NDEBUG
#include <debug.h>

#define TEST_DISK_BYTES (1024 * 1024)
#define TEST_SECTOR_BYTES 512
#define TEST_VPB_TAG ' BPV'

static LONG TestCloseVpbReferences;
static PVPB TestWatchedVpb;
static PDEVICE_OBJECT TestCloseDevice;

static
NTSTATUS
TestDiskComplete(
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
TestDiskIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStackLocation)
{
    ULONG OutputLength = IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    PVOID Data;

    if (IoStackLocation->MajorFunction == IRP_MJ_CLOSE && TestWatchedVpb != NULL)
    {
        TestCloseDevice = DeviceObject;
        InterlockedExchange(&TestCloseVpbReferences, (LONG)TestWatchedVpb->ReferenceCount);
    }
    if (IoStackLocation->MajorFunction == IRP_MJ_READ)
    {
        Data = Irp->MdlAddress ? MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : Irp->UserBuffer;
        if (Data == NULL)
            return TestDiskComplete(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        RtlZeroMemory(Data, IoStackLocation->Parameters.Read.Length);
        return TestDiskComplete(Irp, STATUS_SUCCESS, IoStackLocation->Parameters.Read.Length);
    }
    if (IoStackLocation->MajorFunction != IRP_MJ_DEVICE_CONTROL &&
        IoStackLocation->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL)
    {
        return TestDiskComplete(Irp, STATUS_SUCCESS, 0);
    }

    switch (IoStackLocation->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
        {
            PDISK_GEOMETRY Geometry = Buffer;

            if (OutputLength < sizeof(*Geometry))
                return TestDiskComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlZeroMemory(Geometry, sizeof(*Geometry));
            Geometry->Cylinders.QuadPart = TEST_DISK_BYTES / (TEST_SECTOR_BYTES * 32);
            Geometry->MediaType = FixedMedia;
            Geometry->TracksPerCylinder = 1;
            Geometry->SectorsPerTrack = 32;
            Geometry->BytesPerSector = TEST_SECTOR_BYTES;
            return TestDiskComplete(Irp, STATUS_SUCCESS, sizeof(*Geometry));
        }
        case IOCTL_DISK_GET_PARTITION_INFO:
        {
            PPARTITION_INFORMATION Partition = Buffer;

            if (OutputLength < sizeof(*Partition))
                return TestDiskComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlZeroMemory(Partition, sizeof(*Partition));
            Partition->PartitionLength.QuadPart = TEST_DISK_BYTES;
            Partition->PartitionNumber = 1;
            Partition->RecognizedPartition = TRUE;
            return TestDiskComplete(Irp, STATUS_SUCCESS, sizeof(*Partition));
        }
        case IOCTL_DISK_GET_LENGTH_INFO:
        {
            PGET_LENGTH_INFORMATION Length = Buffer;

            if (OutputLength < sizeof(*Length))
                return TestDiskComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            Length->Length.QuadPart = TEST_DISK_BYTES;
            return TestDiskComplete(Irp, STATUS_SUCCESS, sizeof(*Length));
        }
        case IOCTL_DISK_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY2:
            return TestDiskComplete(Irp, STATUS_SUCCESS, 0);
        default:
            return TestDiskComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static PDEVICE_OBJECT TestDisk;
static PDEVICE_OBJECT TestVolume;
static PDRIVER_DISPATCH TestSavedDispatch[IRP_MJ_MAXIMUM_FUNCTION + 1];

static
NTSTATUS
NTAPI
TestDiskDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceObject == TestDisk || (TestVolume != NULL && DeviceObject == TestVolume))
        return TestDiskIrp(DeviceObject, Irp, Stack);
    ASSERT(TestSavedDispatch[Stack->MajorFunction] != NULL);
    return TestSavedDispatch[Stack->MajorFunction](DeviceObject, Irp);
}

static
NTSTATUS
TestOpenVolume(
    _In_ PCWSTR Path,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    *Handle = NULL;
    return ZwCreateFile(Handle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus, NULL,
                        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, Disposition,
                        Options | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
}

/* A volume no file system recognizes gets RAW only when the volume itself is opened, never for a file on it */
static
VOID
TestRawMount(VOID)
{
    static const UCHAR Majors[] = { IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE, IRP_MJ_READ,
                                    IRP_MJ_DEVICE_CONTROL, IRP_MJ_INTERNAL_DEVICE_CONTROL, IRP_MJ_FLUSH_BUFFERS };
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KmtestRawVolume");
    IO_STATUS_BLOCK IoStatus;
    PDEVICE_OBJECT Disk;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Index;

    Status = IoCreateDevice(KmtDriverObject, 0, &Name, FILE_DEVICE_DISK, 0, FALSE, &Disk);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Disk->Flags |= DO_DIRECT_IO;
    Disk->AlignmentRequirement = FILE_WORD_ALIGNMENT;
    TestDisk = Disk;
    for (Index = 0; Index < RTL_NUMBER_OF(Majors); Index++)
    {
        TestSavedDispatch[Majors[Index]] = KmtDriverObject->MajorFunction[Majors[Index]];
        InterlockedExchangePointer((PVOID *)&KmtDriverObject->MajorFunction[Majors[Index]], (PVOID)TestDiskDispatch);
    }
    Disk->Flags &= ~DO_DEVICE_INITIALIZING;

    Status = TestOpenVolume(L"\\Device\\KmtestRawVolume\\file.txt", FILE_OPEN_IF, FILE_NON_DIRECTORY_FILE, &Handle);
    ok_eq_hex(Status, STATUS_UNRECOGNIZED_VOLUME);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);
    ok_eq_hex(Disk->Vpb->Flags & (VPB_MOUNTED | VPB_RAW_MOUNT), 0);

    Status = TestOpenVolume(L"\\Device\\KmtestRawVolume", FILE_OPEN, 0, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Disk->Vpb->Flags & (VPB_MOUNTED | VPB_RAW_MOUNT), VPB_MOUNTED);
    if (NT_SUCCESS(Status))
    {
        Status = ZwFsControlFile(Handle, NULL, NULL, NULL, &IoStatus, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Handle);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Majors); Index++)
        InterlockedExchangePointer((PVOID *)&KmtDriverObject->MajorFunction[Majors[Index]], (PVOID)TestSavedDispatch[Majors[Index]]);
    IoDeleteDevice(Disk);
    TestDisk = NULL;
}

/* A fastfat-style failed mount: the stream file drops its VPB reference before the close, the swapped VPB stays */
static
VOID
TestStreamFileClose(VOID)
{
    static const UCHAR Majors[] = { IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE, IRP_MJ_READ,
                                    IRP_MJ_DEVICE_CONTROL, IRP_MJ_INTERNAL_DEVICE_CONTROL, IRP_MJ_FLUSH_BUFFERS };
    PDEVICE_OBJECT Disk, Volume;
    PFILE_OBJECT Stream;
    PVPB Mounted, Spare;
    KIRQL OldIrql;
    NTSTATUS Status;
    ULONG Index;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_DISK, 0, FALSE, &Disk);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &Volume);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Disk);
        return;
    }
    Volume->StackSize = Disk->StackSize + 1;
    TestDisk = Disk;
    TestVolume = Volume;
    for (Index = 0; Index < RTL_NUMBER_OF(Majors); Index++)
    {
        TestSavedDispatch[Majors[Index]] = KmtDriverObject->MajorFunction[Majors[Index]];
        InterlockedExchangePointer((PVOID *)&KmtDriverObject->MajorFunction[Majors[Index]], (PVOID)TestDiskDispatch);
    }
    Disk->Flags &= ~DO_DEVICE_INITIALIZING;
    Volume->Flags &= ~DO_DEVICE_INITIALIZING;

    Mounted = Disk->Vpb;
    IoAcquireVpbSpinLock(&OldIrql);
    Mounted->DeviceObject = Volume;
    IoReleaseVpbSpinLock(OldIrql);

    Stream = IoCreateStreamFileObject(NULL, Disk);
    ok(Stream != NULL, "No stream file object\n");
    Spare = ExAllocatePoolWithTag(NonPagedPool, sizeof(VPB), TEST_VPB_TAG);
    ok(Spare != NULL, "No spare VPB\n");
    if (Stream != NULL && Spare != NULL)
    {
        ok_eq_pointer(Stream->DeviceObject, Disk);
        ok_eq_ulong(Mounted->ReferenceCount, 1UL);
        Stream->Vpb = Mounted;
        RtlZeroMemory(Spare, sizeof(VPB));
        Spare->Type = IO_TYPE_VPB;
        Spare->Size = sizeof(VPB);
        Spare->RealDevice = Disk;
        IoAcquireVpbSpinLock(&OldIrql);
        Disk->Vpb = Spare;
        IoReleaseVpbSpinLock(OldIrql);

        TestCloseDevice = NULL;
        TestWatchedVpb = Mounted;
        TestCloseVpbReferences = -1;
        ObDereferenceObject(Stream);
        for (Index = 0; Index < 200 && InterlockedCompareExchange(&TestCloseVpbReferences, -1, -1) == -1; Index++)
        {
            LARGE_INTEGER Interval;

            Interval.QuadPart = -10LL * 1000 * 10;
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }
        TestWatchedVpb = NULL;
        ok_eq_pointer(TestCloseDevice, Volume);
        ok_eq_long(TestCloseVpbReferences, 0L);
        ok_eq_ulong(Spare->ReferenceCount, 0UL);
        ok_eq_hex(Mounted->Type, IO_TYPE_VPB);
        ok_eq_pointer(Mounted->RealDevice, Disk);
        if (Mounted->Type == IO_TYPE_VPB && Mounted->RealDevice == Disk && Mounted->ReferenceCount == 0)
        {
            Mounted->DeviceObject = NULL;
            ExFreePool(Mounted);
        }
    }
    else
    {
        if (Stream != NULL)
            ObDereferenceObject(Stream);
        if (Spare != NULL)
            ExFreePoolWithTag(Spare, TEST_VPB_TAG);
        IoAcquireVpbSpinLock(&OldIrql);
        Mounted->DeviceObject = NULL;
        IoReleaseVpbSpinLock(OldIrql);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Majors); Index++)
        InterlockedExchangePointer((PVOID *)&KmtDriverObject->MajorFunction[Majors[Index]], (PVOID)TestSavedDispatch[Majors[Index]]);
    IoDeleteDevice(Volume);
    IoDeleteDevice(Disk);
    TestVolume = NULL;
    TestDisk = NULL;
}

/* A file system registration readies the control device, so that filters may attach to it from the notification */
static
VOID
TestFileSystemRegistration(VOID)
{
    PDEVICE_OBJECT FileSystem;
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &FileSystem);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_hex(FileSystem->Flags & DO_DEVICE_INITIALIZING, DO_DEVICE_INITIALIZING);

    IoRegisterFileSystem(FileSystem);
    ok_eq_hex(FileSystem->Flags & DO_DEVICE_INITIALIZING, 0);

    IoUnregisterFileSystem(FileSystem);
    IoDeleteDevice(FileSystem);
}

START_TEST(IoVpb)
{
    PDEVICE_OBJECT Lower, Disk, AttachedTo;
    NTSTATUS Status;
    PVPB Vpb;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Lower);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_pointer(Lower->Vpb, NULL);

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_DISK, 0, FALSE, &Disk);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Lower);
        return;
    }

    Vpb = Disk->Vpb;
    ok(Vpb != NULL, "A disk device has no VPB\n");
    if (Vpb != NULL)
    {
        ok_eq_pointer(Vpb->RealDevice, Disk);
        ok_eq_ulong(Vpb->ReferenceCount, 0UL);
        ok_eq_hex(Vpb->Flags & VPB_MOUNTED, 0);
    }

    AttachedTo = IoAttachDeviceToDeviceStack(Disk, Lower);
    ok_eq_pointer(AttachedTo, NULL);
    if (AttachedTo != NULL)
        IoDetachDevice(Lower);
    Lower->Flags &= ~DO_DEVICE_INITIALIZING;

    AttachedTo = IoAttachDeviceToDeviceStack(Disk, Lower);
    ok_eq_pointer(AttachedTo, Lower);
    ok_eq_pointer(Disk->Vpb, Vpb);
    if (Disk->Vpb != NULL)
        ok_eq_pointer(Disk->Vpb->RealDevice, Disk);
    ok_eq_pointer(Lower->Vpb, NULL);
    ok_eq_int(Disk->StackSize, Lower->StackSize + 1);

    if (AttachedTo != NULL)
        IoDetachDevice(Lower);

    IoDeleteDevice(Disk);
    IoDeleteDevice(Lower);

    TestRawMount();
    TestFileSystemRegistration();
    TestStreamFileClose();
}
