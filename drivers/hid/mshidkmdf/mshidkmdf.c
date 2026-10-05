/*
 * PROJECT:     LiberNT HID Class Driver Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID minidriver pass-through for KMDF HID transport drivers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include <hidport.h>

#define NDEBUG
#include <debug.h>

static DRIVER_ADD_DEVICE MsHidKmdfAddDevice;
static DRIVER_UNLOAD MsHidKmdfUnload;

_Dispatch_type_(IRP_MJ_POWER)
static DRIVER_DISPATCH MsHidKmdfPower;

static DRIVER_DISPATCH MsHidKmdfPassThrough;

static
PDEVICE_OBJECT
MsHidKmdfLowerDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;

    return HidExtension->NextDeviceObject;
}

static
NTSTATUS
NTAPI
MsHidKmdfPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(MsHidKmdfLowerDevice(DeviceObject), Irp);
}

static
NTSTATUS
NTAPI
MsHidKmdfPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(MsHidKmdfLowerDevice(DeviceObject), Irp);
}

static
NTSTATUS
NTAPI
MsHidKmdfAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT FunctionalDeviceObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    FunctionalDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MsHidKmdfUnload(
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
    HID_MINIDRIVER_REGISTRATION Registration;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = MsHidKmdfPassThrough;
    }

    DriverObject->MajorFunction[IRP_MJ_POWER] = MsHidKmdfPower;
    DriverObject->DriverExtension->AddDevice = MsHidKmdfAddDevice;
    DriverObject->DriverUnload = MsHidKmdfUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = 0;
    Registration.DevicesArePolled = FALSE;

    Status = HidRegisterMinidriver(&Registration);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MSHIDKMDF: HidRegisterMinidriver failed 0x%08lx\n", Status);
    }

    return Status;
}
