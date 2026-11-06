/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A battery status wait against a miniport whose tag and capacity change
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <batclass.h>
#include "PoIrp.h"

#define TEST_TAG            7
#define TEST_CAPACITY       1000
#define TEST_END_MS         5000
#define TEST_SETTLE_MS      200

typedef struct _STATUS_WAIT
{
    PDEVICE_OBJECT Device;
    PVOID ClassData;
    BATTERY_WAIT_STATUS Wait;
    BATTERY_STATUS Result;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Returned;
} STATUS_WAIT, *PSTATUS_WAIT;

static PDRIVER_OBJECT TestDriverObject;
static KMT_MESSAGE_HANDLER TestMessageHandler;
static KEVENT NotifyArmed;
static volatile ULONG CurrentTag;
static volatile ULONG CurrentCapacity;

static
NTSTATUS
NTAPI
TestQueryTag(
    _In_ PVOID Context,
    _Out_ PULONG BatteryTag)
{
    UNREFERENCED_PARAMETER(Context);
    *BatteryTag = CurrentTag;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestQueryInformation(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ BATTERY_QUERY_INFORMATION_LEVEL Level,
    _In_opt_ LONG AtRate,
    _Out_writes_bytes_to_(BufferLength, *ReturnedLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BatteryTag);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(AtRate);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    *ReturnedLength = 0;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
TestQueryStatus(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _Out_ PBATTERY_STATUS BatteryStatus)
{
    UNREFERENCED_PARAMETER(Context);
    if (BatteryTag != CurrentTag)
        return STATUS_NO_SUCH_DEVICE;

    BatteryStatus->PowerState = BATTERY_DISCHARGING;
    BatteryStatus->Capacity = CurrentCapacity;
    BatteryStatus->Voltage = 11000;
    BatteryStatus->Rate = -100;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestSetStatusNotify(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ PBATTERY_NOTIFY BatteryNotify)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BatteryNotify);
    if (BatteryTag != CurrentTag)
        return STATUS_NO_SUCH_DEVICE;

    KeSetEvent(&NotifyArmed, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestDisableStatusNotify(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
StatusWaitThread(
    _In_ PVOID Context)
{
    PSTATUS_WAIT Request = Context;
    KEVENT Event;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_BATTERY_QUERY_STATUS,
                                        Request->Device,
                                        &Request->Wait,
                                        sizeof(Request->Wait),
                                        &Request->Result,
                                        sizeof(Request->Result),
                                        FALSE,
                                        &Event,
                                        &Request->IoStatus);
    if (Irp == NULL)
    {
        Request->Returned = STATUS_INSUFFICIENT_RESOURCES;
        KeSetEvent(&NotifyArmed, IO_NO_INCREMENT, FALSE);
        return;
    }

    IoSetNextIrpStackLocation(Irp);
    Request->Returned = BatteryClassIoctl(Request->ClassData, Irp);
    if (Request->Returned == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
}

static
PKTHREAD
StartStatusWait(
    _Out_ PSTATUS_WAIT Request,
    _In_ PDEVICE_OBJECT Device,
    _In_ PVOID ClassData)
{
    HANDLE ThreadHandle;
    PKTHREAD Thread;
    NTSTATUS Status;
    LARGE_INTEGER Settle;

    RtlZeroMemory(Request, sizeof(*Request));
    Request->Device = Device;
    Request->ClassData = ClassData;
    Request->Wait.BatteryTag = CurrentTag;
    Request->Wait.Timeout = (ULONG)-1;
    Request->Wait.PowerState = BATTERY_DISCHARGING;
    Request->Wait.LowCapacity = CurrentCapacity;
    Request->Wait.HighCapacity = CurrentCapacity;
    Request->Returned = STATUS_PENDING;

    KeClearEvent(&NotifyArmed);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, StatusWaitThread, Request);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    if (!NT_SUCCESS(Status))
        return NULL;

    /* The class sets its wait up right after arming the notification. */
    Settle.QuadPart = -10000LL * TEST_END_MS;
    Status = KeWaitForSingleObject(&NotifyArmed, Executive, KernelMode, FALSE, &Settle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Settle.QuadPart = -10000LL * TEST_SETTLE_MS;
    KeDelayExecutionThread(KernelMode, FALSE, &Settle);
    return Thread;
}

static
NTSTATUS
WaitForThread(
    _In_ PKTHREAD Thread,
    _In_ LONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10000LL * Milliseconds;
    return KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
}

static
VOID
ReleaseStuckWait(
    _In_ PKTHREAD Thread,
    _In_ PVOID ClassData,
    _In_ ULONG Tag)
{
    CurrentTag = Tag;
    CurrentCapacity += TEST_CAPACITY;
    (VOID)BatteryClassStatusNotify(ClassData);
    KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
}

static
VOID
TestDeadTagEndsWait(
    _In_ PDEVICE_OBJECT Device,
    _In_ PVOID ClassData)
{
    STATUS_WAIT Request;
    PKTHREAD Thread;
    NTSTATUS Status;

    CurrentTag = TEST_TAG;
    CurrentCapacity = TEST_CAPACITY;
    Thread = StartStatusWait(&Request, Device, ClassData);
    if (Thread == NULL)
        return;

    CurrentTag = TEST_TAG + 1;
    (VOID)BatteryClassStatusNotify(ClassData);
    Status = WaitForThread(Thread, TEST_END_MS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        ReleaseStuckWait(Thread, ClassData, TEST_TAG);
    else
        ok_eq_hex(Request.Returned, STATUS_NO_SUCH_DEVICE);
    ObDereferenceObject(Thread);
}

static
VOID
TestUnchangedStatusKeepsWaiting(
    _In_ PDEVICE_OBJECT Device,
    _In_ PVOID ClassData)
{
    STATUS_WAIT Request;
    PKTHREAD Thread;
    NTSTATUS Status;

    CurrentTag = TEST_TAG;
    CurrentCapacity = TEST_CAPACITY;
    Thread = StartStatusWait(&Request, Device, ClassData);
    if (Thread == NULL)
        return;

    Status = BatteryClassStatusNotify(ClassData);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = WaitForThread(Thread, TEST_SETTLE_MS);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    if (Status == STATUS_TIMEOUT)
    {
        CurrentCapacity = TEST_CAPACITY + 1;
        Status = BatteryClassStatusNotify(ClassData);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = WaitForThread(Thread, TEST_END_MS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            ReleaseStuckWait(Thread, ClassData, TEST_TAG);
        else
        {
            ok_eq_hex(Request.Returned, STATUS_SUCCESS);
            ok_eq_ulong(Request.Result.Capacity, TEST_CAPACITY + 1);
        }
    }
    ObDereferenceObject(Thread);
}

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);

    TestDriverObject = DriverObject;
    KeInitializeEvent(&NotifyArmed, NotificationEvent, FALSE);

    *DeviceName = L"BatteryClassNotify";
    *Flags = TESTENTRY_NO_EXCLUSIVE_DEVICE;

    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();
}

static
NTSTATUS
TestMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_ PVOID Buffer OPTIONAL,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    BATTERY_MINIPORT_INFO Info;
    PDEVICE_OBJECT Device;
    PVOID ClassData;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    if (ControlCode != IOCTL_RUN_TEST)
        return STATUS_NOT_SUPPORTED;

    Status = IoCreateDevice(TestDriverObject, 0, NULL, FILE_DEVICE_BATTERY, 0, FALSE, &Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;
    Device->Flags &= ~DO_DEVICE_INITIALIZING;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
    Info.MinorVersion = BATTERY_CLASS_MINOR_VERSION;
    Info.QueryTag = TestQueryTag;
    Info.QueryInformation = TestQueryInformation;
    Info.QueryStatus = TestQueryStatus;
    Info.SetStatusNotify = TestSetStatusNotify;
    Info.DisableStatusNotify = TestDisableStatusNotify;
    Status = BatteryClassInitializeDevice(&Info, &ClassData);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        TestUnchangedStatusKeepsWaiting(Device, ClassData);
        TestDeadTagEndsWait(Device, ClassData);
        (VOID)BatteryClassUnload(ClassData);
    }

    IoDeleteDevice(Device);
    return STATUS_SUCCESS;
}
