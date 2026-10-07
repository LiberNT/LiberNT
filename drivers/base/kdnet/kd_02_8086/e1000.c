/*
 * PROJECT:     LiberNT Intel Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Intel 8254x and 82574 Ethernet controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd8086.h"

#define E1K_DEVICE_82574L 0x10D3

static const USHORT E1kDeviceIds[] =
{
    0x1000, 0x1001, 0x1004, 0x1008, 0x1009, 0x100A, 0x100C, 0x100D,
    0x100E, 0x100F, 0x1010, 0x1011, 0x1012, 0x1013, 0x1014, 0x1015,
    0x1016, 0x1017, 0x1018, 0x1019, 0x101A, 0x101D, 0x101E, 0x1026,
    0x1027, 0x1028, 0x1075, 0x1076, 0x1077, 0x1078, 0x1079, 0x107A,
    0x107B, 0x107C, 0x108A, 0x1099, 0x10B5
};

static const USHORT E1kIgbDeviceIds[] =
{
    0x10A7, 0x10A9, 0x10D6,
    0x10C9, 0x10E6, 0x10E7, 0x10E8, 0x150A, 0x150D, 0x1518, 0x1526,
    0x150E, 0x150F, 0x1510, 0x1511, 0x1516, 0x1527,
    0x1521, 0x1522, 0x1523, 0x1524,
    0x1533, 0x1536, 0x1537, 0x1538, 0x1539, 0x157B, 0x157C
};

static
ULONG
E1kRead(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Registers + Register));
}

static
VOID
E1kWrite(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Registers + Register), Value);
}

static
VOID
E1kStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG64
E1kPhysical(
    _In_ PVOID Address)
{
    return (ULONG64)KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
ULONG
E1kAlignedAdapterSize(VOID)
{
    return (sizeof(E1K_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static
ULONG
E1kRingSize(VOID)
{
    ULONG Size = E1K_RX_COUNT * sizeof(E1K_RX_DESCRIPTOR) + E1K_TX_COUNT * sizeof(E1K_TX_DESCRIPTOR);

    return (Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

E1K_CLASS
E1kClassifyDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    ULONG i;

    if (Device->VendorID != 0x8086)
        return E1kClassNone;

    if (Device->DeviceID == E1K_DEVICE_82574L)
        return E1kClass82574;

    for (i = 0; i < RTL_NUMBER_OF(E1kDeviceIds); i++)
    {
        if (E1kDeviceIds[i] == Device->DeviceID)
            return E1kClass8254x;
    }

    for (i = 0; i < RTL_NUMBER_OF(E1kIgbDeviceIds); i++)
    {
        if (E1kIgbDeviceIds[i] == Device->DeviceID)
            return E1kClassIgb;
    }

    return E1kClassNone;
}

static
VOID
E1kArmReceive(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Index)
{
    volatile E1K_RX_ADVANCED_DESCRIPTOR *Advanced = (PVOID)&Adapter->RxRing[Index];

    if (Adapter->Class == E1kClassIgb)
    {
        Advanced->StatusError = 0;
        Advanced->Length = 0;
        Advanced->Vlan = 0;
        Advanced->Address = E1kPhysical(Adapter->RxBuffers + Index * E1K_BUFFER_SIZE);
    }
    else
    {
        Adapter->RxRing[Index].Status = 0;
    }

    KeMemoryBarrier();
}

static
BOOLEAN
E1kReceiveDone(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Index,
    _Out_ PULONG Length,
    _Out_ PBOOLEAN Valid)
{
    volatile E1K_RX_ADVANCED_DESCRIPTOR *Advanced = (PVOID)&Adapter->RxRing[Index];
    volatile E1K_RX_DESCRIPTOR *Descriptor = &Adapter->RxRing[Index];
    ULONG Status;

    if (Adapter->Class == E1kClassIgb)
    {
        Status = Advanced->StatusError;
        if (!(Status & E1K_RXD_STATUS_DD))
            return FALSE;

        *Length = Advanced->Length;
        *Valid = (Status & E1K_RXD_STATUS_EOP) && !(Status & E1K_RXD_ADVANCED_ERROR);
        return TRUE;
    }

    Status = Descriptor->Status;
    if (!(Status & E1K_RXD_STATUS_DD))
        return FALSE;

    *Length = Descriptor->Length;
    *Valid = (Status & E1K_RXD_STATUS_EOP) && !Descriptor->Errors;
    return TRUE;
}

static
BOOLEAN
E1kPhyAccess(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Command,
    _Out_ PUSHORT Value)
{
    ULONG Data, i;

    E1kWrite(Adapter, E1K_REG_MDIC, Command | E1K_MDIC_ADDRESS);
    for (i = 0; i < 200; i++)
    {
        E1kStall(50);
        Data = E1kRead(Adapter, E1K_REG_MDIC);
        if (Data & E1K_MDIC_READY)
        {
            *Value = (USHORT)Data;
            return !(Data & E1K_MDIC_ERROR);
        }
    }

    return FALSE;
}

static
VOID
E1kRestartNegotiation(
    _In_ PE1K_ADAPTER Adapter)
{
    USHORT Control, Ignored;

    if (!E1kPhyAccess(Adapter, E1K_MDIC_READ | (E1K_PHY_CONTROL << E1K_MDIC_REGISTER), &Control))
        return;

    Control |= E1K_PHY_CONTROL_AUTONEG | E1K_PHY_CONTROL_RESTART;
    E1kPhyAccess(Adapter, E1K_MDIC_WRITE | (E1K_PHY_CONTROL << E1K_MDIC_REGISTER) | Control, &Ignored);
}

static
BOOLEAN
E1kEnableQueue(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Base)
{
    ULONG i;

    E1kWrite(Adapter, Base + E1K_QUEUE_DCTL,
             E1kRead(Adapter, Base + E1K_QUEUE_DCTL) | E1K_DCTL_ENABLE);
    for (i = 0; i < 1000; i++)
    {
        if (E1kRead(Adapter, Base + E1K_QUEUE_DCTL) & E1K_DCTL_ENABLE)
            return TRUE;

        E1kStall(10);
    }

    return FALSE;
}

ULONG
NTAPI
E1kGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return E1kAlignedAdapterSize() + E1kRingSize() +
           (E1K_RX_COUNT + E1K_TX_COUNT) * E1K_BUFFER_SIZE;
}

static
BOOLEAN
E1kReadEeprom(
    _In_ PE1K_ADAPTER Adapter,
    _In_ ULONG Word,
    _In_ ULONG AddressShift,
    _In_ ULONG DoneBit,
    _Out_ PUSHORT Value)
{
    ULONG Data, i;

    E1kWrite(Adapter, E1K_REG_EERD, (Word << AddressShift) | E1K_EERD_START);
    for (i = 0; i < 1000; i++)
    {
        E1kStall(10);
        Data = E1kRead(Adapter, E1K_REG_EERD);
        if (Data & DoneBit)
        {
            *Value = (USHORT)(Data >> 16);
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
E1kValidAddress(
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address)
{
    UCHAR Or = 0, And = 0xFF;
    ULONG i;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        Or |= Address[i];
        And &= Address[i];
    }

    return Or != 0 && And != 0xFF && !(Address[0] & 1);
}

static
BOOLEAN
E1kReadAddress(
    _In_ PE1K_ADAPTER Adapter,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    ULONG Low, High, i;
    USHORT Words[3];

    Low = E1kRead(Adapter, E1K_REG_RAL);
    High = E1kRead(Adapter, E1K_REG_RAH);
    Address[0] = (UCHAR)Low;
    Address[1] = (UCHAR)(Low >> 8);
    Address[2] = (UCHAR)(Low >> 16);
    Address[3] = (UCHAR)(Low >> 24);
    Address[4] = (UCHAR)High;
    Address[5] = (UCHAR)(High >> 8);
    if (E1kValidAddress(Address))
        return TRUE;

    for (i = 0; i < 3; i++)
    {
        if (!E1kReadEeprom(Adapter, i, 8, 0x10, &Words[i]) &&
            !E1kReadEeprom(Adapter, i, 2, 0x02, &Words[i]))
        {
            return FALSE;
        }

        Address[i * 2] = (UCHAR)Words[i];
        Address[i * 2 + 1] = (UCHAR)(Words[i] >> 8);
    }

    return E1kValidAddress(Address);
}

static
VOID
E1kUpdateLink(
    _In_ PE1K_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    ULONG Status = E1kRead(Adapter, E1K_REG_STATUS);

    if (!(Status & E1K_STATUS_LU))
    {
        *KdNet->LinkState = 0;
        return;
    }

    if ((Status & E1K_STATUS_SPEED) == E1K_STATUS_SPEED_10)
        KdNet->LinkSpeed = 10;
    else if ((Status & E1K_STATUS_SPEED) == E1K_STATUS_SPEED_100)
        KdNet->LinkSpeed = 100;
    else
        KdNet->LinkSpeed = 1000;

    KdNet->LinkDuplex = (Status & E1K_STATUS_FD) ? 1 : 0;
    *KdNet->LinkState = 1;
}

NTSTATUS
NTAPI
E1kInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PE1K_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Registers = NULL, Base = KdNet->Hardware;
    UCHAR Address[MAC_ADDRESS_SIZE];
    ULONG64 Physical;
    ULONG Control, i;
    USHORT Command;

    for (i = 0; i < MAXIMUM_DEBUG_BARS; i++)
    {
        if (Device->BaseAddress[i].Valid && Device->BaseAddress[i].Type == CmResourceTypeMemory)
        {
            Registers = Device->BaseAddress[i].TranslatedAddress;
            break;
        }
    }

    if (!Registers)
        return STATUS_NO_SUCH_DEVICE;

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->Registers = Registers;
    Adapter->Class = E1kClassifyDevice(Device);
    Adapter->RxBase = Adapter->Class == E1kClassIgb ? E1K_REG_RX_IGB : E1K_REG_RX;
    Adapter->TxBase = Adapter->Class == E1kClassIgb ? E1K_REG_TX_IGB : E1K_REG_TX;
    Adapter->KdNet = KdNet;
    Adapter->RxRing = (PVOID)(Base + E1kAlignedAdapterSize());
    Adapter->TxRing = (PVOID)(Adapter->RxRing + E1K_RX_COUNT);
    Adapter->RxBuffers = Base + E1kAlignedAdapterSize() + E1kRingSize();
    Adapter->TxBuffers = Adapter->RxBuffers + E1K_RX_COUNT * E1K_BUFFER_SIZE;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Command, 4, sizeof(Command));
    Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    KdNetExtensibilityImports->SetPciDataByOffset(Device->Bus, Device->Slot, &Command, 4, sizeof(Command));

    E1kWrite(Adapter, E1K_REG_IMC, 0xFFFFFFFF);
    E1kWrite(Adapter, E1K_REG_RCTL, 0);
    E1kWrite(Adapter, E1K_REG_TCTL, E1K_TCTL_PSP);
    E1kRead(Adapter, E1K_REG_STATUS);
    E1kStall(10000);

    E1kWrite(Adapter, E1K_REG_CTRL, E1kRead(Adapter, E1K_REG_CTRL) | E1K_CTRL_RST);
    E1kStall(5000);
    for (i = 0; i < 100; i++)
    {
        if (!(E1kRead(Adapter, E1K_REG_CTRL) & E1K_CTRL_RST))
            break;

        E1kStall(1000);
    }

    if (i == 100)
        return STATUS_IO_TIMEOUT;

    if (Adapter->Class == E1kClassIgb)
    {
        for (i = 0; i < 100; i++)
        {
            if (E1kRead(Adapter, E1K_REG_EECD) & E1K_EECD_AUTO_RD)
                break;

            E1kStall(1000);
        }

        E1kWrite(Adapter, E1K_REG_EIMC, 0xFFFFFFFF);
    }

    E1kWrite(Adapter, E1K_REG_IMC, 0xFFFFFFFF);
    E1kRead(Adapter, E1K_REG_ICR);

    Control = E1kRead(Adapter, E1K_REG_CTRL);
    Control &= ~(E1K_CTRL_LRST | E1K_CTRL_PHY_RST | E1K_CTRL_ILOS | E1K_CTRL_VME);
    Control |= E1K_CTRL_SLU | E1K_CTRL_ASDE;
    E1kWrite(Adapter, E1K_REG_CTRL, Control);
    if (Adapter->Class == E1kClassIgb)
        E1kRestartNegotiation(Adapter);

    E1kWrite(Adapter, E1K_REG_FCAL, 0);
    E1kWrite(Adapter, E1K_REG_FCAH, 0);
    E1kWrite(Adapter, E1K_REG_FCT, 0);
    E1kWrite(Adapter, E1K_REG_FCTTV, 0);

    if (!E1kReadAddress(Adapter, Address))
        return STATUS_DEVICE_DATA_ERROR;

    E1kWrite(Adapter, E1K_REG_RAL,
             Address[0] | (Address[1] << 8) | (Address[2] << 16) | ((ULONG)Address[3] << 24));
    E1kWrite(Adapter, E1K_REG_RAH, Address[4] | (Address[5] << 8) | E1K_RAH_AV);
    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        KdNet->TargetMacAddress[i] = Address[i];

    for (i = 0; i < E1K_MTA_ENTRIES; i++)
        E1kWrite(Adapter, E1K_REG_MTA + i * sizeof(ULONG), 0);

    for (i = 0; i < E1K_RX_COUNT; i++)
    {
        Adapter->RxRing[i].Address = E1kPhysical(Adapter->RxBuffers + i * E1K_BUFFER_SIZE);
        E1kArmReceive(Adapter, i);
    }

    for (i = 0; i < E1K_TX_COUNT; i++)
    {
        Adapter->TxRing[i].Address = E1kPhysical(Adapter->TxBuffers + i * E1K_BUFFER_SIZE);
        Adapter->TxRing[i].Status = 0;
    }

    KeMemoryBarrier();

    Physical = E1kPhysical((PVOID)Adapter->RxRing);
    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_BAL, (ULONG)Physical);
    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_BAH, (ULONG)(Physical >> 32));
    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_LEN, E1K_RX_COUNT * sizeof(E1K_RX_DESCRIPTOR));
    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_HEAD, 0);
    if (Adapter->Class == E1kClassIgb)
    {
        E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_SRRCTL,
                 (E1kRead(Adapter, Adapter->RxBase + E1K_QUEUE_SRRCTL) &
                  ~(E1K_SRRCTL_BSIZEPACKET | E1K_SRRCTL_DESCTYPE)) |
                 E1K_SRRCTL_2K | E1K_SRRCTL_ADVANCED);
        if (!E1kEnableQueue(Adapter, Adapter->RxBase))
            return STATUS_IO_TIMEOUT;
    }

    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_TAIL, E1K_RX_COUNT - 1);

    Physical = E1kPhysical((PVOID)Adapter->TxRing);
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_BAL, (ULONG)Physical);
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_BAH, (ULONG)(Physical >> 32));
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_LEN, E1K_TX_COUNT * sizeof(E1K_TX_DESCRIPTOR));
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_HEAD, 0);
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_TAIL, 0);

    if (Adapter->Class == E1kClass8254x)
        E1kWrite(Adapter, E1K_REG_TIPG, E1K_TIPG_COPPER);

    E1kWrite(Adapter, E1K_REG_TCTL, E1K_TCTL_EN | E1K_TCTL_PSP | E1K_TCTL_CT | E1K_TCTL_COLD);
    if (Adapter->Class == E1kClassIgb && !E1kEnableQueue(Adapter, Adapter->TxBase))
        return STATUS_IO_TIMEOUT;

    E1kWrite(Adapter, E1K_REG_RCTL, E1K_RCTL_EN | E1K_RCTL_BAM | E1K_RCTL_SECRC);

    for (i = 0; i < 400; i++)
    {
        E1kUpdateLink(Adapter);
        if (*KdNet->LinkState)
            break;

        E1kStall(10000);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
E1kShutdownController(
    _In_ PVOID Context)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        if (E1kRead(Adapter, Adapter->TxBase + E1K_QUEUE_HEAD) ==
            E1kRead(Adapter, Adapter->TxBase + E1K_QUEUE_TAIL))
        {
            break;
        }

        E1kStall(10);
    }

    E1kWrite(Adapter, E1K_REG_RCTL, 0);
    E1kWrite(Adapter, E1K_REG_TCTL, E1K_TCTL_PSP);
    E1kWrite(Adapter, E1K_REG_IMC, 0xFFFFFFFF);
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
E1kGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG Index, Count, Size;
    BOOLEAN Valid;

    for (Count = 0; Count < E1K_RX_COUNT; Count++)
    {
        Index = Adapter->RxNext;
        if (!E1kReceiveDone(Adapter, Index, &Size, &Valid))
            break;

        Adapter->RxNext = (Index + 1) % E1K_RX_COUNT;
        if (Valid && Size >= E1K_MIN_FRAME && Size <= E1K_BUFFER_SIZE)
        {
            Adapter->RxLength[Index] = (USHORT)Size;
            *Handle = Index;
            *Packet = Adapter->RxBuffers + Index * E1K_BUFFER_SIZE;
            *Length = Size;
            return STATUS_SUCCESS;
        }

        E1kReleaseRxPacket(Adapter, Index);
    }

    if ((Adapter->LinkPoll++ & 0xFF) == 0)
        E1kUpdateLink(Adapter);

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
E1kReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Index >= E1K_RX_COUNT)
        return;

    E1kArmReceive(Adapter, Index);
    E1kWrite(Adapter, Adapter->RxBase + E1K_QUEUE_TAIL, Index);
}

NTSTATUS
NTAPI
E1kGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    if (Adapter->TxSubmitted[Index])
    {
        if (!(Adapter->TxRing[Index].Status & E1K_TXD_STATUS_DD))
            return STATUS_IO_TIMEOUT;

        Adapter->TxSubmitted[Index] = FALSE;
    }

    Adapter->TxNext = (Index + 1) % E1K_TX_COUNT;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
E1kSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PE1K_ADAPTER Adapter = Context;
    volatile E1K_TX_DESCRIPTOR *Descriptor;
    ULONG Index = Handle & ~HANDLE_FLAGS, i;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= E1K_TX_COUNT || Length > E1K_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Descriptor = &Adapter->TxRing[Index];
    Descriptor->Length = (USHORT)Length;
    Descriptor->ChecksumOffset = 0;
    Descriptor->ChecksumStart = 0;
    Descriptor->Special = 0;
    Descriptor->Status = 0;
    Descriptor->Command = E1K_TXD_CMD_EOP | E1K_TXD_CMD_IFCS | E1K_TXD_CMD_RS;
    Adapter->TxSubmitted[Index] = TRUE;
    KeMemoryBarrier();
    E1kWrite(Adapter, Adapter->TxBase + E1K_QUEUE_TAIL, (Index + 1) % E1K_TX_COUNT);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < 10000; i++)
    {
        if (Descriptor->Status & E1K_TXD_STATUS_DD)
            return STATUS_SUCCESS;

        E1kStall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
E1kGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < E1K_TX_COUNT ? Adapter->TxBuffers + Index * E1K_BUFFER_SIZE : NULL;

    return Index < E1K_RX_COUNT ? Adapter->RxBuffers + Index * E1K_BUFFER_SIZE : NULL;
}

ULONG
NTAPI
E1kGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE1K_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return E1K_MAX_FRAME;

    return Index < E1K_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
