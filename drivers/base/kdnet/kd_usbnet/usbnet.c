/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware context, controller start and the KDNET packet interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define USBNET_TX_WAIT          50
#define USBNET_RECONNECT_MASK   0x3F
#define USBNET_REGISTER_SPACE   0x1000

static
ULONG
UsbNetAlignedAdapterSize(VOID)
{
    return (sizeof(USBNET_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

ULONG
NTAPI
UsbNetGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return UsbNetAlignedAdapterSize() + (USBNET_FIXED_PAGES + USBNET_MAX_SCRATCHPAD) * PAGE_SIZE;
}

static
VOID
UsbNetLayout(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Page = (PUCHAR)Adapter + UsbNetAlignedAdapterSize();

    Adapter->Dcbaa = (PULONG64)Page;
    Adapter->ScratchpadArray = (PULONG64)(Page + 1024);
    Adapter->Erst = (PULONG)(Page + 2048);
    Page += PAGE_SIZE;
    Adapter->Command.Trbs = (PULONG)Page;
    Adapter->Events = (PULONG)(Page + USBNET_RING_SIZE);
    Adapter->Ep0.Ring.Trbs = (PULONG)(Page + 2 * USBNET_RING_SIZE);
    Adapter->BulkIn.Ring.Trbs = (PULONG)(Page + 3 * USBNET_RING_SIZE);
    Page += PAGE_SIZE;
    Adapter->BulkOut.Ring.Trbs = (PULONG)Page;
    Adapter->DeviceContext = Page + USBNET_RING_SIZE;
    Page += PAGE_SIZE;
    Adapter->InputContext = Page;
    Page += PAGE_SIZE;
    Adapter->Control = Page;
    Page += PAGE_SIZE;
    Adapter->RxBuffer = Page;
    Page += PAGE_SIZE;
    Adapter->TxBuffers = Page;
    Page += USBNET_TX_COUNT * PAGE_SIZE;
    Adapter->ScratchpadPages = Page;
}

static
ULONG
UsbNetLinkSpeed(
    _In_ UCHAR Speed)
{
    switch (Speed)
    {
        case XHCI_SPEED_LOW:
            return 1;

        case XHCI_SPEED_FULL:
            return 12;

        case XHCI_SPEED_HIGH:
            return 480;

        case XHCI_SPEED_SUPER:
            return 5000;

        default:
            return 10000;
    }
}

static
VOID
UsbNetSetLink(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ BOOLEAN Up)
{
    Adapter->LinkUp = Up;
    Adapter->RxActive = FALSE;
    Adapter->RxHeld = FALSE;
    Adapter->RxLength = 0;
    Adapter->RxOffset = 0;
    Adapter->RxNdp = 0;
    if (Up)
    {
        Adapter->KdNet->LinkSpeed = UsbNetLinkSpeed(Adapter->Speed);
        Adapter->KdNet->LinkDuplex = 1;
    }

    *Adapter->KdNet->LinkState = Up;
}

NTSTATUS
NTAPI
UsbNetInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PUSBNET_ADAPTER Adapter = KdNet->Hardware;
    NTSTATUS Status;
    ULONG i;

    if (!Device->BaseAddress[0].Valid || Device->BaseAddress[0].Type != CmResourceTypeMemory ||
        Device->BaseAddress[0].Length < USBNET_REGISTER_SPACE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->KdNet = KdNet;
    Adapter->Registers = Device->BaseAddress[0].TranslatedAddress;
    UsbNetLayout(Adapter);

    Status = XhciInitialize(Adapter);
    if (NT_SUCCESS(Status))
        Status = UsbFindFunction(Adapter);

    if (!NT_SUCCESS(Status))
    {
        XhciHalt(Adapter);
        return Status;
    }

    UsbNetSetLink(Adapter, TRUE);
    return STATUS_SUCCESS;
}

VOID
NTAPI
UsbNetShutdownController(
    _In_ PVOID Context)
{
    PUSBNET_ADAPTER Adapter = Context;

    XhciHalt(Adapter);
    UsbNetSetLink(Adapter, FALSE);
}

static
BOOLEAN
UsbNetPortAlive(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Value = XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Adapter->Port));

    return (Value & (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) == (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED) &&
           !(Value & XHCI_PORT_CONNECT_CHANGE);
}

static
VOID
UsbNetRecover(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint)
{
    UCHAR Completion = Endpoint->Completion;

    if (!UsbNetPortAlive(Adapter))
    {
        XhciDisableSlot(Adapter);
        UsbNetSetLink(Adapter, FALSE);
        return;
    }

    XhciRecoverEndpoint(Adapter, Endpoint);
    if (Completion == XHCI_CC_STALL)
    {
        XhciControl(Adapter, BMREQUEST_TO_ENDPOINT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_STALL,
                    Endpoint->Address, 0, NULL);
    }
}

static
VOID
UsbNetReconnect(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Port, Value;
    BOOLEAN Changed = FALSE;

    for (Port = 1; Port <= Adapter->MaxPorts; Port++)
    {
        Value = XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Port));
        if ((Value & (XHCI_PORT_CONNECTED | XHCI_PORT_CONNECT_CHANGE)) ==
            (XHCI_PORT_CONNECTED | XHCI_PORT_CONNECT_CHANGE))
        {
            Changed = TRUE;
        }
    }

    if (!Changed)
        return;

    XhciDisableSlot(Adapter);
    if (NT_SUCCESS(UsbFindFunction(Adapter)))
        UsbNetSetLink(Adapter, TRUE);
}

NTSTATUS
NTAPI
UsbNetGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PUSBNET_ADAPTER Adapter = Context;
    PUSBNET_ENDPOINT Endpoint = &Adapter->BulkIn;
    ULONG Round;

    if (!Adapter->LinkUp)
    {
        if (Adapter->Running && !(Adapter->LinkPoll++ & USBNET_RECONNECT_MASK))
            UsbNetReconnect(Adapter);

        return STATUS_IO_TIMEOUT;
    }

    if (Adapter->RxHeld)
        return STATUS_IO_TIMEOUT;

    for (Round = 0; Round < 2; Round++)
    {
        if (Adapter->RxOffset < Adapter->RxLength && UsbNetNextFrame(Adapter))
        {
            Adapter->RxHeld = TRUE;
            *Handle = 0;
            *Packet = Adapter->RxFrame;
            *Length = Adapter->RxFrameLength;
            return STATUS_SUCCESS;
        }

        if (!Adapter->RxActive)
        {
            XhciQueueBulk(Adapter, Endpoint, Adapter->RxBuffer, USBNET_RX_SIZE, FALSE);
            Adapter->RxActive = TRUE;
        }

        if (!XhciPollEndpoint(Adapter, Endpoint))
            return STATUS_IO_TIMEOUT;

        Adapter->RxActive = FALSE;
        if (Endpoint->Completion != XHCI_CC_SUCCESS && Endpoint->Completion != XHCI_CC_SHORT_PACKET)
        {
            UsbNetRecover(Adapter, Endpoint);
            return STATUS_IO_TIMEOUT;
        }

        Adapter->RxLength = Endpoint->Residual <= USBNET_RX_SIZE ? USBNET_RX_SIZE - Endpoint->Residual : 0;
        Adapter->RxOffset = 0;
        Adapter->RxNdp = 0;
    }

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
UsbNetReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PUSBNET_ADAPTER Adapter = Context;

    UNREFERENCED_PARAMETER(Handle);

    Adapter->RxHeld = FALSE;
}

NTSTATUS
NTAPI
UsbNetGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PUSBNET_ADAPTER Adapter = Context;

    if (!Adapter->LinkUp)
        return STATUS_IO_TIMEOUT;

    *Handle = Adapter->TxNext | TRANSMIT_HANDLE;
    Adapter->TxNext = (Adapter->TxNext + 1) % USBNET_TX_COUNT;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
UsbNetSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PUSBNET_ADAPTER Adapter = Context;
    PUSBNET_ENDPOINT Endpoint = &Adapter->BulkOut;
    ULONG Index = Handle & ~HANDLE_FLAGS, Total;
    PUCHAR Buffer;
    NTSTATUS Status;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= USBNET_TX_COUNT || Length > USBNET_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    if (!Adapter->LinkUp)
        return STATUS_IO_TIMEOUT;

    Buffer = Adapter->TxBuffers + Index * PAGE_SIZE;
    for (; Length < USBNET_PAD_FRAME; Length++)
        Buffer[Adapter->TxHeader + Length] = 0;

    Total = UsbNetFrameTransmit(Adapter, Buffer, Length);
    if (!Total)
        return STATUS_INVALID_PARAMETER;

    XhciQueueBulk(Adapter, Endpoint, Buffer, Total, !(Total % Endpoint->MaxPacket));
    Status = XhciWaitEndpoint(Adapter, Endpoint, USBNET_TX_WAIT);
    if (!NT_SUCCESS(Status))
        UsbNetRecover(Adapter, Endpoint);

    return Status;
}

PVOID
NTAPI
UsbNetGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PUSBNET_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < USBNET_TX_COUNT ? Adapter->TxBuffers + Index * PAGE_SIZE + Adapter->TxHeader : NULL;

    return Adapter->RxFrame;
}

ULONG
NTAPI
UsbNetGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PUSBNET_ADAPTER Adapter = Context;

    if (Handle & TRANSMIT_HANDLE)
        return USBNET_MAX_FRAME;

    return Adapter->RxFrameLength;
}
