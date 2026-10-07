/*
 * PROJECT:     LiberNT Raspberry Pi RP1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Cadence GEM driver for the RP1 Ethernet of the Raspberry Pi 5
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdrp1.h"

#define GEM_REGISTER_SPACE      0x04D8
#define GEM_RING_PAGES          2
#define GEM_MDIO_WAIT           10000
#define GEM_TX_WAIT             10000
#define GEM_LINK_WAIT           400

static const UCHAR GemFallbackAddress[MAC_ADDRESS_SIZE] = { 0x02, 0x00, 0x00, 0x27, 0x12, 0x01 };

static
ULONG
GemRead(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Registers + Register));
}

static
VOID
GemWrite(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Registers + Register), Value);
}

static
VOID
GemStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG64
GemPhysical(
    _In_ PVOID Address)
{
    return (ULONG64)KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
ULONG
GemAlignedAdapterSize(VOID)
{
    return (sizeof(GEM_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

ULONG
NTAPI
GemGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return GemAlignedAdapterSize() + GEM_RING_PAGES * PAGE_SIZE +
           (GEM_RX_COUNT + GEM_TX_COUNT) * GEM_BUFFER_SIZE;
}

static
BOOLEAN
GemWaitManagement(
    _In_ PGEM_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < GEM_MDIO_WAIT; i++)
    {
        if (GemRead(Adapter, GEM_NSR) & GEM_NSR_IDLE)
            return TRUE;

        GemStall(10);
    }

    return FALSE;
}

static
BOOLEAN
GemPhyRead(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Phy,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    *Value = 0;
    if (!GemWaitManagement(Adapter))
        return FALSE;

    GemWrite(Adapter, GEM_MAN,
             GEM_MAN_READ | (Phy << GEM_MAN_PHY_SHIFT) | (Register << GEM_MAN_REGISTER_SHIFT));
    if (!GemWaitManagement(Adapter))
        return FALSE;

    *Value = (USHORT)GemRead(Adapter, GEM_MAN);
    return TRUE;
}

static
BOOLEAN
GemPhyWrite(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Phy,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    if (!GemWaitManagement(Adapter))
        return FALSE;

    GemWrite(Adapter, GEM_MAN,
             GEM_MAN_WRITE | (Phy << GEM_MAN_PHY_SHIFT) | (Register << GEM_MAN_REGISTER_SHIFT) | Value);
    return GemWaitManagement(Adapter);
}

static
BOOLEAN
GemPhySelectExtended(
    _In_ PGEM_ADAPTER Adapter,
    _In_ USHORT Device,
    _In_ USHORT Register)
{
    return GemPhyWrite(Adapter, Adapter->PhyAddress, MII_MMD_CTRL, Device) &&
           GemPhyWrite(Adapter, Adapter->PhyAddress, MII_MMD_DATA, Register) &&
           GemPhyWrite(Adapter, Adapter->PhyAddress, MII_MMD_CTRL, MII_MMD_CTRL_NOINCR | Device);
}

static
BOOLEAN
GemPhyIdentify(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Phy,
    _Out_ PULONG Id)
{
    USHORT High, Low;

    if (!GemPhyRead(Adapter, Phy, MII_PHYSID1, &High) || !GemPhyRead(Adapter, Phy, MII_PHYSID2, &Low) ||
        High == 0xFFFF || Low == 0xFFFF || (!High && !Low))
    {
        return FALSE;
    }

    *Id = ((ULONG)High << 16) | Low;
    return TRUE;
}

static
VOID
GemConfigureBroadcomPhy(
    _In_ PGEM_ADAPTER Adapter)
{
    ULONG Phy = Adapter->PhyAddress;
    USHORT Value;

    if (GemPhyWrite(Adapter, Phy, MII_BCM54XX_AUX_CTL,
                    MII_BCM54XX_AUXCTL_MISC | (MII_BCM54XX_AUXCTL_MISC << MII_BCM54XX_AUXCTL_READ_SHIFT)) &&
        GemPhyRead(Adapter, Phy, MII_BCM54XX_AUX_CTL, &Value))
    {
        GemPhyWrite(Adapter, Phy, MII_BCM54XX_AUX_CTL,
                    MII_BCM54XX_AUXCTL_MISC | MII_BCM54XX_AUXCTL_MISC_WREN |
                    MII_BCM54XX_AUXCTL_MISC_SKEW | Value);
    }

    if (GemPhyWrite(Adapter, Phy, MII_BCM54XX_SHD, BCM54810_SHD_CLK_CTL << MII_BCM54XX_SHD_SHIFT) &&
        GemPhyRead(Adapter, Phy, MII_BCM54XX_SHD, &Value))
    {
        GemPhyWrite(Adapter, Phy, MII_BCM54XX_SHD,
                    MII_BCM54XX_SHD_WRITE | (BCM54810_SHD_CLK_CTL << MII_BCM54XX_SHD_SHIFT) |
                    (Value & MII_BCM54XX_SHD_DATA) | BCM54810_SHD_CLK_CTL_GTXCLK_EN);
    }

    GemPhyWrite(Adapter, Phy, MII_BCM54XX_AUX_CTL,
                MII_BCM54XX_AUXCTL_ACTL_SMDSP | MII_BCM54XX_AUXCTL_ACTL_TX_6DB);
    GemPhyWrite(Adapter, Phy, MII_BCM54XX_AUX_CTL, MII_BCM54XX_AUXCTL_ACTL_TX_6DB);

    if (GemPhySelectExtended(Adapter, MDIO_MMD_AN, MDIO_AN_EEE_ADV) &&
        GemPhyRead(Adapter, Phy, MII_MMD_DATA, &Value) &&
        (Value & (MDIO_EEE_100TX | MDIO_EEE_1000T)) &&
        GemPhySelectExtended(Adapter, MDIO_MMD_AN, MDIO_AN_EEE_ADV))
    {
        GemPhyWrite(Adapter, Phy, MII_MMD_DATA, Value & ~(MDIO_EEE_100TX | MDIO_EEE_1000T));
    }
}

static
VOID
GemStartPhy(
    _In_ PGEM_ADAPTER Adapter)
{
    ULONG Id = 0, Phy;

    Adapter->PhyAddress = GEM_PHY_ADDRESS;
    Adapter->PhyValid = GemPhyIdentify(Adapter, GEM_PHY_ADDRESS, &Id);
    for (Phy = 0; !Adapter->PhyValid && Phy < GEM_PHY_COUNT; Phy++)
    {
        if (Phy != GEM_PHY_ADDRESS && GemPhyIdentify(Adapter, Phy, &Id))
        {
            Adapter->PhyAddress = (UCHAR)Phy;
            Adapter->PhyValid = TRUE;
        }
    }

    if (!Adapter->PhyValid)
        return;

    if ((Id & PHY_ID_MODEL_MASK) == PHY_ID_BCM54210E)
        GemConfigureBroadcomPhy(Adapter);

    GemPhyWrite(Adapter, Adapter->PhyAddress, MII_ADVERTISE, ADVERTISE_ALL);
    GemPhyWrite(Adapter, Adapter->PhyAddress, MII_CTRL1000, ADVERTISE_1000FULL);
    GemPhyWrite(Adapter, Adapter->PhyAddress, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

static
VOID
GemArmReceive(
    _In_ PGEM_ADAPTER Adapter,
    _In_ ULONG Index)
{
    volatile GEM_DESCRIPTOR *Descriptor = &Adapter->RxRing[Index];
    ULONG64 Physical = GemPhysical(Adapter->RxBuffers + Index * GEM_BUFFER_SIZE);

    Descriptor->Control = 0;
    Descriptor->AddressHigh = (ULONG)(Physical >> 32);
    KeMemoryBarrier();
    Descriptor->Address = (ULONG)Physical | (Index == GEM_RX_COUNT - 1 ? GEM_RX_WRAP : 0);
    KeMemoryBarrier();
}

static
VOID
GemResetRings(
    _In_ PGEM_ADAPTER Adapter)
{
    ULONG64 Physical;
    ULONG i;

    for (i = 0; i < GEM_RX_COUNT; i++)
        GemArmReceive(Adapter, i);

    for (i = 0; i < GEM_TX_COUNT; i++)
    {
        Physical = GemPhysical(Adapter->TxBuffers + i * GEM_BUFFER_SIZE);
        Adapter->TxRing[i].Address = (ULONG)Physical;
        Adapter->TxRing[i].AddressHigh = (ULONG)(Physical >> 32);
        Adapter->TxRing[i].Control = GEM_TX_USED | (i == GEM_TX_COUNT - 1 ? GEM_TX_WRAP : 0);
        Adapter->TxSubmitted[i] = FALSE;
    }

    Adapter->RxNext = 0;
    Adapter->TxNext = 0;
    KeMemoryBarrier();
    Physical = GemPhysical((PVOID)Adapter->RxRing);
    GemWrite(Adapter, GEM_RBQPH, (ULONG)(Physical >> 32));
    GemWrite(Adapter, GEM_RBQP, (ULONG)Physical);
    Physical = GemPhysical((PVOID)Adapter->TxRing);
    GemWrite(Adapter, GEM_TBQPH, (ULONG)(Physical >> 32));
    GemWrite(Adapter, GEM_TBQP, (ULONG)Physical);
    GemWrite(Adapter, GEM_TSR, GEM_TSR_ALL);
    GemWrite(Adapter, GEM_RSR, GEM_RSR_ALL);
}

static
VOID
GemApplyLink(
    _In_ PGEM_ADAPTER Adapter)
{
    ULONG Configuration = GEM_NCFGR_CLK_DIV96 | GEM_NCFGR_DBW128 | GEM_NCFGR_DRFCS | GEM_NCFGR_BIG;
    ULONG Control = GemRead(Adapter, GEM_NCR) & ~(GEM_NCR_RE | GEM_NCR_TE | GEM_NCR_TSTART);

    Control |= GEM_NCR_MPE;
    GemWrite(Adapter, GEM_NCR, Control);
    if (Adapter->LinkUp)
    {
        if (Adapter->LinkDuplex)
            Configuration |= GEM_NCFGR_FD;

        if (Adapter->LinkSpeed == 1000)
            Configuration |= GEM_NCFGR_GBE;
        else if (Adapter->LinkSpeed == 100)
            Configuration |= GEM_NCFGR_SPD;
    }

    GemWrite(Adapter, GEM_NCFGR, Configuration);
    if (Adapter->LinkUp)
    {
        GemResetRings(Adapter);
        GemWrite(Adapter, GEM_NCR, Control | GEM_NCR_RE | GEM_NCR_TE);
    }
}

static
VOID
GemUpdateLink(
    _In_ PGEM_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    USHORT Control, Status, Partner = 0, Gigabit = 0;
    ULONG Phy = Adapter->PhyAddress, Speed = 10, Duplex = 0;
    BOOLEAN Up;

    Up = Adapter->PhyValid && GemPhyRead(Adapter, Phy, MII_BMCR, &Control) &&
         !(Control & BMCR_ANRESTART) && GemPhyRead(Adapter, Phy, MII_BMSR, &Status) &&
         GemPhyRead(Adapter, Phy, MII_BMSR, &Status) && (Status & BMSR_LSTATUS);
    if (Up)
    {
        GemPhyRead(Adapter, Phy, MII_LPA, &Partner);
        GemPhyRead(Adapter, Phy, MII_STAT1000, &Gigabit);
        if (Gigabit & (LPA_1000FULL | LPA_1000HALF))
        {
            Speed = 1000;
            Duplex = (Gigabit & LPA_1000FULL) != 0;
        }
        else if (Partner & (ADVERTISE_100FULL | ADVERTISE_100HALF))
        {
            Speed = 100;
            Duplex = (Partner & ADVERTISE_100FULL) != 0;
        }
        else
        {
            Duplex = (Partner & ADVERTISE_10FULL) != 0;
        }
    }

    if (Up != Adapter->LinkUp || (Up && (Speed != Adapter->LinkSpeed || Duplex != Adapter->LinkDuplex)))
    {
        Adapter->LinkUp = Up;
        Adapter->LinkSpeed = Speed;
        Adapter->LinkDuplex = Duplex;
        GemApplyLink(Adapter);
    }

    if (Up)
    {
        KdNet->LinkSpeed = Speed;
        KdNet->LinkDuplex = Duplex;
    }

    *KdNet->LinkState = Up;
}

NTSTATUS
NTAPI
GemInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PGEM_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Hardware = KdNet->Hardware;
    UCHAR Suggested[MAC_ADDRESS_SIZE];
    const UCHAR *Fallback;
    ULONG Bottom, Top, Value, i;
    BOOLEAN Valid = FALSE;

    if (!Device->BaseAddress[0].Valid || Device->BaseAddress[0].Type != CmResourceTypeMemory ||
        Device->BaseAddress[0].Length < GEM_REGISTER_SPACE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->KdNet = KdNet;
    Adapter->Registers = Device->BaseAddress[0].TranslatedAddress;
    Adapter->RxRing = (PVOID)(Hardware + GemAlignedAdapterSize());
    Adapter->TxRing = (PVOID)(Hardware + GemAlignedAdapterSize() + PAGE_SIZE);
    Adapter->RxBuffers = Hardware + GemAlignedAdapterSize() + GEM_RING_PAGES * PAGE_SIZE;
    Adapter->TxBuffers = Adapter->RxBuffers + GEM_RX_COUNT * GEM_BUFFER_SIZE;

    if (!(GemRead(Adapter, GEM_DCFG6) & GEM_DCFG6_DAW64))
        return STATUS_NOT_SUPPORTED;

    GemWrite(Adapter, GEM_IDR, 0xFFFFFFFF);
    Value = GemRead(Adapter, GEM_NCR) & ~(GEM_NCR_RE | GEM_NCR_TE | GEM_NCR_TSTART);
    GemWrite(Adapter, GEM_NCR, Value | GEM_NCR_MPE | GEM_NCR_CLRSTAT);
    GemWrite(Adapter, GEM_TSR, GEM_TSR_ALL);
    GemWrite(Adapter, GEM_RSR, GEM_RSR_ALL);

    Bottom = GemRead(Adapter, GEM_SA1B);
    Top = GemRead(Adapter, GEM_SA1T);
    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        Suggested[i] = KdNet->TargetMacAddress[i];
        KdNet->TargetMacAddress[i] = (UCHAR)(i < 4 ? Bottom >> (i * 8) : Top >> ((i - 4) * 8));
        if (KdNet->TargetMacAddress[i])
            Valid = TRUE;
    }

    if (!Valid || (KdNet->TargetMacAddress[0] & 1))
    {
        Valid = FALSE;
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Valid |= Suggested[i] != 0;

        Fallback = (Valid && !(Suggested[0] & 1)) ? Suggested : GemFallbackAddress;
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            KdNet->TargetMacAddress[i] = Fallback[i];
    }

    GemWrite(Adapter, GEM_SA1B,
             KdNet->TargetMacAddress[0] | (KdNet->TargetMacAddress[1] << 8) |
             (KdNet->TargetMacAddress[2] << 16) | ((ULONG)KdNet->TargetMacAddress[3] << 24));
    GemWrite(Adapter, GEM_SA1T, KdNet->TargetMacAddress[4] | (KdNet->TargetMacAddress[5] << 8));

    Value = GemRead(Adapter, GEM_DMACFG) & ~GEM_DMACFG_CLEAR;
    GemWrite(Adapter, GEM_DMACFG,
             Value | GEM_DMACFG_FBLDO_INCR16 | GEM_DMACFG_RXBMS_FULL | GEM_DMACFG_TXPBMS |
             GEM_DMACFG_RXBS_2048 | GEM_DMACFG_ADDR64);
    GemWrite(Adapter, GEM_AMP, (GemRead(Adapter, GEM_AMP) & ~GEM_AMP_PIPE_MASK) | GEM_AMP_PIPE_8_FILL);
    GemWrite(Adapter, GEM_INTMOD, 0);

    GemResetRings(Adapter);
    GemApplyLink(Adapter);
    GemStartPhy(Adapter);
    for (i = 0; i < GEM_LINK_WAIT; i++)
    {
        GemUpdateLink(Adapter);
        if (Adapter->LinkUp)
            break;

        GemStall(10000);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
GemShutdownController(
    _In_ PVOID Context)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG i, Index;

    for (i = 0; i < GEM_TX_WAIT; i++)
    {
        for (Index = 0; Index < GEM_TX_COUNT; Index++)
        {
            if (Adapter->TxSubmitted[Index] && !(Adapter->TxRing[Index].Control & GEM_TX_USED))
                break;
        }

        if (Index == GEM_TX_COUNT)
            break;

        GemStall(10);
    }

    GemWrite(Adapter, GEM_NCR,
             GemRead(Adapter, GEM_NCR) & ~(GEM_NCR_RE | GEM_NCR_TE | GEM_NCR_TSTART));
    Adapter->LinkUp = FALSE;
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
GemGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG Index, Count, Control, Size;

    for (Count = 0; Count < GEM_RX_COUNT; Count++)
    {
        Index = Adapter->RxNext;
        KeMemoryBarrier();
        if (!(Adapter->RxRing[Index].Address & GEM_RX_USED))
            break;

        Control = Adapter->RxRing[Index].Control;
        Size = Control & GEM_RX_LENGTH;
        Adapter->RxNext = (Index + 1) % GEM_RX_COUNT;
        if ((Control & (GEM_RX_SOF | GEM_RX_EOF)) == (GEM_RX_SOF | GEM_RX_EOF) &&
            Size >= GEM_MIN_FRAME && Size <= GEM_MAX_FRAME)
        {
            Adapter->RxLength[Index] = (USHORT)Size;
            *Handle = Index;
            *Packet = Adapter->RxBuffers + Index * GEM_BUFFER_SIZE;
            *Length = Size;
            return STATUS_SUCCESS;
        }

        GemArmReceive(Adapter, Index);
    }

    if (Count)
        GemWrite(Adapter, GEM_RSR, GEM_RSR_ALL);

    if ((Adapter->LinkPoll++ & 0xFF) == 0)
        GemUpdateLink(Adapter);

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
GemReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Index >= GEM_RX_COUNT)
        return;

    GemArmReceive(Adapter, Index);
    GemWrite(Adapter, GEM_RSR, GEM_RSR_ALL);
}

NTSTATUS
NTAPI
GemGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    if (!Adapter->LinkUp)
        return STATUS_IO_TIMEOUT;

    if (Adapter->TxSubmitted[Index])
    {
        KeMemoryBarrier();
        if (!(Adapter->TxRing[Index].Control & GEM_TX_USED))
            return STATUS_IO_TIMEOUT;

        Adapter->TxSubmitted[Index] = FALSE;
    }

    Adapter->TxNext = (Index + 1) % GEM_TX_COUNT;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
GemSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PGEM_ADAPTER Adapter = Context;
    volatile GEM_DESCRIPTOR *Descriptor;
    ULONG Index = Handle & ~HANDLE_FLAGS, i;
    PUCHAR Buffer;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= GEM_TX_COUNT || Length > GEM_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Buffer = Adapter->TxBuffers + Index * GEM_BUFFER_SIZE;
    for (; Length < 60; Length++)
        Buffer[Length] = 0;

    Descriptor = &Adapter->TxRing[Index];
    Adapter->TxSubmitted[Index] = TRUE;
    KeMemoryBarrier();
    Descriptor->Control = Length | GEM_TX_LAST | (Index == GEM_TX_COUNT - 1 ? GEM_TX_WRAP : 0);
    KeMemoryBarrier();
    GemWrite(Adapter, GEM_NCR, GemRead(Adapter, GEM_NCR) | GEM_NCR_TSTART);
    GemRead(Adapter, GEM_NCR);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < GEM_TX_WAIT; i++)
    {
        KeMemoryBarrier();
        if (Descriptor->Control & GEM_TX_USED)
        {
            GemWrite(Adapter, GEM_TSR, GEM_TSR_ALL);
            return STATUS_SUCCESS;
        }

        GemStall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
GemGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < GEM_TX_COUNT ? Adapter->TxBuffers + Index * GEM_BUFFER_SIZE : NULL;

    return Index < GEM_RX_COUNT ? Adapter->RxBuffers + Index * GEM_BUFFER_SIZE : NULL;
}

ULONG
NTAPI
GemGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PGEM_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return GEM_MAX_FRAME;

    return Index < GEM_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
