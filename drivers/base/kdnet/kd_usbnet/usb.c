/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Enumeration of the root ports and selection of a USB Ethernet function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define USB_MAX_CONFIGURATIONS  4
#define USB_NO_INTERFACE        0xFF

typedef struct _USB_SCAN
{
    UCHAR Interface;
    UCHAR Alternate;
    UCHAR Class;
    UCHAR SubClass;
    UCHAR Protocol;
} USB_SCAN, *PUSB_SCAN;

static
USBNET_PROTOCOL
UsbClassify(
    _In_ PUSB_SCAN Scan)
{
    if ((Scan->Class == USB_CLASS_CDC && Scan->SubClass == CDC_SUBCLASS_ACM && Scan->Protocol == CDC_PROTOCOL_VENDOR) ||
        (Scan->Class == USB_CLASS_WIRELESS && Scan->SubClass == 0x01 && Scan->Protocol == 0x03) ||
        (Scan->Class == USB_CLASS_MISC && Scan->SubClass == 0x04 && Scan->Protocol == 0x01) ||
        (Scan->Class == USB_CLASS_MISC && Scan->SubClass == 0x01 && Scan->Protocol == 0x01))
    {
        return UsbNetRndis;
    }

    if (Scan->Class != USB_CLASS_CDC)
        return UsbNetNone;

    if (Scan->SubClass == CDC_SUBCLASS_ECM)
        return UsbNetEcm;

    if (Scan->SubClass == CDC_SUBCLASS_NCM)
        return UsbNetNcm;

    if (Scan->SubClass == CDC_SUBCLASS_EEM && Scan->Protocol == CDC_PROTOCOL_EEM)
        return UsbNetEem;

    return UsbNetNone;
}

static
BOOLEAN
UsbFindControl(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PUSBNET_FUNCTION Function)
{
    USB_SCAN Scan = { USB_NO_INTERFACE };
    ULONG Offset;
    BOOLEAN Found = FALSE;

    Function->Protocol = UsbNetNone;
    Function->DataInterface = USB_NO_INTERFACE;
    Function->MacIndex = 0;
    Function->NcmCapabilities = 0;

    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2 && Offset + Data[Offset] <= Length;
         Offset += Data[Offset])
    {
        if (Data[Offset + 1] == USB_INTERFACE_DESCRIPTOR_TYPE && Data[Offset] >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            if (Found)
                break;

            Scan.Interface = Data[Offset + 2];
            Scan.Alternate = Data[Offset + 3];
            Scan.Class = Data[Offset + 5];
            Scan.SubClass = Data[Offset + 6];
            Scan.Protocol = Data[Offset + 7];
            if (Scan.Alternate)
                continue;

            Function->Protocol = UsbClassify(&Scan);
            if (Function->Protocol != UsbNetNone)
            {
                Function->ControlInterface = Scan.Interface;
                Found = TRUE;
            }

            continue;
        }

        if (!Found || Data[Offset + 1] != CDC_CS_INTERFACE || Data[Offset] < 4)
            continue;

        if (Data[Offset + 2] == CDC_UNION_FUNCTIONAL && Data[Offset] >= 5 &&
            Function->DataInterface == USB_NO_INTERFACE)
        {
            Function->DataInterface = Data[Offset + 4];
        }
        else if (Data[Offset + 2] == CDC_ETHERNET_FUNCTIONAL)
        {
            Function->MacIndex = Data[Offset + 3];
        }
        else if (Data[Offset + 2] == CDC_NCM_FUNCTIONAL && Data[Offset] >= 6)
        {
            Function->NcmCapabilities = Data[Offset + 5];
        }
    }

    if (!Found)
        return FALSE;

    if (Function->Protocol == UsbNetEem)
        Function->DataInterface = Function->ControlInterface;
    else if (Function->DataInterface == USB_NO_INTERFACE)
        Function->DataInterface = Function->ControlInterface + 1;

    return TRUE;
}

static
BOOLEAN
UsbFindEndpoints(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Inout_ PUSBNET_FUNCTION Function)
{
    PUCHAR Burst = NULL;
    ULONG Offset;
    UCHAR Address, Alternate = 0;
    BOOLEAN Inside = FALSE;

    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2 && Offset + Data[Offset] <= Length;
         Offset += Data[Offset])
    {
        if (Data[Offset + 1] == USB_INTERFACE_DESCRIPTOR_TYPE && Data[Offset] >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            if (Inside && Function->InPacket && Function->OutPacket)
                break;

            Inside = Data[Offset + 2] == Function->DataInterface &&
                     (Function->Protocol == UsbNetEem || Data[Offset + 5] == USB_CLASS_CDC_DATA);
            Alternate = Data[Offset + 3];
            Function->InPacket = 0;
            Function->OutPacket = 0;
            Function->InBurst = 0;
            Function->OutBurst = 0;
            Burst = NULL;
            continue;
        }

        if (!Inside)
            continue;

        if (Data[Offset + 1] == USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE && Data[Offset] >= 3 && Burst)
        {
            *Burst = Data[Offset + 2];
            Burst = NULL;
            continue;
        }

        Burst = NULL;
        if (Data[Offset + 1] != USB_ENDPOINT_DESCRIPTOR_TYPE || Data[Offset] < sizeof(USB_ENDPOINT_DESCRIPTOR) ||
            (Data[Offset + 3] & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_TYPE_BULK)
        {
            continue;
        }

        Address = Data[Offset + 2];
        if (USB_ENDPOINT_DIRECTION_IN(Address) && !Function->InPacket)
        {
            Function->InAddress = Address;
            Function->InPacket = UsbNetGet16(Data + Offset + 4) & 0x7FF;
            Burst = &Function->InBurst;
        }
        else if (USB_ENDPOINT_DIRECTION_OUT(Address) && !Function->OutPacket)
        {
            Function->OutAddress = Address;
            Function->OutPacket = UsbNetGet16(Data + Offset + 4) & 0x7FF;
            Burst = &Function->OutBurst;
        }
    }

    if (!Inside || !Function->InPacket || !Function->OutPacket)
        return FALSE;

    Function->DataAlternate = Alternate;
    return TRUE;
}

static
NTSTATUS
UsbReadDescriptor(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ UCHAR Type,
    _In_ UCHAR Index,
    _In_ USHORT Language,
    _In_ USHORT Length,
    _Out_ PULONG Transferred)
{
    return XhciControl(Adapter, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                       (USHORT)((Type << 8) | Index), Language, Length, Transferred);
}

static
NTSTATUS
UsbProbeDevice(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Data = Adapter->Control;
    USBNET_FUNCTION Function;
    NTSTATUS Status;
    ULONG Got, Total, Configurations, i;
    USHORT MaxPacket;

    Status = XhciAddressDevice(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbReadDescriptor(Adapter, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, 8, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < 8)
        return STATUS_DEVICE_DATA_ERROR;

    if (Adapter->Speed >= XHCI_SPEED_SUPER)
    {
        if (Data[7] != 9)
            return STATUS_DEVICE_DATA_ERROR;

        MaxPacket = 512;
    }
    else
    {
        MaxPacket = Data[7];
        if (MaxPacket != 8 && MaxPacket != 16 && MaxPacket != 32 && MaxPacket != 64)
            return STATUS_DEVICE_DATA_ERROR;
    }

    Status = XhciUpdateControlPacketSize(Adapter, MaxPacket);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbReadDescriptor(Adapter, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, sizeof(USB_DEVICE_DESCRIPTOR), &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < sizeof(USB_DEVICE_DESCRIPTOR) || Data[4] == USB_DEVICE_CLASS_HUB)
        return STATUS_NOT_SUPPORTED;

    Configurations = min(Data[17], USB_MAX_CONFIGURATIONS);
    for (i = 0; i < Configurations; i++)
    {
        Status = UsbReadDescriptor(Adapter, USB_CONFIGURATION_DESCRIPTOR_TYPE, (UCHAR)i, 0,
                                   sizeof(USB_CONFIGURATION_DESCRIPTOR), &Got);
        if (!NT_SUCCESS(Status) || Got < sizeof(USB_CONFIGURATION_DESCRIPTOR))
            continue;

        Total = min(UsbNetGet16(Data + 2), USBNET_CONTROL_SIZE);
        Status = UsbReadDescriptor(Adapter, USB_CONFIGURATION_DESCRIPTOR_TYPE, (UCHAR)i, 0, (USHORT)Total, &Got);
        if (!NT_SUCCESS(Status) || Got < sizeof(USB_CONFIGURATION_DESCRIPTOR))
            continue;

        if (!UsbFindControl(Data, Got, &Function) || !UsbFindEndpoints(Data, Got, &Function))
            continue;

        Function.Configuration = Data[5];
        Adapter->Function = Function;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
UsbFindFunction(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Port;
    UCHAR Speed;

    if (!XhciWaitForConnections(Adapter))
        return STATUS_NO_SUCH_DEVICE;

    for (Port = 1; Port <= Adapter->MaxPorts; Port++)
    {
        if (!NT_SUCCESS(XhciResetPort(Adapter, Port, &Speed)))
            continue;

        Adapter->Port = (UCHAR)Port;
        Adapter->Speed = Speed;
        if (NT_SUCCESS(UsbProbeDevice(Adapter)) && NT_SUCCESS(UsbNetStartFunction(Adapter)))
            return STATUS_SUCCESS;

        XhciDisableSlot(Adapter);
    }

    return STATUS_NO_SUCH_DEVICE;
}
