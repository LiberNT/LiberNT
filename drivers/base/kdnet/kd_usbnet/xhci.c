/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled xHCI controller, rings, event ring, commands and root ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define XHCI_STEP               10
#define XHCI_HALT_WAIT          20
#define XHCI_RESET_WAIT         1000
#define XHCI_BIOS_WAIT          1000
#define XHCI_COMMAND_WAIT       1000
#define XHCI_PORT_RESET_WAIT    500
#define XHCI_CONNECT_WAIT       1000
#define XHCI_CONNECT_STABLE     200
#define XHCI_POWER_WAIT         20000
#define XHCI_RESUME_RECOVERY    10000
#define XHCI_RECOVERY_WAIT      10000

ULONG
XhciRead(
    _In_ PUCHAR Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)Register);
}

VOID
XhciWrite(
    _In_ PUCHAR Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)Register, Value);
}

VOID
XhciWrite64(
    _In_ PUCHAR Register,
    _In_ ULONG64 Value)
{
    XhciWrite(Register, (ULONG)Value);
    XhciWrite(Register + sizeof(ULONG), (ULONG)(Value >> 32));
}

VOID
XhciStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

ULONG64
XhciPhysical(
    _In_ PVOID Address)
{
    return KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
BOOLEAN
XhciWaitRegister(
    _In_ PUCHAR Register,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG Milliseconds)
{
    ULONG i;

    for (i = 0; i < Milliseconds * (1000 / XHCI_STEP); i++)
    {
        if ((XhciRead(Register) & Mask) == Value)
            return TRUE;

        XhciStall(XHCI_STEP);
    }

    return FALSE;
}

VOID
XhciInitializeRing(
    _Out_ PUSBNET_RING Ring,
    _In_ PVOID Trbs)
{
    volatile ULONG *Link;
    ULONG i;

    for (i = 0; i < USBNET_RING_SIZE; i++)
        ((volatile UCHAR *)Trbs)[i] = 0;

    Ring->Trbs = Trbs;
    Ring->Physical = XhciPhysical(Trbs);
    Ring->Enqueue = 0;
    Ring->Cycle = TRB_CYCLE;

    Link = Ring->Trbs + (USBNET_RING_TRBS - 1) * 4;
    Link[0] = (ULONG)Ring->Physical;
    Link[1] = (ULONG)(Ring->Physical >> 32);
    Link[2] = 0;
    Link[3] = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_TOGGLE_CYCLE;
}

ULONG64
XhciQueueTrb(
    _Inout_ PUSBNET_RING Ring,
    _In_ ULONG64 Pointer,
    _In_ ULONG Status,
    _In_ ULONG Control)
{
    volatile ULONG *Trb = Ring->Trbs + Ring->Enqueue * 4;
    ULONG64 Address = Ring->Physical + Ring->Enqueue * USBNET_TRB_SIZE;

    Trb[0] = (ULONG)Pointer;
    Trb[1] = (ULONG)(Pointer >> 32);
    Trb[2] = Status;
    KeMemoryBarrier();
    Trb[3] = Control | Ring->Cycle;

    if (++Ring->Enqueue == USBNET_RING_TRBS - 1)
    {
        Trb = Ring->Trbs + Ring->Enqueue * 4;
        KeMemoryBarrier();
        Trb[3] = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_TOGGLE_CYCLE | Ring->Cycle;
        Ring->Enqueue = 0;
        Ring->Cycle ^= TRB_CYCLE;
    }

    return Address;
}

BOOLEAN
XhciSharedLinkReady(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Register = Adapter->Operational + XHCI_OP_PORTSC(Adapter->Share.Port);
    PUCHAR Doorbell = Adapter->Doorbells + Adapter->Device->Slot * sizeof(ULONG);
    ULONG Port;

    if (XhciRead(Adapter->Operational + XHCI_OP_USBSTS) & XHCI_USBSTS_HALTED)
        return FALSE;

    Port = XhciRead(Register);
    if (!(Port & XHCI_PORT_ENABLED) || (Port & XHCI_PORT_LINK_STATE_MASK))
        return FALSE;

    if (Port & XHCI_PORT_LINK_CHANGE)
    {
        XhciStall(XHCI_RESUME_RECOVERY);
        XhciWrite(Register, (Port & XHCI_PORT_PRESERVE_MASK) | XHCI_PORT_LINK_CHANGE);
        KeMemoryBarrier();
        XhciWrite(Doorbell, Adapter->BulkIn.Dci);
        XhciWrite(Doorbell, Adapter->BulkOut.Dci);
        if (Adapter->Status.Dci)
            XhciWrite(Doorbell, Adapter->Status.Dci);
    }

    return TRUE;
}

VOID
XhciRing(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Slot,
    _In_ ULONG Target)
{
    KeMemoryBarrier();
    XhciWrite(Adapter->Doorbells + Slot * sizeof(ULONG), Target);
}

static
PUSBNET_ENDPOINT
XhciFindEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Slot,
    _In_ ULONG Dci)
{
    PUSBNET_DEVICE Device;
    ULONG Tier;

    if (!Slot)
        return NULL;

    for (Tier = 0; Tier < Adapter->Tier; Tier++)
    {
        Device = &Adapter->Devices[Tier];
        if (Slot == Device->Slot && Dci == Device->Ep0.Dci)
            return &Device->Ep0;
    }

    if (Slot != Adapter->Device->Slot)
        return NULL;

    if (Dci == Adapter->Device->Ep0.Dci)
        return &Adapter->Device->Ep0;

    if (Adapter->BulkIn.Dci && Dci == Adapter->BulkIn.Dci)
        return &Adapter->BulkIn;

    if (Adapter->BulkOut.Dci && Dci == Adapter->BulkOut.Dci)
        return &Adapter->BulkOut;

    if (Adapter->Status.Dci && Dci == Adapter->Status.Dci)
        return &Adapter->Status;

    return NULL;
}

static
VOID
XhciHandleEvent(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ const ULONG *Event)
{
    PUSBNET_ENDPOINT Endpoint;
    ULONG64 Pointer = Event[0] | ((ULONG64)Event[1] << 32);
    ULONG Type = (Event[3] >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK;
    UCHAR Code = (UCHAR)(Event[2] >> TRB_CODE_SHIFT);

    if (Type == TRB_COMMAND_EVENT)
    {
        if (!Adapter->CommandDone && Pointer == Adapter->CommandTrb)
        {
            Adapter->CommandCompletion = Code;
            Adapter->CommandSlot = (UCHAR)(Event[3] >> TRB_SLOT_SHIFT);
            Adapter->CommandDone = TRUE;
        }

        return;
    }

    if (Type != TRB_TRANSFER_EVENT)
        return;

    Endpoint = XhciFindEndpoint(Adapter, Event[3] >> TRB_SLOT_SHIFT, (Event[3] >> TRB_ENDPOINT_SHIFT) & 0x1F);
    if (!Endpoint || Endpoint->Done)
        return;

    if (Pointer != Endpoint->FirstTrb && Pointer != Endpoint->DataTrb && Pointer != Endpoint->LastTrb)
        return;

    if (Pointer == Endpoint->DataTrb)
        Endpoint->Residual = Event[2] & TRB_LENGTH_MASK;

    if (Pointer == Endpoint->LastTrb || (Code != XHCI_CC_SUCCESS && Code != XHCI_CC_SHORT_PACKET))
    {
        Endpoint->Completion = Code;
        Endpoint->Done = TRUE;
    }
}

static
VOID
XhciProcessRing(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PULONG Events,
    _In_ ULONG64 Physical,
    _Inout_ PULONG Dequeue,
    _Inout_ PULONG Cycle,
    _In_ ULONG Interrupter)
{
    volatile ULONG *Slot;
    ULONG Event[4];
    BOOLEAN Consumed = FALSE;

    for (;;)
    {
        Slot = Events + *Dequeue * 4;
        Event[3] = Slot[3];
        if ((Event[3] & TRB_CYCLE) != *Cycle)
            break;

        KeMemoryBarrier();
        Event[0] = Slot[0];
        Event[1] = Slot[1];
        Event[2] = Slot[2];
        XhciHandleEvent(Adapter, Event);

        if (++*Dequeue == USBNET_RING_TRBS)
        {
            *Dequeue = 0;
            *Cycle ^= TRB_CYCLE;
        }

        Consumed = TRUE;
    }

    if (Consumed)
    {
        XhciWrite64(Adapter->Runtime + Interrupter * XHCI_RT_INTERRUPTER_SIZE + XHCI_RT_ERDP,
                    (Physical + *Dequeue * USBNET_TRB_SIZE) | XHCI_ERDP_BUSY);
    }
}

VOID
XhciProcessEvents(
    _In_ PUSBNET_ADAPTER Adapter)
{
    if (Adapter->Shared)
    {
        XhciProcessRing(Adapter, Adapter->SharedEvents, Adapter->SharedEventsPhysical,
                        &Adapter->SharedEventDequeue, &Adapter->SharedEventCycle, USBNET_SHARED_INTERRUPTER);
    }
    else
    {
        XhciProcessRing(Adapter, Adapter->Events, Adapter->EventsPhysical, &Adapter->EventDequeue,
                        &Adapter->EventCycle, 0);
    }
}

NTSTATUS
XhciCommand(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG64 Pointer,
    _In_ ULONG Status,
    _In_ ULONG Control)
{
    ULONG i;

    if (Adapter->Shared)
        return STATUS_DEVICE_BUSY;

    Adapter->CommandDone = FALSE;
    Adapter->CommandCompletion = 0;
    Adapter->CommandSlot = 0;
    Adapter->CommandTrb = XhciQueueTrb(&Adapter->Command, Pointer, Status, Control);
    XhciRing(Adapter, 0, 0);

    for (i = 0; i < XHCI_COMMAND_WAIT * (1000 / XHCI_STEP); i++)
    {
        XhciProcessEvents(Adapter);
        if (Adapter->CommandDone)
            return Adapter->CommandCompletion == XHCI_CC_SUCCESS ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

        XhciStall(XHCI_STEP);
    }

    return STATUS_IO_TIMEOUT;
}

static
VOID
XhciTakeOwnership(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Capabilities)
{
    PUCHAR Register;
    ULONG Offset = (Capabilities >> XHCI_HCC_XECP_SHIFT) << 2;
    ULONG Value, Next;

    while (Offset)
    {
        Register = Adapter->Registers + Offset;
        Value = XhciRead(Register);
        if ((Value & 0xFF) == XHCI_EXT_CAP_LEGACY)
        {
            if (Value & XHCI_LEGACY_BIOS_OWNED)
            {
                XhciWrite(Register, Value | XHCI_LEGACY_OS_OWNED);
                if (!XhciWaitRegister(Register, XHCI_LEGACY_BIOS_OWNED, 0, XHCI_BIOS_WAIT))
                    XhciWrite(Register, (XhciRead(Register) & ~XHCI_LEGACY_BIOS_OWNED) | XHCI_LEGACY_OS_OWNED);
            }

            Value = XhciRead(Register + sizeof(ULONG));
            XhciWrite(Register + sizeof(ULONG), (Value & XHCI_LEGACY_PRESERVE) | XHCI_LEGACY_SMI_EVENTS);
            return;
        }

        Next = (Value >> 8) & 0xFF;
        Offset = Next ? Offset + (Next << 2) : 0;
    }
}

static
NTSTATUS
XhciReset(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Command = Adapter->Operational + XHCI_OP_USBCMD;
    PUCHAR Status = Adapter->Operational + XHCI_OP_USBSTS;

    XhciWrite(Command, XhciRead(Command) & ~XHCI_USBCMD_RUN);
    if (!XhciWaitRegister(Status, XHCI_USBSTS_HALTED, XHCI_USBSTS_HALTED, XHCI_HALT_WAIT))
        return STATUS_IO_TIMEOUT;

    XhciWrite(Command, XHCI_USBCMD_RESET);
    XhciStall(1000);
    if (!XhciWaitRegister(Command, XHCI_USBCMD_RESET, 0, XHCI_RESET_WAIT) ||
        !XhciWaitRegister(Status, XHCI_USBSTS_NOT_READY, 0, XHCI_RESET_WAIT))
    {
        return STATUS_IO_TIMEOUT;
    }

    return STATUS_SUCCESS;
}

static
VOID
XhciInitializeEventRing(
    _In_ PUCHAR Interrupter,
    _Out_writes_(4) PULONG Erst,
    _Out_writes_(USBNET_RING_TRBS * 4) PULONG Events,
    _Out_ PULONG64 Physical,
    _Out_ PULONG Dequeue,
    _Out_ PULONG Cycle)
{
    ULONG i, Value;

    for (i = 0; i < USBNET_RING_TRBS * 4; i++)
        ((volatile ULONG *)Events)[i] = 0;

    *Physical = XhciPhysical(Events);
    *Dequeue = 0;
    *Cycle = TRB_CYCLE;
    Erst[0] = (ULONG)*Physical;
    Erst[1] = (ULONG)(*Physical >> 32);
    Erst[2] = USBNET_RING_TRBS;
    Erst[3] = 0;
    KeMemoryBarrier();

    XhciWrite(Interrupter + XHCI_RT_IMAN, 1);
    Value = XhciRead(Interrupter + XHCI_RT_ERSTSZ);
    XhciWrite(Interrupter + XHCI_RT_ERSTSZ, (Value & 0xFFFF0000) | 1);
    XhciWrite64(Interrupter + XHCI_RT_ERDP, *Physical);
    XhciWrite64(Interrupter + XHCI_RT_ERSTBA, XhciPhysical(Erst));
}

NTSTATUS
XhciInitialize(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Registers = Adapter->Registers;
    PUCHAR Operational, Runtime;
    ULONG Params1, Params2, Capabilities, Value, Port, i;
    NTSTATUS Status;

    Adapter->Operational = Operational = Registers + (XhciRead(Registers + XHCI_CAP_LENGTH) & 0xFF);
    Params1 = XhciRead(Registers + XHCI_CAP_HCSPARAMS1);
    Params2 = XhciRead(Registers + XHCI_CAP_HCSPARAMS2);
    Capabilities = XhciRead(Registers + XHCI_CAP_HCCPARAMS1);
    Adapter->Runtime = Runtime = Registers + (XhciRead(Registers + XHCI_CAP_RTSOFF) & ~0x1F);
    Adapter->Doorbells = Registers + (XhciRead(Registers + XHCI_CAP_DBOFF) & ~0x3);
    /* FIXME: a controller with more than USBNET_MAX_SLOTS slots leaves the OS that many while it keeps the debugger's MaxSlotsEn */
    Adapter->MaxSlots = min(Params1 & 0xFF, USBNET_MAX_SLOTS);
    Adapter->MaxPorts = Params1 >> 24;
    Adapter->Interrupters = XHCI_HCS1_INTERRUPTERS(Params1);
    Adapter->ContextSize = (Capabilities & XHCI_HCC_CONTEXT_64) ? 64 : 32;
    Adapter->Scratchpads = (((Params2 >> 21) & 0x1F) << 5) | ((Params2 >> 27) & 0x1F);

    if (Params1 == 0xFFFFFFFF || !Adapter->MaxSlots || !Adapter->MaxPorts)
        return STATUS_DEVICE_NOT_READY;

    if (Adapter->Scratchpads > USBNET_MAX_SCRATCHPAD ||
        !(XhciRead(Operational + XHCI_OP_PAGESIZE) & XHCI_PAGESIZE_4K))
    {
        return STATUS_NOT_SUPPORTED;
    }

    XhciTakeOwnership(Adapter, Capabilities);
    Status = XhciReset(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Value = XhciRead(Operational + XHCI_OP_CONFIG);
    XhciWrite(Operational + XHCI_OP_CONFIG, (Value & ~0xFF) | Adapter->MaxSlots);

    for (i = 0; i <= USBNET_MAX_SLOTS; i++)
        Adapter->Dcbaa[i] = 0;

    for (i = 0; i < Adapter->Scratchpads; i++)
        Adapter->ScratchpadArray[i] = XhciPhysical(Adapter->ScratchpadPages + i * PAGE_SIZE);

    if (Adapter->Scratchpads)
        Adapter->Dcbaa[0] = XhciPhysical(Adapter->ScratchpadArray);

    KeMemoryBarrier();
    XhciWrite64(Operational + XHCI_OP_DCBAAP, XhciPhysical(Adapter->Dcbaa));

    XhciInitializeRing(&Adapter->Command, Adapter->Command.Trbs);
    XhciWrite64(Operational + XHCI_OP_CRCR, Adapter->Command.Physical | TRB_CYCLE);

    XhciInitializeEventRing(Runtime, Adapter->Erst, Adapter->Events, &Adapter->EventsPhysical,
                            &Adapter->EventDequeue, &Adapter->EventCycle);
    if (Adapter->Interrupters > USBNET_SHARED_INTERRUPTER)
    {
        XhciInitializeEventRing(Runtime + USBNET_SHARED_INTERRUPTER * XHCI_RT_INTERRUPTER_SIZE, Adapter->SharedErst,
                                Adapter->SharedEvents, &Adapter->SharedEventsPhysical,
                                &Adapter->SharedEventDequeue, &Adapter->SharedEventCycle);
    }

    XhciWrite(Operational + XHCI_OP_USBCMD, XHCI_USBCMD_RUN);
    if (!XhciWaitRegister(Operational + XHCI_OP_USBSTS, XHCI_USBSTS_HALTED, 0, XHCI_HALT_WAIT))
        return STATUS_IO_TIMEOUT;

    Adapter->Running = TRUE;
    if (Capabilities & XHCI_HCC_PORT_POWER)
    {
        for (Port = 1; Port <= Adapter->MaxPorts; Port++)
        {
            Value = XhciRead(Operational + XHCI_OP_PORTSC(Port));
            if (!(Value & XHCI_PORT_POWER))
            {
                XhciWrite(Operational + XHCI_OP_PORTSC(Port),
                          (Value & (XHCI_PORT_INDICATOR | XHCI_PORT_WAKE_MASK)) | XHCI_PORT_POWER);
            }
        }

        XhciStall(XHCI_POWER_WAIT);
    }

    return STATUS_SUCCESS;
}

VOID
XhciHalt(
    _In_ PUSBNET_ADAPTER Adapter)
{
    if (!Adapter->Operational)
        return;

    if (!Adapter->Shared)
        XhciReset(Adapter);

    Adapter->Running = FALSE;
}

ULONG
XhciWaitForConnections(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Count = 0, Previous = 0, Stable = 0, Port, i;

    for (i = 0; i < XHCI_CONNECT_WAIT / 10; i++)
    {
        Count = 0;
        for (Port = 1; Port <= Adapter->MaxPorts; Port++)
        {
            if (XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Port)) & XHCI_PORT_CONNECTED)
                Count++;
        }

        if (Count && Count == Previous)
        {
            Stable += 10;
            if (Stable >= XHCI_CONNECT_STABLE)
                break;
        }
        else
        {
            Stable = 0;
        }

        Previous = Count;
        XhciStall(10000);
    }

    return Count;
}

NTSTATUS
XhciResetPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port,
    _Out_ PUCHAR Speed)
{
    PUCHAR Register = Adapter->Operational + XHCI_OP_PORTSC(Port);
    ULONG Value, Neutral, i;

    *Speed = 0;
    if (Adapter->Shared)
        return STATUS_DEVICE_BUSY;

    Value = XhciRead(Register);
    if (!(Value & XHCI_PORT_CONNECTED))
        return STATUS_NO_SUCH_DEVICE;

    Neutral = Value & XHCI_PORT_PRESERVE_MASK;
    XhciWrite(Register, Neutral | (Value & XHCI_PORT_CHANGE_MASK));

    XhciWrite(Register, Neutral | XHCI_PORT_RESET);
    for (i = 0; i < XHCI_PORT_RESET_WAIT; i++)
    {
        Value = XhciRead(Register);
        if (Value & XHCI_PORT_RESET_CHANGE)
            break;

        XhciStall(1000);
    }

    if (i == XHCI_PORT_RESET_WAIT)
        return STATUS_IO_TIMEOUT;

    XhciWrite(Register, Neutral | (Value & XHCI_PORT_CHANGE_MASK));
    XhciStall(XHCI_RECOVERY_WAIT);
    Value = XhciRead(Register);

    if (!(Value & XHCI_PORT_CONNECTED) || !(Value & XHCI_PORT_ENABLED))
        return STATUS_DEVICE_NOT_CONNECTED;

    *Speed = (UCHAR)((Value >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK);
    return *Speed ? STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED;
}

BOOLEAN
XhciPortAlive(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port)
{
    ULONG Value = XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Port));

    return (Value & (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) == (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED) &&
           !(Value & XHCI_PORT_CONNECT_CHANGE);
}

BOOLEAN
XhciPortChanged(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port)
{
    ULONG Value = XhciRead(Adapter->Operational + XHCI_OP_PORTSC(Port));

    return !(Value & XHCI_PORT_CONNECTED) || (Value & XHCI_PORT_CONNECT_CHANGE);
}

VOID
XhciShareController(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PKD_USB_SHARE Share = &Adapter->Share;
    PUSBNET_DEVICE Device;
    ULONG Tier;

    if (Adapter->Interrupters <= USBNET_SHARED_INTERRUPTER)
        return;

    Share->Signature = KD_USB_SHARE_SIGNATURE;
    Share->Size = sizeof(*Share);
    Share->Port = Adapter->Devices[0].Port;
    Share->Interrupter = USBNET_SHARED_INTERRUPTER;
    Share->Phone = Adapter->Phone;
    Share->SlotCount = 0;
    for (Tier = 0; Tier <= Adapter->Tier && Tier < KD_USB_SHARE_MAX_SLOTS; Tier++)
    {
        Device = &Adapter->Devices[Tier];
        if (!Device->Slot)
            continue;

        Share->Slots[Share->SlotCount] = Device->Slot;
        Share->DeviceContexts[Share->SlotCount] = Adapter->Dcbaa[Device->Slot];
        Share->ContextAddresses[Share->SlotCount] = (ULONG64)(ULONG_PTR)Device->Context;
        Share->Endpoints[Share->SlotCount] = Device != Adapter->Device ? 0 :
                                             (1UL << Adapter->BulkIn.Dci) | (1UL << Adapter->BulkOut.Dci) |
                                             (Adapter->Status.Dci ? 1UL << Adapter->Status.Dci : 0);
        Share->SlotCount++;
    }

    Share->Dcbaa = XhciPhysical(Adapter->Dcbaa);
    Share->ScratchpadArray = Adapter->Dcbaa[0];
    Adapter->Shared = TRUE;
    Adapter->KdNet->Device->OemData = Share;
    Adapter->KdNet->Device->OemDataLength = sizeof(*Share);
    UsbNetTrace("USB debugger: sharing the controller with the OS, root port %lu and %lu slot(s) stay with the "
                "debugger\n", Share->Port, Share->SlotCount);
}
