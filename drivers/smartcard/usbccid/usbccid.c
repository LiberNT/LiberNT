/*
 * PROJECT:     LiberNT USB CCID Smart Card Reader Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, PnP, power and dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "usbccid.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;
static DRIVER_ADD_DEVICE CcidAddDevice;
static DRIVER_UNLOAD CcidUnload;
static DRIVER_DISPATCH CcidCreateClose;
static DRIVER_DISPATCH CcidCleanup;
static DRIVER_DISPATCH CcidDeviceControl;
static DRIVER_DISPATCH CcidPnp;
static DRIVER_DISPATCH CcidPower;
static DRIVER_DISPATCH CcidPassDown;
static IO_COMPLETION_ROUTINE CcidSignalCompletion;

static
NTSTATUS
CcidComplete(
    _In_ PIRP Irp,
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
CcidSignalCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    ASSERT(Context);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
CcidForwardAndWait(
    _In_ PCCID_DEVICE Device,
    _In_ PIRP Irp)
{
    KEVENT Event;

    PAGED_CODE();
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, CcidSignalCompletion, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Device->LowerDevice, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return Irp->IoStatus.Status;
}

static
NTSTATUS
CcidSubmitUrb(
    _In_ PCCID_DEVICE Device,
    _In_ PURB Urb)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB, Device->LowerDevice, NULL, 0, NULL, 0, TRUE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoGetNextIrpStackLocation(Irp)->Parameters.Others.Argument1 = Urb;
    Status = IoCallDriver(Device->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) && !USBD_SUCCESS(Urb->UrbHeader.Status))
        Status = STATUS_UNSUCCESSFUL;
    return Status;
}

static
NTSTATUS
CcidGetDescriptor(
    _In_ PCCID_DEVICE Device,
    _In_ UCHAR Type,
    _In_ UCHAR Index,
    _In_ USHORT LanguageId,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Transferred)
{
    URB Urb;
    NTSTATUS Status;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), Type, Index, LanguageId, Buffer,
                                 NULL, Length, NULL);
    Status = CcidSubmitUrb(Device, &Urb);
    *Transferred = NT_SUCCESS(Status) ? Urb.UrbControlDescriptorRequest.TransferBufferLength : 0;
    ASSERT(*Transferred <= Length);
    return Status;
}

NTSTATUS
CcidGetString(
    _In_ PCCID_DEVICE Device,
    _In_ UCHAR Index,
    _Out_writes_(Size) PUCHAR Buffer,
    _In_ USHORT Size,
    _Out_ PUSHORT Length)
{
    PUSB_STRING_DESCRIPTOR String;
    ULONG Transferred, Chars, Char;
    NTSTATUS Status;

    *Length = 0;
    if (!Index)
        return STATUS_NOT_FOUND;
    String = ExAllocatePoolZero(NonPagedPool, MAXUCHAR + 1, CCID_TAG);
    if (!String)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = CcidGetDescriptor(Device, USB_STRING_DESCRIPTOR_TYPE, Index, 0x0409, String, MAXUCHAR, &Transferred);
    if (NT_SUCCESS(Status) && (Transferred < 2 || String->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE))
        Status = STATUS_DEVICE_DATA_ERROR;
    if (NT_SUCCESS(Status))
    {
        Chars = (min(Transferred, String->bLength) - FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString)) / sizeof(WCHAR);
        for (Char = 0; Char < Chars && Char < Size; ++Char)
        {
            WCHAR Value = String->bString[Char];

            Buffer[Char] = (Value >= 0x20 && Value < 0x7F) ? (UCHAR)Value : '?';
        }
        *Length = (USHORT)Char;
    }
    ExFreePoolWithTag(String, CCID_TAG);
    return Status;
}

static
PUCHAR
CcidFindClassDescriptor(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR Configuration,
    _In_ PUSB_INTERFACE_DESCRIPTOR Interface)
{
    PUCHAR At = (PUCHAR)Interface + Interface->bLength;
    PUCHAR End = (PUCHAR)Configuration + Configuration->wTotalLength;

    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE)
            break;
        if (At[1] == CCID_CLASS_DESCRIPTOR_TYPE && At[0] >= CCID_CLASS_DESCRIPTOR_LENGTH)
            return At;
        At += At[0];
    }
    return NULL;
}

static
NTSTATUS
CcidStartDevice(
    _In_ PCCID_DEVICE Device)
{
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR Header;
    USBD_INTERFACE_LIST_ENTRY Interfaces[2];
    PUSB_INTERFACE_DESCRIPTOR Interface;
    PUSBD_INTERFACE_INFORMATION Information;
    PUCHAR ClassDescriptor;
    ULONG Transferred, Index;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(!Device->Started);
    Status = CcidGetDescriptor(Device, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, &DeviceDescriptor, sizeof(DeviceDescriptor),
                               &Transferred);
    if (!NT_SUCCESS(Status) || Transferred < sizeof(DeviceDescriptor))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    Status = CcidGetDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, &Header, sizeof(Header), &Transferred);
    if (!NT_SUCCESS(Status) || Transferred < sizeof(Header) || Header.wTotalLength < sizeof(Header))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    if (Device->Configuration)
        ExFreePoolWithTag(Device->Configuration, CCID_TAG);
    Device->Configuration = ExAllocatePoolZero(NonPagedPool, Header.wTotalLength, CCID_TAG);
    if (!Device->Configuration)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = CcidGetDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Device->Configuration,
                               Header.wTotalLength, &Transferred);
    if (!NT_SUCCESS(Status) || Transferred < Header.wTotalLength)
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;

    Interface = USBD_ParseConfigurationDescriptorEx(Device->Configuration, Device->Configuration, -1, -1,
                                                    CCID_INTERFACE_CLASS, -1, -1);
    if (!Interface)
    {
        DPRINT1("USBCCID: no smart card interface\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ClassDescriptor = CcidFindClassDescriptor(Device->Configuration, Interface);
    if (!ClassDescriptor)
    {
        DPRINT1("USBCCID: interface %u has no CCID class descriptor\n", Interface->bInterfaceNumber);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(Interfaces, sizeof(Interfaces));
    Interfaces[0].InterfaceDescriptor = Interface;
    if (Device->SelectUrb)
        ExFreePool(Device->SelectUrb);
    Device->SelectUrb = USBD_CreateConfigurationRequestEx(Device->Configuration, Interfaces);
    if (!Device->SelectUrb)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = CcidSubmitUrb(Device, Device->SelectUrb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBCCID: selecting the configuration failed %x\n", Status);
        return Status;
    }

    Information = Interfaces[0].Interface;
    ASSERT(Information);
    Device->Reader.BulkIn = Device->Reader.BulkOut = Device->Reader.Interrupt = NULL;
    for (Index = 0; Index < Information->NumberOfPipes; ++Index)
    {
        PUSBD_PIPE_INFORMATION Pipe = &Information->Pipes[Index];
        BOOLEAN In = USB_ENDPOINT_DIRECTION_IN(Pipe->EndpointAddress);

        if (Pipe->PipeType == UsbdPipeTypeBulk && In && !Device->Reader.BulkIn)
            Device->Reader.BulkIn = Pipe->PipeHandle;
        else if (Pipe->PipeType == UsbdPipeTypeBulk && !In && !Device->Reader.BulkOut)
            Device->Reader.BulkOut = Pipe->PipeHandle;
        else if (Pipe->PipeType == UsbdPipeTypeInterrupt && In && !Device->Reader.Interrupt)
        {
            Device->Reader.Interrupt = Pipe->PipeHandle;
            Device->Reader.InterruptLength = min(Pipe->MaximumPacketSize, sizeof(Device->Reader.InterruptBuffer));
        }
    }
    if (!Device->Reader.BulkIn || !Device->Reader.BulkOut)
    {
        DPRINT1("USBCCID: the smart card interface lacks its bulk pipes\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = CcidStartReader(Device, &DeviceDescriptor, ClassDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;
    Device->Started = TRUE;
    Status = IoSetDeviceInterfaceState(&Device->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
        DPRINT1("USBCCID: enabling the reader interface failed %x\n", Status);
    return STATUS_SUCCESS;
}

static
VOID
CcidStopDevice(
    _In_ PCCID_DEVICE Device)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (!Device->Started)
        return;
    Device->Started = FALSE;
    Status = IoSetDeviceInterfaceState(&Device->InterfaceName, FALSE);
    if (!NT_SUCCESS(Status))
        DPRINT1("USBCCID: disabling the reader interface failed %x\n", Status);
    CcidStopReader(Device);
}

static
NTSTATUS
NTAPI
CcidPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return CcidComplete(Irp, Status);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = CcidForwardAndWait(Device, Irp);
            if (NT_SUCCESS(Status))
                Status = CcidStartDevice(Device);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&Device->RemoveLock, Irp);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            CcidStopDevice(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            CcidStopDevice(Device);
            IoReleaseRemoveLockAndWait(&Device->RemoveLock, Irp);
            CcidReleaseReader(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Device->LowerDevice, Irp);
            IoDetachDevice(Device->LowerDevice);
            RtlFreeUnicodeString(&Device->InterfaceName);
            if (Device->SelectUrb)
                ExFreePool(Device->SelectUrb);
            if (Device->Configuration)
                ExFreePoolWithTag(Device->Configuration, CCID_TAG);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Device->LowerDevice, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
CcidPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
CcidPassDown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
CcidCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_CLOSE)
    {
        ASSERT(Device->OpenCount == 1);
        InterlockedExchange(&Device->OpenCount, 0);
        return CcidComplete(Irp, STATUS_SUCCESS);
    }
    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return CcidComplete(Irp, Status);
    if (!Device->Started)
        Status = STATUS_DEVICE_NOT_READY;
    else if (InterlockedCompareExchange(&Device->OpenCount, 1, 0) != 0)
        Status = STATUS_ACCESS_DENIED;
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return CcidComplete(Irp, Status);
}

static
NTSTATUS
NTAPI
CcidCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;

    if (Device->SmartcardInitialized)
        CcidCompleteTracking(&Device->Smartcard, STATUS_CANCELLED);
    return CcidComplete(Irp, STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
CcidDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return CcidComplete(Irp, Status);
    if (!Device->Started)
        Status = CcidComplete(Irp, STATUS_DEVICE_NOT_READY);
    else
        Status = SmartcardDeviceControl(&Device->Smartcard, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
CcidAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT DeviceObject;
    PCCID_DEVICE Device;
    NTSTATUS Status;

    PAGED_CODE();
    Status = IoCreateDevice(DriverObject, sizeof(CCID_DEVICE), NULL, FILE_DEVICE_SMARTCARD, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->DeviceObject = DeviceObject;
    Device->Pdo = Pdo;
    IoInitializeRemoveLock(&Device->RemoveLock, CCID_TAG, 0, 0);
    Status = IoRegisterDeviceInterface(Pdo, &SmartCardReaderGuid, NULL, &Device->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Device->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, Pdo);
    if (!Device->LowerDevice)
    {
        RtlFreeUnicodeString(&Device->InterfaceName);
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    Device->Reader.DeviceObject = DeviceObject;
    Device->Reader.LowerDevice = Device->LowerDevice;
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
CcidUnload(
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

    DriverObject->DriverExtension->AddDevice = CcidAddDevice;
    DriverObject->DriverUnload = CcidUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CcidCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CcidCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = CcidCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CcidDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = CcidPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = CcidPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = CcidPassDown;
    return STATUS_SUCCESS;
}
