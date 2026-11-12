/*
 * PROJECT:     LiberNT Virtual WiFi Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual WiFi bus upper filter for Native 802.11 miniports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define VWIFIBUS_TAG 'suBV'

typedef struct _VWIFIBUS_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    volatile LONG Removing;
} VWIFIBUS_DEVICE_EXTENSION, *PVWIFIBUS_DEVICE_EXTENSION;

static
NTSTATUS
VWifiBusComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
VWifiBusCompletion(
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
VWifiBusForwardSynchronously(
    _In_ PVWIFIBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceExtension->LowerDevice != NULL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, VWifiBusCompletion, &Event, TRUE, TRUE, TRUE);
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
NTAPI
VWifiBusPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVWIFIBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    ASSERT(DeviceExtension->LowerDevice != NULL);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return VWifiBusComplete(Irp, Status);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
VWifiBusPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVWIFIBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    ASSERT(DeviceExtension->LowerDevice != NULL);

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
VWifiBusPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVWIFIBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    ASSERT(DeviceExtension->LowerDevice != NULL);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
                return VWifiBusComplete(Irp, Status);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = VWifiBusForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            VWifiBusComplete(Irp, Status);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            break;

        default:
            break;
    }

    /* TODO: BusRelations enumeration of the virtual Native 802.11 child adapters (Wi-Fi Direct, hosted network) is not implemented; the filter stays transparent. */
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
VWifiBusAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PVWIFIBUS_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    ASSERT(PhysicalDeviceObject != NULL);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

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
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, VWIFIBUS_TAG, 0, 0);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject,
                                             PhysicalDeviceObject,
                                             &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    ASSERT(DeviceExtension->LowerDevice != NULL);
    DeviceObject->DeviceType = DeviceExtension->LowerDevice->DeviceType;
    DeviceObject->Characteristics = DeviceExtension->LowerDevice->Characteristics;
    DeviceObject->Flags |= DeviceExtension->LowerDevice->Flags &
                           (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
VWifiBusUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = VWifiBusPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = VWifiBusPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = VWifiBusPower;
    DriverObject->DriverExtension->AddDevice = VWifiBusAddDevice;
    DriverObject->DriverUnload = VWifiBusUnload;
    return STATUS_SUCCESS;
}
