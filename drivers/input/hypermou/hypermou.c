/*
 * PROJECT:     LiberNT Hyper-V Mouse Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mouse port driver for the Hyper-V synthetic HID mouse
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntddmou.h>
#include <kbdmou.h>
#include <vmbusapi.h>

#define NDEBUG
#include <debug.h>

#define HYPERMOU_RING_SIZE  (32 * 1024)
#define HYPERMOU_PACKET_MAX 1024
#define HYPERMOU_WAIT_MS    5000
#define HYPERMOU_BUTTONS    5
#define HYPERMOU_WHEEL_DELTA 120

#define PIPE_MESSAGE_DATA 1

#define SYNTHHID_VERSION                2
#define SYNTHHID_PROTOCOL_REQUEST       0
#define SYNTHHID_PROTOCOL_RESPONSE      1
#define SYNTHHID_INIT_DEVICE_INFO       2
#define SYNTHHID_INIT_DEVICE_INFO_ACK   3
#define SYNTHHID_INPUT_REPORT           4

#define SYNTHHID_ATTRIBUTES_SIZE 32
#define SYNTHHID_DESCRIPTOR_SIZE 9

typedef struct _HYPERMOU_FIELD
{
    ULONG BitOffset;
    ULONG BitSize;
    LONG LogicalMinimum;
    LONG LogicalMaximum;
    BOOLEAN Present;
    BOOLEAN Relative;
} HYPERMOU_FIELD, *PHYPERMOU_FIELD;

typedef struct _HYPERMOU_LAYOUT
{
    HYPERMOU_FIELD X;
    HYPERMOU_FIELD Y;
    HYPERMOU_FIELD Wheel;
    HYPERMOU_FIELD Buttons[HYPERMOU_BUTTONS];
    BOOLEAN ReportId;
} HYPERMOU_LAYOUT, *PHYPERMOU_LAYOUT;

typedef struct _HYPERMOU_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    CONNECT_DATA ConnectData;
    MOUSE_ATTRIBUTES Attributes;
    KEVENT ProtocolEvent;
    KEVENT DeviceInfoEvent;
    BOOLEAN ProtocolAccepted;
    HYPERMOU_LAYOUT Layout;
    ULONG ButtonState;
    UCHAR Packet[HYPERMOU_PACKET_MAX];
} HYPERMOU_EXTENSION, *PHYPERMOU_EXTENSION;

static const USHORT HyperMouButtonDown[HYPERMOU_BUTTONS] =
{
    MOUSE_LEFT_BUTTON_DOWN, MOUSE_RIGHT_BUTTON_DOWN, MOUSE_MIDDLE_BUTTON_DOWN,
    MOUSE_BUTTON_4_DOWN, MOUSE_BUTTON_5_DOWN
};

static const USHORT HyperMouButtonUp[HYPERMOU_BUTTONS] =
{
    MOUSE_LEFT_BUTTON_UP, MOUSE_RIGHT_BUTTON_UP, MOUSE_MIDDLE_BUTTON_UP,
    MOUSE_BUTTON_4_UP, MOUSE_BUTTON_5_UP
};

static
VOID
HyperMouDefaultLayout(
    _Out_ PHYPERMOU_LAYOUT Layout)
{
    ULONG Index;

    RtlZeroMemory(Layout, sizeof(*Layout));
    for (Index = 0; Index < HYPERMOU_BUTTONS; Index++)
    {
        Layout->Buttons[Index].BitOffset = Index;
        Layout->Buttons[Index].BitSize = 1;
        Layout->Buttons[Index].LogicalMaximum = 1;
        Layout->Buttons[Index].Present = TRUE;
    }

    Layout->X.BitOffset = 8;
    Layout->X.BitSize = 16;
    Layout->X.LogicalMaximum = 0x7FFF;
    Layout->X.Present = TRUE;
    Layout->Y.BitOffset = 24;
    Layout->Y.BitSize = 16;
    Layout->Y.LogicalMaximum = 0x7FFF;
    Layout->Y.Present = TRUE;
    Layout->Wheel.BitOffset = 40;
    Layout->Wheel.BitSize = 16;
    Layout->Wheel.LogicalMinimum = -0x7FFF;
    Layout->Wheel.LogicalMaximum = 0x7FFF;
    Layout->Wheel.Present = TRUE;
    Layout->Wheel.Relative = TRUE;
}

static
LONG
HyperMouSignExtend(
    _In_ ULONG Value,
    _In_ ULONG Size)
{
    if (Size == 0 || Size >= 32)
        return (LONG)Value;
    if (Value & (1UL << (Size - 1)))
        return (LONG)(Value | ~((1UL << Size) - 1));
    return (LONG)Value;
}

static
BOOLEAN
HyperMouParseDescriptor(
    _In_reads_bytes_(Length) const UCHAR *Descriptor,
    _In_ ULONG Length,
    _Out_ PHYPERMOU_LAYOUT Layout)
{
    ULONG Usages[16];
    ULONG UsageCount = 0;
    ULONG UsageMinimum = 0;
    ULONG UsageMaximum = 0;
    BOOLEAN UsageRange = FALSE;
    ULONG UsagePage = 0;
    ULONG ReportSize = 0;
    ULONG ReportCount = 0;
    LONG LogicalMinimum = 0;
    LONG LogicalMaximum = 0;
    ULONG BitOffset = 0;
    ULONG Offset = 0;
    ULONG Field;
    ULONG Usage;
    ULONG Page;
    ULONG Data;
    ULONG Size;
    UCHAR Prefix;
    UCHAR Tag;
    UCHAR Type;
    PHYPERMOU_FIELD Target;

    RtlZeroMemory(Layout, sizeof(*Layout));

    while (Offset < Length)
    {
        Prefix = Descriptor[Offset++];
        if (Prefix == 0xFE)
            return FALSE;

        Size = Prefix & 3;
        if (Size == 3)
            Size = 4;
        if (Offset + Size > Length)
            return FALSE;

        Data = 0;
        RtlCopyMemory(&Data, Descriptor + Offset, Size);
        Offset += Size;
        Type = (Prefix >> 2) & 3;
        Tag = Prefix >> 4;

        if (Type == 1)
        {
            switch (Tag)
            {
                case 0x0: UsagePage = Data; break;
                case 0x1: LogicalMinimum = HyperMouSignExtend(Data, Size * 8); break;
                case 0x2: LogicalMaximum = (LogicalMinimum < 0) ? HyperMouSignExtend(Data, Size * 8) : (LONG)Data; break;
                case 0x7: ReportSize = Data; break;
                case 0x8: Layout->ReportId = TRUE; BitOffset = 8; break;
                case 0x9: ReportCount = Data; break;
                default: break;
            }
        }
        else if (Type == 2)
        {
            switch (Tag)
            {
                case 0x0:
                    if (UsageCount < RTL_NUMBER_OF(Usages))
                        Usages[UsageCount++] = (Size == 4) ? Data : ((UsagePage << 16) | Data);
                    break;
                case 0x1: UsageMinimum = Data; UsageRange = TRUE; break;
                case 0x2: UsageMaximum = Data; UsageRange = TRUE; break;
                default: break;
            }
        }
        else if (Type == 0)
        {
            if (Tag == 0x8)
            {
                for (Field = 0; Field < ReportCount; Field++)
                {
                    if (Data & 1)
                    {
                        BitOffset += ReportSize;
                        continue;
                    }

                    if (UsageRange)
                        Usage = (UsagePage << 16) | min(UsageMinimum + Field, UsageMaximum);
                    else if (UsageCount != 0)
                        Usage = Usages[min(Field, UsageCount - 1)];
                    else
                        Usage = 0;

                    Page = Usage >> 16;
                    Target = NULL;
                    if (Page == 0x01 && (Usage & 0xFFFF) == 0x30)
                        Target = &Layout->X;
                    else if (Page == 0x01 && (Usage & 0xFFFF) == 0x31)
                        Target = &Layout->Y;
                    else if (Page == 0x01 && (Usage & 0xFFFF) == 0x38)
                        Target = &Layout->Wheel;
                    else if (Page == 0x09 && (Usage & 0xFFFF) >= 1 && (Usage & 0xFFFF) <= HYPERMOU_BUTTONS)
                        Target = &Layout->Buttons[(Usage & 0xFFFF) - 1];

                    if (Target != NULL && !Target->Present && ReportSize != 0 && ReportSize <= 32)
                    {
                        Target->BitOffset = BitOffset;
                        Target->BitSize = ReportSize;
                        Target->LogicalMinimum = LogicalMinimum;
                        Target->LogicalMaximum = LogicalMaximum;
                        Target->Relative = (Data & 4) != 0;
                        Target->Present = TRUE;
                    }

                    BitOffset += ReportSize;
                }
            }

            if (Tag == 0x8 || Tag == 0x9 || Tag == 0xB)
            {
                UsageCount = 0;
                UsageRange = FALSE;
                UsageMinimum = 0;
                UsageMaximum = 0;
            }
        }
    }

    return Layout->X.Present && Layout->Y.Present && !Layout->X.Relative && !Layout->Y.Relative &&
           Layout->X.LogicalMaximum > Layout->X.LogicalMinimum &&
           Layout->Y.LogicalMaximum > Layout->Y.LogicalMinimum;
}

static
BOOLEAN
HyperMouReadField(
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length,
    _In_ const HYPERMOU_FIELD *Field,
    _Out_ PLONG Value)
{
    ULONG Bit;
    ULONG Raw = 0;

    if (!Field->Present || Field->BitOffset + Field->BitSize > Length * 8)
        return FALSE;

    for (Bit = 0; Bit < Field->BitSize; Bit++)
    {
        ULONG Position = Field->BitOffset + Bit;
        if (Report[Position / 8] & (1 << (Position % 8)))
            Raw |= 1UL << Bit;
    }

    *Value = (Field->LogicalMinimum < 0) ? HyperMouSignExtend(Raw, Field->BitSize) : (LONG)Raw;
    return TRUE;
}

static
ULONG
HyperMouScale(
    _In_ LONG Value,
    _In_ const HYPERMOU_FIELD *Field)
{
    LONG64 Range = (LONG64)Field->LogicalMaximum - Field->LogicalMinimum;
    LONG64 Position = (LONG64)Value - Field->LogicalMinimum;

    if (Position < 0)
        Position = 0;
    if (Position > Range)
        Position = Range;

    return (ULONG)((Position * 0xFFFF) / Range);
}

static
VOID
HyperMouSendPipe(
    _In_ PHYPERMOU_EXTENSION Extension,
    _In_ ULONG Type,
    _In_reads_bytes_(Length) const VOID *Payload,
    _In_ ULONG Length)
{
    UCHAR Message[32];
    ULONG Header[4];

    if (Length > sizeof(Message) - sizeof(Header))
        return;

    Header[0] = PIPE_MESSAGE_DATA;
    Header[1] = 2 * sizeof(ULONG) + Length;
    Header[2] = Type;
    Header[3] = Length;
    RtlCopyMemory(Message, Header, sizeof(Header));
    RtlCopyMemory(Message + sizeof(Header), Payload, Length);
    VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0, Message, sizeof(Header) + Length);
}

static
VOID
HyperMouHandleDeviceInfo(
    _Inout_ PHYPERMOU_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Info,
    _In_ ULONG Length)
{
    USHORT DescriptorLength;
    UCHAR Ack = 1;

    HyperMouDefaultLayout(&Extension->Layout);

    if (Length >= SYNTHHID_ATTRIBUTES_SIZE + SYNTHHID_DESCRIPTOR_SIZE)
    {
        RtlCopyMemory(&DescriptorLength, Info + SYNTHHID_ATTRIBUTES_SIZE + 7, sizeof(DescriptorLength));
        if (DescriptorLength != 0 &&
            SYNTHHID_ATTRIBUTES_SIZE + SYNTHHID_DESCRIPTOR_SIZE + DescriptorLength <= Length &&
            !HyperMouParseDescriptor(Info + SYNTHHID_ATTRIBUTES_SIZE + SYNTHHID_DESCRIPTOR_SIZE,
                                     DescriptorLength,
                                     &Extension->Layout))
        {
            DPRINT1("HYPERMOU: unexpected report descriptor, using the default layout\n");
            HyperMouDefaultLayout(&Extension->Layout);
        }
    }

    HyperMouSendPipe(Extension, SYNTHHID_INIT_DEVICE_INFO_ACK, &Ack, sizeof(Ack));
    KeSetEvent(&Extension->DeviceInfoEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
HyperMouHandleReport(
    _Inout_ PHYPERMOU_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PHYPERMOU_LAYOUT Layout = &Extension->Layout;
    PSERVICE_CALLBACK_ROUTINE ClassService;
    MOUSE_INPUT_DATA Input;
    ULONG Buttons = 0;
    ULONG Changed;
    ULONG Index;
    ULONG Consumed;
    LONG Value;
    LONG X;
    LONG Y;

    if (!HyperMouReadField(Report, Length, &Layout->X, &X) ||
        !HyperMouReadField(Report, Length, &Layout->Y, &Y))
    {
        return;
    }

    for (Index = 0; Index < HYPERMOU_BUTTONS; Index++)
    {
        if (HyperMouReadField(Report, Length, &Layout->Buttons[Index], &Value) && Value != 0)
            Buttons |= 1UL << Index;
    }

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Flags = MOUSE_MOVE_ABSOLUTE;
    Input.LastX = (LONG)HyperMouScale(X, &Layout->X);
    Input.LastY = (LONG)HyperMouScale(Y, &Layout->Y);

    Changed = Buttons ^ Extension->ButtonState;
    for (Index = 0; Index < HYPERMOU_BUTTONS; Index++)
    {
        if (Changed & (1UL << Index))
            Input.ButtonFlags |= (Buttons & (1UL << Index)) ? HyperMouButtonDown[Index] : HyperMouButtonUp[Index];
    }
    Extension->ButtonState = Buttons;

    if (HyperMouReadField(Report, Length, &Layout->Wheel, &Value) && Value != 0)
    {
        if (Value > -(HYPERMOU_WHEEL_DELTA / 2) && Value < HYPERMOU_WHEEL_DELTA / 2)
            Value *= HYPERMOU_WHEEL_DELTA;
        Input.ButtonFlags |= MOUSE_WHEEL;
        Input.ButtonData = (USHORT)(SHORT)Value;
    }

    ClassService = (PSERVICE_CALLBACK_ROUTINE)Extension->ConnectData.ClassService;
    if (ClassService != NULL)
    {
        Consumed = 0;
        ClassService(Extension->ConnectData.ClassDeviceObject, &Input, &Input + 1, &Consumed);
    }
}

static
VOID
NTAPI
HyperMouChannelCallback(
    _In_opt_ PVOID Context)
{
    PHYPERMOU_EXTENSION Extension = Context;
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    PUCHAR Payload;
    ULONG Length;
    ULONG Offset;
    ULONG Header[4];
    ULONG Available;
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

        if (Descriptor->Type != VMBUS_PACKET_TYPE_INBAND || Offset + sizeof(Header) > Length)
            continue;

        Payload = Extension->Packet + Offset;
        RtlCopyMemory(Header, Payload, sizeof(Header));
        if (Header[0] != PIPE_MESSAGE_DATA || Header[1] < 2 * sizeof(ULONG) || Header[1] > Length - Offset - 2 * sizeof(ULONG))
            continue;

        Available = min(Header[3], Header[1] - 2 * sizeof(ULONG));
        Payload += sizeof(Header);

        switch (Header[2])
        {
            case SYNTHHID_PROTOCOL_RESPONSE:
                if (Available >= 5)
                {
                    Extension->ProtocolAccepted = Payload[4] != 0;
                    KeSetEvent(&Extension->ProtocolEvent, IO_NO_INCREMENT, FALSE);
                }
                break;

            case SYNTHHID_INIT_DEVICE_INFO:
                HyperMouHandleDeviceInfo(Extension, Payload, Available);
                break;

            case SYNTHHID_INPUT_REPORT:
                HyperMouHandleReport(Extension, Payload, Available);
                break;

            default:
                break;
        }
    }
}

static
NTSTATUS
HyperMouStart(
    _Inout_ PHYPERMOU_EXTENSION Extension)
{
    LARGE_INTEGER Timeout;
    ULONG Version = SYNTHHID_VERSION << 16;
    NTSTATUS Status;

    Extension->Channel = VmbChannelFromDevice(Extension->Pdo);
    if (Extension->Channel == NULL)
        return STATUS_NO_SUCH_DEVICE;

    KeClearEvent(&Extension->ProtocolEvent);
    KeClearEvent(&Extension->DeviceInfoEvent);
    Extension->ProtocolAccepted = FALSE;
    Extension->ButtonState = 0;
    HyperMouDefaultLayout(&Extension->Layout);

    Status = VmbChannelOpen(Extension->Channel, HYPERMOU_RING_SIZE, HyperMouChannelCallback, Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERMOU: VmbChannelOpen failed with 0x%lx\n", Status);
        return Status;
    }
    Extension->Open = TRUE;

    HyperMouSendPipe(Extension, SYNTHHID_PROTOCOL_REQUEST, &Version, sizeof(Version));

    Timeout.QuadPart = -10000LL * HYPERMOU_WAIT_MS;
    if (KeWaitForSingleObject(&Extension->ProtocolEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        Status = STATUS_IO_TIMEOUT;
    else if (!Extension->ProtocolAccepted)
        Status = STATUS_REVISION_MISMATCH;
    else if (KeWaitForSingleObject(&Extension->DeviceInfoEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        Status = STATUS_IO_TIMEOUT;

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERMOU: protocol negotiation failed with 0x%lx\n", Status);
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }

    return Status;
}

static
VOID
HyperMouStop(
    _Inout_ PHYPERMOU_EXTENSION Extension)
{
    if (Extension->Open)
    {
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }
}

static
NTSTATUS
HyperMouComplete(
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
HyperMouInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERMOU_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PCONNECT_DATA ConnectData;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_MOUSE_CONNECT:
            if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA))
                return HyperMouComplete(Irp, STATUS_INVALID_PARAMETER, 0);
            if (Extension->ConnectData.ClassService != NULL)
                return HyperMouComplete(Irp, STATUS_SHARING_VIOLATION, 0);
            ConnectData = Stack->Parameters.DeviceIoControl.Type3InputBuffer;
            Extension->ConnectData = *ConnectData;
            return HyperMouComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_INTERNAL_MOUSE_DISCONNECT:
            Extension->ConnectData.ClassService = NULL;
            Extension->ConnectData.ClassDeviceObject = NULL;
            return HyperMouComplete(Irp, STATUS_SUCCESS, 0);

        case IOCTL_MOUSE_QUERY_ATTRIBUTES:
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUSE_ATTRIBUTES))
                return HyperMouComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Extension->Attributes, sizeof(MOUSE_ATTRIBUTES));
            return HyperMouComplete(Irp, STATUS_SUCCESS, sizeof(MOUSE_ATTRIBUTES));

        default:
            return HyperMouComplete(Irp, STATUS_NOT_SUPPORTED, 0);
    }
}

static
NTSTATUS
NTAPI
HyperMouPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERMOU_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = HyperMouStart(Extension);
            return HyperMouComplete(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            HyperMouStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            HyperMouStop(Extension);
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
HyperMouPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERMOU_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HyperMouPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHYPERMOU_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HyperMouCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return HyperMouComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
HyperMouAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PHYPERMOU_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(HYPERMOU_EXTENSION),
                            NULL,
                            FILE_DEVICE_MOUSE,
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
    KeInitializeEvent(&Extension->DeviceInfoEvent, NotificationEvent, FALSE);
    Extension->Attributes.MouseIdentifier = MOUSE_HID_HARDWARE;
    Extension->Attributes.NumberOfButtons = 3;
    Extension->Attributes.SampleRate = 0;
    Extension->Attributes.InputDataQueueLength = 100;

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
HyperMouUnload(
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

    DriverObject->DriverExtension->AddDevice = HyperMouAddDevice;
    DriverObject->DriverUnload = HyperMouUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = HyperMouCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HyperMouCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HyperMouInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HyperMouPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HyperMouPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HyperMouPassThrough;
    return STATUS_SUCCESS;
}
