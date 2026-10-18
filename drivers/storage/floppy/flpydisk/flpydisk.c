/*
 * PROJECT:     LiberNT Floppy Disk Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver of the floppy drives the fdc bus driver enumerates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h>
#include <ntddstor.h>
#include <mountdev.h>
#include <mountmgr.h>

#define NDEBUG
#include <debug.h>

#define FLPYDISK_MAX_DRIVES 16

typedef struct _FLPYDISK_EXTENSION
{
    PDEVICE_OBJECT LowerDevice;
    UNICODE_STRING DeviceName;
    WCHAR DeviceNameBuffer[32];
    UNICODE_STRING MountedDeviceInterface;
    UNICODE_STRING FloppyInterface;
} FLPYDISK_EXTENSION, *PFLPYDISK_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static VOID
SetInterfaceState(PUNICODE_STRING Interface, BOOLEAN Enable)
{
    PAGED_CODE();

    if (Interface->Buffer && !NT_SUCCESS(IoSetDeviceInterfaceState(Interface, Enable)))
        DPRINT1("IoSetDeviceInterfaceState(%wZ, %u) failed\n", Interface, Enable);
}

static VOID
SetInterfaces(PFLPYDISK_EXTENSION Extension, BOOLEAN Enable)
{
    PAGED_CODE();

    SetInterfaceState(&Extension->MountedDeviceInterface, Enable);
    SetInterfaceState(&Extension->FloppyInterface, Enable);
}

static NTSTATUS
CompleteName(PIRP Irp, PCUNICODE_STRING Name, BOOLEAN UniqueId)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG HeaderLength = UniqueId ? FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) : FIELD_OFFSET(MOUNTDEV_NAME, Name);
    NTSTATUS Status;

    if (OutputLength < (UniqueId ? sizeof(MOUNTDEV_UNIQUE_ID) : sizeof(MOUNTDEV_NAME)))
    {
        Irp->IoStatus.Information = 0;
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (UniqueId)
    {
        PMOUNTDEV_UNIQUE_ID Id = Irp->AssociatedIrp.SystemBuffer;

        Id->UniqueIdLength = Name->Length;
        if (OutputLength < HeaderLength + Name->Length)
        {
            Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
            Status = STATUS_BUFFER_OVERFLOW;
        }
        else
        {
            RtlCopyMemory(Id->UniqueId, Name->Buffer, Name->Length);
            Irp->IoStatus.Information = HeaderLength + Name->Length;
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        PMOUNTDEV_NAME DeviceName = Irp->AssociatedIrp.SystemBuffer;

        DeviceName->NameLength = Name->Length;
        if (OutputLength < HeaderLength + Name->Length)
        {
            Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
            Status = STATUS_BUFFER_OVERFLOW;
        }
        else
        {
            RtlCopyMemory(DeviceName->Name, Name->Buffer, Name->Length);
            Irp->IoStatus.Information = HeaderLength + Name->Length;
            Status = STATUS_SUCCESS;
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
DispatchForward(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFLPYDISK_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFLPYDISK_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
            return CompleteName(Irp, &Extension->DeviceName, FALSE);

        case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
            if (!Extension->MountedDeviceInterface.Buffer)
                break;
            return CompleteName(Irp, &Extension->MountedDeviceInterface, TRUE);
    }

    return DispatchForward(DeviceObject, Irp);
}

static NTSTATUS NTAPI
DispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFLPYDISK_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
DispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFLPYDISK_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT LowerDevice = Extension->LowerDevice;
    NTSTATUS Status;

    PAGED_CODE();

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                SetInterfaces(Extension, TRUE);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            SetInterfaces(Extension, FALSE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            SetInterfaces(Extension, FALSE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(LowerDevice, Irp);
            IoDetachDevice(LowerDevice);
            RtlFreeUnicodeString(&Extension->MountedDeviceInterface);
            RtlFreeUnicodeString(&Extension->FloppyInterface);
            IoGetConfigurationInformation()->FloppyCount--;
            IoDeleteDevice(DeviceObject);
            return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(LowerDevice, Irp);
}

static NTSTATUS NTAPI
AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    PFLPYDISK_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject = NULL;
    WCHAR NameBuffer[32];
    UNICODE_STRING Name;
    NTSTATUS Status = STATUS_OBJECT_NAME_COLLISION;
    ULONG Number;

    PAGED_CODE();

    for (Number = 0; Number < FLPYDISK_MAX_DRIVES && Status == STATUS_OBJECT_NAME_COLLISION; Number++)
    {
        RtlStringCbPrintfW(NameBuffer, sizeof(NameBuffer), L"\\Device\\Floppy%lu", Number);
        RtlInitUnicodeString(&Name, NameBuffer);
        Status = IoCreateDevice(DriverObject, sizeof(FLPYDISK_EXTENSION), &Name, FILE_DEVICE_DISK,
                                FILE_REMOVABLE_MEDIA | FILE_FLOPPY_DISKETTE, FALSE, &DeviceObject);
    }
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    RtlInitEmptyUnicodeString(&Extension->DeviceName, Extension->DeviceNameBuffer, sizeof(Extension->DeviceNameBuffer));
    RtlCopyUnicodeString(&Extension->DeviceName, &Name);

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &MOUNTDEV_MOUNTED_DEVICE_GUID, NULL,
                                       &Extension->MountedDeviceInterface);
    if (!NT_SUCCESS(Status))
        RtlInitUnicodeString(&Extension->MountedDeviceInterface, NULL);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_FLOPPY, NULL,
                                       &Extension->FloppyInterface);
    if (!NT_SUCCESS(Status))
        RtlInitUnicodeString(&Extension->FloppyInterface, NULL);

    IoGetConfigurationInformation()->FloppyCount++;

    DeviceObject->AlignmentRequirement = Extension->LowerDevice->AlignmentRequirement;
    DeviceObject->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        DriverObject->MajorFunction[i] = DispatchForward;

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DispatchPower;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
    DriverObject->DriverExtension->AddDevice = AddDevice;
    return STATUS_SUCCESS;
}
