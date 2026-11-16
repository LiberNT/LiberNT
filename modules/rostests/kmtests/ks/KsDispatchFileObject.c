/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A request another driver sends to a streaming device without a file object is refused
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ks.h>

static KSOBJECT_CREATE_ITEM FileObjectItems[1];

static
NTSTATUS
NTAPI
FileObjectKeepIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Context);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
TestRequestWithoutFileObject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MajorFunction)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PIRP Irp;

    Irp = IoAllocateIrp(2, FALSE);
    ok(Irp != NULL, "No IRP\n");
    if (skip(Irp != NULL, "No IRP\n"))
        return;

    IoSetNextIrpStackLocation(Irp);
    IoSetCompletionRoutine(Irp, FileObjectKeepIrp, NULL, TRUE, TRUE, TRUE);

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = MajorFunction;
    IoStack->DeviceObject = DeviceObject;
    IoStack->FileObject = NULL;
    IoSetNextIrpStackLocation(Irp);

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Status = KsDispatchIrp(DeviceObject, Irp);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_REQUEST);
    ok_eq_hex(Irp->IoStatus.Status, STATUS_INVALID_DEVICE_REQUEST);

    IoFreeIrp(Irp);
}

START_TEST(KsDispatchFileObject)
{
    PDEVICE_OBJECT DeviceObject;
    KSDEVICE_HEADER Header;
    NTSTATUS Status;

    RtlZeroMemory(FileObjectItems, sizeof(FileObjectItems));

    Status = IoCreateDevice(KmtDriverObject,
                            sizeof(KSDEVICE_HEADER),
                            NULL,
                            FILE_DEVICE_KS,
                            0,
                            FALSE,
                            &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No device object\n"))
        return;

    Status = KsAllocateDeviceHeader(&Header, RTL_NUMBER_OF(FileObjectItems), FileObjectItems);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No device header\n"))
    {
        IoDeleteDevice(DeviceObject);
        return;
    }

    *(KSDEVICE_HEADER *)DeviceObject->DeviceExtension = Header;

    TestRequestWithoutFileObject(DeviceObject, IRP_MJ_DEVICE_CONTROL);
    TestRequestWithoutFileObject(DeviceObject, IRP_MJ_CLOSE);

    KsFreeDeviceHeader(Header);
    IoDeleteDevice(DeviceObject);
}

/* EOF */
