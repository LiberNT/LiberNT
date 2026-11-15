/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pass-through function driver that only starts its device, for user-mode and component packages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define TAG_UMPASS 'ssPU'

typedef struct _UMPASS_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
} UMPASS_EXTENSION, *PUMPASS_EXTENSION;

DRIVER_INITIALIZE DriverEntry;
static DRIVER_ADD_DEVICE UmPassAddDevice;
static DRIVER_UNLOAD UmPassUnload;
_Dispatch_type_(IRP_MJ_PNP) static DRIVER_DISPATCH UmPassPnp;
_Dispatch_type_(IRP_MJ_POWER) static DRIVER_DISPATCH UmPassPower;
static DRIVER_DISPATCH UmPassForward;

static
NTSTATUS
NTAPI
UmPassForward(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUMPASS_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    ASSERT(Extension->Self == DeviceObject);

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Extension->LowerDevice, Irp);

    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
UmPassPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUMPASS_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Extension->Self == DeviceObject);

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    if (Stack->MinorFunction != IRP_MN_REMOVE_DEVICE)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        Status = IoCallDriver(Extension->LowerDevice, Irp);
        IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
        return Status;
    }

    /* The stack goes away: nothing may still run in this device once it is detached */
    IoReleaseRemoveLockAndWait(&Extension->RemoveLock, Irp);

    Lower = Extension->LowerDevice;
    ASSERT(Lower != NULL);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Lower, Irp);

    Extension->LowerDevice = NULL;
    IoDetachDevice(Lower);
    IoDeleteDevice(DeviceObject);

    return Status;
}

static
NTSTATUS
NTAPI
UmPassPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUMPASS_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    ASSERT(Extension->Self == DeviceObject);

    PoStartNextPowerIrp(Irp);

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(Extension->LowerDevice, Irp);

    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
UmPassAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PUMPASS_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoCreateDevice(DriverObject,
                            sizeof(UMPASS_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    IoInitializeRemoveLock(&Extension->RemoveLock, TAG_UMPASS, 0, 0);

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (Extension->LowerDevice == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= Extension->LowerDevice->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    DeviceObject->DeviceType = Extension->LowerDevice->DeviceType;
    DeviceObject->Characteristics = Extension->LowerDevice->Characteristics;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
UmPassUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    ASSERT(DriverObject->DeviceObject == NULL);
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
    {
        DriverObject->MajorFunction[Index] = UmPassForward;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = UmPassPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = UmPassPower;
    DriverObject->DriverExtension->AddDevice = UmPassAddDevice;
    DriverObject->DriverUnload = UmPassUnload;

    return STATUS_SUCCESS;
}
