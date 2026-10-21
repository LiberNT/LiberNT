/*
 * PROJECT:     LiberNT Hyper-V Keyboard Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keyboard port driver for the Hyper-V synthetic keyboard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntddkbd.h>
#include <kbdmou.h>
#include <vmbusapi.h>

#define NDEBUG
#include <debug.h>

#define HYPERKBD_RING_SIZE  (32 * 1024)
#define HYPERKBD_PACKET_MAX 256
#define HYPERKBD_WAIT_MS    5000

#define SYNTHKBD_VERSION_WIN8           0x00010000
#define SYNTHKBD_PROTOCOL_REQUEST       1
#define SYNTHKBD_PROTOCOL_RESPONSE      2
#define SYNTHKBD_EVENT                  3
#define SYNTHKBD_SET_LED_INDICATORS     4

#define SYNTHKBD_IS_UNICODE  0x01
#define SYNTHKBD_IS_BREAK    0x02
#define SYNTHKBD_IS_E0       0x04
#define SYNTHKBD_IS_E1       0x08

typedef struct _LOCAL_KEYBOARD_INDICATOR_TRANSLATION
{
    USHORT NumberOfIndicatorKeys;
    INDICATOR_LIST IndicatorList[3];
} LOCAL_KEYBOARD_INDICATOR_TRANSLATION;

typedef struct _HYPERKBD_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    CONNECT_DATA ConnectData;
    KEYBOARD_ATTRIBUTES Attributes;
    KEYBOARD_INDICATOR_PARAMETERS Indicators;
    KEYBOARD_TYPEMATIC_PARAMETERS Typematic;
    KEVENT ProtocolEvent;
    BOOLEAN ProtocolAccepted;
    UCHAR Packet[HYPERKBD_PACKET_MAX];
} HYPERKBD_EXTENSION, *PHYPERKBD_EXTENSION;

static const LOCAL_KEYBOARD_INDICATOR_TRANSLATION HyperKbdIndicatorTranslation =
{
    3,
    {
        { 0x3A, KEYBOARD_CAPS_LOCK_ON },
        { 0x45, KEYBOARD_NUM_LOCK_ON },
        { 0x46, KEYBOARD_SCROLL_LOCK_ON },
    }
};

static
VOID
HyperKbdSendLeds(
    _In_ PHYPERKBD_EXTENSION Extension)
{
    ULONG Message[2];

    if (!Extension->Open)
        return;

    Message[0] = SYNTHKBD_SET_LED_INDICATORS;
    Message[1] = Extension->Indicators.LedFlags & 0xFFFF;
    VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0, Message, sizeof(Message));
}

static
VOID
NTAPI
HyperKbdChannelCallback(
    _In_opt_ PVOID Context)
{
    PHYPERKBD_EXTENSION Extension = Context;
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    PSERVICE_CALLBACK_ROUTINE ClassService;
    KEYBOARD_INPUT_DATA Input;
    PUCHAR Payload;
    ULONG Length;
    ULONG Offset;
    ULONG Type;
    ULONG Flags;
    ULONG Consumed;
    ULONG Zero = 0;
    NTSTATUS Status;

    if (Extension == NULL)
        return;

    for (;;)
    {
        Status = VmbChannelReceivePacket(Extension->Channel, Extension->Packet, sizeof(Extension->Packet), &Length);
        if (!NT_SUCCESS(Status))
            break;

        Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Extension->Packet;
        Offset = (ULONG)Descriptor->DataOffset8 * 8;
        if (Descriptor->Flags & VMBUS_PACKET_FLAG_COMPLETION_REQUESTED)
        {
            VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_COMPLETION, 0,
                                 Descriptor->TransactionId, &Zero, sizeof(Zero));
        }

        if (Descriptor->Type != VMBUS_PACKET_TYPE_INBAND || Offset + sizeof(ULONG) > Length)
            continue;

        Payload = Extension->Packet + Offset;
        RtlCopyMemory(&Type, Payload, sizeof(Type));

        if (Type == SYNTHKBD_PROTOCOL_RESPONSE && Offset + 2 * sizeof(ULONG) <= Length)
        {
            RtlCopyMemory(&Flags, Payload + 4, sizeof(Flags));
            Extension->ProtocolAccepted = (Flags & 1) != 0;
            KeSetEvent(&Extension->ProtocolEvent, IO_NO_INCREMENT, FALSE);
            continue;
        }

        if (Type != SYNTHKBD_EVENT || Offset + 3 * sizeof(ULONG) > Length)
            continue;

        RtlCopyMemory(&Flags, Payload + 8, sizeof(Flags));
        if (Flags & SYNTHKBD_IS_UNICODE)
            continue;

        RtlZeroMemory(&Input, sizeof(Input));
        RtlCopyMemory(&Input.MakeCode, Payload + 4, sizeof(Input.MakeCode));
        if (Flags & SYNTHKBD_IS_BREAK)
            Input.Flags |= KEY_BREAK;
        if (Flags & SYNTHKBD_IS_E0)
            Input.Flags |= KEY_E0;
        if (Flags & SYNTHKBD_IS_E1)
            Input.Flags |= KEY_E1;

        ClassService = (PSERVICE_CALLBACK_ROUTINE)Extension->ConnectData.ClassService;
        if (ClassService != NULL)
        {
            Consumed = 0;
            ClassService(Extension->ConnectData.ClassDeviceObject, &Input, &Input + 1, &Consumed);
        }
    }
}

static
NTSTATUS
HyperKbdStart(
    _Inout_ PHYPERKBD_EXTENSION Extension)
{
    LARGE_INTEGER Timeout;
    ULONG Request[2];
    NTSTATUS Status;

    Extension->Channel = VmbChannelFromDevice(Extension->Pdo);
    if (Extension->Channel == NULL)
        return STATUS_NO_SUCH_DEVICE;

    KeClearEvent(&Extension->ProtocolEvent);
    Extension->ProtocolAccepted = FALSE;

    Status = VmbChannelOpen(Extension->Channel, HYPERKBD_RING_SIZE, HyperKbdChannelCallback, Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERKBD: VmbChannelOpen failed with 0x%lx\n", Status);
        return Status;
    }
    Extension->Open = TRUE;

    Request[0] = SYNTHKBD_PROTOCOL_REQUEST;
    Request[1] = SYNTHKBD_VERSION_WIN8;
    Status = VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0, Request, sizeof(Request));
    if (NT_SUCCESS(Status))
    {
        Timeout.QuadPart = -10000LL * HYPERKBD_WAIT_MS;
        if (KeWaitForSingleObject(&Extension->ProtocolEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
            Status = STATUS_IO_TIMEOUT;
        else if (!Extension->ProtocolAccepted)
            Status = STATUS_REVISION_MISMATCH;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERKBD: protocol negotiation failed with 0x%lx\n", Status);
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }

    return Status;
}

static
VOID
HyperKbdStop(
    _Inout_ PHYPERKBD_EXTENSION Extension)
{
    if (Extension->Open)
    {
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }
}

static
NTSTATUS
HyperKbdComplete(
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
HyperKbdInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERKBD_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    PCONNECT_DATA ConnectData;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_KEYBOARD_CONNECT:
            if (InputLength < sizeof(CONNECT_DATA))
                return HyperKbdComplete(Irp, STATUS_INVALID_PARAMETER, 0);
            if (Extension->ConnectData.ClassService != NULL)
                return HyperKbdComplete(Irp, STATUS_SHARING_VIOLATION, 0);
            ConnectData = Stack->Parameters.DeviceIoControl.Type3InputBuffer;
            Extension->ConnectData = *ConnectData;
            return HyperKbdComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
            Extension->ConnectData.ClassService = NULL;
            Extension->ConnectData.ClassDeviceObject = NULL;
            return HyperKbdComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
            if (OutputLength < sizeof(KEYBOARD_ATTRIBUTES))
                return HyperKbdComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlCopyMemory(Buffer, &Extension->Attributes, sizeof(KEYBOARD_ATTRIBUTES));
            return HyperKbdComplete(Irp, STATUS_SUCCESS, sizeof(KEYBOARD_ATTRIBUTES));

        case IOCTL_KEYBOARD_QUERY_INDICATORS:
            if (OutputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
                return HyperKbdComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlCopyMemory(Buffer, &Extension->Indicators, sizeof(KEYBOARD_INDICATOR_PARAMETERS));
            return HyperKbdComplete(Irp, STATUS_SUCCESS, sizeof(KEYBOARD_INDICATOR_PARAMETERS));

        case IOCTL_KEYBOARD_SET_INDICATORS:
            if (InputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
                return HyperKbdComplete(Irp, STATUS_INVALID_PARAMETER, 0);
            RtlCopyMemory(&Extension->Indicators, Buffer, sizeof(KEYBOARD_INDICATOR_PARAMETERS));
            HyperKbdSendLeds(Extension);
            return HyperKbdComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
            if (OutputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS))
                return HyperKbdComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlCopyMemory(Buffer, &Extension->Typematic, sizeof(KEYBOARD_TYPEMATIC_PARAMETERS));
            return HyperKbdComplete(Irp, STATUS_SUCCESS, sizeof(KEYBOARD_TYPEMATIC_PARAMETERS));

        case IOCTL_KEYBOARD_SET_TYPEMATIC:
            if (InputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS))
                return HyperKbdComplete(Irp, STATUS_INVALID_PARAMETER, 0);
            RtlCopyMemory(&Extension->Typematic, Buffer, sizeof(KEYBOARD_TYPEMATIC_PARAMETERS));
            return HyperKbdComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
            if (OutputLength < sizeof(HyperKbdIndicatorTranslation))
                return HyperKbdComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlCopyMemory(Buffer, &HyperKbdIndicatorTranslation, sizeof(HyperKbdIndicatorTranslation));
            return HyperKbdComplete(Irp, STATUS_SUCCESS, sizeof(HyperKbdIndicatorTranslation));

        default:
            return HyperKbdComplete(Irp, STATUS_NOT_SUPPORTED, 0);
    }
}

static
NTSTATUS
NTAPI
HyperKbdPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERKBD_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = HyperKbdStart(Extension);
            return HyperKbdComplete(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            HyperKbdStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            HyperKbdStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HyperKbdPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERKBD_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HyperKbdPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERKBD_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HyperKbdCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return HyperKbdComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
HyperKbdAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PHYPERKBD_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(HYPERKBD_EXTENSION),
                            NULL,
                            FILE_DEVICE_KEYBOARD,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    Extension->Pdo = PhysicalDeviceObject;
    KeInitializeEvent(&Extension->ProtocolEvent, NotificationEvent, FALSE);

    Extension->Attributes.KeyboardIdentifier.Type = 4;
    Extension->Attributes.KeyboardMode = 1;
    Extension->Attributes.NumberOfFunctionKeys = 12;
    Extension->Attributes.NumberOfIndicators = 3;
    Extension->Attributes.NumberOfKeysTotal = 101;
    Extension->Attributes.InputDataQueueLength = 100;
    Extension->Attributes.KeyRepeatMinimum.UnitId = 0;
    Extension->Attributes.KeyRepeatMinimum.Rate = 2;
    Extension->Attributes.KeyRepeatMinimum.Delay = 250;
    Extension->Attributes.KeyRepeatMaximum.UnitId = 0;
    Extension->Attributes.KeyRepeatMaximum.Rate = 30;
    Extension->Attributes.KeyRepeatMaximum.Delay = 1000;
    Extension->Typematic.Rate = 30;
    Extension->Typematic.Delay = 250;

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (Extension->LowerDevice == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HyperKbdUnload(
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

    DriverObject->DriverExtension->AddDevice = HyperKbdAddDevice;
    DriverObject->DriverUnload = HyperKbdUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = HyperKbdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HyperKbdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HyperKbdInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HyperKbdPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HyperKbdPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HyperKbdPassThrough;
    return STATUS_SUCCESS;
}
