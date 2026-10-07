/*
 * PROJECT:     LiberNT SpacemiT K1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled driver for the SpacemiT K1 EMAC
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdk1x.h"
#include <reactos/riscv64/fdtlib.h>

#define K1X_RING_PAGES          2
#define K1X_MDIO_WAIT           1000
#define K1X_TX_WAIT             10000
#define K1X_LINK_WAIT           400

static const struct
{
    ULONG64 RegisterBase;
    ULONG ControlRegister;
    ULONG DelayLineRegister;
    ULONG FirstDataPin;
    ULONG ReferenceClockPin;
} K1xPorts[] =
{
    { 0xCAC80000ULL, 0x3E4, 0x3E8, 0, 45 },
    { 0xCAC81000ULL, 0x3EC, 0x3F0, 29, 46 },
};

static const UCHAR K1xFallbackAddress[MAC_ADDRESS_SIZE] = { 0x02, 0x4B, 0x31, 0x00, 0x00, 0x00 };

static
ULONG
K1xRead(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)(Adapter->Registers + Register));
}

static
VOID
K1xWrite(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)(Adapter->Registers + Register), Value);
}

static
VOID
K1xStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG
K1xPhysical(
    _In_ PVOID Address)
{
    return KdNetExtensibilityImports->GetPhysicalAddress(Address).LowPart;
}

static
VOID
K1xCacheClean(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Base & ~(ULONG_PTR)(K1X_CACHE_LINE - 1);
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    KeMemoryBarrier();
    for (; Address < End; Address += K1X_CACHE_LINE)
        __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 1" :: "r"(Address) : "memory");

    KeMemoryBarrier();
}

static
VOID
K1xCacheFlush(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Base & ~(ULONG_PTR)(K1X_CACHE_LINE - 1);
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    KeMemoryBarrier();
    for (; Address < End; Address += K1X_CACHE_LINE)
        __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 2" :: "r"(Address) : "memory");

    KeMemoryBarrier();
}

static
ULONG
K1xAlignedAdapterSize(VOID)
{
    return (sizeof(K1X_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

ULONG
NTAPI
K1xGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return K1xAlignedAdapterSize() + K1X_RING_PAGES * PAGE_SIZE +
           (K1X_RX_COUNT + K1X_TX_COUNT) * K1X_BUFFER_SIZE;
}

static
BOOLEAN
K1xWaitManagement(
    _In_ PK1X_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < K1X_MDIO_WAIT; i++)
    {
        if (!(K1xRead(Adapter, EMAC_MAC_MDIO_CONTROL) & EMAC_MDIO_START))
            return TRUE;

        K1xStall(10);
    }

    return FALSE;
}

static
BOOLEAN
K1xPhyRead(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Phy,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    *Value = 0xFFFF;
    if (!K1xWaitManagement(Adapter))
        return FALSE;

    K1xWrite(Adapter, EMAC_MAC_MDIO_DATA, 0);
    K1xWrite(Adapter, EMAC_MAC_MDIO_CONTROL,
             Phy | (Register << EMAC_MDIO_REGISTER_SHIFT) | EMAC_MDIO_READ | EMAC_MDIO_START);
    if (!K1xWaitManagement(Adapter))
        return FALSE;

    *Value = (USHORT)K1xRead(Adapter, EMAC_MAC_MDIO_DATA);
    return TRUE;
}

static
BOOLEAN
K1xPhyWrite(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Phy,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    if (!K1xWaitManagement(Adapter))
        return FALSE;

    K1xWrite(Adapter, EMAC_MAC_MDIO_DATA, Value);
    K1xWrite(Adapter, EMAC_MAC_MDIO_CONTROL,
             Phy | (Register << EMAC_MDIO_REGISTER_SHIFT) | EMAC_MDIO_START);
    return K1xWaitManagement(Adapter);
}

static
BOOLEAN
K1xPhyIdentify(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Phy,
    _Out_ PULONG Id)
{
    USHORT High, Low;

    if (!K1xPhyRead(Adapter, Phy, MII_PHYSID1, &High) || !K1xPhyRead(Adapter, Phy, MII_PHYSID2, &Low) ||
        (High == 0xFFFF && Low == 0xFFFF) || (!High && !Low))
    {
        return FALSE;
    }

    *Id = ((ULONG)High << 16) | Low;
    return TRUE;
}

static
VOID
K1xSetPhyDelay(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Bit,
    _In_ BOOLEAN Enable)
{
    USHORT Value, Wanted;

    if (!K1xPhyRead(Adapter, Adapter->PhyAddress, Register, &Value))
        return;

    Wanted = Enable ? (Value | Bit) : (Value & ~Bit);
    if (Wanted != Value)
        K1xPhyWrite(Adapter, Adapter->PhyAddress, Register, Wanted);
}

static
VOID
K1xStartPhy(
    _In_ PK1X_ADAPTER Adapter)
{
    ULONG Phy = Adapter->PhyAddress;
    USHORT Value;
    BOOLEAN Restart = FALSE;

    Adapter->PhyId = 0;
    if (!K1xPhyIdentify(Adapter, Phy, &Adapter->PhyId))
    {
        for (Phy = 0; Phy < K1X_PHY_COUNT; Phy++)
        {
            if (K1xPhyIdentify(Adapter, Phy, &Adapter->PhyId))
            {
                Adapter->PhyAddress = (UCHAR)Phy;
                break;
            }
        }
    }

    if (!Adapter->PhyId)
        return;

    Phy = Adapter->PhyAddress;
    if (Adapter->PhyId == PHY_ID_RTL8211F && Adapter->Rgmii)
    {
        K1xPhyWrite(Adapter, Phy, RTL8211F_PAGE_SELECT, RTL8211F_PAGE_RGMII);
        K1xSetPhyDelay(Adapter, RTL8211F_TX_DELAY_REGISTER, RTL8211F_TX_DELAY, Adapter->PhyTxDelay);
        K1xSetPhyDelay(Adapter, RTL8211F_RX_DELAY_REGISTER, RTL8211F_RX_DELAY, Adapter->PhyRxDelay);
        K1xPhyWrite(Adapter, Phy, RTL8211F_PAGE_SELECT, 0);
    }

    if (K1xPhyRead(Adapter, Phy, MII_ADVERTISE, &Value) && (Value & ADVERTISE_ALL) != ADVERTISE_ALL)
    {
        K1xPhyWrite(Adapter, Phy, MII_ADVERTISE, Value | ADVERTISE_ALL);
        Restart = TRUE;
    }

    if (Adapter->Rgmii && K1xPhyRead(Adapter, Phy, MII_CTRL1000, &Value) && !(Value & ADVERTISE_1000FULL))
    {
        K1xPhyWrite(Adapter, Phy, MII_CTRL1000, Value | ADVERTISE_1000FULL);
        Restart = TRUE;
    }

    if (K1xPhyRead(Adapter, Phy, MII_BMCR, &Value) && (Restart || !(Value & BMCR_ANENABLE)))
        K1xPhyWrite(Adapter, Phy, MII_BMCR, Value | BMCR_ANENABLE | BMCR_ANRESTART);
}

static
VOID
K1xUpdateLink(
    _In_ PK1X_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;
    USHORT Control, Status, Partner = 0, Gigabit = 0;
    ULONG Phy = Adapter->PhyAddress, Speed = 10, Duplex = 0, Value;
    BOOLEAN Up;

    Up = Adapter->PhyId && K1xPhyRead(Adapter, Phy, MII_BMCR, &Control) &&
         !(Control & BMCR_ANRESTART) && K1xPhyRead(Adapter, Phy, MII_BMSR, &Status) &&
         K1xPhyRead(Adapter, Phy, MII_BMSR, &Status) && (Status & BMSR_LSTATUS);
    if (Up)
    {
        K1xPhyRead(Adapter, Phy, MII_LPA, &Partner);
        if (Adapter->Rgmii)
            K1xPhyRead(Adapter, Phy, MII_STAT1000, &Gigabit);

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

        if (!Adapter->LinkUp || Speed != Adapter->LinkSpeed || Duplex != Adapter->LinkDuplex)
        {
            Value = K1xRead(Adapter, EMAC_MAC_GLOBAL_CONTROL) &
                    ~(EMAC_MAC_GLOBAL_SPEED_MASK | EMAC_MAC_GLOBAL_FULL_DUPLEX);
            if (Speed == 1000)
                Value |= EMAC_MAC_GLOBAL_SPEED_1000;
            else if (Speed == 100)
                Value |= EMAC_MAC_GLOBAL_SPEED_100;

            if (Duplex)
                Value |= EMAC_MAC_GLOBAL_FULL_DUPLEX;

            K1xWrite(Adapter, EMAC_MAC_GLOBAL_CONTROL, Value);
        }

        Adapter->LinkSpeed = Speed;
        Adapter->LinkDuplex = Duplex;
        KdNet->LinkSpeed = Speed;
        KdNet->LinkDuplex = Duplex;
    }

    Adapter->LinkUp = Up;
    *KdNet->LinkState = Up;
}

static
VOID
K1xArmReceive(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PK1X_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];
    PUCHAR Buffer = Adapter->RxBuffers + Index * K1X_BUFFER_SIZE;

    K1xCacheFlush(Buffer, K1X_BUFFER_SIZE);
    Descriptor->Buffer2 = 0;
    Descriptor->Buffer1 = K1xPhysical(Buffer);
    Descriptor->Control = K1X_BUFFER_SIZE | (Index == K1X_RX_COUNT - 1 ? EMAC_DESC_END_OF_RING : 0);
    KeMemoryBarrier();
    Descriptor->Status = EMAC_DESC_OWN;
    K1xCacheClean(Descriptor, sizeof(*Descriptor));
}

static
BOOLEAN
K1xReadCell(
    _In_ const RISCV_FDT *Tree,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_ PULONG Value)
{
    const UCHAR *Cell;
    ULONG Length;

    Cell = RiscvFdtGetProperty(Tree, Node, Name, &Length);
    if (!Cell || Length != sizeof(ULONG))
        return FALSE;

    *Value = ((ULONG)Cell[0] << 24) | ((ULONG)Cell[1] << 16) | ((ULONG)Cell[2] << 8) | Cell[3];
    return TRUE;
}

static
BOOLEAN
K1xHasProperty(
    _In_ const RISCV_FDT *Tree,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name)
{
    ULONG Length;

    return RiscvFdtGetProperty(Tree, Node, Name, &Length) != NULL;
}

static
BOOLEAN
K1xReadAddress(
    _In_ const RISCV_FDT *Tree,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    const UCHAR *Property;
    ULONG Length, i;
    BOOLEAN Valid = FALSE;

    Property = RiscvFdtGetProperty(Tree, Node, Name, &Length);
    if (!Property || Length != MAC_ADDRESS_SIZE || (Property[0] & 1))
        return FALSE;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        Address[i] = Property[i];
        if (Property[i])
            Valid = TRUE;
    }

    return Valid;
}

static
NTSTATUS
K1xReadConfiguration(
    _In_ PK1X_ADAPTER Adapter,
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    PKD_FDT_DEVICE Description = Device->OemData;
    ULONGLONG Base, Size;
    const CHAR *Mode;
    RISCV_FDT Tree;
    ULONG Node, Port, Length, Value, i;

    Node = Description->Node;
    if (!RiscvFdtOpen(Description->Blob, Description->Size, &Tree) ||
        !RiscvFdtReadReg(&Tree, Node, Description->Parent, 0, &Base, &Size))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (Port = 0; Port < RTL_NUMBER_OF(K1xPorts); Port++)
    {
        if (K1xPorts[Port].RegisterBase == Base)
            break;
    }

    if (Port == RTL_NUMBER_OF(K1xPorts))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Adapter->Port = Port;
    Adapter->ControlRegister = K1xPorts[Port].ControlRegister;
    Adapter->DelayLineRegister = K1xPorts[Port].DelayLineRegister;
    Adapter->Rgmii = TRUE;
    Adapter->PhyAddress = 1;
    K1xReadCell(&Tree, Node, "ctrl-reg", &Adapter->ControlRegister);
    K1xReadCell(&Tree, Node, "dline-reg", &Adapter->DelayLineRegister);
    if (Adapter->ControlRegister > K1X_APMU_SPAN - sizeof(ULONG) ||
        Adapter->DelayLineRegister > K1X_APMU_SPAN - sizeof(ULONG))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (K1xReadCell(&Tree, Node, "phy-addr", &Value) && Value < K1X_PHY_COUNT)
        Adapter->PhyAddress = (UCHAR)Value;

    Mode = RiscvFdtGetProperty(&Tree, Node, "phy-mode", &Length);
    if (Mode && Length >= 2)
    {
        Adapter->Rgmii = Mode[0] == 'r' && Mode[1] == 'g';
        Adapter->PhyTxDelay = RiscvFdtStringListContains(Mode, Length, "rgmii-id") ||
                              RiscvFdtStringListContains(Mode, Length, "rgmii-txid");
        Adapter->PhyRxDelay = RiscvFdtStringListContains(Mode, Length, "rgmii-id") ||
                              RiscvFdtStringListContains(Mode, Length, "rgmii-rxid");
    }

    Adapter->ReferenceClockFromPhy = K1xHasProperty(&Tree, Node, "ref-clock-from-phy");
    Adapter->DelayLineTuning =
        (K1xHasProperty(&Tree, Node, "clk_tuning_enable") || K1xHasProperty(&Tree, Node, "clk-tuning-enable")) &&
        K1xHasProperty(&Tree, Node, "clk-tuning-by-delayline") &&
        K1xReadCell(&Tree, Node, "tx-phase", &Adapter->TxPhase) &&
        K1xReadCell(&Tree, Node, "rx-phase", &Adapter->RxPhase) &&
        Adapter->TxPhase <= 0xFF && Adapter->RxPhase <= 0xFF;

    if (!K1xReadAddress(&Tree, Node, "local-mac-address", Address) &&
        !K1xReadAddress(&Tree, Node, "mac-address", Address))
    {
        for (i = 0; i < MAC_ADDRESS_SIZE; i++)
            Address[i] = K1xFallbackAddress[i];

        Address[MAC_ADDRESS_SIZE - 1] = (UCHAR)Port;
    }

    return STATUS_SUCCESS;
}

static
VOID
K1xSelectPin(
    _In_ PUCHAR Pins,
    _In_ ULONG Pin)
{
    PULONG Register = (PULONG)(Pins + (Pin + 1) * sizeof(ULONG));

    if ((KdNetExtensibilityImports->ReadRegisterULong(Register) & K1X_MFPR_FUNCTION_MASK) !=
        (K1X_MFPR_GMAC & K1X_MFPR_FUNCTION_MASK))
    {
        KdNetExtensibilityImports->WriteRegisterULong(Register, K1X_MFPR_GMAC);
    }
}

static
NTSTATUS
K1xPowerOn(
    _In_ PK1X_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS Physical;
    PULONG Control, DelayLine;
    PUCHAR Pins, Clocks;
    ULONG Value, Pin;

    Physical.QuadPart = K1X_MFPR_BASE & ~(ULONG64)(PAGE_SIZE - 1);
    Pins = KdNetExtensibilityImports->MapPhysicalMemory64(Physical, 1, FALSE);
    if (!Pins)
        return STATUS_INSUFFICIENT_RESOURCES;

    Pins += K1X_MFPR_BASE & (PAGE_SIZE - 1);
    for (Pin = 0; Pin < K1X_GMAC_DATA_PINS; Pin++)
        K1xSelectPin(Pins, K1xPorts[Adapter->Port].FirstDataPin + Pin);

    K1xSelectPin(Pins, K1xPorts[Adapter->Port].ReferenceClockPin);
    KdNetExtensibilityImports->UnmapVirtualAddress(Pins - (K1X_MFPR_BASE & (PAGE_SIZE - 1)), 1, FALSE);

    Physical.QuadPart = K1X_APMU_BASE & ~(ULONG64)(PAGE_SIZE - 1);
    Clocks = KdNetExtensibilityImports->MapPhysicalMemory64(Physical, 1, FALSE);
    if (!Clocks)
        return STATUS_INSUFFICIENT_RESOURCES;

    Clocks += K1X_APMU_BASE & (PAGE_SIZE - 1);
    Control = (PULONG)(Clocks + Adapter->ControlRegister);
    DelayLine = (PULONG)(Clocks + Adapter->DelayLineRegister);

    Value = KdNetExtensibilityImports->ReadRegisterULong(Control) | APMU_EMAC_BUS_CLOCK_ENABLE;
    KdNetExtensibilityImports->WriteRegisterULong(Control, Value);
    K1xStall(100);
    Value |= APMU_EMAC_RESET_RELEASE | APMU_EMAC_AXI_SINGLE_ID;
    if (Adapter->Rgmii)
    {
        Value |= APMU_EMAC_RGMII;
        if (Adapter->ReferenceClockFromPhy)
            Value &= ~APMU_EMAC_RGMII_TX_CLOCK_SOC;
        else
            Value |= APMU_EMAC_RGMII_TX_CLOCK_SOC;
    }
    else
    {
        Value &= ~APMU_EMAC_RGMII;
        if (Adapter->ReferenceClockFromPhy)
            Value &= ~APMU_EMAC_RMII_REFERENCE_SOC;
        else
            Value |= APMU_EMAC_RMII_REFERENCE_SOC;
    }

    KdNetExtensibilityImports->WriteRegisterULong(Control, Value);
    K1xStall(1000);

    if (Adapter->Rgmii && Adapter->DelayLineTuning)
    {
        Value = KdNetExtensibilityImports->ReadRegisterULong(DelayLine) & ~APMU_DLINE_CODE_MASK;
        Value |= (Adapter->TxPhase << APMU_DLINE_TX_CODE_SHIFT) | APMU_DLINE_TX_ENABLE |
                 (Adapter->RxPhase << APMU_DLINE_RX_CODE_SHIFT) | APMU_DLINE_RX_ENABLE;
        KdNetExtensibilityImports->WriteRegisterULong(DelayLine, Value);
    }

    KdNetExtensibilityImports->UnmapVirtualAddress(Clocks - (K1X_APMU_BASE & (PAGE_SIZE - 1)), 1, FALSE);
    return STATUS_SUCCESS;
}

static
VOID
K1xStopHardware(
    _In_ PK1X_ADAPTER Adapter)
{
    K1xWrite(Adapter, EMAC_MAC_INTERRUPT_ENABLE, 0);
    K1xWrite(Adapter, EMAC_DMA_INTERRUPT_ENABLE, 0);
    K1xWrite(Adapter, EMAC_MAC_RX_CONTROL, 0);
    K1xWrite(Adapter, EMAC_MAC_TX_CONTROL, 0);
    K1xWrite(Adapter, EMAC_DMA_CONTROL, 0);
    K1xWrite(Adapter, EMAC_MAC_GLOBAL_CONTROL, EMAC_MAC_GLOBAL_RESET_COUNTERS);
    K1xWrite(Adapter, EMAC_MAC_GLOBAL_CONTROL, 0);
    K1xWrite(Adapter, EMAC_DMA_STATUS, EMAC_DMA_INT_ALL);
}

NTSTATUS
NTAPI
K1xInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PK1X_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Hardware = KdNet->Hardware;
    NTSTATUS Status;
    ULONG Value, i;

    if (!Device->BaseAddress[0].Valid || Device->BaseAddress[0].Type != CmResourceTypeMemory ||
        Device->BaseAddress[0].Length < EMAC_REGISTER_SPACE || !Device->OemData ||
        Device->OemDataLength != sizeof(KD_FDT_DEVICE))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->KdNet = KdNet;
    Adapter->Registers = Device->BaseAddress[0].TranslatedAddress;
    Adapter->RxRing = (PVOID)(Hardware + K1xAlignedAdapterSize());
    Adapter->TxRing = (PVOID)(Hardware + K1xAlignedAdapterSize() + PAGE_SIZE);
    Adapter->RxBuffers = Hardware + K1xAlignedAdapterSize() + K1X_RING_PAGES * PAGE_SIZE;
    Adapter->TxBuffers = Adapter->RxBuffers + K1X_RX_COUNT * K1X_BUFFER_SIZE;

    Status = K1xReadConfiguration(Adapter, Device, KdNet->TargetMacAddress);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = K1xPowerOn(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    K1xStopHardware(Adapter);
    K1xWrite(Adapter, EMAC_MAC_TX_FIFO_ALMOST_FULL, EMAC_TX_FIFO_ALMOST_FULL_VALUE);
    K1xWrite(Adapter, EMAC_MAC_TX_START_THRESHOLD, EMAC_TX_STORE_FORWARD);
    K1xWrite(Adapter, EMAC_MAC_RX_START_THRESHOLD, EMAC_RX_START_VALUE);
    K1xWrite(Adapter, EMAC_MAC_FLOW_CONTROL, EMAC_MAC_FLOW_DECODE);
    K1xWrite(Adapter, EMAC_DMA_CONFIGURATION, EMAC_DMA_CONFIGURATION_RESET);
    K1xStall(10000);
    K1xWrite(Adapter, EMAC_DMA_CONFIGURATION, 0);
    K1xStall(10000);
    K1xWrite(Adapter, EMAC_DMA_CONFIGURATION,
             EMAC_DMA_CONFIGURATION_STRICT | EMAC_DMA_CONFIGURATION_64BIT |
             EMAC_DMA_CONFIGURATION_BURST_16 | EMAC_DMA_CONFIGURATION_SKIP_12);

    K1xWrite(Adapter, EMAC_MAC_ADDRESS1_HIGH, (KdNet->TargetMacAddress[1] << 8) | KdNet->TargetMacAddress[0]);
    K1xWrite(Adapter, EMAC_MAC_ADDRESS1_MED, (KdNet->TargetMacAddress[3] << 8) | KdNet->TargetMacAddress[2]);
    K1xWrite(Adapter, EMAC_MAC_ADDRESS1_LOW, (KdNet->TargetMacAddress[5] << 8) | KdNet->TargetMacAddress[4]);
    for (i = 0; i < 4; i++)
        K1xWrite(Adapter, EMAC_MAC_HASH_TABLE1 + i * sizeof(ULONG), 0);

    K1xWrite(Adapter, EMAC_MAC_ADDRESS_CONTROL, EMAC_MAC_ADDRESS1_ENABLE);

    K1xStartPhy(Adapter);
    K1xUpdateLink(Adapter);

    for (i = 0; i < K1X_RX_COUNT; i++)
        K1xArmReceive(Adapter, i);

    Adapter->TxRing[K1X_TX_COUNT - 1].Control = EMAC_DESC_END_OF_RING;
    K1xCacheFlush(Adapter->TxRing, K1X_TX_COUNT * sizeof(K1X_DESCRIPTOR));

    K1xWrite(Adapter, EMAC_DMA_TX_BASE, K1xPhysical(Adapter->TxRing));
    K1xWrite(Adapter, EMAC_DMA_RX_BASE, K1xPhysical(Adapter->RxRing));
    Value = K1xRead(Adapter, EMAC_MAC_TX_CONTROL) & ~EMAC_MAC_TX_IFG_MASK;
    K1xWrite(Adapter, EMAC_MAC_TX_CONTROL, Value | EMAC_MAC_TX_ENABLE | EMAC_MAC_TX_AUTO_RETRY);
    K1xWrite(Adapter, EMAC_DMA_TX_AUTO_POLL, 0);
    Value = K1xRead(Adapter, EMAC_MAC_RX_CONTROL);
    K1xWrite(Adapter, EMAC_MAC_RX_CONTROL, Value | EMAC_MAC_RX_ENABLE | EMAC_MAC_RX_STORE_FORWARD);
    K1xWrite(Adapter, EMAC_DMA_CONTROL, EMAC_DMA_CONTROL_START_TX | EMAC_DMA_CONTROL_START_RX);
    for (i = 0; i < K1X_LINK_WAIT && !Adapter->LinkUp; i++)
    {
        K1xStall(10000);
        K1xUpdateLink(Adapter);
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
K1xTransmitDone(
    _In_ PK1X_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PK1X_DESCRIPTOR Descriptor = &Adapter->TxRing[Index];

    K1xCacheFlush(Descriptor, sizeof(*Descriptor));
    return !(Descriptor->Status & EMAC_DESC_OWN);
}

VOID
NTAPI
K1xShutdownController(
    _In_ PVOID Context)
{
    PK1X_ADAPTER Adapter = Context;
    ULONG i, Index;

    for (i = 0; i < K1X_TX_WAIT; i++)
    {
        for (Index = 0; Index < K1X_TX_COUNT; Index++)
        {
            if (Adapter->TxSubmitted[Index] && !K1xTransmitDone(Adapter, Index))
                break;
        }

        if (Index == K1X_TX_COUNT)
            break;

        K1xStall(10);
    }

    K1xStopHardware(Adapter);
    Adapter->LinkUp = FALSE;
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
K1xGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PK1X_ADAPTER Adapter = Context;
    PK1X_DESCRIPTOR Descriptor;
    ULONG Index, Count, Status, Size;
    BOOLEAN Armed = FALSE;

    for (Count = 0; Count < K1X_RX_COUNT; Count++)
    {
        Index = Adapter->RxNext;
        Descriptor = &Adapter->RxRing[Index];
        K1xCacheFlush(Descriptor, sizeof(*Descriptor));
        Status = Descriptor->Status;
        if (Status & EMAC_DESC_OWN)
            break;

        Size = Status & EMAC_RX_STATUS_LENGTH;
        Adapter->RxNext = (Index + 1) % K1X_RX_COUNT;
        if ((Status & (EMAC_RX_STATUS_FIRST | EMAC_RX_STATUS_LAST)) ==
                (EMAC_RX_STATUS_FIRST | EMAC_RX_STATUS_LAST) &&
            !(Status & EMAC_RX_STATUS_ERRORS) &&
            Size >= K1X_MIN_FRAME + K1X_FCS_SIZE && Size <= K1X_MAX_FRAME + K1X_FCS_SIZE)
        {
            Size -= K1X_FCS_SIZE;
            K1xCacheFlush(Adapter->RxBuffers + Index * K1X_BUFFER_SIZE, Size);
            Adapter->RxLength[Index] = (USHORT)Size;
            *Handle = Index;
            *Packet = Adapter->RxBuffers + Index * K1X_BUFFER_SIZE;
            *Length = Size;
            if (Armed)
                K1xWrite(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xFF);

            return STATUS_SUCCESS;
        }

        K1xArmReceive(Adapter, Index);
        Armed = TRUE;
    }

    if (Armed)
        K1xWrite(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xFF);

    if ((Adapter->LinkPoll++ & 0xFF) == 0)
        K1xUpdateLink(Adapter);

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
K1xReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PK1X_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Index >= K1X_RX_COUNT)
        return;

    K1xArmReceive(Adapter, Index);
    K1xWrite(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xFF);
}

NTSTATUS
NTAPI
K1xGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PK1X_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    if (Adapter->TxSubmitted[Index])
    {
        if (!K1xTransmitDone(Adapter, Index))
            return STATUS_IO_TIMEOUT;

        Adapter->TxSubmitted[Index] = FALSE;
    }

    Adapter->TxNext = (Index + 1) % K1X_TX_COUNT;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
K1xSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PK1X_ADAPTER Adapter = Context;
    PK1X_DESCRIPTOR Descriptor;
    ULONG Index = Handle & ~HANDLE_FLAGS, i;
    PUCHAR Buffer;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= K1X_TX_COUNT || Length > K1X_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Buffer = Adapter->TxBuffers + Index * K1X_BUFFER_SIZE;
    for (; Length < 60; Length++)
        Buffer[Length] = 0;

    K1xCacheClean(Buffer, Length);
    Descriptor = &Adapter->TxRing[Index];
    Descriptor->Buffer2 = 0;
    Descriptor->Buffer1 = K1xPhysical(Buffer);
    Descriptor->Control = Length | EMAC_TX_CONTROL_FIRST | EMAC_TX_CONTROL_LAST |
                          (Index == K1X_TX_COUNT - 1 ? EMAC_DESC_END_OF_RING : 0);
    KeMemoryBarrier();
    Descriptor->Status = EMAC_DESC_OWN;
    K1xCacheClean(Descriptor, sizeof(*Descriptor));
    Adapter->TxSubmitted[Index] = TRUE;
    K1xWrite(Adapter, EMAC_DMA_TX_POLL_DEMAND, 0xFF);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < K1X_TX_WAIT; i++)
    {
        if (K1xTransmitDone(Adapter, Index))
            return STATUS_SUCCESS;

        K1xStall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
K1xGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PK1X_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return Index < K1X_TX_COUNT ? Adapter->TxBuffers + Index * K1X_BUFFER_SIZE : NULL;

    return Index < K1X_RX_COUNT ? Adapter->RxBuffers + Index * K1X_BUFFER_SIZE : NULL;
}

ULONG
NTAPI
K1xGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PK1X_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return K1X_MAX_FRAME;

    return Index < K1X_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
