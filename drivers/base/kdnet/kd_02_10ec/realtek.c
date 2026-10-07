/*
 * PROJECT:     LiberNT Realtek Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Realtek RTL8139C+, RTL8169 and RTL8168 Ethernet controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd10ec.h"

#define RTL_REVISION_8139_CPLUS 0x20

static
UCHAR
RtlRead8(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register)
{
    if (Adapter->Mapped)
        return KdNetExtensibilityImports->ReadRegisterUChar(Adapter->Base + Register);

    return KdNetExtensibilityImports->ReadPortUChar(Adapter->Base + Register);
}

static
USHORT
RtlRead16(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register)
{
    if (Adapter->Mapped)
        return KdNetExtensibilityImports->ReadRegisterUShort((PUSHORT)(Adapter->Base + Register));

    return KdNetExtensibilityImports->ReadPortUShort((PUSHORT)(Adapter->Base + Register));
}

static
ULONG
RtlRead32(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register)
{
    if (Adapter->Mapped)
        return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Base + Register));

    return KdNetExtensibilityImports->ReadPortULong((PULONG)(Adapter->Base + Register));
}

static
VOID
RtlWrite8(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    if (Adapter->Mapped)
        KdNetExtensibilityImports->WriteRegisterUChar(Adapter->Base + Register, Value);
    else
        KdNetExtensibilityImports->WritePortUChar(Adapter->Base + Register, Value);
}

static
VOID
RtlWrite16(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    if (Adapter->Mapped)
        KdNetExtensibilityImports->WriteRegisterUShort((PUSHORT)(Adapter->Base + Register), Value);
    else
        KdNetExtensibilityImports->WritePortUShort((PUSHORT)(Adapter->Base + Register), Value);
}

static
VOID
RtlWrite32(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    if (Adapter->Mapped)
        KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Base + Register), Value);
    else
        KdNetExtensibilityImports->WritePortULong((PULONG)(Adapter->Base + Register), Value);
}

static
VOID
RtlStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG64
RtlPhysical(
    _In_ PVOID Address)
{
    return (ULONG64)KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
ULONG
RtlAlignedAdapterSize(VOID)
{
    return (sizeof(RTL_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static
ULONG
RtlRingSize(
    _In_ ULONG Count)
{
    return (Count * sizeof(RTL_DESCRIPTOR) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

RTL_FAMILY
RtlIdentifyDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UCHAR Revision = 0;

    if (Device->VendorID != 0x10EC)
        return RtlFamilyNone;

    switch (Device->DeviceID)
    {
        case 0x8139:
            KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Revision,
                                                          FIELD_OFFSET(PCI_COMMON_HEADER, RevisionID),
                                                          sizeof(Revision));
            return Revision >= RTL_REVISION_8139_CPLUS ? RtlFamily8139 : RtlFamilyNone;

        case 0x8136:
        case 0x8161:
        case 0x8167:
        case 0x8168:
        case 0x8169:
            return RtlFamily8169;
    }

    return RtlFamilyNone;
}

ULONG
NTAPI
RtlGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return RtlAlignedAdapterSize() + RtlRingSize(RTL_RX_COUNT) + RtlRingSize(RTL_TX_COUNT) +
           (RTL_RX_COUNT + RTL_TX_COUNT) * RTL_BUFFER_SIZE;
}

static
VOID
RtlUpdateLink(
    _In_ PRTL_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    UCHAR Status;

    if (Adapter->Family == RtlFamily8139)
    {
        Status = RtlRead8(Adapter, RTL_REG_MSR_8139);
        if (Status & RTL_MSR_LINKB)
        {
            *KdNet->LinkState = 0;
            return;
        }

        KdNet->LinkSpeed = (Status & RTL_MSR_SPEED_10) ? 10 : 100;
        KdNet->LinkDuplex = (RtlRead16(Adapter, RTL_REG_BMCR_8139) & RTL_BMCR_DUPLEX) ? 1 : 0;
        *KdNet->LinkState = 1;
        return;
    }

    Status = RtlRead8(Adapter, RTL_REG_PHYSTATUS_8169);
    if (!(Status & RTL_PHYSTATUS_LINK))
    {
        *KdNet->LinkState = 0;
        return;
    }

    if (Status & RTL_PHYSTATUS_10M)
        KdNet->LinkSpeed = 10;
    else if (Status & RTL_PHYSTATUS_100M)
        KdNet->LinkSpeed = 100;
    else
        KdNet->LinkSpeed = 1000;

    KdNet->LinkDuplex = (Status & RTL_PHYSTATUS_FULLDUP) ? 1 : 0;
    *KdNet->LinkState = 1;
}

static
VOID
RtlArmReceive(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Index)
{
    ULONG Flags = RTL_DESC_OWN | RTL_BUFFER_SIZE;

    if (Index == RTL_RX_COUNT - 1)
        Flags |= RTL_DESC_EOR;

    Adapter->RxRing[Index].Vlan = 0;
    KeMemoryBarrier();
    Adapter->RxRing[Index].Flags = Flags;
    KeMemoryBarrier();
}

typedef struct _RTL_CHIP
{
    USHORT Mask;
    USHORT Value;
    UCHAR Flags;
} RTL_CHIP;

static const RTL_CHIP RtlChips[] =
{
    { 0x7CF, 0x54B, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x54A, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x502, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x541, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x6C0, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x5C8, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x509, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7CF, 0x4C0, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED | RTL_CHIP_GATED },
    { 0x7C8, 0x488, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED },
    { 0x7CF, 0x481, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED },
    { 0x7CF, 0x480, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED },
    { 0x7C8, 0x2C8, RTL_CHIP_MULTI | RTL_CHIP_EXTENDED },
    { 0x7CF, 0x28A, RTL_CHIP_NO_PHY },
    { 0x7CF, 0x28B, RTL_CHIP_NO_PHY },
    { 0x7C8, 0x3C8, RTL_CHIP_MULTI },
    { 0x7C8, 0x3C0, RTL_CHIP_MULTI },
    { 0x7C8, 0x380, RTL_CHIP_THRESHOLD },
    { 0x7C8, 0x340, RTL_CHIP_THRESHOLD },
    { 0x7CF, 0x240, RTL_CHIP_THRESHOLD },
    { 0xFC8, 0x980, RTL_CHIP_THRESHOLD },
    { 0xFC8, 0x180, RTL_CHIP_THRESHOLD },
    { 0xFC8, 0x100, RTL_CHIP_THRESHOLD },
    { 0xFC8, 0x040, RTL_CHIP_THRESHOLD },
    { 0xFC8, 0x008, RTL_CHIP_THRESHOLD }
};

static const UCHAR RtlFallbackAddress[MAC_ADDRESS_SIZE] = { 0x02, 0xDE, 0xAD, 0x10, 0xEC, 0x68 };

static
VOID
RtlIdentifyChip(
    _In_ PRTL_ADAPTER Adapter)
{
    ULONG i;

    Adapter->ChipId = (USHORT)((RtlRead32(Adapter, RTL_REG_TCR) >> RTL_TCR_ID_SHIFT) & RTL_TCR_ID_MASK);
    Adapter->ChipFlags = 0;
    for (i = 0; i < RTL_NUMBER_OF(RtlChips); i++)
    {
        if ((Adapter->ChipId & RtlChips[i].Mask) == RtlChips[i].Value)
        {
            Adapter->ChipFlags = RtlChips[i].Flags;
            break;
        }
    }
}

static
BOOLEAN
RtlWaitFlag(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ BOOLEAN Set)
{
    ULONG i;

    for (i = 0; i < 100; i++)
    {
        if (!(RtlRead32(Adapter, Register) & RTL_FLAG) == !Set)
            return TRUE;

        RtlStall(100);
    }

    return FALSE;
}

static
VOID
RtlWaitMcu(
    _In_ PRTL_ADAPTER Adapter,
    _In_ UCHAR Bits)
{
    ULONG i;

    for (i = 0; i < 100 && (RtlRead8(Adapter, RTL_REG_MCU) & Bits) != Bits; i++)
        RtlStall(100);
}

static
ULONG
RtlReadExtended(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Address)
{
    RtlWrite32(Adapter, RTL_REG_ERIAR, (RTL_ERI_ALL << RTL_ERIAR_MASK_SHIFT) | Address);
    return RtlWaitFlag(Adapter, RTL_REG_ERIAR, TRUE) ? RtlRead32(Adapter, RTL_REG_ERIDR) : 0;
}

static
VOID
RtlWriteExtended(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    RtlWrite32(Adapter, RTL_REG_ERIDR, Value);
    RtlWrite32(Adapter, RTL_REG_ERIAR, RTL_FLAG | (Mask << RTL_ERIAR_MASK_SHIFT) | Address);
    RtlWaitFlag(Adapter, RTL_REG_ERIAR, FALSE);
}

static
USHORT
RtlReadController(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register)
{
    RtlWrite32(Adapter, RTL_REG_OCPDR, Register << RTL_OCP_REGISTER);
    return (USHORT)RtlRead32(Adapter, RTL_REG_OCPDR);
}

static
VOID
RtlWriteController(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    RtlWrite32(Adapter, RTL_REG_OCPDR, RTL_FLAG | (Register << RTL_OCP_REGISTER) | Value);
}

static
BOOLEAN
RtlReadPhy(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    ULONG Access = RTL_REG_PHYAR, Command = Register << RTL_PHYAR_REGISTER;

    if (Adapter->ChipFlags & RTL_CHIP_GATED)
    {
        Access = RTL_REG_GPHY_OCP;
        Command = (RTL_OCP_PHY_BASE + Register * 2) << RTL_OCP_REGISTER;
    }

    *Value = 0;
    RtlWrite32(Adapter, Access, Command);
    if (!RtlWaitFlag(Adapter, Access, TRUE))
        return FALSE;

    *Value = (USHORT)RtlRead32(Adapter, Access);
    RtlStall(20);
    return TRUE;
}

static
VOID
RtlWritePhy(
    _In_ PRTL_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    ULONG Access = RTL_REG_PHYAR, Command = Register << RTL_PHYAR_REGISTER;

    if (Adapter->ChipFlags & RTL_CHIP_GATED)
    {
        Access = RTL_REG_GPHY_OCP;
        Command = (RTL_OCP_PHY_BASE + Register * 2) << RTL_OCP_REGISTER;
    }

    RtlWrite32(Adapter, Access, RTL_FLAG | Command | Value);
    RtlWaitFlag(Adapter, Access, FALSE);
    RtlStall(20);
}

static
VOID
RtlLeaveOutOfBand(
    _In_ PRTL_ADAPTER Adapter)
{
    ULONG i;

    RtlWrite32(Adapter, RTL_REG_MISC, RtlRead32(Adapter, RTL_REG_MISC) | RTL_MISC_RECEIVE_GATE);
    RtlStall(2000);
    for (i = 0; i < 100 && !(RtlRead32(Adapter, RTL_REG_TCR) & RTL_TCR_EMPTY); i++)
        RtlStall(100);

    RtlWaitMcu(Adapter, RTL_MCU_EMPTY);
    RtlWrite8(Adapter, RTL_REG_CR, RtlRead8(Adapter, RTL_REG_CR) & ~(RTL_CR_TE | RTL_CR_RE));
    RtlStall(1000);
    RtlWrite8(Adapter, RTL_REG_MCU, RtlRead8(Adapter, RTL_REG_MCU) & ~RTL_MCU_OUT_OF_BAND);
    RtlWriteController(Adapter, RTL_OCP_LINK_LIST,
                       RtlReadController(Adapter, RTL_OCP_LINK_LIST) & ~RTL_OCP_LINK_LIST_HOLD);
    RtlWaitMcu(Adapter, RTL_MCU_LINK_LIST_READY);
    RtlWriteController(Adapter, RTL_OCP_LINK_LIST,
                       RtlReadController(Adapter, RTL_OCP_LINK_LIST) | RTL_OCP_LINK_LIST_GO);
    RtlWaitMcu(Adapter, RTL_MCU_LINK_LIST_READY);
}

static
VOID
RtlStartExtended(
    _In_ PRTL_ADAPTER Adapter)
{
    BOOLEAN Gated = (Adapter->ChipFlags & RTL_CHIP_GATED) != 0;
    ULONG Filter;

    RtlWriteExtended(Adapter, RTL_ERI_TX_TIMER, RTL_ERI_WORD, 0);
    RtlWriteExtended(Adapter, RTL_ERI_RX_TIMER, Gated ? RTL_ERI_WORD : RTL_ERI_ALL, 0);
    RtlWriteExtended(Adapter, RTL_ERI_RX_FIFO, RTL_ERI_ALL,
                     Gated ? RTL_ERI_RX_FIFO_GATED : RTL_ERI_RX_FIFO_VALUE);
    RtlWriteExtended(Adapter, RTL_ERI_TX_FIFO, RTL_ERI_ALL, RTL_ERI_TX_FIFO_VALUE);
    Filter = RtlReadExtended(Adapter, RTL_ERI_FILTER);
    RtlWriteExtended(Adapter, RTL_ERI_FILTER, RTL_ERI_ALL, Filter & ~1);
    RtlWriteExtended(Adapter, RTL_ERI_FILTER, RTL_ERI_ALL, Filter | 1);
    RtlWrite8(Adapter, RTL_REG_MCU, RtlRead8(Adapter, RTL_REG_MCU) & ~RTL_MCU_OUT_OF_BAND);
    if (Gated)
        RtlWrite32(Adapter, RTL_REG_MISC, RtlRead32(Adapter, RTL_REG_MISC) & ~RTL_MISC_RECEIVE_GATE);
}

static
VOID
RtlWakePhy(
    _In_ PRTL_ADAPTER Adapter)
{
    USHORT Control;

    if ((Adapter->ChipFlags & RTL_CHIP_NO_PHY) || !RtlReadPhy(Adapter, RTL_PHY_BMCR, &Control) ||
        Control == 0xFFFF ||
        ((Control & RTL_BMCR_ANENABLE) && !(Control & (RTL_BMCR_PDOWN | RTL_BMCR_ISOLATE))))
    {
        return;
    }

    if (Adapter->ChipId == RTL_ID_8168G)
    {
        RtlWriteExtended(Adapter, RTL_ERI_PHY_INTERRUPTS, RTL_ERI_ALL,
                         RtlReadExtended(Adapter, RTL_ERI_PHY_INTERRUPTS) | RTL_ERI_PHY_INTERRUPT_BITS);
    }

    RtlWritePhy(Adapter, RTL_PHY_BMCR, RTL_BMCR_ANENABLE | RTL_BMCR_ANRESTART);
}

static
BOOLEAN
RtlValidAddress(
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
RtlSetAddress(
    _In_ PRTL_ADAPTER Adapter,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    BOOLEAN Extended = Adapter->Family == RtlFamily8169 && (Adapter->ChipFlags & RTL_CHIP_EXTENDED);
    BOOLEAN Early = (Adapter->ChipId & RTL_ID_8168EVL_MASK) == RTL_ID_8168EVL;
    ULONG Low, High, i;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        Address[i] = RtlRead8(Adapter, RTL_REG_IDR0 + i);

    if (RtlValidAddress(Address))
        return;

    if (Extended && !Early)
    {
        Low = RtlReadExtended(Adapter, RTL_ERI_ADDRESS);
        High = RtlReadExtended(Adapter, RTL_ERI_ADDRESS + 4);
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Address[i] = (UCHAR)(i < 4 ? Low >> (i * 8) : High >> ((i - 4) * 8));
    }

    if (!RtlValidAddress(Address))
    {
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Address[i] = RtlFallbackAddress[i];
    }

    Low = Address[0] | (Address[1] << 8) | (Address[2] << 16) | ((ULONG)Address[3] << 24);
    High = Address[4] | (Address[5] << 8);
    RtlWrite8(Adapter, RTL_REG_9346CR, RTL_9346CR_UNLOCK);
    RtlWrite32(Adapter, RTL_REG_IDR0 + 4, High);
    RtlRead8(Adapter, RTL_REG_CR);
    RtlWrite32(Adapter, RTL_REG_IDR0, Low);
    RtlRead8(Adapter, RTL_REG_CR);
    if (Extended && Early)
    {
        RtlWriteExtended(Adapter, RTL_ERI_ADDRESS, RTL_ERI_ALL, Low);
        RtlWriteExtended(Adapter, RTL_ERI_ADDRESS + 4, RTL_ERI_ALL, High);
        RtlWriteExtended(Adapter, RTL_ERI_ADDRESS_COPY, RTL_ERI_ALL, (Low & 0xFFFF) << 16);
        RtlWriteExtended(Adapter, RTL_ERI_ADDRESS_COPY + 4, RTL_ERI_ALL, (Low >> 16) | (High << 16));
    }

    RtlWrite8(Adapter, RTL_REG_9346CR, RTL_9346CR_LOCK);
}

NTSTATUS
NTAPI
RtlInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PRTL_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Hardware = KdNet->Hardware, Base = NULL;
    BOOLEAN Mapped = FALSE;
    ULONG64 Physical;
    USHORT Command;
    ULONG Configuration, i;

    for (i = 0; i < MAXIMUM_DEBUG_BARS; i++)
    {
        if (Device->BaseAddress[i].Valid && Device->BaseAddress[i].Type == CmResourceTypeMemory)
        {
            Base = Device->BaseAddress[i].TranslatedAddress;
            Mapped = TRUE;
            break;
        }
    }

    for (i = 0; !Base && i < MAXIMUM_DEBUG_BARS; i++)
    {
        if (Device->BaseAddress[i].Valid && Device->BaseAddress[i].Type == CmResourceTypePort)
            Base = Device->BaseAddress[i].TranslatedAddress;
    }

    if (!Base)
        return STATUS_NO_SUCH_DEVICE;

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->Base = Base;
    Adapter->Mapped = Mapped;
    Adapter->Family = RtlIdentifyDevice(Device);
    Adapter->KdNet = KdNet;
    Adapter->RxRing = (PVOID)(Hardware + RtlAlignedAdapterSize());
    Adapter->TxRing = (PVOID)((PUCHAR)Adapter->RxRing + RtlRingSize(RTL_RX_COUNT));
    Adapter->RxBuffers = (PUCHAR)Adapter->TxRing + RtlRingSize(RTL_TX_COUNT);
    Adapter->TxBuffers = Adapter->RxBuffers + RTL_RX_COUNT * RTL_BUFFER_SIZE;
    if (Adapter->Family == RtlFamilyNone)
        return STATUS_NOT_SUPPORTED;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Command,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Command));
    Command |= PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    KdNetExtensibilityImports->SetPciDataByOffset(Device->Bus, Device->Slot, &Command,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Command));

    RtlWrite16(Adapter, RTL_REG_IMR, 0);
    if (Adapter->Family == RtlFamily8169)
    {
        RtlIdentifyChip(Adapter);
        if (Adapter->ChipFlags & RTL_CHIP_GATED)
            RtlLeaveOutOfBand(Adapter);
    }

    RtlWrite8(Adapter, RTL_REG_CR, RTL_CR_RST);
    for (i = 0; i < 1000; i++)
    {
        RtlStall(100);
        if (!(RtlRead8(Adapter, RTL_REG_CR) & RTL_CR_RST))
            break;
    }

    if (i == 1000)
        return STATUS_IO_TIMEOUT;

    RtlWrite16(Adapter, RTL_REG_IMR, 0);
    RtlWrite16(Adapter, RTL_REG_ISR, 0xFFFF);

    Configuration = RTL_RCR_MXDMA;
    if (Adapter->Family == RtlFamily8169)
    {
        if (Adapter->ChipFlags & RTL_CHIP_THRESHOLD)
            Configuration |= RTL_RCR_RXFTH;
        else
            Configuration |= RTL_RCR_RX128;

        if (Adapter->ChipFlags & RTL_CHIP_MULTI)
            Configuration |= RTL_RCR_MULTI;

        if (Adapter->ChipFlags & RTL_CHIP_GATED)
            Configuration |= RTL_RCR_EARLY_OFF;

        RtlWrite32(Adapter, RTL_REG_RCR, Configuration);
    }

    RtlSetAddress(Adapter, KdNet->TargetMacAddress);

    for (i = 0; i < RTL_RX_COUNT; i++)
    {
        Physical = RtlPhysical(Adapter->RxBuffers + i * RTL_BUFFER_SIZE);
        Adapter->RxRing[i].AddressLow = (ULONG)Physical;
        Adapter->RxRing[i].AddressHigh = (ULONG)(Physical >> 32);
        RtlArmReceive(Adapter, i);
    }

    for (i = 0; i < RTL_TX_COUNT; i++)
    {
        Physical = RtlPhysical(Adapter->TxBuffers + i * RTL_BUFFER_SIZE);
        Adapter->TxRing[i].AddressLow = (ULONG)Physical;
        Adapter->TxRing[i].AddressHigh = (ULONG)(Physical >> 32);
        Adapter->TxRing[i].Vlan = 0;
        Adapter->TxRing[i].Flags = i == RTL_TX_COUNT - 1 ? RTL_DESC_EOR : 0;
    }

    KeMemoryBarrier();

    RtlWrite8(Adapter, RTL_REG_9346CR, RTL_9346CR_UNLOCK);

    Command = RtlRead16(Adapter, RTL_REG_CPCR);
    Command &= ~(RTL_CPCR_RXCHKSUM | RTL_CPCR_RXVLAN);
    if (Adapter->Family == RtlFamily8139)
        Command |= RTL_CPCR_TXEN | RTL_CPCR_RXEN;
    RtlWrite16(Adapter, RTL_REG_CPCR, Command);

    RtlWrite16(Adapter, RTL_REG_RMS, RTL_RECEIVE_SIZE);
    if (Adapter->Family == RtlFamily8169)
    {
        RtlWrite8(Adapter, RTL_REG_MTPS,
                  (Adapter->ChipFlags & RTL_CHIP_EXTENDED) ? RTL_MTPS_EARLY : RTL_MTPS_DEFAULT);
        if (Adapter->ChipFlags & RTL_CHIP_EXTENDED)
            RtlStartExtended(Adapter);
    }

    Physical = RtlPhysical((PVOID)Adapter->TxRing);
    RtlWrite32(Adapter, RTL_REG_TNPDS + 4, (ULONG)(Physical >> 32));
    RtlWrite32(Adapter, RTL_REG_TNPDS, (ULONG)Physical);
    Physical = RtlPhysical((PVOID)Adapter->RxRing);
    RtlWrite32(Adapter, RTL_REG_RDSAR + 4, (ULONG)(Physical >> 32));
    RtlWrite32(Adapter, RTL_REG_RDSAR, (ULONG)Physical);

    RtlWrite8(Adapter, RTL_REG_CR, RTL_CR_RE | RTL_CR_TE);
    if (Adapter->Family == RtlFamily8139)
    {
        RtlWrite32(Adapter, RTL_REG_TCR, RTL_TCR_DEFAULT);
        RtlWrite32(Adapter, RTL_REG_RCR,
                   (RtlRead32(Adapter, RTL_REG_RCR) & ~RTL_RCR_FILTER) |
                   RTL_RCR_RXFTH | RTL_RCR_MXDMA | RTL_RCR_AB | RTL_RCR_APM);
    }
    else
    {
        RtlWrite32(Adapter, RTL_REG_TCR,
                   RTL_TCR_DEFAULT | ((Adapter->ChipFlags & RTL_CHIP_EXTENDED) ? RTL_TCR_AUTO_FIFO : 0));
        RtlWrite32(Adapter, RTL_REG_RCR, Configuration | RTL_RCR_AB | RTL_RCR_APM);
    }

    RtlWrite32(Adapter, RTL_REG_MAR0, 0);
    RtlWrite32(Adapter, RTL_REG_MAR0 + 4, 0);

    RtlWrite8(Adapter, RTL_REG_9346CR, RTL_9346CR_LOCK);
    RtlWrite16(Adapter, RTL_REG_IMR, 0);
    RtlWrite16(Adapter, RTL_REG_ISR, 0xFFFF);
    if (Adapter->Family == RtlFamily8169)
        RtlWakePhy(Adapter);

    for (i = 0; i < 400; i++)
    {
        RtlUpdateLink(Adapter);
        if (*KdNet->LinkState)
            break;

        RtlStall(10000);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlShutdownController(
    _In_ PVOID Context)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG i, Index;

    for (i = 0; i < 10000; i++)
    {
        for (Index = 0; Index < RTL_TX_COUNT; Index++)
        {
            if (Adapter->TxRing[Index].Flags & RTL_DESC_OWN)
                break;
        }

        if (Index == RTL_TX_COUNT)
            break;

        RtlStall(10);
    }

    RtlWrite8(Adapter, RTL_REG_CR, 0);
    RtlWrite16(Adapter, RTL_REG_IMR, 0);
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
RtlGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index, Count, Flags, Size, Error, Mask;

    Error = Adapter->Family == RtlFamily8139 ? RTL_DESC_RES_8139 : RTL_DESC_RES_8169;
    Mask = Adapter->Family == RtlFamily8139 ? RTL_DESC_LENGTH_8139 : RTL_DESC_LENGTH_8169;
    for (Count = 0; Count < RTL_RX_COUNT; Count++)
    {
        Index = Adapter->RxNext;
        Flags = Adapter->RxRing[Index].Flags;
        if (Flags & RTL_DESC_OWN)
            break;

        Adapter->RxNext = (Index + 1) % RTL_RX_COUNT;
        Size = Flags & Mask;
        if ((Flags & (RTL_DESC_FS | RTL_DESC_LS)) == (RTL_DESC_FS | RTL_DESC_LS) && !(Flags & Error) &&
            Size >= 14 + RTL_CRC_SIZE && Size <= RTL_BUFFER_SIZE)
        {
            Adapter->RxLength[Index] = (USHORT)(Size - RTL_CRC_SIZE);
            *Handle = Index;
            *Packet = Adapter->RxBuffers + Index * RTL_BUFFER_SIZE;
            *Length = Size - RTL_CRC_SIZE;
            return STATUS_SUCCESS;
        }

        RtlArmReceive(Adapter, Index);
    }

    if ((Adapter->LinkPoll++ & 0xFF) == 0)
        RtlUpdateLink(Adapter);

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
RtlReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Index < RTL_RX_COUNT)
        RtlArmReceive(Adapter, Index);
}

NTSTATUS
NTAPI
RtlGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    if (Adapter->TxSubmitted[Index])
    {
        if (Adapter->TxRing[Index].Flags & RTL_DESC_OWN)
            return STATUS_IO_TIMEOUT;

        Adapter->TxSubmitted[Index] = FALSE;
    }

    Adapter->TxNext = (Index + 1) % RTL_TX_COUNT;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS, Flags, i;
    PUCHAR Buffer;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= RTL_TX_COUNT || Length > RTL_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Buffer = Adapter->TxBuffers + Index * RTL_BUFFER_SIZE;
    while (Length < RTL_MIN_FRAME)
        Buffer[Length++] = 0;

    Flags = RTL_DESC_OWN | RTL_DESC_FS | RTL_DESC_LS | Length;
    if (Index == RTL_TX_COUNT - 1)
        Flags |= RTL_DESC_EOR;

    Adapter->TxRing[Index].Vlan = 0;
    Adapter->TxSubmitted[Index] = TRUE;
    KeMemoryBarrier();
    Adapter->TxRing[Index].Flags = Flags;
    KeMemoryBarrier();
    RtlWrite8(Adapter,
              Adapter->Family == RtlFamily8139 ? RTL_REG_TPPOLL_8139 : RTL_REG_TPPOLL_8169,
              RTL_TPPOLL_NPQ);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < 10000; i++)
    {
        if (!(Adapter->TxRing[Index].Flags & RTL_DESC_OWN))
            return STATUS_SUCCESS;

        RtlStall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
RtlGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < RTL_TX_COUNT ? Adapter->TxBuffers + Index * RTL_BUFFER_SIZE : NULL;

    return Index < RTL_RX_COUNT ? Adapter->RxBuffers + Index * RTL_BUFFER_SIZE : NULL;
}

ULONG
NTAPI
RtlGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PRTL_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return RTL_MAX_FRAME;

    return Index < RTL_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
