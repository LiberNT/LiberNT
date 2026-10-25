/*
 * PROJECT:     LiberNT Intel Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Intel 8255x (PRO/100) Ethernet controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd8086.h"

static const USHORT E100DeviceIds[] =
{
    0x1029, 0x1030, 0x1059, 0x1209, 0x1229
};

static const USHORT E100IchDeviceIds[] =
{
    0x1031, 0x1032, 0x1033, 0x1034, 0x1038, 0x1039, 0x103A, 0x103B,
    0x103C, 0x103D, 0x103E, 0x1050, 0x1051, 0x1052, 0x1053, 0x1054,
    0x1055, 0x1056, 0x1057, 0x1064, 0x1065, 0x1066, 0x1067, 0x1068,
    0x1069, 0x106A, 0x106B, 0x1091, 0x1092, 0x1093, 0x1094, 0x1095,
    0x10FE, 0x2449, 0x2459, 0x245D, 0x27DC
};

static const UCHAR E100Config82557[E100_CONFIG_SIZE] =
{
    0x16, 0x08, 0x00, 0x00, 0x00, 0x00, 0x32, 0x02,
    0x01, 0x00, 0x2E, 0x00, 0x60, 0x00, 0xF2, 0x48,
    0x00, 0x40, 0xF3, 0x80, 0x3F, 0x05
};

static const UCHAR E100Config82558[E100_CONFIG_SIZE] =
{
    0x16, 0x08, 0x00, 0x00, 0x00, 0x00, 0x32, 0x02,
    0x01, 0x00, 0x2E, 0x00, 0x61, 0x00, 0xF2, 0x48,
    0x00, 0x00, 0xF3, 0x80, 0x3F, 0x05
};

static
UCHAR
E100Read8(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterUChar(Adapter->Registers + Register);
}

static
VOID
E100Write8(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    KdNetExtensibilityImports->WriteRegisterUChar(Adapter->Registers + Register, Value);
}

static
ULONG
E100Read32(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Registers + Register));
}

static
VOID
E100Write32(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Registers + Register), Value);
}

static
VOID
E100Stall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG64
E100Physical(
    _In_ PVOID Address)
{
    return (ULONG64)KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
ULONG
E100AdapterSize(VOID)
{
    return (sizeof(E100_ADAPTER) + 15) & ~15;
}

static
ULONG
E100AlignedAdapterSize(VOID)
{
    return (E100AdapterSize() + sizeof(E100_ACTION) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static
volatile E100_RFD *
E100Rfd(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Index)
{
    return (volatile E100_RFD *)(Adapter->RxSlots + Index * E100_SLOT_SIZE);
}

static
volatile E100_TCB *
E100Tcb(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Index)
{
    return (volatile E100_TCB *)(Adapter->TxSlots + Index * E100_SLOT_SIZE);
}

static
BOOLEAN
E100IsIch(
    _In_ USHORT DeviceID)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(E100IchDeviceIds); i++)
    {
        if (E100IchDeviceIds[i] == DeviceID)
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
E100IsDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    ULONG i;

    if (Device->VendorID != 0x8086)
        return FALSE;

    for (i = 0; i < RTL_NUMBER_OF(E100DeviceIds); i++)
    {
        if (E100DeviceIds[i] == Device->DeviceID)
            return TRUE;
    }

    return E100IsIch(Device->DeviceID);
}

static
VOID
E100EepromWrite(
    _In_ PE100_ADAPTER Adapter,
    _In_ UCHAR Value)
{
    E100Write8(Adapter, E100_REG_EEPROM, Value);
    E100Read8(Adapter, E100_REG_EEPROM);
    E100Stall(2);
}

static
BOOLEAN
E100EepromShiftOut(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Bit)
{
    UCHAR Value = E100_EEPROM_CS | (Bit ? E100_EEPROM_DI : 0);

    E100EepromWrite(Adapter, Value);
    E100EepromWrite(Adapter, Value | E100_EEPROM_SK);
    E100EepromWrite(Adapter, Value);
    return (E100Read8(Adapter, E100_REG_EEPROM) & E100_EEPROM_DO) != 0;
}

static
USHORT
E100EepromShiftIn(
    _In_ PE100_ADAPTER Adapter)
{
    USHORT Data = 0;
    ULONG i;

    for (i = 0; i < 16; i++)
    {
        E100EepromWrite(Adapter, E100_EEPROM_CS | E100_EEPROM_SK);
        Data = (USHORT)((Data << 1) | ((E100Read8(Adapter, E100_REG_EEPROM) & E100_EEPROM_DO) ? 1 : 0));
        E100EepromWrite(Adapter, E100_EEPROM_CS);
    }

    return Data;
}

static
BOOLEAN
E100ReadEeprom(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Word,
    _Inout_ PULONG AddressBits,
    _Out_ PUSHORT Value)
{
    BOOLEAN DataOut = TRUE;
    ULONG i;

    E100EepromWrite(Adapter, E100_EEPROM_CS);
    for (i = 3; i-- > 0;)
        E100EepromShiftOut(Adapter, (E100_EEPROM_READ >> i) & 1);

    if (!*AddressBits)
    {
        for (i = 1; i <= E100_EEPROM_MAX_ADDRESS_BITS; i++)
        {
            DataOut = E100EepromShiftOut(Adapter, 0);
            if (!DataOut)
            {
                *AddressBits = i;
                break;
            }
        }
    }
    else
    {
        for (i = *AddressBits; i-- > 0;)
            DataOut = E100EepromShiftOut(Adapter, (Word >> i) & 1);
    }

    if (DataOut)
    {
        E100EepromWrite(Adapter, 0);
        return FALSE;
    }

    *Value = E100EepromShiftIn(Adapter);
    E100EepromWrite(Adapter, 0);
    return TRUE;
}

static
BOOLEAN
E100ValidAddress(
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
E100ReadAddress(
    _In_ PE100_ADAPTER Adapter,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    ULONG AddressBits = 0, i;
    USHORT Word;

    for (i = 0; i < MAC_ADDRESS_SIZE / 2; i++)
    {
        if (!E100ReadEeprom(Adapter, i, &AddressBits, &Word))
            return FALSE;

        Address[i * 2] = (UCHAR)Word;
        Address[i * 2 + 1] = (UCHAR)(Word >> 8);
    }

    return E100ValidAddress(Address);
}

static
BOOLEAN
E100MdiRead(
    _In_ PE100_ADAPTER Adapter,
    _In_ ULONG Phy,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    ULONG Data, i;

    E100Write32(Adapter, E100_REG_MDI,
                E100_MDI_READ | (Phy << E100_MDI_PHY) | (Register << E100_MDI_REGISTER));
    for (i = 0; i < 200; i++)
    {
        E100Stall(10);
        Data = E100Read32(Adapter, E100_REG_MDI);
        if (Data & E100_MDI_READY)
        {
            *Value = (USHORT)Data;
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
E100FindPhy(
    _In_ PE100_ADAPTER Adapter)
{
    USHORT Control, Status;
    ULONG Phy, i;

    for (i = 0; i < 32; i++)
    {
        Phy = i == 0 ? 1 : i == 1 ? 0 : i;
        if (!E100MdiRead(Adapter, Phy, E100_PHY_CONTROL, &Control) ||
            !E100MdiRead(Adapter, Phy, E100_PHY_STATUS, &Status))
        {
            continue;
        }

        if (Control == 0xFFFF || (Control == 0 && Status == 0))
            continue;

        Adapter->PhyAddress = (UCHAR)Phy;
        Adapter->Mii = TRUE;
        return;
    }
}

static
VOID
E100UpdateLink(
    _In_ PE100_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    USHORT Control, Status, Advertise, Partner, Common;
    UCHAR General;

    if (!Adapter->Mii)
    {
        if (Adapter->Class == E100Class82559)
        {
            General = E100Read8(Adapter, E100_REG_GSTATUS);
            KdNet->LinkSpeed = (General & E100_GSTATUS_100) ? 100 : 10;
            KdNet->LinkDuplex = (General & E100_GSTATUS_FDX) ? 1 : 0;
            *KdNet->LinkState = (General & E100_GSTATUS_LINK) ? 1 : 0;
            return;
        }

        KdNet->LinkSpeed = 10;
        KdNet->LinkDuplex = 0;
        *KdNet->LinkState = 1;
        return;
    }

    if (!E100MdiRead(Adapter, Adapter->PhyAddress, E100_PHY_STATUS, &Status) ||
        !E100MdiRead(Adapter, Adapter->PhyAddress, E100_PHY_STATUS, &Status) ||
        !(Status & E100_PHY_STATUS_LINK) ||
        !E100MdiRead(Adapter, Adapter->PhyAddress, E100_PHY_CONTROL, &Control))
    {
        *KdNet->LinkState = 0;
        return;
    }

    if (!(Control & E100_PHY_CONTROL_AUTONEG))
    {
        KdNet->LinkSpeed = (Control & E100_PHY_CONTROL_100) ? 100 : 10;
        KdNet->LinkDuplex = (Control & E100_PHY_CONTROL_FDX) ? 1 : 0;
    }
    else if (E100MdiRead(Adapter, Adapter->PhyAddress, E100_PHY_ADVERTISE, &Advertise) &&
             E100MdiRead(Adapter, Adapter->PhyAddress, E100_PHY_PARTNER, &Partner))
    {
        Common = Advertise & Partner;
        KdNet->LinkSpeed = (Common & E100_PHY_ABILITY_100) ? 100 : 10;
        KdNet->LinkDuplex = (Common & E100_PHY_ABILITY_100FD) ||
                            (!(Common & E100_PHY_ABILITY_100) && (Common & E100_PHY_ABILITY_10FD));
    }

    *KdNet->LinkState = 1;
}

static
BOOLEAN
E100WaitCommand(
    _In_ PE100_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        if (!E100Read8(Adapter, E100_REG_COMMAND))
            return TRUE;

        E100Stall(10);
    }

    return FALSE;
}

static
BOOLEAN
E100Command(
    _In_ PE100_ADAPTER Adapter,
    _In_ UCHAR Command,
    _In_ ULONG Pointer)
{
    if (!E100WaitCommand(Adapter))
        return FALSE;

    E100Write32(Adapter, E100_REG_POINTER, Pointer);
    E100Write8(Adapter, E100_REG_COMMAND, Command);
    return TRUE;
}

static
BOOLEAN
E100WaitCommandUnit(
    _In_ PE100_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        if ((!Adapter->CuStatus || (*Adapter->CuStatus & E100_CB_STATUS_C)) &&
            !E100Read8(Adapter, E100_REG_COMMAND) &&
            !(E100Read8(Adapter, E100_REG_STATUS) & E100_STATUS_CU))
        {
            return TRUE;
        }

        E100Stall(10);
    }

    return FALSE;
}

static
VOID
E100StartCommandUnit(
    _In_ PE100_ADAPTER Adapter,
    _In_ volatile USHORT *Status,
    _In_ ULONG Pointer)
{
    Adapter->CuStatus = Status;
    E100Write32(Adapter, E100_REG_POINTER, Pointer);
    E100Write8(Adapter, E100_REG_COMMAND, E100_CU_START);
}

static
NTSTATUS
E100RunAction(
    _In_ PE100_ADAPTER Adapter,
    _In_ USHORT Command)
{
    volatile E100_ACTION *Action = Adapter->Action;
    ULONG Physical = (ULONG)E100Physical((PVOID)Action), i;

    if (!E100WaitCommandUnit(Adapter))
        return STATUS_IO_TIMEOUT;

    Action->Status = 0;
    Action->Command = E100_CB_EL | Command;
    Action->Link = Physical;
    KeMemoryBarrier();
    E100StartCommandUnit(Adapter, &Action->Status, Physical);

    for (i = 0; i < 10000; i++)
    {
        if (Action->Status & E100_CB_STATUS_C)
            return (Action->Status & E100_CB_STATUS_OK) ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;

        E100Stall(10);
    }

    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
E100Configure(
    _In_ PE100_ADAPTER Adapter)
{
    const UCHAR *Config = Adapter->Class == E100Class82557 ? E100Config82557 : E100Config82558;
    ULONG i;

    for (i = 0; i < E100_CONFIG_SIZE; i++)
        Adapter->Action->Data[i] = Config[i];

    if (Adapter->Class == E100Class82557 && !Adapter->Mii)
    {
        Adapter->Action->Data[8] = 0x00;
        Adapter->Action->Data[15] |= 0x80;
    }

    return E100RunAction(Adapter, E100_CB_CONFIGURE);
}

static
VOID
E100ClearReceiveLockup(
    _In_ PE100_ADAPTER Adapter)
{
    ULONG64 Frequency, Now = KdNetExtensibilityImports->ReadCycleCounter(&Frequency);

    if (Now - Adapter->LastReceive < E100_LOCKUP_IDLE_SECONDS * Frequency)
        return;

    Adapter->LastReceive = Now;
    if (!NT_SUCCESS(E100Configure(Adapter)))
        return;

    Adapter->Action->Data[0] = 0;
    Adapter->Action->Data[1] = 0;
    E100RunAction(Adapter, E100_CB_MULTICAST);
}

static
VOID
E100StartReceive(
    _In_ PE100_ADAPTER Adapter)
{
    ULONG i;

    if ((E100Read8(Adapter, E100_REG_STATUS) & E100_STATUS_RU) == E100_STATUS_RU_READY ||
        (E100Rfd(Adapter, Adapter->RxNext)->Status & E100_CB_STATUS_C))
    {
        return;
    }

    if (!E100Command(Adapter, E100_RU_START, (ULONG)E100Physical((PVOID)E100Rfd(Adapter, Adapter->RxNext))))
        return;

    for (i = 0; i < 100; i++)
    {
        if ((E100Read8(Adapter, E100_REG_STATUS) & E100_STATUS_RU) == E100_STATUS_RU_READY)
            break;

        E100Stall(10);
    }
}

ULONG
NTAPI
E100GetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return E100AlignedAdapterSize() + (E100_RX_COUNT + E100_TX_COUNT) * E100_SLOT_SIZE;
}

NTSTATUS
NTAPI
E100InitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PE100_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Registers = NULL, Base = KdNet->Hardware;
    UCHAR Address[MAC_ADDRESS_SIZE], Revision = 0;
    volatile E100_RFD *Rfd;
    NTSTATUS Status;
    ULONG i;
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
    Adapter->KdNet = KdNet;
    Adapter->Action = (PVOID)(Base + E100AdapterSize());
    Adapter->RxSlots = Base + E100AlignedAdapterSize();
    Adapter->TxSlots = Adapter->RxSlots + E100_RX_COUNT * E100_SLOT_SIZE;
    if (E100Physical(Adapter->TxSlots + E100_TX_COUNT * E100_SLOT_SIZE - 1) >> 32)
        return STATUS_INSUFFICIENT_RESOURCES;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Revision,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, RevisionID),
                                                  sizeof(Revision));
    Adapter->Ich = E100IsIch(Device->DeviceID);
    if (Adapter->Ich || Revision >= 6)
        Adapter->Class = E100Class82559;
    else if (Revision >= 4)
        Adapter->Class = E100Class82558;
    else
        Adapter->Class = E100Class82557;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Command, 4, sizeof(Command));
    Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    KdNetExtensibilityImports->SetPciDataByOffset(Device->Bus, Device->Slot, &Command, 4, sizeof(Command));

    E100Write32(Adapter, E100_REG_PORT, E100_PORT_SOFTWARE_RESET);
    E100Stall(20);
    E100Write8(Adapter, E100_REG_INTERRUPT, E100_INTERRUPT_MASK);
    E100Write8(Adapter, E100_REG_ACK, 0xFF);

    if (!E100Command(Adapter, E100_CU_LOAD_BASE, 0) ||
        !E100Command(Adapter, E100_RU_LOAD_BASE, 0) ||
        !E100WaitCommand(Adapter))
    {
        return STATUS_IO_TIMEOUT;
    }

    E100FindPhy(Adapter);

    if (!E100ReadAddress(Adapter, Address))
    {
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Address[i] = KdNet->TargetMacAddress[i];

        if (!E100ValidAddress(Address))
            return STATUS_DEVICE_DATA_ERROR;
    }

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        KdNet->TargetMacAddress[i] = Address[i];

    Status = E100Configure(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        Adapter->Action->Data[i] = Address[i];

    Status = E100RunAction(Adapter, E100_CB_IA_SETUP);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < E100_RX_COUNT; i++)
    {
        Rfd = E100Rfd(Adapter, i);
        Rfd->Status = 0;
        Rfd->Command = i == E100_RX_COUNT - 1 ? E100_CB_EL : 0;
        Rfd->Link = (ULONG)E100Physical((PVOID)E100Rfd(Adapter, (i + 1) % E100_RX_COUNT));
        Rfd->Reserved = E100_NULL_POINTER;
        Rfd->Count = 0;
        Rfd->Size = E100_RX_DATA;
    }

    Adapter->RxTail = E100_RX_COUNT - 1;
    Adapter->LastReceive = KdNetExtensibilityImports->ReadCycleCounter(NULL);
    KeMemoryBarrier();
    E100StartReceive(Adapter);

    for (i = 0; i < 400; i++)
    {
        E100UpdateLink(Adapter);
        if (*KdNet->LinkState)
            break;

        E100Stall(10000);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
E100ShutdownController(
    _In_ PVOID Context)
{
    PE100_ADAPTER Adapter = Context;
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        if (!Adapter->CuStatus || (*Adapter->CuStatus & E100_CB_STATUS_C))
            break;

        E100Stall(10);
    }

    E100Command(Adapter, E100_RU_ABORT, 0);
    E100WaitCommand(Adapter);
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
E100GetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PE100_ADAPTER Adapter = Context;
    volatile E100_RFD *Rfd;
    ULONG Index, Count, Size;

    for (Count = 0; Count < E100_RX_COUNT; Count++)
    {
        Index = Adapter->RxNext;
        Rfd = E100Rfd(Adapter, Index);
        if (!(Rfd->Status & E100_CB_STATUS_C))
            break;

        Adapter->RxNext = (Index + 1) % E100_RX_COUNT;
        if (Adapter->Class == E100Class82557)
            Adapter->LastReceive = KdNetExtensibilityImports->ReadCycleCounter(NULL);

        Size = Rfd->Count & E100_RFD_COUNT;
        if ((Rfd->Status & E100_CB_STATUS_OK) && Size >= E100_MIN_FRAME && Size <= E100_RX_DATA)
        {
            Adapter->RxLength[Index] = (USHORT)Size;
            *Handle = Index;
            *Packet = (PUCHAR)Rfd + sizeof(E100_RFD);
            *Length = Size;
            return STATUS_SUCCESS;
        }

        E100ReleaseRxPacket(Adapter, Index);
    }

    E100StartReceive(Adapter);
    if ((Adapter->LinkPoll++ & 0xFF) == 0)
    {
        E100UpdateLink(Adapter);
        if (Adapter->Class == E100Class82557)
            E100ClearReceiveLockup(Adapter);
    }

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
E100ReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE100_ADAPTER Adapter = Context;
    volatile E100_RFD *Rfd;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Index >= E100_RX_COUNT)
        return;

    Rfd = E100Rfd(Adapter, Index);
    Rfd->Status = 0;
    Rfd->Count = 0;
    Rfd->Size = E100_RX_DATA;
    Rfd->Command = E100_CB_EL;
    KeMemoryBarrier();
    if (Index != Adapter->RxTail)
    {
        E100Rfd(Adapter, Adapter->RxTail)->Command = 0;
        Adapter->RxTail = Index;
    }

    KeMemoryBarrier();
}

NTSTATUS
NTAPI
E100GetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PE100_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    if (Adapter->TxSubmitted[Index])
    {
        if (!(E100Tcb(Adapter, Index)->Status & E100_CB_STATUS_C))
            return STATUS_IO_TIMEOUT;

        Adapter->TxSubmitted[Index] = FALSE;
    }

    Adapter->TxNext = (Index + 1) % E100_TX_COUNT;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
E100SendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PE100_ADAPTER Adapter = Context;
    volatile E100_TCB *Tcb;
    ULONG Index = Handle & ~HANDLE_FLAGS, Physical, i;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= E100_TX_COUNT || Length > E100_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Tcb = E100Tcb(Adapter, Index);
    Physical = (ULONG)E100Physical((PVOID)Tcb);
    Tcb->Status = 0;
    Tcb->Command = E100_CB_EL | E100_CB_TRANSMIT;
    Tcb->Link = Physical;
    Tcb->TbdArray = E100_NULL_POINTER;
    Tcb->ByteCount = (USHORT)Length | E100_TCB_EOF;
    Tcb->Threshold = E100_TCB_THRESHOLD;
    Tcb->TbdNumber = 0;
    Adapter->TxSubmitted[Index] = TRUE;
    KeMemoryBarrier();

    if (Adapter->Ich && Adapter->KdNet->LinkSpeed == 10 && !Adapter->KdNet->LinkDuplex &&
        E100Command(Adapter, E100_CU_NOP, 0))
    {
        E100Stall(1);
    }

    if (!E100WaitCommandUnit(Adapter))
    {
        Adapter->TxSubmitted[Index] = FALSE;
        return STATUS_IO_TIMEOUT;
    }

    E100StartCommandUnit(Adapter, &Tcb->Status, Physical);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < 10000; i++)
    {
        if (Tcb->Status & E100_CB_STATUS_C)
            return STATUS_SUCCESS;

        E100Stall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
E100GetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE100_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < E100_TX_COUNT ? Adapter->TxSlots + Index * E100_SLOT_SIZE + sizeof(E100_TCB) : NULL;

    return Index < E100_RX_COUNT ? Adapter->RxSlots + Index * E100_SLOT_SIZE + sizeof(E100_RFD) : NULL;
}

ULONG
NTAPI
E100GetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PE100_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return E100_MAX_FRAME;

    return Index < E100_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
