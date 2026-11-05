/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Crash-time storage interface: polled writes at HIGH_LEVEL reach the medium
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntdddisk.h>
#include <reactos/drivers/dumpstor.h>

/* The test writes raw sectors: it only touches a disk whose last sector carries this text. */
#define TEST_MARKER         "LIBERNT DUMP TEST DISK"
#define TEST_SECTOR         512
#define TEST_MAX_DISKS      16
#define TEST_AREA_BACK      (1024 * 1024)
#define TEST_BUFFER_SIZE    (3 * PAGE_SIZE)
#define TEST_POOL_TAG       'pDtT'

typedef struct _TEST_WRITE
{
    ULONG Offset;
    ULONG Source;
    ULONG Length;
} TEST_WRITE;

static const TEST_WRITE TestWrites[] =
{
    { 0, 0, PAGE_SIZE },
    { 0x4000, PAGE_SIZE / 2, PAGE_SIZE },
    { 0x8000, 0x200, TEST_SECTOR },
};

static
NTSTATUS
TestQueryInterface(
    _In_ PDEVICE_OBJECT Device,
    _Inout_ PROS_STORAGE_DUMP_INTERFACE Interface)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE,
                                        Device,
                                        Interface,
                                        sizeof(*Interface),
                                        Interface,
                                        sizeof(*Interface),
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
TestWritePath(
    _In_ PROS_STORAGE_DUMP_INTERFACE Interface,
    _In_ HANDLE Disk,
    _In_ ULONG64 DiskLength)
{
    NTSTATUS Results[RTL_NUMBER_OF(TestWrites)];
    NTSTATUS Prepare, Flush, Unaligned, Short, PastEnd;
    PHYSICAL_ADDRESS Highest, Physical, Target;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    PUCHAR Source, ReadBack;
    ULONG64 Area = DiskLength - TEST_AREA_BACK;
    KIRQL OldIrql;
    ULONG Index;
    NTSTATUS Status;

    Highest.QuadPart = MAXULONGLONG;
    Source = MmAllocateContiguousMemory(TEST_BUFFER_SIZE, Highest);
    ReadBack = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TEST_POOL_TAG);
    ok(Source != NULL && ReadBack != NULL, "Could not allocate the buffers\n");
    if (Source == NULL || ReadBack == NULL)
    {
        if (Source != NULL)
            MmFreeContiguousMemory(Source);
        if (ReadBack != NULL)
            ExFreePoolWithTag(ReadBack, TEST_POOL_TAG);
        return;
    }

    for (Index = 0; Index < TEST_BUFFER_SIZE; Index++)
        Source[Index] = (UCHAR)((Index * 7) ^ (Index >> 8));
    Physical = MmGetPhysicalAddress(Source);

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    Prepare = Interface->Prepare(Interface->Context);
    for (Index = 0; Index < RTL_NUMBER_OF(TestWrites); Index++)
    {
        Target.QuadPart = Physical.QuadPart + TestWrites[Index].Source;
        Results[Index] = NT_SUCCESS(Prepare)
                             ? Interface->WriteRoutine(Interface->Context,
                                                       Area + TestWrites[Index].Offset,
                                                       Target,
                                                       TestWrites[Index].Length)
                             : Prepare;
    }
    Unaligned = Interface->WriteRoutine(Interface->Context, Area + 1, Physical, TEST_SECTOR);
    Short = Interface->WriteRoutine(Interface->Context, Area, Physical, TEST_SECTOR - 1);
    PastEnd = Interface->WriteRoutine(Interface->Context, DiskLength, Physical, TEST_SECTOR);
    Flush = Interface->Flush(Interface->Context);
    KeLowerIrql(OldIrql);

    ok_eq_hex(Prepare, STATUS_SUCCESS);
    ok_eq_hex(Unaligned, STATUS_INVALID_PARAMETER);
    ok_eq_hex(Short, STATUS_INVALID_PARAMETER);
    ok_eq_hex(PastEnd, STATUS_END_OF_MEDIA);
    ok_eq_hex(Flush, STATUS_SUCCESS);

    /* The normal driver takes the controller back here: it has to read what the polled path wrote. */
    for (Index = 0; Index < RTL_NUMBER_OF(TestWrites); Index++)
    {
        ok_eq_hex(Results[Index], STATUS_SUCCESS);
        Offset.QuadPart = Area + TestWrites[Index].Offset;
        Status = ZwReadFile(Disk, NULL, NULL, NULL, &IoStatus, ReadBack, TestWrites[Index].Length, &Offset, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(RtlCompareMemory(ReadBack, Source + TestWrites[Index].Source, TestWrites[Index].Length) ==
                   TestWrites[Index].Length,
               "Write %lu did not reach the medium\n",
               Index);
        }
    }

    MmFreeContiguousMemory(Source);
    ExFreePoolWithTag(ReadBack, TEST_POOL_TAG);
}

static
BOOLEAN
TestDisk(
    _In_ ULONG Number)
{
    ROS_STORAGE_DUMP_INTERFACE Interface;
    GET_LENGTH_INFORMATION LengthInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    LARGE_INTEGER Offset;
    PDEVICE_OBJECT Device;
    PFILE_OBJECT File;
    HANDLE Handle;
    WCHAR NameBuffer[64];
    PUCHAR Sector;
    BOOLEAN Marked = FALSE;
    NTSTATUS Status;

    Status = RtlStringCbPrintfW(NameBuffer, sizeof(NameBuffer), L"\\Device\\Harddisk%lu\\Partition0", Number);
    if (!NT_SUCCESS(Status))
        return FALSE;
    RtlInitUnicodeString(&Name, NameBuffer);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle,
                        GENERIC_READ | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Sector = ExAllocatePoolWithTag(NonPagedPool, TEST_SECTOR, TEST_POOL_TAG);
    Status = ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatus, IOCTL_DISK_GET_LENGTH_INFO,
                                   NULL, 0, &LengthInfo, sizeof(LengthInfo));
    if (NT_SUCCESS(Status) && Sector != NULL && LengthInfo.Length.QuadPart > TEST_AREA_BACK)
    {
        Offset.QuadPart = LengthInfo.Length.QuadPart - TEST_SECTOR;
        Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Sector, TEST_SECTOR, &Offset, NULL);
        Marked = NT_SUCCESS(Status) &&
                 RtlCompareMemory(Sector, TEST_MARKER, sizeof(TEST_MARKER) - 1) == sizeof(TEST_MARKER) - 1;
    }

    if (Marked)
    {
        Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &File, &Device);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(&Interface, sizeof(Interface));
            Interface.BytesPerSector = TEST_SECTOR + 8;
            Status = TestQueryInterface(Device, &Interface);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

            RtlZeroMemory(&Interface, sizeof(Interface));
            Interface.BytesPerSector = TEST_SECTOR;
            Status = TestQueryInterface(Device, &Interface);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok_eq_ulong(Interface.Version, ROS_STORAGE_DUMP_INTERFACE_VERSION);
                ok_eq_ulong(Interface.BytesPerSector, TEST_SECTOR);
                ok(Interface.MaximumTransferLength >= PAGE_SIZE, "Transfer limit %lu\n", Interface.MaximumTransferLength);
                TestWritePath(&Interface, Handle, LengthInfo.Length.QuadPart);
            }
            ObDereferenceObject(File);
        }
    }

    if (Sector != NULL)
        ExFreePoolWithTag(Sector, TEST_POOL_TAG);
    ZwClose(Handle);
    return Marked;
}

START_TEST(IoStorageDumpPath)
{
    ULONG Number;

    for (Number = 0; Number < TEST_MAX_DISKS; Number++)
    {
        if (TestDisk(Number))
            return;
    }

    skip(FALSE, "No disk carries the test marker\n");
}
