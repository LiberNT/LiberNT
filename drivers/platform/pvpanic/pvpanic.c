/*
 * PROJECT:     LiberNT QEMU pvpanic Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reports bug checks to the QEMU pvpanic device
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define PVPANIC_TAG 'npVP'
#define PVPANIC_PANICKED 0x01

typedef struct _PVPANIC_DEVICE_EXTENSION
{
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    PUCHAR Register;
    BOOLEAN PortSpace;
    BOOLEAN Mapped;
    UCHAR Events;
    BOOLEAN CallbackRegistered;
    KBUGCHECK_CALLBACK_RECORD CallbackRecord;
} PVPANIC_DEVICE_EXTENSION, *PPVPANIC_DEVICE_EXTENSION;

static
NTSTATUS
NTAPI
PvPanicCompletion(
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
PvPanicForwardSynchronously(
    _In_ PPVPANIC_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PvPanicCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
UCHAR
PvPanicRead(
    _In_ PPVPANIC_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->PortSpace)
        return READ_PORT_UCHAR(DeviceExtension->Register);
    return READ_REGISTER_UCHAR(DeviceExtension->Register);
}

static
VOID
PvPanicWrite(
    _In_ PPVPANIC_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Value)
{
    if (DeviceExtension->PortSpace)
        WRITE_PORT_UCHAR(DeviceExtension->Register, Value);
    else
        WRITE_REGISTER_UCHAR(DeviceExtension->Register, Value);
}

static
VOID
NTAPI
PvPanicBugCheckCallback(
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    PPVPANIC_DEVICE_EXTENSION DeviceExtension = Buffer;

    UNREFERENCED_PARAMETER(Length);
    if (DeviceExtension->Register && (DeviceExtension->Events & PVPANIC_PANICKED))
        PvPanicWrite(DeviceExtension, PVPANIC_PANICKED);
}

static
VOID
PvPanicStopHardware(
    _Inout_ PPVPANIC_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->CallbackRegistered)
        DeviceExtension->CallbackRegistered = !KeDeregisterBugCheckCallback(&DeviceExtension->CallbackRecord);
    if (DeviceExtension->Mapped)
        MmUnmapIoSpace(DeviceExtension->Register, sizeof(UCHAR));
    DeviceExtension->Register = NULL;
    DeviceExtension->Mapped = FALSE;
    DeviceExtension->Events = 0;
}

static
NTSTATUS
PvPanicStartHardware(
    _Inout_ PPVPANIC_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PHYSICAL_ADDRESS Address;
    ULONG Index;

    if (!Resources || Resources->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    PartialList = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < PartialList->Count; Index++)
    {
        Descriptor = &PartialList->PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypePort && Descriptor->u.Port.Length >= sizeof(UCHAR))
        {
            Address = Descriptor->u.Port.Start;
            DeviceExtension->PortSpace = (Descriptor->Flags & CM_RESOURCE_PORT_IO) != 0;
            break;
        }
        if (Descriptor->Type == CmResourceTypeMemory && Descriptor->u.Memory.Length >= sizeof(UCHAR))
        {
            Address = Descriptor->u.Memory.Start;
            DeviceExtension->PortSpace = FALSE;
            break;
        }
    }
    if (Index == PartialList->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (DeviceExtension->PortSpace)
    {
        DeviceExtension->Register = (PUCHAR)(ULONG_PTR)Address.QuadPart;
    }
    else
    {
        DeviceExtension->Register = MmMapIoSpace(Address, sizeof(UCHAR), MmNonCached);
        if (!DeviceExtension->Register)
            return STATUS_INSUFFICIENT_RESOURCES;
        DeviceExtension->Mapped = TRUE;
    }

    DeviceExtension->Events = PvPanicRead(DeviceExtension) & PVPANIC_PANICKED;
    if (DeviceExtension->Events)
    {
        KeInitializeCallbackRecord(&DeviceExtension->CallbackRecord);
        DeviceExtension->CallbackRegistered = KeRegisterBugCheckCallback(&DeviceExtension->CallbackRecord,
                                                                         PvPanicBugCheckCallback,
                                                                         DeviceExtension,
                                                                         sizeof(*DeviceExtension),
                                                                         (PUCHAR)"PvPanic");
    }

    DPRINT1("PVPANIC: register=%p port=%u events=0x%02x callback=%u\n",
            DeviceExtension->Register,
            DeviceExtension->PortSpace,
            DeviceExtension->Events,
            DeviceExtension->CallbackRegistered);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PvPanicPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPVPANIC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = PvPanicForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = PvPanicStartHardware(DeviceExtension, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
            if (!NT_SUCCESS(Status))
                PvPanicStopHardware(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            PvPanicStopHardware(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            PvPanicStopHardware(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = PvPanicForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
PvPanicPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPVPANIC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
PvPanicAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PPVPANIC_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, PVPANIC_TAG, 0, 0);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
PvPanicUnload(
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
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_PNP] = PvPanicPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PvPanicPower;
    DriverObject->DriverExtension->AddDevice = PvPanicAddDevice;
    DriverObject->DriverUnload = PvPanicUnload;
    return STATUS_SUCCESS;
}
