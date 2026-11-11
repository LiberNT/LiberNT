/*
 * PROJECT:     LiberNT WinUSB driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Plug and Play, configuration and device interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb.h"

#define NDEBUG
#include <debug.h>

static IO_COMPLETION_ROUTINE WinUsbSyncComplete;

static
NTSTATUS
NTAPI
WinUsbSyncComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
WinUsbForwardSync(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PIRP Irp)
{
    KEVENT Event;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, WinUsbSyncComplete, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Device->Lower, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return Irp->IoStatus.Status;
}

NTSTATUS
WinUsbSubmitUrbSync(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PURB Urb)
{
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Irp = IoAllocateIrp(Device->Lower->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(Irp, WinUsbSyncComplete, &Event, TRUE, TRUE, TRUE);

    if (IoCallDriver(Device->Lower, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

static
NTSTATUS
WinUsbGetDeviceDescriptor(
    _In_ PWINUSB_DEVICE Device,
    _In_ UCHAR Type,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Transferred)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST Urb;
    NTSTATUS Status;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest((PURB)&Urb, sizeof(Urb), Type, 0, 0, Buffer, NULL, Length, NULL);
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    *Transferred = NT_SUCCESS(Status) ? Urb.TransferBufferLength : 0;
    return Status;
}

static
VOID
WinUsbQuerySpeed(
    _In_ PWINUSB_DEVICE Device)
{
    USB_BUS_INTERFACE_USBDI_V1 BusInterface;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Device->Speed = LowSpeed;
    Irp = IoAllocateIrp(Device->Lower->StackSize, FALSE);
    if (!Irp)
        return;

    RtlZeroMemory(&BusInterface, sizeof(BusInterface));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_PNP;
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType = &USB_BUS_INTERFACE_USBDI_GUID;
    IoStack->Parameters.QueryInterface.Size = sizeof(BusInterface);
    IoStack->Parameters.QueryInterface.Version = USB_BUSIF_USBDI_VERSION_1;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)&BusInterface;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    IoSetCompletionRoutine(Irp, WinUsbSyncComplete, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Device->Lower, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: no USBDI interface: %lx\n", Status);
        return;
    }

    if (BusInterface.IsDeviceHighSpeed && BusInterface.IsDeviceHighSpeed(BusInterface.BusContext))
        Device->Speed = HighSpeed;
    if (BusInterface.InterfaceDereference)
        BusInterface.InterfaceDereference(BusInterface.BusContext);
}

static
VOID
WinUsbReadPowerDefaults(
    _In_ PWINUSB_DEVICE Device)
{
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name;
    HANDLE Key;
    ULONG Length;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Device->DefaultAutoSuspend = FALSE;
    Device->DefaultSuspendDelay = 0;
    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Device->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Key)))
        return;

    RtlInitUnicodeString(&Name, L"DefaultIdleState");
    if (NT_SUCCESS(ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Value, sizeof(Buffer), &Length)) &&
        Value->Type == REG_DWORD && Value->DataLength == sizeof(ULONG))
    {
        Device->DefaultAutoSuspend = *(PULONG)Value->Data != 0;
    }
    RtlInitUnicodeString(&Name, L"DefaultIdleTimeout");
    if (NT_SUCCESS(ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Value, sizeof(Buffer), &Length)) &&
        Value->Type == REG_DWORD && Value->DataLength == sizeof(ULONG))
    {
        Device->DefaultSuspendDelay = *(PULONG)Value->Data;
    }
    ZwClose(Key);
}

static
VOID
WinUsbRegisterGuid(
    _In_ PWINUSB_DEVICE Device,
    _In_ PCWSTR Text,
    _In_ SIZE_T Characters)
{
    UNICODE_STRING String;
    GUID Guid;
    NTSTATUS Status;

    if (Device->InterfaceNameCount >= WINUSB_MAX_INTERFACE_GUIDS || Characters > UNICODE_STRING_MAX_CHARS)
        return;

    String.Buffer = (PWSTR)Text;
    String.Length = String.MaximumLength = (USHORT)(Characters * sizeof(WCHAR));
    Status = RtlGUIDFromString(&String, &Guid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: bad interface GUID %wZ\n", &String);
        return;
    }

    Status = IoRegisterDeviceInterface(Device->Pdo, &Guid, NULL,
                                       &Device->InterfaceNames[Device->InterfaceNameCount]);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: IoRegisterDeviceInterface(%wZ) failed: %lx\n", &String, Status);
        return;
    }
    Device->InterfaceNameCount++;
}

static
VOID
WinUsbRegisterInterfaces(
    _In_ PWINUSB_DEVICE Device)
{
    static PCWSTR Names[] = { L"DeviceInterfaceGUIDs", L"DeviceInterfaceGUID" };
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    UNICODE_STRING Name;
    ULONG Length, Index;
    PCWSTR Text, End;
    HANDLE Key;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Device->InterfaceNameCount == 0);

    Status = IoOpenDeviceRegistryKey(Device->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Key);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: no device key: %lx\n", Status);
        return;
    }

    Length = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 1024 * sizeof(WCHAR);
    Value = ExAllocatePoolWithTag(PagedPool, Length + 2 * sizeof(WCHAR), WINUSB_TAG);
    if (!Value)
    {
        ZwClose(Key);
        return;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Names) && !Device->InterfaceNameCount; Index++)
    {
        RtlInitUnicodeString(&Name, Names[Index]);
        RtlZeroMemory(Value, Length + 2 * sizeof(WCHAR));
        Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Value, Length, &Length);
        Length = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 1024 * sizeof(WCHAR);
        if (!NT_SUCCESS(Status) || (Value->Type != REG_MULTI_SZ && Value->Type != REG_SZ))
            continue;

        Text = (PCWSTR)Value->Data;
        End = Text + Value->DataLength / sizeof(WCHAR);
        while (Text < End && *Text)
        {
            SIZE_T Characters = 0;

            while (Text + Characters < End && Text[Characters])
                Characters++;

            WinUsbRegisterGuid(Device, Text, Characters);
            if (Value->Type == REG_SZ)
                break;
            Text += Characters + 1;
        }
    }

    ExFreePoolWithTag(Value, WINUSB_TAG);
    ZwClose(Key);

    if (!Device->InterfaceNameCount)
        DPRINT1("WinUSB: the device has no DeviceInterfaceGUIDs\n");
}

static
VOID
WinUsbSetInterfaces(
    _In_ PWINUSB_DEVICE Device,
    _In_ BOOLEAN Enable)
{
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NTSTATUS Status;

    for (Index = 0; Index < Device->InterfaceNameCount; Index++)
    {
        Status = IoSetDeviceInterfaceState(&Device->InterfaceNames[Index], Enable);
        if (!NT_SUCCESS(Status))
            DPRINT1("WinUSB: interface %wZ state %u failed: %lx\n", &Device->InterfaceNames[Index], Enable, Status);
    }
}

static
VOID
WinUsbFreeInterfaces(
    _In_ PWINUSB_DEVICE Device)
{
    ULONG Index;

    for (Index = 0; Index < Device->InterfaceNameCount; Index++)
        RtlFreeUnicodeString(&Device->InterfaceNames[Index]);
    Device->InterfaceNameCount = 0;
}

VOID
WinUsbLoadPipes(
    _Inout_ PWINUSB_INTERFACE Interface,
    _In_ PUSBD_INTERFACE_INFORMATION Info)
{
    ULONG Index;

    ASSERT(Info->NumberOfPipes <= WINUSB_MAX_PIPES);

    Interface->Number = Info->InterfaceNumber;
    Interface->Setting = Info->AlternateSetting;
    Interface->PipeCount = (UCHAR)Info->NumberOfPipes;
    for (Index = 0; Index < Info->NumberOfPipes; Index++)
    {
        PWINUSB_PIPE Pipe = &Interface->Pipes[Index];

        ASSERT(Pipe->Queue.Flink == NULL || IsListEmpty(&Pipe->Queue));
        ASSERT(Pipe->ActiveList.Flink == NULL || IsListEmpty(&Pipe->ActiveList));
        RtlZeroMemory(Pipe, sizeof(*Pipe));
        InitializeListHead(&Pipe->Queue);
        InitializeListHead(&Pipe->ActiveList);
        Pipe->EndpointAddress = Info->Pipes[Index].EndpointAddress;
        Pipe->Interval = Info->Pipes[Index].Interval;
        Pipe->MaximumPacketSize = Info->Pipes[Index].MaximumPacketSize;
        Pipe->Type = Info->Pipes[Index].PipeType;
        Pipe->Handle = Info->Pipes[Index].PipeHandle;
        WinUsbResetPipePolicy(Pipe);
    }
}

NTSTATUS
WinUsbConfigure(
    _In_ PWINUSB_DEVICE Device)
{
    PUSBD_INTERFACE_LIST_ENTRY List;
    USB_CONFIGURATION_DESCRIPTOR Header;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    ULONG Transferred, Count = 0, Index;
    NTSTATUS Status;
    PURB Urb;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Device->Configuration == NULL);

    Status = WinUsbGetDeviceDescriptor(Device, USB_DEVICE_DESCRIPTOR_TYPE, &Device->DeviceDescriptor,
                                       sizeof(Device->DeviceDescriptor), &Transferred);
    if (!NT_SUCCESS(Status) || Transferred != sizeof(Device->DeviceDescriptor))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;

    Status = WinUsbGetDeviceDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, &Header, sizeof(Header),
                                       &Transferred);
    if (!NT_SUCCESS(Status) || Transferred != sizeof(Header) || Header.wTotalLength < sizeof(Header))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;

    Configuration = ExAllocatePoolWithTag(NonPagedPool, Header.wTotalLength, WINUSB_TAG);
    if (!Configuration)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = WinUsbGetDeviceDescriptor(Device, USB_CONFIGURATION_DESCRIPTOR_TYPE, Configuration,
                                       Header.wTotalLength, &Transferred);
    if (!NT_SUCCESS(Status) || Transferred != Header.wTotalLength)
    {
        ExFreePoolWithTag(Configuration, WINUSB_TAG);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }

    List = ExAllocatePoolZero(NonPagedPool, (LNT_WINUSB_MAX_INTERFACES + 1) * sizeof(*List), WINUSB_TAG);
    if (!List)
    {
        ExFreePoolWithTag(Configuration, WINUSB_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Descriptor = (PUSB_INTERFACE_DESCRIPTOR)Configuration;
    while (Count < LNT_WINUSB_MAX_INTERFACES &&
           (Descriptor = USBD_ParseConfigurationDescriptorEx(Configuration,
                                                             (PUCHAR)Descriptor + Descriptor->bLength,
                                                             -1, 0, -1, -1, -1)) != NULL)
    {
        List[Count++].InterfaceDescriptor = Descriptor;
    }

    if (!Count)
    {
        ExFreePoolWithTag(List, WINUSB_TAG);
        ExFreePoolWithTag(Configuration, WINUSB_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Urb = USBD_CreateConfigurationRequestEx(Configuration, List);
    if (!Urb)
    {
        ExFreePoolWithTag(List, WINUSB_TAG);
        ExFreePoolWithTag(Configuration, WINUSB_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (Index = 0; Index < Count; Index++)
    {
        PUSBD_INTERFACE_INFORMATION Info = List[Index].Interface;
        ULONG Pipe;

        for (Pipe = 0; Pipe < Info->NumberOfPipes; Pipe++)
            Info->Pipes[Pipe].MaximumTransferSize = LNT_WINUSB_MAXIMUM_TRANSFER_SIZE;
    }

    Status = WinUsbSubmitUrbSync(Device, Urb);
    if (NT_SUCCESS(Status))
    {
        Device->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;
        for (Index = 0; Index < Count; Index++)
        {
            if (List[Index].Interface->NumberOfPipes > WINUSB_MAX_PIPES)
            {
                Status = STATUS_DEVICE_CONFIGURATION_ERROR;
                break;
            }
            WinUsbLoadPipes(&Device->Interfaces[Index], List[Index].Interface);
        }
        Device->InterfaceCount = (UCHAR)Count;
    }
    else
    {
        DPRINT1("WinUSB: select configuration failed: %lx\n", Status);
    }

    ExFreePool(Urb);
    ExFreePoolWithTag(List, WINUSB_TAG);
    if (!NT_SUCCESS(Status))
    {
        Device->InterfaceCount = 0;
        ExFreePoolWithTag(Configuration, WINUSB_TAG);
        return Status;
    }

    Device->Configuration = Configuration;
    WinUsbQuerySpeed(Device);
    WinUsbReadPowerDefaults(Device);
    Device->AutoSuspend = Device->DefaultAutoSuspend;
    Device->SuspendDelay = Device->DefaultSuspendDelay;
    return STATUS_SUCCESS;
}

VOID
WinUsbDeconfigure(
    _In_ PWINUSB_DEVICE Device)
{
    ULONG Interface, Pipe;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    for (Interface = 0; Interface < Device->InterfaceCount; Interface++)
    {
        for (Pipe = 0; Pipe < Device->Interfaces[Interface].PipeCount; Pipe++)
        {
            PWINUSB_PIPE Entry = &Device->Interfaces[Interface].Pipes[Pipe];

            ASSERT(IsListEmpty(&Entry->Queue));
            ASSERT(Entry->Active == 0);
            if (Entry->Saved)
                ExFreePoolWithTag(Entry->Saved, WINUSB_TAG);
            Entry->Saved = NULL;
        }
    }
    Device->InterfaceCount = 0;
    if (Device->Configuration)
        ExFreePoolWithTag(Device->Configuration, WINUSB_TAG);
    Device->Configuration = NULL;
}

static
VOID
WinUsbStop(
    _In_ PWINUSB_DEVICE Device)
{
    ULONG Interface, Pipe;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (!Device->Started)
        return;
    Device->Started = FALSE;
    WinUsbSetInterfaces(Device, FALSE);
    for (Interface = 0; Interface < Device->InterfaceCount; Interface++)
    {
        for (Pipe = 0; Pipe < Device->Interfaces[Interface].PipeCount; Pipe++)
            WinUsbAbortPipe(Device, &Device->Interfaces[Interface].Pipes[Pipe]);
    }
    WinUsbCancelFileRequests(Device, NULL);
}

static
NTSTATUS
WinUsbStartDevice(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;

    Status = WinUsbForwardSync(Device, Irp);
    if (!NT_SUCCESS(Status))
        return Status;

    KeWaitForSingleObject(&Device->ConfigurationMutex, Executive, KernelMode, FALSE, NULL);
    WinUsbDeconfigure(Device);
    Status = WinUsbConfigure(Device);
    KeReleaseMutex(&Device->ConfigurationMutex, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: configuration failed: %lx\n", Status);
        return Status;
    }

    if (!Device->InterfaceNameCount)
        WinUsbRegisterInterfaces(Device);
    Device->Gone = FALSE;
    Device->Started = TRUE;
    WinUsbSetInterfaces(Device, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
WinUsbDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = WinUsbStartDevice(Device, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&Device->RemoveLock, Irp);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            WinUsbStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            Device->Gone = TRUE;
            WinUsbStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Device->Gone = TRUE;
            WinUsbStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Device->Lower, Irp);
            IoReleaseRemoveLockAndWait(&Device->RemoveLock, Irp);
            KeWaitForSingleObject(&Device->ConfigurationMutex, Executive, KernelMode, FALSE, NULL);
            WinUsbDeconfigure(Device);
            KeReleaseMutex(&Device->ConfigurationMutex, FALSE);
            WinUsbFreeInterfaces(Device);
            IoDetachDevice(Device->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Device->Lower, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}
