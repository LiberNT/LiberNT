/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Enumeration of the root ports and selection of a USB Ethernet function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define USB_MAX_CONFIGURATIONS  4
#define USB_NO_INTERFACE        0xFF
#define USB_HUB_RESET_WAIT      50
#define USB_HUB_STEP            10000
#define USB_HUB_DEBOUNCE        100000
#define USB_HUB_RECOVERY        10000

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
UsbHasPhoneInterface(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Offset;

    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2 && Offset + Data[Offset] <= Length;
         Offset += Data[Offset])
    {
        if (Data[Offset + 1] != USB_INTERFACE_DESCRIPTOR_TYPE || Data[Offset] < sizeof(USB_INTERFACE_DESCRIPTOR))
            continue;

        if (Data[Offset + 5] == USB_DEVICE_CLASS_VENDOR_SPECIFIC && Data[Offset + 6] == 0x42 &&
            Data[Offset + 7] == 0x01)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
UsbPhoneHeld(
    _In_ PUSBNET_ADAPTER Adapter)
{
    return Adapter->HoldPhone && Adapter->PhoneRoute;
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
    BOOLEAN Found = FALSE, AfterStatus = FALSE;

    Function->Protocol = UsbNetNone;
    Function->DataInterface = USB_NO_INTERFACE;
    Function->MacIndex = 0;
    Function->NcmCapabilities = 0;
    Function->StatusAddress = 0;
    Function->StatusBurst = 0;
    Function->StatusInterval = 0;
    Function->StatusPacket = 0;

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

        if (Found && AfterStatus && Data[Offset + 1] == USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE &&
            Data[Offset] >= 3)
        {
            Function->StatusBurst = Data[Offset + 2];
        }

        AfterStatus = FALSE;
        if (Found && !Function->StatusPacket && Data[Offset + 1] == USB_ENDPOINT_DESCRIPTOR_TYPE &&
            Data[Offset] >= sizeof(USB_ENDPOINT_DESCRIPTOR) &&
            (Data[Offset + 3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT &&
            USB_ENDPOINT_DIRECTION_IN(Data[Offset + 2]))
        {
            Function->StatusAddress = Data[Offset + 2];
            Function->StatusPacket = UsbNetGet16(Data + Offset + 4) & 0x7FF;
            Function->StatusInterval = Data[Offset + 6];
            AfterStatus = TRUE;
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
UsbProbeTier(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Tier);

static
NTSTATUS
UsbHubPortStatus(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Hub,
    _In_ ULONG Port,
    _Out_ PUSHORT PortStatus,
    _Out_ PUSHORT PortChange)
{
    PUCHAR Data = Adapter->Control;
    NTSTATUS Status;
    ULONG Got;

    *PortStatus = 0;
    *PortChange = 0;
    Status = XhciDeviceControl(Adapter, Hub, USB_HUB_PORT_IN, USB_REQUEST_GET_STATUS, 0, (USHORT)Port,
                               USB_HUB_PORT_STATUS_SIZE, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < USB_HUB_PORT_STATUS_SIZE)
        return STATUS_DEVICE_DATA_ERROR;

    *PortStatus = UsbNetGet16(Data);
    *PortChange = UsbNetGet16(Data + 2);
    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbHubPortFeature(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Hub,
    _In_ UCHAR Request,
    _In_ USHORT Feature,
    _In_ ULONG Port)
{
    return XhciDeviceControl(Adapter, Hub, USB_HUB_PORT_OUT, Request, Feature, (USHORT)Port, 0, NULL);
}

static
NTSTATUS
UsbHubResetPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Hub,
    _In_ ULONG Port,
    _Out_ PUCHAR Speed)
{
    USHORT PortStatus, PortChange;
    NTSTATUS Status;
    ULONG i;

    *Speed = 0;
    Status = UsbHubPortFeature(Adapter, Hub, USB_REQUEST_SET_FEATURE, HUB_FEATURE_PORT_RESET, Port);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < USB_HUB_RESET_WAIT; i++)
    {
        XhciStall(USB_HUB_STEP);
        Status = UsbHubPortStatus(Adapter, Hub, Port, &PortStatus, &PortChange);
        if (!NT_SUCCESS(Status))
            return Status;

        if ((PortChange & HUB_PORT_CHANGE_RESET) && !(PortStatus & USB_PORT_STATUS_RESET))
            break;
    }

    if (i == USB_HUB_RESET_WAIT)
        return STATUS_IO_TIMEOUT;

    UsbHubPortFeature(Adapter, Hub, USB_REQUEST_CLEAR_FEATURE, HUB_FEATURE_C_PORT_RESET, Port);
    UsbHubPortFeature(Adapter, Hub, USB_REQUEST_CLEAR_FEATURE, HUB_FEATURE_C_PORT_CONNECTION, Port);
    UsbHubPortFeature(Adapter, Hub, USB_REQUEST_CLEAR_FEATURE, HUB_FEATURE_C_PORT_ENABLE, Port);
    XhciStall(USB_HUB_RECOVERY);

    if ((PortStatus & (USB_PORT_STATUS_CONNECT | USB_PORT_STATUS_ENABLE)) !=
        (USB_PORT_STATUS_CONNECT | USB_PORT_STATUS_ENABLE))
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    if (Hub->Speed >= XHCI_SPEED_SUPER)
        *Speed = XHCI_SPEED_SUPER;
    else if (PortStatus & USB_PORT_STATUS_LOW_SPEED)
        *Speed = XHCI_SPEED_LOW;
    else if (PortStatus & USB_PORT_STATUS_HIGH_SPEED)
        *Speed = XHCI_SPEED_HIGH;
    else
        *Speed = XHCI_SPEED_FULL;

    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbProbeHubPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Tier,
    _In_ ULONG Port)
{
    PUSBNET_DEVICE Hub = &Adapter->Devices[Tier];
    PUSBNET_DEVICE Child = &Adapter->Devices[Tier + 1];
    NTSTATUS Status;
    UCHAR Speed;

    Status = UsbHubResetPort(Adapter, Hub, Port, &Speed);
    if (!NT_SUCCESS(Status))
        return Status;

    Child->Port = Hub->Port;
    Child->HubPort = (UCHAR)Port;
    Child->Route = Hub->Route | (Port << (Tier * XHCI_ROUTE_TIER_SHIFT));
    Child->Speed = Speed;
    Child->TtSlot = 0;
    Child->TtPort = 0;
    if (Speed == XHCI_SPEED_LOW || Speed == XHCI_SPEED_FULL)
    {
        Child->TtSlot = Hub->Speed == XHCI_SPEED_HIGH ? Hub->Slot : Hub->TtSlot;
        Child->TtPort = Hub->Speed == XHCI_SPEED_HIGH ? (UCHAR)Port : Hub->TtPort;
    }

    Status = UsbProbeTier(Adapter, Tier + 1);
    if (!NT_SUCCESS(Status) && (!UsbPhoneHeld(Adapter) || Child->Route == Adapter->PhoneRoute))
    {
        XhciDisableSlot(Adapter, Child);
        Adapter->Tier = Tier;
        Adapter->Device = Hub;
    }

    return Status;
}

static
NTSTATUS
UsbEnumerateHub(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Tier)
{
    PUSBNET_DEVICE Hub = &Adapter->Devices[Tier];
    PUCHAR Data = Adapter->Control;
    USHORT PortStatus, PortChange;
    NTSTATUS Status;
    ULONG Got, Port, Settle;

    if (Tier + 1 >= USBNET_MAX_TIERS)
        return STATUS_NOT_SUPPORTED;

    Status = UsbReadDescriptor(Adapter, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                               sizeof(USB_CONFIGURATION_DESCRIPTOR), &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < sizeof(USB_CONFIGURATION_DESCRIPTOR))
        return STATUS_DEVICE_DATA_ERROR;

    Status = XhciControl(Adapter, 0, USB_REQUEST_SET_CONFIGURATION, Data[5], 0, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = XhciControl(Adapter, USB_HUB_CLASS_IN, USB_REQUEST_GET_DESCRIPTOR,
                         (Hub->Speed >= XHCI_SPEED_SUPER ? USB_30_HUB_DESCRIPTOR_TYPE : USB_20_HUB_DESCRIPTOR_TYPE) << 8,
                         0, USB_HUB_DESCRIPTOR_SIZE, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < 7 || !Data[2])
        return STATUS_DEVICE_DATA_ERROR;

    Hub->Hub = TRUE;
    Hub->Ports = min(Data[2], XHCI_ROUTE_PORT_MAX);
    Hub->ThinkTime = Hub->Speed == XHCI_SPEED_HIGH ?
                     (UCHAR)((UsbNetGet16(Data + 3) >> USB_HUB_THINK_TIME_SHIFT) & USB_HUB_THINK_TIME_MASK) : 0;
    Settle = max((ULONG)Data[5] * 2000, USB_HUB_DEBOUNCE);

    if (Hub->Speed >= XHCI_SPEED_SUPER)
    {
        Status = XhciControl(Adapter, USB_HUB_CLASS_OUT, USB_REQUEST_SET_HUB_DEPTH, (USHORT)Tier, 0, 0, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = XhciConfigureHub(Adapter, Hub);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Port = 1; Port <= Hub->Ports; Port++)
        UsbHubPortFeature(Adapter, Hub, USB_REQUEST_SET_FEATURE, HUB_FEATURE_PORT_POWER, Port);

    XhciStall(Settle);
    for (Port = 1; Port <= Hub->Ports; Port++)
    {
        if (!NT_SUCCESS(UsbHubPortStatus(Adapter, Hub, Port, &PortStatus, &PortChange)) ||
            !(PortStatus & USB_PORT_STATUS_CONNECT))
        {
            continue;
        }

        Status = UsbProbeHubPort(Adapter, Tier, Port);
        if (NT_SUCCESS(Status) || UsbPhoneHeld(Adapter))
            return Status;
    }

    return STATUS_NO_SUCH_DEVICE;
}

static
NTSTATUS
UsbProbeDevice(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Tier)
{
    PUSBNET_DEVICE Device = Adapter->Device;
    PUCHAR Data = Adapter->Control;
    USBNET_FUNCTION Function;
    NTSTATUS Status;
    ULONG Got, Total, Configurations, i;
    USHORT MaxPacket, VendorId, ProductId;
    BOOLEAN Phone;

    Status = XhciAddressDevice(Adapter, Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbReadDescriptor(Adapter, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, 8, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < 8)
        return STATUS_DEVICE_DATA_ERROR;

    if (Device->Speed >= XHCI_SPEED_SUPER)
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

    Status = XhciUpdateControlPacketSize(Adapter, Device, MaxPacket);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbReadDescriptor(Adapter, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, sizeof(USB_DEVICE_DESCRIPTOR), &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < sizeof(USB_DEVICE_DESCRIPTOR))
        return STATUS_NOT_SUPPORTED;

    if (Data[4] == USB_DEVICE_CLASS_HUB)
        return UsbEnumerateHub(Adapter, Tier);

    VendorId = UsbNetGet16(Data + 8);
    ProductId = UsbNetGet16(Data + 10);
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

        if (UsbHasPhoneInterface(Data, Got))
        {
            Adapter->PhonePort = Device->Port;
            Adapter->PhoneRoute = Device->Route;
        }

        if (!UsbFindControl(Data, Got, &Function) || !UsbFindEndpoints(Data, Got, &Function))
            continue;

        Phone = Function.Protocol == UsbNetRndis ||
                (Adapter->PhonePort == Device->Port && Adapter->PhoneRoute == Device->Route);
        if (!Phone && !Adapter->AcceptEthernet)
        {
            if (!Adapter->EthernetPort)
                Adapter->EthernetPort = Device->Port;

            return STATUS_NOT_SUPPORTED;
        }

        Function.Configuration = Data[5];
        Adapter->Function = Function;
        Adapter->Phone = Phone;
        Adapter->VendorId = VendorId;
        Adapter->ProductId = ProductId;
        return UsbNetStartFunction(Adapter);
    }

    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
UsbProbeTier(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Tier)
{
    PUSBNET_DEVICE Device = &Adapter->Devices[Tier];
    NTSTATUS Status;

    Adapter->Tier = Tier;
    Adapter->Device = Device;
    Device->Hub = FALSE;
    Device->Ports = 0;
    Device->ThinkTime = 0;
    Status = UsbProbeDevice(Adapter, Tier);
    if (!NT_SUCCESS(Status) && !UsbPhoneHeld(Adapter))
    {
        Adapter->Tier = Tier;
        Adapter->Device = Device;
    }

    return Status;
}

VOID
UsbReleaseDevices(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Tier = Adapter->Tier + 1;

    while (Tier--)
        XhciDisableSlot(Adapter, &Adapter->Devices[Tier]);

    Adapter->Tier = 0;
    Adapter->Device = &Adapter->Devices[0];
}

BOOLEAN
UsbPathAlive(
    _In_ PUSBNET_ADAPTER Adapter)
{
    USHORT PortStatus, PortChange;
    ULONG Tier;

    if (!XhciPortAlive(Adapter, Adapter->Devices[0].Port))
        return FALSE;

    for (Tier = 0; Tier < Adapter->Tier; Tier++)
    {
        if (!NT_SUCCESS(UsbHubPortStatus(Adapter, &Adapter->Devices[Tier], Adapter->Devices[Tier + 1].HubPort,
                                         &PortStatus, &PortChange)) ||
            (PortStatus & (USB_PORT_STATUS_CONNECT | USB_PORT_STATUS_ENABLE)) !=
            (USB_PORT_STATUS_CONNECT | USB_PORT_STATUS_ENABLE) ||
            (PortChange & HUB_PORT_CHANGE_CONNECTION))
        {
            return FALSE;
        }
    }

    return TRUE;
}

NTSTATUS
UsbReconnectBehindHub(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Tier = Adapter->Tier - 1, Port;
    PUSBNET_DEVICE Hub = &Adapter->Devices[Tier];
    USHORT PortStatus, PortChange;
    NTSTATUS Status = STATUS_NO_SUCH_DEVICE;

    Adapter->Tier = Tier;
    Adapter->Device = Hub;
    for (Port = 1; Port <= Hub->Ports; Port++)
    {
        Status = UsbHubPortStatus(Adapter, Hub, Port, &PortStatus, &PortChange);
        if (!NT_SUCCESS(Status))
            break;

        Status = STATUS_NO_SUCH_DEVICE;
        if (!(PortChange & HUB_PORT_CHANGE_CONNECTION))
            continue;

        UsbHubPortFeature(Adapter, Hub, USB_REQUEST_CLEAR_FEATURE, HUB_FEATURE_C_PORT_CONNECTION, Port);
        if ((PortStatus & USB_PORT_STATUS_CONNECT) && NT_SUCCESS(UsbProbeHubPort(Adapter, Tier, Port)))
            return STATUS_SUCCESS;
    }

    Adapter->Tier = Tier + 1;
    Adapter->Device = &Adapter->Devices[Tier + 1];
    return Status;
}

static
NTSTATUS
UsbProbeRootPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port)
{
    PUSBNET_DEVICE Device = &Adapter->Devices[0];
    NTSTATUS Status;
    UCHAR Speed;

    Adapter->Tier = 0;
    Adapter->Device = Device;
    Status = XhciResetPort(Adapter, Port, &Speed);
    if (Status == STATUS_NO_SUCH_DEVICE)
        return Status;

    if (NT_SUCCESS(Status))
    {
        Device->Port = (UCHAR)Port;
        Device->HubPort = 0;
        Device->Route = 0;
        Device->Speed = Speed;
        Device->TtSlot = 0;
        Device->TtPort = 0;
        Status = UsbProbeTier(Adapter, 0);
        if (NT_SUCCESS(Status))
            return STATUS_SUCCESS;

        if (UsbPhoneHeld(Adapter))
            return Status;

        XhciDisableSlot(Adapter, Device);
    }

    if (Status != STATUS_NOT_SUPPORTED && XhciPortChanged(Adapter, Port))
    {
        if (!Adapter->WaitPort)
            Adapter->WaitPort = Port;

        return STATUS_DEVICE_NOT_CONNECTED;
    }

    return Status;
}

NTSTATUS
UsbFindFunction(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Port;

    Adapter->WaitPort = 0;
    Adapter->PhonePort = 0;
    Adapter->PhoneRoute = 0;
    Adapter->EthernetPort = 0;
    if (!XhciWaitForConnections(Adapter))
        return STATUS_NO_SUCH_DEVICE;

    for (Port = 1; Port <= Adapter->MaxPorts; Port++)
    {
        if (NT_SUCCESS(UsbProbeRootPort(Adapter, Port)))
            return STATUS_SUCCESS;
    }

    Adapter->Tier = 0;
    Adapter->Device = &Adapter->Devices[0];
    if (!Adapter->PhonePort && NT_SUCCESS(UsbStartEthernet(Adapter)))
        return STATUS_SUCCESS;

    return Adapter->WaitPort ? STATUS_DEVICE_NOT_CONNECTED : STATUS_NO_SUCH_DEVICE;
}

NTSTATUS
UsbStartEthernet(
    _In_ PUSBNET_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (!Adapter->EthernetPort)
        return STATUS_NO_SUCH_DEVICE;

    Adapter->AcceptEthernet = TRUE;
    Status = UsbProbeRootPort(Adapter, Adapter->EthernetPort);
    Adapter->AcceptEthernet = FALSE;
    return Status;
}

static
BOOLEAN
UsbWaitPortConnected(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port,
    _Out_ PBOOLEAN Changed)
{
    USHORT PortStatus, PortChange;
    ULONG Value;
    BOOLEAN Dropped = FALSE;

    if (UsbPhoneHeld(Adapter))
    {
        if (UsbPathAlive(Adapter) &&
            NT_SUCCESS(UsbHubPortStatus(Adapter, Adapter->Device, Adapter->Devices[Adapter->Tier + 1].HubPort,
                                        &PortStatus, &PortChange)))
        {
            *Changed = (PortChange & HUB_PORT_CHANGE_CONNECTION) != 0;
            return (PortStatus & USB_PORT_STATUS_CONNECT) != 0;
        }

        UsbNetTrace("USB debugger: the hub of the phone on root port %lu stopped answering\n", Port);
        UsbReleaseDevices(Adapter);
        Adapter->PhoneRoute = 0;
        Dropped = TRUE;
    }

    Value = XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Port));
    *Changed = Dropped || (Value & XHCI_PORT_CONNECT_CHANGE);
    return (Value & XHCI_PORT_CONNECTED) != 0;
}

static
NTSTATUS
UsbProbeWaitPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port)
{
    ULONG Tier = Adapter->Tier, HubPort;

    Adapter->PhonePort = 0;
    if (!UsbPhoneHeld(Adapter))
    {
        Adapter->PhoneRoute = 0;
        return UsbProbeRootPort(Adapter, Port);
    }

    Adapter->PhoneRoute = 0;
    HubPort = Adapter->Devices[Tier + 1].HubPort;
    UsbHubPortFeature(Adapter, &Adapter->Devices[Tier], USB_REQUEST_CLEAR_FEATURE, HUB_FEATURE_C_PORT_CONNECTION,
                      HubPort);
    return UsbProbeHubPort(Adapter, Tier, HubPort);
}

NTSTATUS
UsbWaitForTethering(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Port = Adapter->PhonePort ? Adapter->PhonePort : Adapter->WaitPort, Waited = 0, Gone = 0;
    BOOLEAN Phone = FALSE, Probe = Adapter->PhoneRoute != 0, Changed;

    if (!Port)
        return STATUS_NO_SUCH_DEVICE;

    if (Adapter->PhonePort != Port)
        UsbNetTrace("USB debugger: the device on root port %lu left during its reset, waiting for it\n", Port);

    Adapter->PhoneRoute = 0;
    Adapter->HoldPhone = TRUE;
    for (;;)
    {
        if (Probe)
        {
            Probe = FALSE;
            if (NT_SUCCESS(UsbProbeWaitPort(Adapter, Port)))
            {
                Adapter->HoldPhone = FALSE;
                return STATUS_SUCCESS;
            }

            if (Adapter->PhonePort != Port)
                break;
        }

        if (!Phone && Adapter->PhonePort == Port)
        {
            Phone = TRUE;
            UsbNetSetMessage(STATUS_DEVICE_NOT_CONNECTED,
                             L"Connect the computer to your phone and enable USB tethering");
            UsbNetTrace("USB debugger: turn on USB tethering on the phone on root port %lu, %lu hub(s) away, "
                        "the boot waits for it (unplug the phone to boot without the debugger)\n",
                        Port, UsbPhoneHeld(Adapter) ? Adapter->Tier + 1 : 0);
        }

        XhciStall(USBNET_PHONE_STEP * 1000);
        Waited += USBNET_PHONE_STEP;
        if (!UsbWaitPortConnected(Adapter, Port, &Changed))
        {
            Gone += USBNET_PHONE_STEP;
            if (Gone >= USBNET_PHONE_GONE)
                break;

            continue;
        }

        Gone = 0;
        if (!Changed)
        {
            if (!Phone && Waited >= USBNET_PHONE_GONE)
                break;

            if (Phone && !(Waited % USBNET_PHONE_REMIND))
                UsbNetTrace("USB debugger: still waiting for USB tethering on the phone on root port %lu\n", Port);

            continue;
        }

        XhciStall(USBNET_PHONE_SETTLE * 1000);
        Waited += USBNET_PHONE_SETTLE;
        Probe = TRUE;
    }

    UsbReleaseDevices(Adapter);
    Adapter->HoldPhone = FALSE;
    UsbNetSetMessage(STATUS_NO_SUCH_DEVICE, NULL);
    UsbNetTrace("USB debugger: stopped waiting for root port %lu\n", Port);
    Adapter->PhonePort = 0;
    Adapter->PhoneRoute = 0;
    return STATUS_NO_SUCH_DEVICE;
}
