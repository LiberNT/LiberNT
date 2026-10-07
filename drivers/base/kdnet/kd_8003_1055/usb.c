/*
 * PROJECT:     LiberNT LAN95xx USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Control transfers and enumeration of the adapter, directly or behind one hub
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdlan.h"

#define USB_HUB_ADDRESS             1
#define USB_FIRST_DEVICE_ADDRESS    2
#define USB_HUB_GET                 0xA0
#define USB_PORT_GET                0xA3
#define USB_PORT_SET                0x23
#define USB_PORT_STATUS_SIZE        4
#define USB_PORT_RESET_CHANGE       0x0010
#define USB_RESET_POLLS             50

static const USHORT UsbProducts[] =
{
    0x9500, 0x9505, 0x9530, 0x9730, 0x9E00, 0x9E01, 0x9E08, 0xEC00,
    0x9900, 0x9901, 0x9902, 0x9903, 0x9904, 0x9905, 0x9906, 0x9907, 0x9908, 0x9909
};

NTSTATUS
UsbControl(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Actual)
{
    PUCHAR Setup = Adapter->SetupBuffer;
    BOOLEAN In = (RequestType & USB_ENDPOINT_DIRECTION_MASK) != 0;
    NTSTATUS Status;

    if (Actual)
        *Actual = 0;

    if (Length > LAN_CONTROL_SIZE)
        return STATUS_INVALID_PARAMETER;

    Setup[0] = RequestType;
    Setup[1] = Request;
    Setup[2] = (UCHAR)Value;
    Setup[3] = (UCHAR)(Value >> 8);
    Setup[4] = (UCHAR)Index;
    Setup[5] = (UCHAR)(Index >> 8);
    Setup[6] = (UCHAR)Length;
    Setup[7] = (UCHAR)(Length >> 8);

    Pipe->Toggle = 0;
    Status = DwcTransfer(Adapter, Pipe, TRUE, FALSE, Setup, 8, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length)
    {
        Pipe->Toggle = 1;
        Status = DwcTransfer(Adapter, Pipe, FALSE, In, Adapter->ControlBuffer, Length, Actual);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Pipe->Toggle = 1;
    return DwcTransfer(Adapter, Pipe, FALSE, !In || !Length, Setup + 16, 0, NULL);
}

static
NTSTATUS
UsbOpenDevice(
    _In_ PLAN_ADAPTER Adapter,
    _Out_ PLAN_PIPE Pipe,
    _In_ UCHAR Address,
    _Out_ PUSB_DEVICE_DESCRIPTOR Descriptor)
{
    PUCHAR Data = Adapter->ControlBuffer;
    NTSTATUS Status;
    ULONG Length, i;

    Pipe->Channel = DWC_CHANNEL_CONTROL;
    Pipe->Address = 0;
    Pipe->Endpoint = 0;
    Pipe->Type = USB_ENDPOINT_TYPE_CONTROL;
    Pipe->MaxPacket = Adapter->HighSpeed ? 64 : 8;
    Pipe->Toggle = 0;

    Status = UsbControl(Adapter, Pipe, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                        USB_DEVICE_DESCRIPTOR_TYPE << 8, 0, 8, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length < 8 || (Data[7] != 8 && Data[7] != 16 && Data[7] != 32 && Data[7] != 64))
        return STATUS_DEVICE_DATA_ERROR;

    Pipe->MaxPacket = Data[7];
    Status = UsbControl(Adapter, Pipe, 0, USB_REQUEST_SET_ADDRESS, Address, 0, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    LanStall(10000);
    Pipe->Address = Address;
    Status = UsbControl(Adapter, Pipe, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                        USB_DEVICE_DESCRIPTOR_TYPE << 8, 0, sizeof(*Descriptor), &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length < sizeof(*Descriptor))
        return STATUS_DEVICE_DATA_ERROR;

    for (i = 0; i < sizeof(*Descriptor); i++)
        ((PUCHAR)Descriptor)[i] = Data[i];

    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbConfigure(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _Out_ PULONG Length)
{
    PUCHAR Data = Adapter->ControlBuffer;
    NTSTATUS Status;
    ULONG Total, Got;
    UCHAR Value;

    *Length = 0;
    Status = UsbControl(Adapter, Pipe, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                        USB_CONFIGURATION_DESCRIPTOR_TYPE << 8, 0, sizeof(USB_CONFIGURATION_DESCRIPTOR), &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < sizeof(USB_CONFIGURATION_DESCRIPTOR))
        return STATUS_DEVICE_DATA_ERROR;

    Total = Data[2] | (Data[3] << 8);
    if (Total > LAN_CONTROL_SIZE)
        Total = LAN_CONTROL_SIZE;

    Value = Data[5];
    Status = UsbControl(Adapter, Pipe, 0, USB_REQUEST_SET_CONFIGURATION, Value, 0, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbControl(Adapter, Pipe, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                        USB_CONFIGURATION_DESCRIPTOR_TYPE << 8, 0, (USHORT)Total, &Got);
    if (NT_SUCCESS(Status))
        *Length = Got;

    return Status;
}

static
BOOLEAN
UsbIsAdapter(
    _In_ PUSB_DEVICE_DESCRIPTOR Descriptor)
{
    ULONG i;

    if (Descriptor->idVendor != SMSC_VENDOR_ID)
        return FALSE;

    for (i = 0; i < RTL_NUMBER_OF(UsbProducts); i++)
    {
        if (Descriptor->idProduct == UsbProducts[i])
            return TRUE;
    }

    return FALSE;
}

static
NTSTATUS
UsbPortStatus(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Hub,
    _In_ USHORT Port,
    _Out_ PUSHORT Status,
    _Out_ PUSHORT Change)
{
    PUCHAR Data = Adapter->ControlBuffer;
    NTSTATUS Result;
    ULONG Length;

    *Status = 0;
    *Change = 0;
    Result = UsbControl(Adapter, Hub, USB_PORT_GET, USB_REQUEST_GET_STATUS, 0, Port,
                        USB_PORT_STATUS_SIZE, &Length);
    if (!NT_SUCCESS(Result))
        return Result;

    if (Length < USB_PORT_STATUS_SIZE)
        return STATUS_DEVICE_DATA_ERROR;

    *Status = Data[0] | (Data[1] << 8);
    *Change = Data[2] | (Data[3] << 8);
    return STATUS_SUCCESS;
}

static
BOOLEAN
UsbResetHubPort(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Hub,
    _In_ USHORT Port)
{
    USHORT Status, Change;
    ULONG i;

    if (!NT_SUCCESS(UsbPortStatus(Adapter, Hub, Port, &Status, &Change)) ||
        !(Status & USB_PORT_STATUS_CONNECT) ||
        !NT_SUCCESS(UsbControl(Adapter, Hub, USB_PORT_SET, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_RESET, Port, 0, NULL)))
    {
        return FALSE;
    }

    for (i = 0; i < USB_RESET_POLLS; i++)
    {
        LanStall(10000);
        if (!NT_SUCCESS(UsbPortStatus(Adapter, Hub, Port, &Status, &Change)))
            return FALSE;

        if (Change & USB_PORT_RESET_CHANGE)
            break;
    }

    if (i == USB_RESET_POLLS)
        return FALSE;

    UsbControl(Adapter, Hub, USB_PORT_SET, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_RESET, Port, 0, NULL);
    LanStall(10000);
    return (Status & USB_PORT_STATUS_ENABLE) &&
           (!Adapter->HighSpeed || (Status & USB_PORT_STATUS_HIGH_SPEED));
}

static
NTSTATUS
UsbSearchHub(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Hub)
{
    USB_DEVICE_DESCRIPTOR Descriptor;
    PUCHAR Data = Adapter->ControlBuffer;
    ULONG Length, Ports, Delay;
    USHORT Port;
    UCHAR Address = USB_FIRST_DEVICE_ADDRESS;

    if (!NT_SUCCESS(UsbConfigure(Adapter, Hub, &Length)) ||
        !NT_SUCCESS(UsbControl(Adapter, Hub, USB_HUB_GET, USB_REQUEST_GET_DESCRIPTOR,
                               USB_20_HUB_DESCRIPTOR_TYPE << 8, 0,
                               FIELD_OFFSET(USB_HUB_DESCRIPTOR, bRemoveAndPowerMask), &Length)) ||
        Length < FIELD_OFFSET(USB_HUB_DESCRIPTOR, bRemoveAndPowerMask))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Ports = Data[FIELD_OFFSET(USB_HUB_DESCRIPTOR, bNumberOfPorts)];
    Delay = Data[FIELD_OFFSET(USB_HUB_DESCRIPTOR, bPowerOnToPowerGood)] * 2000;
    for (Port = 1; Port <= Ports; Port++)
        UsbControl(Adapter, Hub, USB_PORT_SET, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_POWER, Port, 0, NULL);

    LanStall(Delay + 100000);
    for (Port = 1; Port <= Ports; Port++)
    {
        if (!UsbResetHubPort(Adapter, Hub, Port))
            continue;

        if (NT_SUCCESS(UsbOpenDevice(Adapter, &Adapter->Device, Address, &Descriptor)))
        {
            if (UsbIsAdapter(&Descriptor))
                return STATUS_SUCCESS;

            Address++;
        }
    }

    return STATUS_NO_SUCH_DEVICE;
}

NTSTATUS
UsbFindAdapter(
    _In_ PLAN_ADAPTER Adapter)
{
    USB_DEVICE_DESCRIPTOR Descriptor;
    PUSB_ENDPOINT_DESCRIPTOR Endpoint;
    PUCHAR Data = Adapter->ControlBuffer;
    LAN_PIPE Root;
    NTSTATUS Status;
    ULONG Length, Offset;

    Status = UsbOpenDevice(Adapter, &Root, USB_HUB_ADDRESS, &Descriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    if (UsbIsAdapter(&Descriptor))
        Adapter->Device = Root;
    else if (Descriptor.bDeviceClass != USB_DEVICE_CLASS_HUB)
        return STATUS_NO_SUCH_DEVICE;
    else if (!NT_SUCCESS(Status = UsbSearchHub(Adapter, &Root)))
        return Status;

    Status = UsbConfigure(Adapter, &Adapter->Device, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->BulkIn.MaxPacket = 0;
    Adapter->BulkOut.MaxPacket = 0;
    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2; Offset += Data[Offset])
    {
        Endpoint = (PUSB_ENDPOINT_DESCRIPTOR)(Data + Offset);
        if (Data[Offset + 1] != USB_ENDPOINT_DESCRIPTOR_TYPE || Offset + sizeof(*Endpoint) > Length ||
            (Endpoint->bmAttributes & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_TYPE_BULK)
        {
            continue;
        }

        if (USB_ENDPOINT_DIRECTION_IN(Endpoint->bEndpointAddress) && !Adapter->BulkIn.MaxPacket)
        {
            Adapter->BulkIn.Channel = DWC_CHANNEL_RECEIVE;
            Adapter->BulkIn.Address = Adapter->Device.Address;
            Adapter->BulkIn.Endpoint = Endpoint->bEndpointAddress & 0x0F;
            Adapter->BulkIn.Type = USB_ENDPOINT_TYPE_BULK;
            Adapter->BulkIn.MaxPacket = (Data[Offset + 4] | (Data[Offset + 5] << 8)) & DWC_HCCHAR_PACKET_MASK;
            Adapter->BulkIn.Toggle = 0;
        }
        else if (USB_ENDPOINT_DIRECTION_OUT(Endpoint->bEndpointAddress) && !Adapter->BulkOut.MaxPacket)
        {
            Adapter->BulkOut.Channel = DWC_CHANNEL_TRANSMIT;
            Adapter->BulkOut.Address = Adapter->Device.Address;
            Adapter->BulkOut.Endpoint = Endpoint->bEndpointAddress & 0x0F;
            Adapter->BulkOut.Type = USB_ENDPOINT_TYPE_BULK;
            Adapter->BulkOut.MaxPacket = (Data[Offset + 4] | (Data[Offset + 5] << 8)) & DWC_HCCHAR_PACKET_MASK;
            Adapter->BulkOut.Toggle = 0;
        }
    }

    return Adapter->BulkIn.MaxPacket && Adapter->BulkOut.MaxPacket ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}
