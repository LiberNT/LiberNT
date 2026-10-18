/*
 * PROJECT:     LiberNT Monitor Class Function Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver for the monitor devices enumerated by the graphics kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "monitor.h"
#include <ntddvdeo.h>

#define NDEBUG
#include <debug.h>

UNICODE_STRING MonitorRegistryPath;

static
NTSTATUS
NTAPI
MonitorCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
MonitorForwardSynchronously(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, MonitorCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
MonitorCompleteRequest(
    _Inout_ PIRP Irp,
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
MonitorCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return MonitorCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
MonitorDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return MonitorCompleteRequest(Irp, STATUS_NOT_SUPPORTED, 0);
}

static
NTSTATUS
NTAPI
MonitorSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    SYSCTL_IRP_DISPOSITION Disposition = IrpForward;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return MonitorCompleteRequest(Irp, Status, 0);

    if (DeviceExtension->WmiRegistered)
        Status = WmiSystemControl(&DeviceExtension->WmiLibContext, DeviceObject, Irp, &Disposition);

    switch (Disposition)
    {
        case IrpProcessed:
            break;

        case IrpNotCompleted:
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            break;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            break;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
MonitorPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return MonitorCompleteRequest(Irp, Status, 0);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = MonitorForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                MonitorWmiRegister(DeviceExtension);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return MonitorCompleteRequest(Irp, Status, Irp->IoStatus.Information);

        case IRP_MN_REMOVE_DEVICE:
            MonitorWmiDeregister(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
MonitorPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        return MonitorCompleteRequest(Irp, Status, 0);
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(DeviceExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
MonitorUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return MonitorCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static
NTSTATUS
NTAPI
MonitorAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(*DeviceExtension),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->DeviceObject = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, MONITOR_TAG, 0, 0);

    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MonitorUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (MonitorRegistryPath.Buffer)
        ExFreePoolWithTag(MonitorRegistryPath.Buffer, MONITOR_TAG);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    MonitorRegistryPath.Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, RegistryPath->Length + sizeof(WCHAR), MONITOR_TAG);
    if (!MonitorRegistryPath.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(MonitorRegistryPath.Buffer, RegistryPath->Buffer, RegistryPath->Length);
    MonitorRegistryPath.Length = RegistryPath->Length;
    MonitorRegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);
    MonitorRegistryPath.Buffer[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = MonitorUnsupported;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = MonitorCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = MonitorCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = MonitorCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MonitorDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = MonitorSystemControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = MonitorPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = MonitorPower;
    DriverObject->DriverExtension->AddDevice = MonitorAddDevice;
    DriverObject->DriverUnload = MonitorUnload;
    return STATUS_SUCCESS;
}
