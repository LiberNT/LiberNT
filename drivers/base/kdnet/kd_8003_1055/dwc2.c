/*
 * PROJECT:     LiberNT LAN95xx USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Synopsys DWC2 host controller in buffer DMA mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdlan.h"

#define DWC_STEP                10
#define DWC_RESET_WAIT          10000
#define DWC_FLUSH_WAIT          1000
#define DWC_HALT_WAIT           10000
#define DWC_TRANSFER_WAIT       50000
#define DWC_RETRY_DELAY         125

ULONG
LanRead(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Registers + Register));
}

VOID
LanWrite(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Registers + Register), Value);
}

VOID
LanStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

ULONG
LanPhysical(
    _In_ PVOID Address)
{
    return KdNetExtensibilityImports->GetPhysicalAddress(Address).LowPart;
}

static
BOOLEAN
DwcWait(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if ((LanRead(Adapter, Register) & Mask) == Value)
            return TRUE;

        LanStall(DWC_STEP);
    }

    return FALSE;
}

static
VOID
DwcWritePort(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Set,
    _In_ ULONG Clear)
{
    ULONG Port = LanRead(Adapter, DWC_HPRT0) & ~(DWC_HPRT_WRITE_CLEAR | Clear);

    LanWrite(Adapter, DWC_HPRT0, Port | Set);
}

VOID
DwcHalt(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Channel)
{
    ULONG Character = LanRead(Adapter, DWC_HCCHAR(Channel));

    if (Character & DWC_HCCHAR_ENABLE)
    {
        LanWrite(Adapter, DWC_HCCHAR(Channel), Character | DWC_HCCHAR_DISABLE | DWC_HCCHAR_ENABLE);
        DwcWait(Adapter, DWC_HCCHAR(Channel), DWC_HCCHAR_ENABLE, 0, DWC_HALT_WAIT);
    }

    LanWrite(Adapter, DWC_HCINTMSK(Channel), 0);
    LanWrite(Adapter, DWC_HCINT(Channel), DWC_HCINT_ALL);
}

static
BOOLEAN
DwcResetPort(
    _In_ PLAN_ADAPTER Adapter)
{
    ULONG Port;

    DwcWritePort(Adapter, DWC_HPRT_POWER, 0);
    if (!DwcWait(Adapter, DWC_HPRT0, DWC_HPRT_CONNECTED, DWC_HPRT_CONNECTED, 50000))
        return FALSE;

    LanStall(100000);
    DwcWritePort(Adapter, DWC_HPRT_RESET | DWC_HPRT_POWER, 0);
    LanStall(60000);
    DwcWritePort(Adapter, 0, DWC_HPRT_RESET);
    LanStall(20000);

    Port = LanRead(Adapter, DWC_HPRT0);
    if (!(Port & DWC_HPRT_ENABLED))
        return FALSE;

    LanWrite(Adapter, DWC_HPRT0,
             (Port & ~DWC_HPRT_WRITE_CLEAR) | DWC_HPRT_CONNECT_CHANGE | DWC_HPRT_ENABLE_CHANGE |
             DWC_HPRT_OVERCURRENT_CHANGE);
    Adapter->HighSpeed = ((Port >> DWC_HPRT_SPEED_SHIFT) & DWC_HPRT_SPEED_MASK) == DWC_HPRT_SPEED_HIGH;
    return TRUE;
}

NTSTATUS
DwcInitialize(
    _In_ PLAN_ADAPTER Adapter)
{
    ULONG Configuration, Depth, Value, Channel;

    if ((LanRead(Adapter, DWC_GSNPSID) & DWC_GSNPSID_MASK) != DWC_GSNPSID_VALUE)
        return STATUS_NO_SUCH_DEVICE;

    LanWrite(Adapter, DWC_GAHBCFG, LanRead(Adapter, DWC_GAHBCFG) & ~DWC_GAHBCFG_GLOBAL_ENABLE);
    LanWrite(Adapter, DWC_GINTMSK, 0);
    LanWrite(Adapter, DWC_PCGCCTL, 0);
    if (!DwcWait(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_AHB_IDLE, DWC_GRSTCTL_AHB_IDLE, DWC_RESET_WAIT))
        return STATUS_IO_TIMEOUT;

    LanWrite(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_CORE_RESET);
    if (!DwcWait(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_CORE_RESET, 0, DWC_RESET_WAIT))
        return STATUS_IO_TIMEOUT;

    LanStall(100000);
    LanWrite(Adapter, DWC_PCGCCTL, 0);
    LanWrite(Adapter, DWC_GUSBCFG, LanRead(Adapter, DWC_GUSBCFG) | DWC_GUSBCFG_FORCE_HOST);
    LanStall(25000);
    if (!(LanRead(Adapter, DWC_GINTSTS) & DWC_GINTSTS_HOST_MODE))
        return STATUS_DEVICE_NOT_READY;

    Configuration = LanRead(Adapter, DWC_GHWCFG2);
    Adapter->Channels = ((Configuration >> DWC_GHWCFG2_CHANNELS_SHIFT) & DWC_GHWCFG2_CHANNELS_MASK) + 1;
    if (Adapter->Channels <= DWC_CHANNEL_TRANSMIT)
        return STATUS_NOT_SUPPORTED;

    LanWrite(Adapter, DWC_HCFG, LanRead(Adapter, DWC_HCFG) & ~DWC_HCFG_CLOCK_MASK);
    Depth = LanRead(Adapter, DWC_GHWCFG3) >> DWC_GHWCFG3_DEPTH_SHIFT;
    if ((Configuration & DWC_GHWCFG2_DYNAMIC_FIFO) &&
        Depth >= DWC_RX_FIFO_SIZE + DWC_NPTX_FIFO_SIZE + DWC_PTX_FIFO_SIZE)
    {
        LanWrite(Adapter, DWC_GRXFSIZ, DWC_RX_FIFO_SIZE);
        LanWrite(Adapter, DWC_GNPTXFSIZ, (DWC_NPTX_FIFO_SIZE << 16) | DWC_RX_FIFO_SIZE);
        LanWrite(Adapter, DWC_HPTXFSIZ, (DWC_PTX_FIFO_SIZE << 16) | (DWC_RX_FIFO_SIZE + DWC_NPTX_FIFO_SIZE));
    }

    Value = LanRead(Adapter, DWC_GAHBCFG) & ~(DWC_GAHBCFG_GLOBAL_ENABLE | DWC_GAHBCFG_BURST_MASK);
    LanWrite(Adapter, DWC_GAHBCFG, Value | DWC_GAHBCFG_DMA_ENABLE | DWC_GAHBCFG_WAIT_AXI_WRITES);

    LanWrite(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_TX_ALL | DWC_GRSTCTL_TX_FLUSH);
    if (!DwcWait(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_TX_FLUSH, 0, DWC_FLUSH_WAIT))
        return STATUS_IO_TIMEOUT;

    LanWrite(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_RX_FLUSH);
    if (!DwcWait(Adapter, DWC_GRSTCTL, DWC_GRSTCTL_RX_FLUSH, 0, DWC_FLUSH_WAIT))
        return STATUS_IO_TIMEOUT;

    for (Channel = 0; Channel < Adapter->Channels; Channel++)
        DwcHalt(Adapter, Channel);

    LanWrite(Adapter, DWC_HAINTMSK, 0);
    LanWrite(Adapter, DWC_GINTSTS, 0xFFFFFFFF);
    return DwcResetPort(Adapter) ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

VOID
DwcStop(
    _In_ PLAN_ADAPTER Adapter)
{
    ULONG Channel;

    for (Channel = 0; Channel < Adapter->Channels; Channel++)
        DwcHalt(Adapter, Channel);
}

VOID
DwcStart(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ ULONG Pid,
    _In_ BOOLEAN In,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Channel = Pipe->Channel;
    ULONG Packets = Length ? (Length + Pipe->MaxPacket - 1) / Pipe->MaxPacket : 1;
    ULONG Character;

    LanWrite(Adapter, DWC_HCINTMSK(Channel), 0);
    LanWrite(Adapter, DWC_HCINT(Channel), DWC_HCINT_ALL);
    LanWrite(Adapter, DWC_HCSPLT(Channel), 0);
    LanWrite(Adapter, DWC_HCTSIZ(Channel),
             (Pid << DWC_HCTSIZ_PID_SHIFT) | (Packets << DWC_HCTSIZ_PACKETS_SHIFT) | Length);
    LanWrite(Adapter, DWC_HCDMA(Channel), LanPhysical(Buffer));
    Character = ((ULONG)Pipe->Address << DWC_HCCHAR_ADDRESS_SHIFT) |
                ((ULONG)Pipe->Endpoint << DWC_HCCHAR_ENDPOINT_SHIFT) |
                ((ULONG)Pipe->Type << DWC_HCCHAR_TYPE_SHIFT) |
                Pipe->MaxPacket | DWC_HCCHAR_ONE_PER_FRAME;
    if (In)
        Character |= DWC_HCCHAR_IN;

    KeMemoryBarrier();
    LanWrite(Adapter, DWC_HCCHAR(Channel), Character | DWC_HCCHAR_ENABLE);
}

BOOLEAN
DwcFinished(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _Out_ PULONG Status,
    _Out_ PULONG Remaining,
    _Out_ PULONG Packets)
{
    ULONG Channel = Pipe->Channel;
    ULONG Value, Size, i;

    *Remaining = 0;
    *Packets = 0;
    Value = LanRead(Adapter, DWC_HCINT(Channel)) & DWC_HCINT_ALL;
    *Status = Value;
    if (!(Value & DWC_HCINT_HALTED))
    {
        if (!(Value & DWC_HCINT_AHB_ERROR))
            return FALSE;

        DwcHalt(Adapter, Channel);
        return TRUE;
    }

    for (i = 0; i < 10 && !(Value & DWC_HCINT_REASONS); i++)
    {
        LanStall(1);
        Value |= LanRead(Adapter, DWC_HCINT(Channel)) & DWC_HCINT_ALL;
    }

    Size = LanRead(Adapter, DWC_HCTSIZ(Channel));
    LanWrite(Adapter, DWC_HCINT(Channel), Value);
    *Status = Value;
    *Remaining = Size & DWC_HCTSIZ_SIZE_MASK;
    *Packets = (Size >> DWC_HCTSIZ_PACKETS_SHIFT) & DWC_HCTSIZ_PACKETS_MASK;
    return TRUE;
}

NTSTATUS
DwcTransfer(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ BOOLEAN Setup,
    _In_ BOOLEAN In,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG Actual)
{
    ULONG Offset = 0, Errors = 0, Waited = 0;
    ULONG Size, Programmed, Status, Remaining, Left, Done, Moved;

    if (Actual)
        *Actual = 0;

    for (;;)
    {
        Size = Length - Offset;
        Programmed = Size ? (Size + Pipe->MaxPacket - 1) / Pipe->MaxPacket : 1;
        DwcStart(Adapter, Pipe,
                 Setup ? DWC_PID_SETUP : (Pipe->Toggle ? DWC_PID_DATA1 : DWC_PID_DATA0),
                 In, Buffer + Offset, Size);
        while (!DwcFinished(Adapter, Pipe, &Status, &Remaining, &Left))
        {
            if (++Waited > DWC_TRANSFER_WAIT)
            {
                DwcHalt(Adapter, Pipe->Channel);
                return STATUS_IO_TIMEOUT;
            }

            LanStall(DWC_STEP);
        }

        if ((Status & DWC_HCINT_AHB_ERROR) || Remaining > Size || Left > Programmed)
            return STATUS_DEVICE_DATA_ERROR;

        Done = Programmed - Left;
        if (Status & DWC_HCINT_COMPLETE)
        {
            if (In)
            {
                Moved = Size - Remaining;
                if (!Done)
                    Done = Moved ? (Moved + Pipe->MaxPacket - 1) / Pipe->MaxPacket : 1;
            }
            else
            {
                Moved = Size;
                Done = Programmed;
            }

            Pipe->Toggle ^= Done & 1;
            if (Actual)
                *Actual = Offset + Moved;

            return STATUS_SUCCESS;
        }

        if (Status & DWC_HCINT_STALL)
            return STATUS_DEVICE_NOT_READY;

        if ((Status & DWC_HCINT_ERRORS) && ++Errors >= DWC_ERROR_LIMIT)
            return STATUS_DEVICE_DATA_ERROR;

        if (!(Status & (DWC_HCINT_ERRORS | DWC_HCINT_NAK | DWC_HCINT_NYET)))
            return STATUS_DEVICE_DATA_ERROR;

        if (In)
            Done = (Size - Remaining) / Pipe->MaxPacket;

        Moved = Done * Pipe->MaxPacket;
        if (Moved > Size)
            Moved = Size;

        Pipe->Toggle ^= Done & 1;
        Offset += Moved;
        if (!In && Size && Offset == Length)
        {
            if (Actual)
                *Actual = Offset;

            return STATUS_SUCCESS;
        }

        Waited += DWC_RETRY_DELAY / DWC_STEP;
        if (Waited > DWC_TRANSFER_WAIT)
            return STATUS_IO_TIMEOUT;

        LanStall(DWC_RETRY_DELAY);
    }
}
