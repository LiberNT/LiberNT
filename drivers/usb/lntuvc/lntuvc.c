/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, Plug and Play and USB requests of the video class driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>

static const UNICODE_STRING UvcReferenceString = RTL_CONSTANT_STRING(L"GLOBAL");

PVOID
UvcAllocate(
    PUVC_DEVICE Device,
    SIZE_T Size)
{
    PVOID Memory = ExAllocatePoolWithTag(NonPagedPool, Size, LNTUVC_TAG);

    if (!Memory)
        return NULL;

    RtlZeroMemory(Memory, Size);

    if (!NT_SUCCESS(KsAddItemToObjectBag(Device->KsDevice->Bag, Memory, ExFreePool)))
    {
        ExFreePoolWithTag(Memory, LNTUVC_TAG);
        return NULL;
    }

    return Memory;
}

NTSTATUS
UvcSubmitUrbSync(
    PUVC_DEVICE Device,
    PURB Urb)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        Device->LowerDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(Device->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS
UvcClassRequest(
    PUVC_DEVICE Device,
    BOOLEAN In,
    UCHAR Request,
    USHORT Value,
    USHORT Index,
    PVOID Buffer,
    ULONG Length,
    PULONG Transferred)
{
    struct
    {
        struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST Urb;
        UCHAR Data[UVC_MAX_PROBE + 16];
    } *Block;
    NTSTATUS Status;

    if (Length > sizeof(Block->Data))
    {
        PUCHAR Large = ExAllocatePoolWithTag(NonPagedPool, sizeof(Block->Urb) + Length, LNTUVC_TAG);
        struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *Urb = (PVOID)Large;

        if (!Large)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Urb, sizeof(*Urb));
        if (!In)
            RtlCopyMemory(Large + sizeof(*Urb), Buffer, Length);

        UsbBuildVendorRequest((PURB)Urb,
                              URB_FUNCTION_CLASS_INTERFACE,
                              sizeof(*Urb),
                              In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : USBD_TRANSFER_DIRECTION_OUT,
                              0,
                              Request,
                              Value,
                              Index,
                              Large + sizeof(*Urb),
                              NULL,
                              Length,
                              NULL);

        KeWaitForSingleObject(&Device->ControlMutex, Executive, KernelMode, FALSE, NULL);
        Status = UvcSubmitUrbSync(Device, (PURB)Urb);
        KeReleaseMutex(&Device->ControlMutex, FALSE);

        if (NT_SUCCESS(Status) && In)
            RtlCopyMemory(Buffer, Large + sizeof(*Urb), min(Length, Urb->TransferBufferLength));
        if (Transferred)
            *Transferred = NT_SUCCESS(Status) ? Urb->TransferBufferLength : 0;

        ExFreePoolWithTag(Large, LNTUVC_TAG);
        return Status;
    }

    Block = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Block), LNTUVC_TAG);
    if (!Block)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    if (!In && Length)
        RtlCopyMemory(Block->Data, Buffer, Length);

    UsbBuildVendorRequest((PURB)&Block->Urb,
                          URB_FUNCTION_CLASS_INTERFACE,
                          sizeof(Block->Urb),
                          In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : USBD_TRANSFER_DIRECTION_OUT,
                          0,
                          Request,
                          Value,
                          Index,
                          Block->Data,
                          NULL,
                          Length,
                          NULL);

    KeWaitForSingleObject(&Device->ControlMutex, Executive, KernelMode, FALSE, NULL);
    Status = UvcSubmitUrbSync(Device, (PURB)&Block->Urb);
    KeReleaseMutex(&Device->ControlMutex, FALSE);

    if (NT_SUCCESS(Status) && In)
        RtlCopyMemory(Buffer, Block->Data, min(Length, Block->Urb.TransferBufferLength));
    if (Transferred)
        *Transferred = NT_SUCCESS(Status) ? Block->Urb.TransferBufferLength : 0;

    ExFreePoolWithTag(Block, LNTUVC_TAG);
    return Status;
}

static
NTSTATUS
UvcGetDescriptor(
    PUVC_DEVICE Device,
    UCHAR Type,
    UCHAR Index,
    USHORT Language,
    PVOID Buffer,
    PULONG Length)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST Urb;
    PVOID Copy;
    NTSTATUS Status;

    Copy = ExAllocatePoolWithTag(NonPagedPool, *Length, LNTUVC_TAG);
    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest((PURB)&Urb,
                                 sizeof(Urb),
                                 Type,
                                 Index,
                                 Language,
                                 Copy,
                                 NULL,
                                 *Length,
                                 NULL);

    Status = UvcSubmitUrbSync(Device, (PURB)&Urb);
    if (NT_SUCCESS(Status))
    {
        *Length = Urb.TransferBufferLength;
        RtlCopyMemory(Buffer, Copy, *Length);
    }

    ExFreePoolWithTag(Copy, LNTUVC_TAG);
    return Status;
}

static
NTSTATUS
UvcReadConfiguration(
    PUVC_DEVICE Device)
{
    USB_CONFIGURATION_DESCRIPTOR Header;
    ULONG Length = sizeof(Device->DeviceDescriptor);
    NTSTATUS Status;

    Status = UvcGetDescriptor(Device, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, &Device->DeviceDescriptor, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Length = sizeof(Header);
    Status = UvcGetDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, &Header, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length < sizeof(Header) || Header.wTotalLength < sizeof(Header))
        return STATUS_DEVICE_DATA_ERROR;

    Device->Config = UvcAllocate(Device, Header.wTotalLength);
    if (!Device->Config)
        return STATUS_INSUFFICIENT_RESOURCES;

    Length = Header.wTotalLength;
    Status = UvcGetDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Device->Config, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length < sizeof(Header) || Device->Config->wTotalLength > Length)
        return STATUS_DEVICE_DATA_ERROR;

    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcSelectConfiguration(
    PUVC_DEVICE Device)
{
    PUSBD_INTERFACE_LIST_ENTRY List;
    PUSB_INTERFACE_DESCRIPTOR Interface;
    PURB Urb;
    ULONG Count = 0;
    NTSTATUS Status;

    List = ExAllocatePoolWithTag(NonPagedPool,
                                 (Device->Config->bNumInterfaces + 1) * sizeof(*List),
                                 LNTUVC_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(List, (Device->Config->bNumInterfaces + 1) * sizeof(*List));

    Interface = USBD_ParseConfigurationDescriptorEx(Device->Config, Device->Config, -1, 0, USB_DEVICE_CLASS_VIDEO, -1, -1);
    while (Interface && Count < Device->Config->bNumInterfaces)
    {
        List[Count++].InterfaceDescriptor = Interface;
        Interface = USBD_ParseConfigurationDescriptorEx(Device->Config,
                                                        (PUCHAR)Interface + Interface->bLength,
                                                        -1,
                                                        0,
                                                        USB_DEVICE_CLASS_VIDEO,
                                                        -1,
                                                        -1);
    }

    if (Count == 0)
    {
        ExFreePoolWithTag(List, LNTUVC_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Urb = USBD_CreateConfigurationRequestEx(Device->Config, List);
    if (!Urb)
    {
        ExFreePoolWithTag(List, LNTUVC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = UvcSubmitUrbSync(Device, Urb);
    if (NT_SUCCESS(Status))
    {
        ULONG Index, Pipe;

        Device->ConfigHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;
        Device->StatusPipe = NULL;
        for (Index = 0; Index < Count; Index++)
        {
            PUSBD_INTERFACE_INFORMATION Info = List[Index].Interface;

            if (!Info || Info->InterfaceNumber != Device->ControlInterface)
                continue;
            for (Pipe = 0; Pipe < Info->NumberOfPipes; Pipe++)
            {
                if (Info->Pipes[Pipe].PipeType == UsbdPipeTypeInterrupt)
                    Device->StatusPipe = Info->Pipes[Pipe].PipeHandle;
            }
        }
    }

    ExFreePoolWithTag(List, LNTUVC_TAG);
    ExFreePool(Urb);
    return Status;
}

static
VOID
UvcPublishFriendlyName(
    PUVC_DEVICE Device,
    const GUID *Category,
    PCWSTR Name)
{
    PWSTR Links = NULL, Link;
    UNICODE_STRING Value = RTL_CONSTANT_STRING(L"FriendlyName");

    if (!NT_SUCCESS(IoGetDeviceInterfaces(Category,
                                          Device->KsDevice->PhysicalDeviceObject,
                                          DEVICE_INTERFACE_INCLUDE_NONACTIVE,
                                          &Links)))
    {
        return;
    }

    for (Link = Links; *Link; Link += wcslen(Link) + 1)
    {
        UNICODE_STRING LinkName;
        HANDLE Key;

        RtlInitUnicodeString(&LinkName, Link);
        if (NT_SUCCESS(IoOpenDeviceInterfaceRegistryKey(&LinkName, KEY_SET_VALUE, &Key)))
        {
            ZwSetValueKey(Key, &Value, 0, REG_SZ, (PVOID)Name, (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR));
            ZwClose(Key);
        }
    }

    ExFreePool(Links);
}

static
VOID
UvcPublishNames(
    PUVC_DEVICE Device)
{
    UCHAR Buffer[256];
    PUSB_STRING_DESCRIPTOR Text = (PUSB_STRING_DESCRIPTOR)Buffer;
    WCHAR Name[128];
    ULONG Length = sizeof(Buffer);
    ULONG Chars;

    if (Device->DeviceDescriptor.iProduct == 0 ||
        !NT_SUCCESS(UvcGetDescriptor(Device,
                                     USB_STRING_DESCRIPTOR_TYPE,
                                     Device->DeviceDescriptor.iProduct,
                                     0x0409,
                                     Buffer,
                                     &Length)) ||
        Length < 4 || Text->bLength < 4)
    {
        return;
    }

    Chars = min(min(Length, Text->bLength) - 2, sizeof(Name) - sizeof(WCHAR)) / sizeof(WCHAR);
    RtlCopyMemory(Name, Text->bString, Chars * sizeof(WCHAR));
    Name[Chars] = UNICODE_NULL;

    UvcPublishFriendlyName(Device, &UVC_KSCATEGORY_CAPTURE, Name);
    UvcPublishFriendlyName(Device, &UVC_KSCATEGORY_VIDEO, Name);
    UvcPublishFriendlyName(Device, &UVC_KSCATEGORY_VIDEO_CAMERA, Name);
}

static
NTSTATUS
NTAPI
UvcPnpStart(
    PKSDEVICE KsDevice,
    PIRP Irp,
    PCM_RESOURCE_LIST Translated,
    PCM_RESOURCE_LIST Untranslated)
{
    PUVC_DEVICE Device = KsDevice->Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Translated);
    UNREFERENCED_PARAMETER(Untranslated);

    if (Device)
    {
        InterlockedExchange(&Device->Gone, 0);
        Status = UvcSelectConfiguration(Device);
        if (NT_SUCCESS(Status))
            UvcStatusStart(Device);
        return Status;
    }

    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device), LNTUVC_TAG);
    if (!Device)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Device, sizeof(*Device));
    Device->KsDevice = KsDevice;
    Device->LowerDevice = KsDevice->NextDeviceObject;
    KeInitializeMutex(&Device->ControlMutex, 0);
    KeInitializeSpinLock(&Device->StillLock);

    Status = KsAddItemToObjectBag(KsDevice->Bag, Device, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device, LNTUVC_TAG);
        return Status;
    }
    KsDevice->Context = Device;

    Status = UvcReadConfiguration(Device);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USB video function: reading the descriptors failed %lx\n", Status);
        return Status;
    }

    Status = UvcParseDescriptors(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcSelectConfiguration(Device);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USB video function: selecting the configuration failed %lx\n", Status);
        return Status;
    }

    Status = UvcBuildFilter(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = KsCreateFilterFactory(KsDevice->FunctionalDeviceObject,
                                   Device->FilterDescriptor,
                                   UvcReferenceString.Buffer,
                                   NULL,
                                   KSCREATE_ITEM_FREEONSTOP,
                                   NULL,
                                   NULL,
                                   &Device->FilterFactory);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USB video function: creating the filter failed %lx\n", Status);
        return Status;
    }

    UvcPublishNames(Device);
    UvcStatusStart(Device);
    return STATUS_SUCCESS;
}

static
VOID
UvcSignalDeviceLost(
    PKSDEVICE KsDevice)
{
    PKSFILTERFACTORY Factory;
    PKSFILTER Filter;

    KsAcquireDevice(KsDevice);
    for (Factory = KsGetFirstChild(KsDevice); Factory; Factory = KsGetNextSibling(Factory))
    {
        for (Filter = KsGetFirstChild(Factory); Filter; Filter = KsGetNextSibling(Filter))
            KsGenerateEvents(Filter, &UVC_KSEVENTSETID_Device, KSEVENT_DEVICE_LOST, 0, NULL, NULL, NULL);
    }
    KsReleaseDevice(KsDevice);
}

static
VOID
NTAPI
UvcPnpGone(
    PKSDEVICE KsDevice,
    PIRP Irp)
{
    PUVC_DEVICE Device = KsDevice->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (Device)
    {
        InterlockedExchange(&Device->Gone, 1);
        UvcStatusStop(Device);
    }
    UvcSignalDeviceLost(KsDevice);
}

static
VOID
NTAPI
UvcPnpRemove(
    PKSDEVICE KsDevice,
    PIRP Irp)
{
    PUVC_DEVICE Device = KsDevice->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (Device)
    {
        InterlockedExchange(&Device->Gone, 1);
        UvcStatusFree(Device);
    }
}

static
VOID
NTAPI
UvcPnpStop(
    PKSDEVICE KsDevice,
    PIRP Irp)
{
    PUVC_DEVICE Device = KsDevice->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (Device)
        UvcStatusStop(Device);
}

static
NTSTATUS
NTAPI
UvcPnpQueryCapabilities(
    PKSDEVICE KsDevice,
    PIRP Irp,
    PDEVICE_CAPABILITIES Capabilities)
{
    UNREFERENCED_PARAMETER(KsDevice);
    UNREFERENCED_PARAMETER(Irp);

    Capabilities->SurpriseRemovalOK = TRUE;
    return STATUS_SUCCESS;
}

static const KSDEVICE_DISPATCH UvcDeviceDispatch =
{
    NULL,
    UvcPnpStart,
    NULL,
    NULL,
    NULL,
    UvcPnpStop,
    NULL,
    NULL,
    UvcPnpRemove,
    UvcPnpQueryCapabilities,
    UvcPnpGone,
    NULL,
    NULL,
    NULL
};

static const KSDEVICE_DESCRIPTOR UvcDeviceDescriptor =
{
    &UvcDeviceDispatch,
    0,
    NULL
};

NTSTATUS
NTAPI
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    return KsInitializeDriver(DriverObject, RegistryPath, &UvcDeviceDescriptor);
}
