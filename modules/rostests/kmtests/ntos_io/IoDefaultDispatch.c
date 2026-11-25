/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The default dispatch of an unhandled IRP and the second open of an exclusive device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSTATUS NTAPI IoCreateDriver(_In_opt_ PUNICODE_STRING DriverName, _In_ PDRIVER_INITIALIZE InitializationFunction);

#define DEFDISP_SENTINEL ((NTSTATUS)0x0BADF00D)

static PDEVICE_OBJECT PlainDevice;
static PDEVICE_OBJECT ExclusiveDevice;
static PDEVICE_OBJECT SharedDevice;

static
NTSTATUS
NTAPI
DefDispCompleteSuccess(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
DefDispCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCWSTR Name,
    _In_ BOOLEAN Exclusive,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    UNICODE_STRING DeviceName;
    NTSTATUS Status;

    RtlInitUnicodeString(&DeviceName, Name);
    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, Exclusive, DeviceObject);
    if (NT_SUCCESS(Status))
        (*DeviceObject)->Flags &= ~DO_DEVICE_INITIALIZING;
    return Status;
}

/* Every other major function keeps the default dispatch the I/O manager installs */
static
NTSTATUS
NTAPI
DefDispInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DefDispCompleteSuccess;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DefDispCompleteSuccess;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DefDispCompleteSuccess;
    Status = DefDispCreateDevice(DriverObject, L"\\Device\\KmtestDefDispPlain", FALSE, &PlainDevice);
    if (NT_SUCCESS(Status))
        Status = DefDispCreateDevice(DriverObject, L"\\Device\\KmtestDefDispExclusive", TRUE, &ExclusiveDevice);
    if (NT_SUCCESS(Status))
        Status = DefDispCreateDevice(DriverObject, L"\\Device\\KmtestDefDispShared", FALSE, &SharedDevice);
    return Status;
}

static
VOID
DefDispSendUnhandled(
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ NTSTATUS Preset,
    _In_ ULONG_PTR PresetInformation,
    _In_ PCSTR Name)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoStatus.Status = DEFDISP_SENTINEL;
    IoStatus.Information = (ULONG_PTR)DEFDISP_SENTINEL;
    Irp = IoBuildSynchronousFsdRequest(MajorFunction, PlainDevice, NULL, 0, NULL, &Event, &IoStatus);
    ok(Irp != NULL, "%s: no IRP\n", Name);
    if (Irp == NULL)
        return;
    Irp->IoStatus.Status = Preset;
    Irp->IoStatus.Information = PresetInformation;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = MinorFunction;
    if (MajorFunction == IRP_MJ_PNP && MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS)
        Stack->Parameters.QueryDeviceRelations.Type = TargetDeviceRelation;
    Status = IoCallDriver(PlainDevice, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_REQUEST);
    ok(IoStatus.Status == DEFDISP_SENTINEL && IoStatus.Information == (ULONG_PTR)DEFDISP_SENTINEL,
       "%s preset 0x%08lx: the default dispatch wrote 0x%08lx/0x%Ix into the I/O status block\n",
       Name, (ULONG)Preset, (ULONG)IoStatus.Status, IoStatus.Information);
}

static
NTSTATUS
DefDispOpen(
    _In_ PCWSTR Name,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;

    RtlInitUnicodeString(&Path, Name);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    *Handle = NULL;
    return ZwCreateFile(Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL, 0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
}

static
VOID
DefDispOpenTwice(
    _In_ PCWSTR Name,
    _In_ NTSTATUS ExpectedWhileOpen)
{
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    HANDLE First, Second, Third;
    UNICODE_STRING Path;
    NTSTATUS Status;

    Status = DefDispOpen(Name, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DefDispOpen(Name, &Second);
    ok(Status == ExpectedWhileOpen, "%ls: second open returned 0x%08lx, expected 0x%08lx\n", Name, (ULONG)Status, (ULONG)ExpectedWhileOpen);
    RtlInitUnicodeString(&Path, Name);
    Status = IoGetDeviceObjectPointer(&Path, FILE_READ_DATA, &FileObject, &DeviceObject);
    ok(Status == ExpectedWhileOpen, "%ls: IoGetDeviceObjectPointer while open returned 0x%08lx, expected 0x%08lx\n",
       Name, (ULONG)Status, (ULONG)ExpectedWhileOpen);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(FileObject);
    if (Second != NULL)
        ZwClose(Second);
    if (First != NULL)
        ZwClose(First);
    Status = DefDispOpen(Name, &Third);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Third != NULL)
        ZwClose(Third);
}

START_TEST(IoDefaultDispatch)
{
    NTSTATUS Status;

    Status = IoCreateDriver(NULL, DefDispInitialize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || PlainDevice == NULL || ExclusiveDevice == NULL || SharedDevice == NULL)
    {
        skip(FALSE, "the test devices were not created\n");
        return;
    }

    DefDispSendUnhandled(IRP_MJ_PNP, IRP_MN_QUERY_DEVICE_RELATIONS, STATUS_NOT_SUPPORTED, 0x5A5A, "PnP TargetDeviceRelation");
    DefDispSendUnhandled(IRP_MJ_PNP, IRP_MN_QUERY_DEVICE_RELATIONS, STATUS_SUCCESS, 0x77, "PnP TargetDeviceRelation");
    DefDispSendUnhandled(IRP_MJ_PNP, IRP_MN_QUERY_CAPABILITIES, STATUS_NOT_SUPPORTED, 0, "PnP capabilities");
    DefDispSendUnhandled(IRP_MJ_FLUSH_BUFFERS, 0, STATUS_NOT_SUPPORTED, 0x5A5A, "flush");
    DefDispSendUnhandled(IRP_MJ_FLUSH_BUFFERS, 0, STATUS_SUCCESS, 0x77, "flush");

    DefDispOpenTwice(L"\\Device\\KmtestDefDispExclusive", STATUS_ACCESS_DENIED);
    DefDispOpenTwice(L"\\Device\\KmtestDefDispShared", STATUS_SUCCESS);

    IoDeleteDevice(PlainDevice);
    IoDeleteDevice(ExclusiveDevice);
    IoDeleteDevice(SharedDevice);
}
