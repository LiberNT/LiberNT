/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt object the i8042 port hands to the top of its device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntdd8042.h>
#include <hook_core.h>

#include "IoI8042StartInformation.h"

typedef enum _START_COMPLETION
{
    CompleteSuccess,
    CompleteFailure,
    CompletePending
} START_COMPLETION;

typedef struct _START_REQUEST
{
    ULONG Calls;
    UCHAR MajorFunction;
    ULONG IoControlCode;
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    BOOLEAN InputIsType3;
    KPROCESSOR_MODE RequestorMode;
    KIRQL Irql;
    INTERNAL_I8042_START_INFORMATION Information;
} START_REQUEST, *PSTART_REQUEST;

static KMT_IRP_HANDLER TestIrpHandler;
static KMT_MESSAGE_HANDLER TestMessageHandler;
static IO_WORKITEM_ROUTINE TestCompleteLater;

static START_REQUEST TestRequest;
static START_COMPLETION TestCompletion;
static PIRP TestPendingIrp;

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"IoI8042StartInformation";
    KmtRegisterIrpHandler(IRP_MJ_INTERNAL_DEVICE_CONTROL, NULL, TestIrpHandler);
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
}

static
VOID
NTAPI
TestCompleteLater(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PIO_WORKITEM WorkItem = Context;
    PIRP Irp = TestPendingIrp;

    UNREFERENCED_PARAMETER(DeviceObject);

    TestPendingIrp = NULL;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoFreeWorkItem(WorkItem);
}

static
NTSTATUS
TestIrpHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IoStackLocation)
{
    PINTERNAL_I8042_START_INFORMATION Information;
    PIO_WORKITEM WorkItem;
    NTSTATUS Status;

    TestRequest.Calls++;
    TestRequest.MajorFunction = IoStackLocation->MajorFunction;
    TestRequest.IoControlCode = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
    TestRequest.InputBufferLength = IoStackLocation->Parameters.DeviceIoControl.InputBufferLength;
    TestRequest.OutputBufferLength = IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength;
    TestRequest.RequestorMode = Irp->RequestorMode;
    TestRequest.Irql = KeGetCurrentIrql();

    Information = IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;
    TestRequest.InputIsType3 = Information != NULL && Irp->AssociatedIrp.SystemBuffer == NULL;
    if (Information != NULL && TestRequest.InputBufferLength >= sizeof(*Information))
        TestRequest.Information = *Information;

    switch (TestCompletion)
    {
        case CompletePending:
            WorkItem = IoAllocateWorkItem(DeviceObject);
            ok(WorkItem != NULL, "IoAllocateWorkItem failed\n");
            if (WorkItem != NULL)
            {
                TestPendingIrp = Irp;
                IoMarkIrpPending(Irp);
                IoQueueWorkItem(WorkItem, TestCompleteLater, DelayedWorkQueue, WorkItem);
                return STATUS_PENDING;
            }
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        case CompleteFailure:
            Status = STATUS_DEVICE_NOT_READY;
            break;
        default:
            Status = STATUS_SUCCESS;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
TestSend(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_ START_COMPLETION Completion,
    _In_ NTSTATUS ExpectedStatus)
{
    PKINTERRUPT Interrupt = (PKINTERRUPT)(ULONG_PTR)(0x5A5A000 + (IoControlCode & 0xFFF));
    NTSTATUS Status;
    ULONG Index;

    RtlFillMemory(&TestRequest, sizeof(TestRequest), 0xCC);
    TestRequest.Calls = 0;
    TestCompletion = Completion;

    Status = I8042CoreSendStartInformation(DeviceObject, IoControlCode, Interrupt);

    ok_eq_hex(Status, ExpectedStatus);
    ok_eq_ulong(TestRequest.Calls, 1UL);
    ok_eq_uint(TestRequest.MajorFunction, IRP_MJ_INTERNAL_DEVICE_CONTROL);
    ok_eq_hex(TestRequest.IoControlCode, IoControlCode);
    ok_eq_ulong(TestRequest.InputBufferLength, (ULONG)sizeof(INTERNAL_I8042_START_INFORMATION));
    ok_eq_ulong(TestRequest.OutputBufferLength, 0UL);
    ok_eq_bool(TestRequest.InputIsType3, TRUE);
    ok_eq_uint(TestRequest.RequestorMode, KernelMode);
    ok_eq_uint(TestRequest.Irql, PASSIVE_LEVEL);
    ok_eq_ulong(TestRequest.Information.Size, (ULONG)sizeof(INTERNAL_I8042_START_INFORMATION));
    ok_eq_pointer(TestRequest.Information.InterruptObject, Interrupt);
    for (Index = 0; Index < RTL_NUMBER_OF(TestRequest.Information.Reserved); Index++)
        ok_eq_ulong(TestRequest.Information.Reserved[Index], 0UL);
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    if (ControlCode != IOCTL_RUN_START_INFORMATION)
        return STATUS_NOT_SUPPORTED;

    TestSend(DeviceObject, IOCTL_INTERNAL_I8042_KEYBOARD_START_INFORMATION, CompleteSuccess, STATUS_SUCCESS);
    TestSend(DeviceObject, IOCTL_INTERNAL_I8042_MOUSE_START_INFORMATION, CompleteSuccess, STATUS_SUCCESS);
    TestSend(DeviceObject, IOCTL_INTERNAL_I8042_MOUSE_START_INFORMATION, CompleteFailure, STATUS_DEVICE_NOT_READY);
    TestSend(DeviceObject, IOCTL_INTERNAL_I8042_KEYBOARD_START_INFORMATION, CompletePending, STATUS_SUCCESS);
    ok(TestPendingIrp == NULL, "The pending request was not completed before the send returned\n");
    return STATUS_SUCCESS;
}
