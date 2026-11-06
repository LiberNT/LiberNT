/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled xHCI device slot, contexts, control and bulk transfers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define XHCI_STEP               10
#define XHCI_CONTROL_WAIT       500
#define XHCI_ADDRESS_RECOVERY   2000
#define XHCI_DEVICE_CONTEXTS    32
#define XHCI_INTERVAL_MAX       16
#define XHCI_INTERVAL_MIN_FRAME 3
#define XHCI_INTERVAL_MAX_FRAME 10

static
volatile ULONG *
XhciInputContext(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Index)
{
    return (volatile ULONG *)(Adapter->InputContext + Index * Adapter->ContextSize);
}

static
VOID
XhciClearInput(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < (XHCI_DEVICE_CONTEXTS + 1) * Adapter->ContextSize; i++)
        ((volatile UCHAR *)Adapter->InputContext)[i] = 0;
}

static
VOID
XhciSetSlotContext(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device,
    _In_ ULONG Entries)
{
    volatile ULONG *Context = XhciInputContext(Adapter, 1);

    Context[0] = Device->Route | ((ULONG)Device->Speed << 20) | (Device->Hub ? XHCI_SLOT_HUB : 0) | (Entries << 27);
    Context[1] = ((ULONG)Device->Port << 16) | ((ULONG)Device->Ports << 24);
    Context[2] = Device->TtSlot | ((ULONG)Device->TtPort << 8) | ((ULONG)Device->ThinkTime << 16);
    Context[3] = 0;
}

static
VOID
XhciSetEndpointContext(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint,
    _In_ ULONG Type,
    _In_ ULONG Average)
{
    volatile ULONG *Context = XhciInputContext(Adapter, Endpoint->Dci + 1);

    Context[0] = 0;
    Context[1] = (EP_ERROR_COUNT << 1) | (Type << 3) | ((ULONG)Endpoint->MaxBurst << 8) |
                 ((ULONG)Endpoint->MaxPacket << 16);
    Context[2] = (ULONG)Endpoint->Ring.Physical | Endpoint->Ring.Cycle;
    Context[3] = (ULONG)(Endpoint->Ring.Physical >> 32);
    Context[4] = Average;
}

static
ULONG
XhciSlotControl(
    _In_ PUSBNET_DEVICE Device,
    _In_ ULONG Type)
{
    return (Type << TRB_TYPE_SHIFT) | ((ULONG)Device->Slot << TRB_SLOT_SHIFT);
}

NTSTATUS
XhciAddressDevice(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device)
{
    NTSTATUS Status;
    ULONG i;

    Status = XhciCommand(Adapter, 0, 0, TRB_ENABLE_SLOT << TRB_TYPE_SHIFT);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Adapter->CommandSlot || Adapter->CommandSlot > Adapter->MaxSlots)
        return STATUS_DEVICE_PROTOCOL_ERROR;

    Device->Slot = Adapter->CommandSlot;
    for (i = 0; i < XHCI_DEVICE_CONTEXTS * Adapter->ContextSize; i++)
        ((volatile UCHAR *)Device->Context)[i] = 0;

    Adapter->Dcbaa[Device->Slot] = XhciPhysical(Device->Context);

    Device->Ep0.Dci = 1;
    Device->Ep0.MaxBurst = 0;
    switch (Device->Speed)
    {
        case XHCI_SPEED_LOW:
            Device->Ep0.MaxPacket = 8;
            break;

        case XHCI_SPEED_FULL:
        case XHCI_SPEED_HIGH:
            Device->Ep0.MaxPacket = 64;
            break;

        default:
            Device->Ep0.MaxPacket = 512;
            break;
    }

    XhciInitializeRing(&Device->Ep0.Ring, Device->Ep0.Ring.Trbs);
    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 0x3;
    XhciSetSlotContext(Adapter, Device, 1);
    XhciSetEndpointContext(Adapter, &Device->Ep0, EP_TYPE_CONTROL, EP_CONTROL_AVERAGE);
    KeMemoryBarrier();

    Status = XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                         XhciSlotControl(Device, TRB_ADDRESS_DEVICE));
    if (NT_SUCCESS(Status))
        XhciStall(XHCI_ADDRESS_RECOVERY);

    return Status;
}

NTSTATUS
XhciUpdateControlPacketSize(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device,
    _In_ USHORT MaxPacket)
{
    if (MaxPacket == Device->Ep0.MaxPacket)
        return STATUS_SUCCESS;

    Device->Ep0.MaxPacket = MaxPacket;
    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 0x2;
    XhciSetEndpointContext(Adapter, &Device->Ep0, EP_TYPE_CONTROL, EP_CONTROL_AVERAGE);
    KeMemoryBarrier();

    return XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                       XhciSlotControl(Device, TRB_EVALUATE_CONTEXT));
}

NTSTATUS
XhciConfigureHub(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device)
{
    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 0x1;
    XhciSetSlotContext(Adapter, Device, 1);
    KeMemoryBarrier();

    return XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                       XhciSlotControl(Device, TRB_CONFIGURE_ENDPOINT));
}

static
ULONG
XhciInterruptInterval(
    _In_ UCHAR Speed,
    _In_ UCHAR Interval)
{
    ULONG Exponent = XHCI_INTERVAL_MIN_FRAME;

    if (Speed != XHCI_SPEED_FULL && Speed != XHCI_SPEED_LOW)
        return Interval ? min(Interval, XHCI_INTERVAL_MAX) - 1 : 0;

    while (Exponent < XHCI_INTERVAL_MAX_FRAME && (2UL << Exponent) <= (ULONG)Interval * 8)
        Exponent++;

    return Exponent;
}

NTSTATUS
XhciConfigureBulk(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUSBNET_DEVICE Device = Adapter->Device;
    PUSBNET_ENDPOINT In = &Adapter->BulkIn, Out = &Adapter->BulkOut, Notify = &Adapter->Status;
    ULONG Mask, Entries, Payload;
    NTSTATUS Status;

    In->Dci = (UCHAR)((In->Address & 0x0F) * 2 + 1);
    Out->Dci = (UCHAR)((Out->Address & 0x0F) * 2);
    Notify->Dci = Notify->MaxPacket ? (UCHAR)((Notify->Address & 0x0F) * 2 + 1) : 0;
    XhciInitializeRing(&In->Ring, In->Ring.Trbs);
    XhciInitializeRing(&Out->Ring, Out->Ring.Trbs);

    Mask = 1 | (1 << In->Dci) | (1 << Out->Dci);
    Entries = max(In->Dci, Out->Dci);
    if (Notify->Dci)
    {
        XhciInitializeRing(&Notify->Ring, Notify->Ring.Trbs);
        Mask |= 1 << Notify->Dci;
        Entries = max(Entries, Notify->Dci);
    }

    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = Mask;
    XhciSetSlotContext(Adapter, Device, Entries);
    XhciSetEndpointContext(Adapter, In, EP_TYPE_BULK_IN, EP_BULK_AVERAGE);
    XhciSetEndpointContext(Adapter, Out, EP_TYPE_BULK_OUT, EP_BULK_AVERAGE);
    if (Notify->Dci)
    {
        Payload = (ULONG)Notify->MaxPacket * (Notify->MaxBurst + 1);
        XhciSetEndpointContext(Adapter, Notify, EP_TYPE_INTERRUPT_IN, Payload | (Payload << 16));
        XhciInputContext(Adapter, Notify->Dci + 1)[0] =
            XhciInterruptInterval(Device->Speed, Adapter->Function.StatusInterval) << 16;
    }

    KeMemoryBarrier();

    Status = XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                         XhciSlotControl(Device, TRB_CONFIGURE_ENDPOINT));
    if (!NT_SUCCESS(Status))
    {
        In->Dci = 0;
        Out->Dci = 0;
        Notify->Dci = 0;
    }

    return Status;
}

VOID
XhciDisableSlot(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device)
{
    if (!Device->Slot)
        return;

    XhciCommand(Adapter, 0, 0, XhciSlotControl(Device, TRB_DISABLE_SLOT));
    Adapter->Dcbaa[Device->Slot] = 0;
    Device->Slot = 0;
    if (Device == Adapter->Device)
    {
        Adapter->BulkIn.Dci = 0;
        Adapter->BulkOut.Dci = 0;
        Adapter->Status.Dci = 0;
        Adapter->StatusActive = FALSE;
    }
}

NTSTATUS
XhciRecoverEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device,
    _In_ PUSBNET_ENDPOINT Endpoint)
{
    volatile ULONG *Context = (volatile ULONG *)(Device->Context + Endpoint->Dci * Adapter->ContextSize);
    PUSBNET_RING Ring = &Endpoint->Ring;
    ULONG Control = (ULONG)Endpoint->Dci << TRB_ENDPOINT_SHIFT;
    ULONG State;

    Endpoint->Done = TRUE;
    State = Context[0] & EP_STATE_MASK;
    if (State == EP_STATE_HALTED)
        XhciCommand(Adapter, 0, 0, XhciSlotControl(Device, TRB_RESET_ENDPOINT) | Control);
    else if (State == EP_STATE_RUNNING)
        XhciCommand(Adapter, 0, 0, XhciSlotControl(Device, TRB_STOP_ENDPOINT) | Control);

    return XhciCommand(Adapter, (Ring->Physical + Ring->Enqueue * USBNET_TRB_SIZE) | Ring->Cycle, 0,
                       XhciSlotControl(Device, TRB_SET_DEQUEUE) | Control);
}

BOOLEAN
XhciPollEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint)
{
    XhciProcessEvents(Adapter);
    return Endpoint->Done;
}

NTSTATUS
XhciWaitEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint,
    _In_ ULONG Milliseconds)
{
    ULONG i;

    for (i = 0; i < Milliseconds * (1000 / XHCI_STEP); i++)
    {
        if (XhciPollEndpoint(Adapter, Endpoint))
        {
            return Endpoint->Completion == XHCI_CC_SUCCESS || Endpoint->Completion == XHCI_CC_SHORT_PACKET ?
                   STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }

        XhciStall(XHCI_STEP);
    }

    Endpoint->Completion = XHCI_CC_TIMEOUT;
    return STATUS_IO_TIMEOUT;
}

NTSTATUS
XhciDeviceControl(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_DEVICE Device,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Transferred)
{
    PUSBNET_ENDPOINT Endpoint = &Device->Ep0;
    BOOLEAN In = (RequestType & USB_ENDPOINT_DIRECTION_MASK) != 0;
    ULONG64 Setup;
    ULONG Transfer;
    NTSTATUS Status;

    if (Transferred)
        *Transferred = 0;

    if (Length > USBNET_CONTROL_SIZE || !Device->Slot)
        return STATUS_INVALID_PARAMETER;

    Transfer = !Length ? TRB_SETUP_NO_DATA : In ? TRB_SETUP_IN_DATA : TRB_SETUP_OUT_DATA;
    Setup = RequestType | ((ULONG)Request << 8) | ((ULONG)Value << 16) |
            ((ULONG64)(Index | ((ULONG)Length << 16)) << 32);

    Endpoint->Done = FALSE;
    Endpoint->Residual = 0;
    Endpoint->Completion = 0;
    Endpoint->DataTrb = 0;
    Endpoint->FirstTrb = XhciQueueTrb(&Endpoint->Ring, Setup, XhciTransferStatus(Adapter, 8),
                                      (TRB_SETUP << TRB_TYPE_SHIFT) | TRB_IMMEDIATE |
                                      (Transfer << TRB_TRANSFER_SHIFT));
    if (Length)
    {
        Endpoint->DataTrb = XhciQueueTrb(&Endpoint->Ring, XhciPhysical(Adapter->Control),
                                         XhciTransferStatus(Adapter, Length),
                                         (TRB_DATA << TRB_TYPE_SHIFT) |
                                         (In ? TRB_DIRECTION_IN | TRB_SHORT_PACKET : 0));
    }

    Endpoint->LastTrb = XhciQueueTrb(&Endpoint->Ring, 0, XhciTransferStatus(Adapter, 0),
                                     (TRB_STATUS << TRB_TYPE_SHIFT) | TRB_IOC |
                                     (Length && In ? 0 : TRB_DIRECTION_IN));
    XhciRing(Adapter, Device->Slot, Endpoint->Dci);

    Status = XhciWaitEndpoint(Adapter, Endpoint, XHCI_CONTROL_WAIT);
    if (!NT_SUCCESS(Status))
    {
        XhciRecoverEndpoint(Adapter, Device, Endpoint);
        return Status;
    }

    if (Transferred)
        *Transferred = Endpoint->Residual <= Length ? Length - Endpoint->Residual : 0;

    return STATUS_SUCCESS;
}

NTSTATUS
XhciControl(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Transferred)
{
    return XhciDeviceControl(Adapter, Adapter->Device, RequestType, Request, Value, Index, Length, Transferred);
}

VOID
XhciQueueBulk(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Terminate)
{
    ULONG Control = (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC;

    if (Endpoint != &Adapter->BulkOut)
        Control |= TRB_SHORT_PACKET;

    Endpoint->Done = FALSE;
    Endpoint->Residual = 0;
    Endpoint->Completion = 0;
    Endpoint->FirstTrb = XhciQueueTrb(&Endpoint->Ring, XhciPhysical(Buffer), XhciTransferStatus(Adapter, Length),
                                      Control);
    Endpoint->DataTrb = Endpoint->FirstTrb;
    Endpoint->LastTrb = Endpoint->FirstTrb;
    if (Terminate)
        Endpoint->LastTrb = XhciQueueTrb(&Endpoint->Ring, 0, XhciTransferStatus(Adapter, 0),
                                         (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC);

    XhciRing(Adapter, Adapter->Device->Slot, Endpoint->Dci);
}
