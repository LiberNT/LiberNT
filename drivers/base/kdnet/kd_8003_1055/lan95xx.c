/*
 * PROJECT:     LiberNT LAN95xx USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled LAN95xx Ethernet adapter over the DWC2 host controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdlan.h"

#define LAN_VENDOR_IN           0xC0
#define LAN_VENDOR_OUT          0x40
#define LAN_BUSY_POLLS          100
#define LAN_RESET_POLLS         100
#define LAN_LINK_WAIT           400

static const UCHAR LanFallbackAddress[MAC_ADDRESS_SIZE] = { 0x02, 0x00, 0x00, 0x95, 0x14, 0x01 };

static
ULONG
LanAlignedAdapterSize(VOID)
{
    return (sizeof(LAN_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

ULONG
NTAPI
LanGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return LanAlignedAdapterSize() + PAGE_SIZE + (1 + LAN_TX_COUNT) * LAN_BUFFER_SIZE;
}

static
NTSTATUS
LanReadRegister(
    _In_ PLAN_ADAPTER Adapter,
    _In_ USHORT Register,
    _Out_ PULONG Value)
{
    PUCHAR Data = Adapter->ControlBuffer;
    NTSTATUS Status;
    ULONG Length;

    *Value = 0;
    Status = UsbControl(Adapter, &Adapter->Device, LAN_VENDOR_IN, SMSC_READ_REGISTER, 0, Register,
                        sizeof(ULONG), &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length != sizeof(ULONG))
        return STATUS_DEVICE_DATA_ERROR;

    *Value = Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
    return STATUS_SUCCESS;
}

static
NTSTATUS
LanWriteRegister(
    _In_ PLAN_ADAPTER Adapter,
    _In_ USHORT Register,
    _In_ ULONG Value)
{
    PUCHAR Data = Adapter->ControlBuffer;

    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
    return UsbControl(Adapter, &Adapter->Device, LAN_VENDOR_OUT, SMSC_WRITE_REGISTER, 0, Register,
                      sizeof(ULONG), NULL);
}

static
NTSTATUS
LanWaitClear(
    _In_ PLAN_ADAPTER Adapter,
    _In_ USHORT Register,
    _In_ ULONG Mask,
    _Out_ PULONG Value)
{
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < LAN_BUSY_POLLS; i++)
    {
        Status = LanReadRegister(Adapter, Register, Value);
        if (!NT_SUCCESS(Status) || !(*Value & Mask))
            return Status;

        LanStall(1000);
    }

    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
LanReadPhy(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    NTSTATUS Status;
    ULONG Data;

    *Value = 0;
    Status = LanWaitClear(Adapter, SMSC_MII_ADDR, SMSC_MII_BUSY, &Data);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LanWriteRegister(Adapter, SMSC_MII_ADDR,
                              (SMSC_PHY_ADDRESS << SMSC_MII_PHY_SHIFT) |
                              (Register << SMSC_MII_REGISTER_SHIFT) | SMSC_MII_BUSY);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LanWaitClear(Adapter, SMSC_MII_ADDR, SMSC_MII_BUSY, &Data);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LanReadRegister(Adapter, SMSC_MII_DATA, &Data);
    *Value = (USHORT)Data;
    return Status;
}

static
NTSTATUS
LanWritePhy(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    NTSTATUS Status;
    ULONG Data;

    Status = LanWaitClear(Adapter, SMSC_MII_ADDR, SMSC_MII_BUSY, &Data);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LanWriteRegister(Adapter, SMSC_MII_DATA, Value);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LanWriteRegister(Adapter, SMSC_MII_ADDR,
                              (SMSC_PHY_ADDRESS << SMSC_MII_PHY_SHIFT) |
                              (Register << SMSC_MII_REGISTER_SHIFT) | SMSC_MII_WRITE | SMSC_MII_BUSY);
    if (!NT_SUCCESS(Status))
        return Status;

    return LanWaitClear(Adapter, SMSC_MII_ADDR, SMSC_MII_BUSY, &Data);
}

static
BOOLEAN
LanValidAddress(
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address)
{
    ULONG i;
    UCHAR Any = 0, All = 0xFF;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        Any |= Address[i];
        All &= Address[i];
    }

    return Any && All != 0xFF && !(Address[0] & 1);
}

static
VOID
LanReadAddress(
    _In_ PLAN_ADAPTER Adapter,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    UCHAR Suggested[MAC_ADDRESS_SIZE];
    const UCHAR *Fallback;
    ULONG Low, High, Value, i;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        Suggested[i] = Address[i];

    if (NT_SUCCESS(LanReadRegister(Adapter, SMSC_ADDRL, &Low)) &&
        NT_SUCCESS(LanReadRegister(Adapter, SMSC_ADDRH, &High)))
    {
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Address[i] = (UCHAR)(i < 4 ? Low >> (i * 8) : High >> ((i - 4) * 8));

        if (LanValidAddress(Address))
            return;
    }

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        if (!NT_SUCCESS(LanWaitClear(Adapter, SMSC_E2P_CMD, SMSC_E2P_BUSY, &Value)) ||
            !NT_SUCCESS(LanWriteRegister(Adapter, SMSC_E2P_CMD,
                                         SMSC_E2P_BUSY | ((SMSC_EEPROM_MAC_OFFSET + i) & SMSC_E2P_ADDRESS_MASK))) ||
            !NT_SUCCESS(LanWaitClear(Adapter, SMSC_E2P_CMD, SMSC_E2P_BUSY, &Value)) ||
            (Value & SMSC_E2P_TIMEOUT) ||
            !NT_SUCCESS(LanReadRegister(Adapter, SMSC_E2P_DATA, &Value)))
        {
            break;
        }

        Address[i] = (UCHAR)Value;
    }

    if (i == MAC_ADDRESS_SIZE && LanValidAddress(Address))
        return;

    Fallback = LanValidAddress(Suggested) ? Suggested : LanFallbackAddress;
    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        Address[i] = Fallback[i];
}

static
VOID
LanUpdateLink(
    _In_ PLAN_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    USHORT Status, Special = 0;
    ULONG Control, Speed = 100, Duplex = 1;
    BOOLEAN Up;

    Up = NT_SUCCESS(LanReadPhy(Adapter, MII_BMSR, &Status)) &&
         NT_SUCCESS(LanReadPhy(Adapter, MII_BMSR, &Status)) && (Status & BMSR_LSTATUS);
    if (Up)
    {
        LanReadPhy(Adapter, MII_SMSC_SPECIAL, &Special);
        Special &= SMSC_SPECIAL_SPEED;
        if (Special == SMSC_SPECIAL_10_HALF || Special == SMSC_SPECIAL_10_FULL)
            Speed = 10;

        if (Special == SMSC_SPECIAL_10_HALF || Special == SMSC_SPECIAL_100_HALF)
            Duplex = 0;

        Control = Adapter->MacControl & ~(SMSC_MAC_CR_FDPX | SMSC_MAC_CR_RCVOWN);
        Control |= Duplex ? SMSC_MAC_CR_FDPX : SMSC_MAC_CR_RCVOWN;
        if (Control != Adapter->MacControl && NT_SUCCESS(LanWriteRegister(Adapter, SMSC_MAC_CR, Control)))
            Adapter->MacControl = Control;

        KdNet->LinkSpeed = Speed;
        KdNet->LinkDuplex = Duplex;
    }

    Adapter->LinkUp = Up;
    *KdNet->LinkState = Up;
}

static
NTSTATUS
LanStartAdapter(
    _In_ PLAN_ADAPTER Adapter,
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address)
{
    NTSTATUS Status;
    ULONG Value, i;
    USHORT Control;

    Status = LanWriteRegister(Adapter, SMSC_HW_CFG, SMSC_HW_CFG_LRST);
    for (i = 0; NT_SUCCESS(Status) && i < LAN_RESET_POLLS; i++)
    {
        LanStall(10000);
        Status = LanReadRegister(Adapter, SMSC_HW_CFG, &Value);
        if (NT_SUCCESS(Status) && !(Value & SMSC_HW_CFG_LRST))
            break;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    if (i == LAN_RESET_POLLS)
        return STATUS_IO_TIMEOUT;

    if (!NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_ADDRL,
                                              Address[0] | (Address[1] << 8) | (Address[2] << 16) |
                                              ((ULONG)Address[3] << 24))) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_ADDRH, Address[4] | (Address[5] << 8))) ||
        !NT_SUCCESS(Status = LanReadRegister(Adapter, SMSC_HW_CFG, &Value)))
    {
        return Status;
    }

    Value = (Value | SMSC_HW_CFG_BIR) & ~(SMSC_HW_CFG_MEF | SMSC_HW_CFG_BCE | SMSC_HW_CFG_RXDOFF);
    if (!NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_HW_CFG, Value)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_BURST_CAP, 0)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_BULK_IN_DLY, 0)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_INT_STS, 0xFFFFFFFF)) ||
        !NT_SUCCESS(Status = LanReadRegister(Adapter, SMSC_LED_GPIO_CFG, &Value)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_LED_GPIO_CFG, Value | SMSC_LED_GPIO_ALL)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_FLOW, 0)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_AFC_CFG, SMSC_AFC_CFG_DEFAULT)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_VLAN1, 0x8100)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_COE_CR, 0)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_HASHH, 0)) ||
        !NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_HASHL, 0)) ||
        !NT_SUCCESS(Status = LanReadRegister(Adapter, SMSC_MAC_CR, &Value)))
    {
        return Status;
    }

    Adapter->MacControl = (Value & ~SMSC_MAC_CR_FILTER_MASK) | SMSC_MAC_CR_TXEN | SMSC_MAC_CR_RXEN;
    if (!NT_SUCCESS(Status = LanWriteRegister(Adapter, SMSC_MAC_CR, Adapter->MacControl)) ||
        !NT_SUCCESS(Status = LanWritePhy(Adapter, MII_BMCR, BMCR_RESET)))
    {
        return Status;
    }

    for (i = 0; i < LAN_RESET_POLLS; i++)
    {
        LanStall(10000);
        Status = LanReadPhy(Adapter, MII_BMCR, &Control);
        if (!NT_SUCCESS(Status))
            return Status;

        if (!(Control & BMCR_RESET))
            break;
    }

    if (i == LAN_RESET_POLLS)
        return STATUS_IO_TIMEOUT;

    Control = (Control & ~(BMCR_PDOWN | BMCR_ISOLATE)) | BMCR_ANENABLE | BMCR_ANRESTART;
    if (!NT_SUCCESS(Status = LanWritePhy(Adapter, MII_ADVERTISE, ADVERTISE_ALL)) ||
        !NT_SUCCESS(Status = LanWritePhy(Adapter, MII_BMCR, Control)))
    {
        return Status;
    }

    return LanWriteRegister(Adapter, SMSC_TX_CFG, SMSC_TX_CFG_ON);
}

NTSTATUS
NTAPI
LanInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PLAN_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Hardware = KdNet->Hardware;
    NTSTATUS Status;
    ULONG i;

    if (!Device->BaseAddress[0].Valid || Device->BaseAddress[0].Type != CmResourceTypeMemory ||
        Device->BaseAddress[0].Length < DWC_REGISTER_SPACE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->KdNet = KdNet;
    Adapter->Registers = Device->BaseAddress[0].TranslatedAddress;
    Adapter->SetupBuffer = Hardware + LanAlignedAdapterSize();
    Adapter->ControlBuffer = Adapter->SetupBuffer + LAN_SETUP_SIZE;
    Adapter->RxBuffer = Hardware + LanAlignedAdapterSize() + PAGE_SIZE;
    Adapter->TxBuffers = Adapter->RxBuffer + LAN_BUFFER_SIZE;

    Status = DwcInitialize(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbFindAdapter(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    LanReadAddress(Adapter, KdNet->TargetMacAddress);
    Status = LanStartAdapter(Adapter, KdNet->TargetMacAddress);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < LAN_LINK_WAIT; i++)
    {
        LanUpdateLink(Adapter);
        if (Adapter->LinkUp)
            break;

        LanStall(10000);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
LanShutdownController(
    _In_ PVOID Context)
{
    PLAN_ADAPTER Adapter = Context;

    DwcStop(Adapter);
    Adapter->RxActive = FALSE;
    Adapter->LinkUp = FALSE;
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
LanGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PLAN_ADAPTER Adapter = Context;
    PLAN_PIPE Pipe = &Adapter->BulkIn;
    PUCHAR Data = Adapter->RxBuffer;
    ULONG Status, Remaining, Packets, Moved, Done, Header, Size;

    if (Adapter->RxHeld)
        return STATUS_IO_TIMEOUT;

    if (!Adapter->RxActive)
    {
        DwcStart(Adapter, Pipe, Pipe->Toggle ? DWC_PID_DATA1 : DWC_PID_DATA0, TRUE, Data, LAN_BUFFER_SIZE);
        Adapter->RxActive = TRUE;
    }

    if (!DwcFinished(Adapter, Pipe, &Status, &Remaining, &Packets))
    {
        if ((Adapter->LinkPoll++ & (Adapter->LinkUp ? 0x3FF : 0x1F)) == 0)
            LanUpdateLink(Adapter);

        return STATUS_IO_TIMEOUT;
    }

    Adapter->RxActive = FALSE;
    if (Remaining > LAN_BUFFER_SIZE || (Status & DWC_HCINT_AHB_ERROR))
        return STATUS_IO_TIMEOUT;

    Moved = LAN_BUFFER_SIZE - Remaining;
    if (Status & DWC_HCINT_COMPLETE)
        Done = Moved ? (Moved + Pipe->MaxPacket - 1) / Pipe->MaxPacket : 1;
    else
        Done = Moved / Pipe->MaxPacket;

    Pipe->Toggle ^= Done & 1;
    if (!(Status & DWC_HCINT_COMPLETE) || Moved < SMSC_RX_HEADER_SIZE)
        return STATUS_IO_TIMEOUT;

    Header = Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
    Size = (Header >> SMSC_RX_STATUS_LENGTH_SHIFT) & SMSC_RX_STATUS_LENGTH_MASK;
    if ((Header & SMSC_RX_STATUS_ERROR) || Size < LAN_MIN_FRAME + LAN_FCS_SIZE ||
        Size > LAN_MAX_FRAME + LAN_FCS_SIZE || SMSC_RX_HEADER_SIZE + Size > Moved)
    {
        return STATUS_IO_TIMEOUT;
    }

    Adapter->RxLength = Size - LAN_FCS_SIZE;
    Adapter->RxHeld = TRUE;
    *Handle = 0;
    *Packet = Data + SMSC_RX_HEADER_SIZE;
    *Length = Adapter->RxLength;
    return STATUS_SUCCESS;
}

VOID
NTAPI
LanReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PLAN_ADAPTER Adapter = Context;

    UNREFERENCED_PARAMETER(Handle);

    Adapter->RxHeld = FALSE;
}

NTSTATUS
NTAPI
LanGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PLAN_ADAPTER Adapter = Context;

    if (!Adapter->LinkUp)
        return STATUS_IO_TIMEOUT;

    *Handle = Adapter->TxNext | TRANSMIT_HANDLE;
    Adapter->TxNext = (Adapter->TxNext + 1) % LAN_TX_COUNT;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LanSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PLAN_ADAPTER Adapter = Context;
    PLAN_PIPE Pipe = &Adapter->BulkOut;
    ULONG Index = Handle & ~HANDLE_FLAGS, Command;
    PUCHAR Data;
    NTSTATUS Status;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= LAN_TX_COUNT || Length > LAN_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Data = Adapter->TxBuffers + Index * LAN_BUFFER_SIZE;
    for (; Length < 60; Length++)
        Data[SMSC_TX_HEADER_SIZE + Length] = 0;

    Command = Length | SMSC_TX_CMD_FIRST_LAST;
    Data[0] = (UCHAR)Command;
    Data[1] = (UCHAR)(Command >> 8);
    Data[2] = 0;
    Data[3] = 0;
    Data[4] = (UCHAR)Length;
    Data[5] = (UCHAR)(Length >> 8);
    Data[6] = 0;
    Data[7] = 0;

    Status = DwcTransfer(Adapter, Pipe, FALSE, FALSE, Data, SMSC_TX_HEADER_SIZE + Length, NULL);
    if (NT_SUCCESS(Status) && !((SMSC_TX_HEADER_SIZE + Length) % Pipe->MaxPacket))
        Status = DwcTransfer(Adapter, Pipe, FALSE, FALSE, Data, 0, NULL);

    return Status;
}

PVOID
NTAPI
LanGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PLAN_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
    {
        return Index < LAN_TX_COUNT ?
               Adapter->TxBuffers + Index * LAN_BUFFER_SIZE + SMSC_TX_HEADER_SIZE : NULL;
    }

    return Adapter->RxBuffer + SMSC_RX_HEADER_SIZE;
}

ULONG
NTAPI
LanGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PLAN_ADAPTER Adapter = Context;

    if (Handle & TRANSMIT_HANDLE)
        return LAN_MAX_FRAME;

    return Adapter->RxLength;
}
