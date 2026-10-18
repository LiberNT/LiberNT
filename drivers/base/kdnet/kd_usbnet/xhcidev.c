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
    _In_ ULONG Entries)
{
    volatile ULONG *Context = XhciInputContext(Adapter, 1);

    Context[0] = ((ULONG)Adapter->Speed << 20) | (Entries << 27);
    Context[1] = (ULONG)Adapter->Port << 16;
    Context[2] = 0;
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
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Type)
{
    return (Type << TRB_TYPE_SHIFT) | ((ULONG)Adapter->Slot << TRB_SLOT_SHIFT);
}

NTSTATUS
XhciAddressDevice(
    _In_ PUSBNET_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG i;

    Status = XhciCommand(Adapter, 0, 0, TRB_ENABLE_SLOT << TRB_TYPE_SHIFT);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Adapter->CommandSlot || Adapter->CommandSlot > Adapter->MaxSlots)
        return STATUS_DEVICE_PROTOCOL_ERROR;

    Adapter->Slot = Adapter->CommandSlot;
    for (i = 0; i < XHCI_DEVICE_CONTEXTS * Adapter->ContextSize; i++)
        ((volatile UCHAR *)Adapter->DeviceContext)[i] = 0;

    Adapter->Dcbaa[Adapter->Slot] = XhciPhysical(Adapter->DeviceContext);

    Adapter->Ep0.Dci = 1;
    Adapter->Ep0.MaxBurst = 0;
    switch (Adapter->Speed)
    {
        case XHCI_SPEED_LOW:
            Adapter->Ep0.MaxPacket = 8;
            break;

        case XHCI_SPEED_FULL:
        case XHCI_SPEED_HIGH:
            Adapter->Ep0.MaxPacket = 64;
            break;

        default:
            Adapter->Ep0.MaxPacket = 512;
            break;
    }

    XhciInitializeRing(&Adapter->Ep0.Ring, Adapter->Ep0.Ring.Trbs);
    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 0x3;
    XhciSetSlotContext(Adapter, 1);
    XhciSetEndpointContext(Adapter, &Adapter->Ep0, EP_TYPE_CONTROL, EP_CONTROL_AVERAGE);
    KeMemoryBarrier();

    Status = XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                         XhciSlotControl(Adapter, TRB_ADDRESS_DEVICE));
    if (NT_SUCCESS(Status))
        XhciStall(XHCI_ADDRESS_RECOVERY);

    return Status;
}

NTSTATUS
XhciUpdateControlPacketSize(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ USHORT MaxPacket)
{
    if (MaxPacket == Adapter->Ep0.MaxPacket)
        return STATUS_SUCCESS;

    Adapter->Ep0.MaxPacket = MaxPacket;
    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 0x2;
    XhciSetEndpointContext(Adapter, &Adapter->Ep0, EP_TYPE_CONTROL, EP_CONTROL_AVERAGE);
    KeMemoryBarrier();

    return XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                       XhciSlotControl(Adapter, TRB_EVALUATE_CONTEXT));
}

NTSTATUS
XhciConfigureBulk(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUSBNET_ENDPOINT In = &Adapter->BulkIn, Out = &Adapter->BulkOut;
    NTSTATUS Status;

    In->Dci = (UCHAR)((In->Address & 0x0F) * 2 + 1);
    Out->Dci = (UCHAR)((Out->Address & 0x0F) * 2);
    XhciInitializeRing(&In->Ring, In->Ring.Trbs);
    XhciInitializeRing(&Out->Ring, Out->Ring.Trbs);

    XhciClearInput(Adapter);
    XhciInputContext(Adapter, 0)[1] = 1 | (1 << In->Dci) | (1 << Out->Dci);
    XhciSetSlotContext(Adapter, max(In->Dci, Out->Dci));
    XhciSetEndpointContext(Adapter, In, EP_TYPE_BULK_IN, EP_BULK_AVERAGE);
    XhciSetEndpointContext(Adapter, Out, EP_TYPE_BULK_OUT, EP_BULK_AVERAGE);
    KeMemoryBarrier();

    Status = XhciCommand(Adapter, XhciPhysical(Adapter->InputContext), 0,
                         XhciSlotControl(Adapter, TRB_CONFIGURE_ENDPOINT));
    if (!NT_SUCCESS(Status))
    {
        In->Dci = 0;
        Out->Dci = 0;
    }

    return Status;
}

VOID
XhciDisableSlot(
    _In_ PUSBNET_ADAPTER Adapter)
{
    if (!Adapter->Slot)
        return;

    XhciCommand(Adapter, 0, 0, XhciSlotControl(Adapter, TRB_DISABLE_SLOT));
    Adapter->Dcbaa[Adapter->Slot] = 0;
    Adapter->Slot = 0;
    Adapter->BulkIn.Dci = 0;
    Adapter->BulkOut.Dci = 0;
}

NTSTATUS
XhciRecoverEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint)
{
    volatile ULONG *Context = (volatile ULONG *)(Adapter->DeviceContext + Endpoint->Dci * Adapter->ContextSize);
    PUSBNET_RING Ring = &Endpoint->Ring;
    ULONG Control = (ULONG)Endpoint->Dci << TRB_ENDPOINT_SHIFT;
    ULONG State;

    Endpoint->Done = TRUE;
    State = Context[0] & EP_STATE_MASK;
    if (State == EP_STATE_HALTED)
        XhciCommand(Adapter, 0, 0, XhciSlotControl(Adapter, TRB_RESET_ENDPOINT) | Control);
    else if (State == EP_STATE_RUNNING)
        XhciCommand(Adapter, 0, 0, XhciSlotControl(Adapter, TRB_STOP_ENDPOINT) | Control);

    return XhciCommand(Adapter, (Ring->Physical + Ring->Enqueue * USBNET_TRB_SIZE) | Ring->Cycle, 0,
                       XhciSlotControl(Adapter, TRB_SET_DEQUEUE) | Control);
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
XhciControl(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Transferred)
{
    PUSBNET_ENDPOINT Endpoint = &Adapter->Ep0;
    BOOLEAN In = (RequestType & USB_ENDPOINT_DIRECTION_MASK) != 0;
    ULONG64 Setup;
    ULONG Transfer;
    NTSTATUS Status;

    if (Transferred)
        *Transferred = 0;

    if (Length > USBNET_CONTROL_SIZE || !Adapter->Slot)
        return STATUS_INVALID_PARAMETER;

    Transfer = !Length ? TRB_SETUP_NO_DATA : In ? TRB_SETUP_IN_DATA : TRB_SETUP_OUT_DATA;
    Setup = RequestType | ((ULONG)Request << 8) | ((ULONG)Value << 16) |
            ((ULONG64)(Index | ((ULONG)Length << 16)) << 32);

    Endpoint->Done = FALSE;
    Endpoint->Residual = 0;
    Endpoint->Completion = 0;
    Endpoint->DataTrb = 0;
    Endpoint->FirstTrb = XhciQueueTrb(&Endpoint->Ring, Setup, 8,
                                      (TRB_SETUP << TRB_TYPE_SHIFT) | TRB_IMMEDIATE |
                                      (Transfer << TRB_TRANSFER_SHIFT));
    if (Length)
    {
        Endpoint->DataTrb = XhciQueueTrb(&Endpoint->Ring, XhciPhysical(Adapter->Control), Length,
                                         (TRB_DATA << TRB_TYPE_SHIFT) |
                                         (In ? TRB_DIRECTION_IN | TRB_SHORT_PACKET : 0));
    }

    Endpoint->LastTrb = XhciQueueTrb(&Endpoint->Ring, 0, 0,
                                     (TRB_STATUS << TRB_TYPE_SHIFT) | TRB_IOC |
                                     (Length && In ? 0 : TRB_DIRECTION_IN));
    XhciRing(Adapter, Adapter->Slot, Endpoint->Dci);

    Status = XhciWaitEndpoint(Adapter, Endpoint, XHCI_CONTROL_WAIT);
    if (!NT_SUCCESS(Status))
    {
        XhciRecoverEndpoint(Adapter, Endpoint);
        return Status;
    }

    if (Transferred)
        *Transferred = Endpoint->Residual <= Length ? Length - Endpoint->Residual : 0;

    return STATUS_SUCCESS;
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

    if (Endpoint == &Adapter->BulkIn)
        Control |= TRB_SHORT_PACKET;

    Endpoint->Done = FALSE;
    Endpoint->Residual = 0;
    Endpoint->Completion = 0;
    Endpoint->FirstTrb = XhciQueueTrb(&Endpoint->Ring, XhciPhysical(Buffer), Length, Control);
    Endpoint->DataTrb = Endpoint->FirstTrb;
    Endpoint->LastTrb = Endpoint->FirstTrb;
    if (Terminate)
        Endpoint->LastTrb = XhciQueueTrb(&Endpoint->Ring, 0, 0, (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC);

    XhciRing(Adapter, Adapter->Slot, Endpoint->Dci);
}
