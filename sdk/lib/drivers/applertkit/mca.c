/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon multi-channel audio (MCA) serial ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applemca.h>

#define MCA_CLUSTER_STRIDE          0x4000
#define MCA_SWITCH_STRIDE           0x8000
#define MCA_MAX_CLUSTERS            6

#define MCA_STATUS                  0x000
#define MCA_STATUS_MCLK_ENABLE      0x00000001
#define MCA_MCLK_CONFIG             0x004
#define MCA_MCLK_DIVIDER_ONE        0x00000100
#define MCA_SYNC_STATUS             0x100
#define MCA_SYNC_ENABLE             0x00000001
#define MCA_SYNC_MCLK_SELECT        0x104
#define MCA_SYNC_PORT_CLOCK_BASE    7
#define MCA_SYNC_HIGH_PERIOD        0x108
#define MCA_SYNC_LOW_PERIOD         0x10c
#define MCA_PORT_ENABLES            0x600
#define MCA_PORT_CLOCKS             0x00000006
#define MCA_PORT_TX_DATA            0x00000008
#define MCA_PORT_CLOCK_SELECT       0x604
#define MCA_PORT_CLOCK_SHIFT        8
#define MCA_PORT_DATA_SELECT        0x608

#define MCA_TX_SERDES               0x300
#define MCA_SERDES_STATUS           0x00
#define MCA_SERDES_ENABLE           0x00000001
#define MCA_SERDES_RESET            0x00000002
#define MCA_TX_CONFIG               0x04
#define MCA_TX_BIT_START            0x08
#define MCA_TX_SLOT_MASK            0x0c
#define MCA_CONFIG_CHANNELS         0x0000000f
#define MCA_CONFIG_WIDTH            0x000001f0
#define MCA_CONFIG_WIDTH_16         0x00000040
#define MCA_CONFIG_WIDTH_20         0x00000080
#define MCA_CONFIG_WIDTH_24         0x000000c0
#define MCA_CONFIG_WIDTH_32         0x00000100
#define MCA_CONFIG_BCLK_POLARITY    0x00000400
#define MCA_CONFIG_TX_UNKNOWN       0x00007000
#define MCA_CONFIG_SYNC_SELECT      0x00070000
#define MCA_CONFIG_SYNC_SHIFT       16
#define MCA_CONFIG_SYNC_RESET       7

#define MCA_RX_SERDES               0x400
#define MCA_RX_PORT                 0x04
#define MCA_RX_CONFIG               0x08
#define MCA_RX_BIT_START            0x0c
#define MCA_RX_SLOT_MASK            0x10
#define MCA_CONFIG_RX_UNKNOWN       0x00003000
#define MCA_CONFIG_NO_DATA_FEEDBACK 0x00008000

#define MCA_ADAPTER_B               0x4000
#define MCA_ADAPTER_TX_LSB_PAD_SHIFT 0
#define MCA_ADAPTER_TX_CHANNELS_SHIFT 5
#define MCA_ADAPTER_RX_MSB_PAD_SHIFT 8
#define MCA_ADAPTER_RX_CHANNELS_SHIFT 13
#define MCA_ADAPTER_CHANNELS_SHIFT  20

#define MCA_SERDES_SETTLE_US        50
#define MCA_CLOCK_SETTLE_US         100

static
volatile ULONG *
McaRegister(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Offset)
{
    return (volatile ULONG *)(Mca->Clusters + Cluster * MCA_CLUSTER_STRIDE + Offset);
}

static
VOID
McaModify(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    volatile ULONG *Register = McaRegister(Mca, Cluster, Offset);

    WRITE_REGISTER_ULONG(Register, (READ_REGISTER_ULONG(Register) & ~Mask) | (Value & Mask));
}

static
ULONG
McaPopulation(
    _In_ ULONG Value)
{
    ULONG Count = 0;

    for (; Value; Value &= Value - 1)
        ++Count;
    return Count;
}

static
ULONG
McaCropMask(
    _In_ ULONG Mask,
    _In_ ULONG Channels)
{
    ULONG Bit;

    for (Bit = 31; McaPopulation(Mask) > Channels; --Bit)
        Mask &= ~(1UL << Bit);
    return Mask;
}

NTSTATUS
NTAPI
AppleMcaOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_MCA Mca)
{
    const VOID *Compatible;
    ULONG Length;

    RtlZeroMemory(Mca, sizeof(*Mca));
    Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8103-mca") &&
        !FdtStringListContains(Compatible, Length, "apple,mca"))
        return STATUS_NOT_SUPPORTED;
    Mca->Clusters = FdtDriverMapReg(Tree, Node, 0, NULL, &Mca->ClustersSize);
    Mca->Switch = FdtDriverMapReg(Tree, Node, 1, NULL, &Mca->SwitchSize);
    if (!Mca->Clusters || !Mca->Switch || Mca->ClustersSize < MCA_CLUSTER_STRIDE)
    {
        AppleMcaClose(Mca);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Mca->ClusterCount = min(Mca->ClustersSize / MCA_CLUSTER_STRIDE, MCA_MAX_CLUSTERS);
    if (Mca->SwitchSize < (Mca->ClusterCount - 1) * MCA_SWITCH_STRIDE + sizeof(ULONG))
    {
        AppleMcaClose(Mca);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleMcaClose(
    _Inout_ PAPPLE_MCA Mca)
{
    if (Mca->Clusters)
        MmUnmapIoSpace(Mca->Clusters, Mca->ClustersSize);
    if (Mca->Switch)
        MmUnmapIoSpace(Mca->Switch, Mca->SwitchSize);
    RtlZeroMemory(Mca, sizeof(*Mca));
}

NTSTATUS
NTAPI
AppleMcaConfigureTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Slots,
    _In_ ULONG SlotWidth,
    _In_ ULONG Channels,
    _In_ ULONG SampleBits)
{
    ULONG Config, Mask, Ratio, Pad;

    if (!Mca->Clusters || Cluster >= Mca->ClusterCount || !Slots || Slots > 16 || !Channels ||
        Channels > Slots || SampleBits > SlotWidth || SampleBits > 32)
        return STATUS_INVALID_PARAMETER;
    switch (SlotWidth)
    {
        case 16:
            Config = MCA_CONFIG_WIDTH_16;
            break;
        case 20:
            Config = MCA_CONFIG_WIDTH_20;
            break;
        case 24:
            Config = MCA_CONFIG_WIDTH_24;
            break;
        case 32:
            Config = MCA_CONFIG_WIDTH_32;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }
    Config |= (Slots - 1) | MCA_CONFIG_TX_UNKNOWN | ((Cluster + 1) << MCA_CONFIG_SYNC_SHIFT);
    McaModify(Mca, Cluster, MCA_TX_SERDES + MCA_TX_CONFIG,
              MCA_CONFIG_CHANNELS | MCA_CONFIG_WIDTH | MCA_CONFIG_BCLK_POLARITY | MCA_CONFIG_TX_UNKNOWN |
              MCA_CONFIG_SYNC_SELECT,
              Config);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_TX_SERDES + MCA_TX_BIT_START), 1);

    Mask = (1UL << Slots) - 1;
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_TX_SERDES + MCA_TX_SLOT_MASK), MAXULONG);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_TX_SERDES + MCA_TX_SLOT_MASK + 0x4), ~McaCropMask(Mask, Channels));
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_TX_SERDES + MCA_TX_SLOT_MASK + 0x8), MAXULONG);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_TX_SERDES + MCA_TX_SLOT_MASK + 0xc), ~Mask);

    Pad = 32 - SampleBits;
    WRITE_REGISTER_ULONG((volatile ULONG *)(Mca->Switch + Cluster * MCA_SWITCH_STRIDE),
                         (min(Channels, 4) << MCA_ADAPTER_CHANNELS_SHIFT) |
                         (2 << MCA_ADAPTER_TX_CHANNELS_SHIFT) | (2 << MCA_ADAPTER_RX_CHANNELS_SHIFT) |
                         (Pad << MCA_ADAPTER_TX_LSB_PAD_SHIFT) | (Pad << MCA_ADAPTER_RX_MSB_PAD_SHIFT));

    Ratio = Slots * SlotWidth;
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_HIGH_PERIOD), Ratio / 2 - 1);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_LOW_PERIOD), (Ratio + 1) / 2 - 1);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_MCLK_CONFIG), MCA_MCLK_DIVIDER_ONE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleMcaConfigureRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Slots,
    _In_ ULONG SlotWidth,
    _In_ ULONG Channels,
    _In_ ULONG SampleBits,
    _In_ ULONG PortMask)
{
    ULONG Config, Mask, Ratio, Pad;

    if (!Mca->Clusters || Cluster >= Mca->ClusterCount || !Slots || Slots > 16 || !Channels ||
        Channels > Slots || SampleBits > SlotWidth || SampleBits > 32 || !PortMask)
        return STATUS_INVALID_PARAMETER;
    switch (SlotWidth)
    {
        case 16:
            Config = MCA_CONFIG_WIDTH_16;
            break;
        case 20:
            Config = MCA_CONFIG_WIDTH_20;
            break;
        case 24:
            Config = MCA_CONFIG_WIDTH_24;
            break;
        case 32:
            Config = MCA_CONFIG_WIDTH_32;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }
    Config |= (Slots - 1) | MCA_CONFIG_RX_UNKNOWN | MCA_CONFIG_NO_DATA_FEEDBACK |
              ((Cluster + 1) << MCA_CONFIG_SYNC_SHIFT);
    McaModify(Mca, Cluster, MCA_RX_SERDES + MCA_RX_CONFIG,
              MCA_CONFIG_CHANNELS | MCA_CONFIG_WIDTH | MCA_CONFIG_BCLK_POLARITY | MCA_CONFIG_TX_UNKNOWN |
              MCA_CONFIG_NO_DATA_FEEDBACK | MCA_CONFIG_SYNC_SELECT,
              Config);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_RX_SERDES + MCA_RX_BIT_START), 1);

    Mask = (1UL << Slots) - 1;
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_RX_SERDES + MCA_RX_SLOT_MASK), MAXULONG);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_RX_SERDES + MCA_RX_SLOT_MASK + 0x4), ~McaCropMask(Mask, Channels));
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_RX_SERDES + MCA_RX_PORT), PortMask);

    Pad = 32 - SampleBits;
    WRITE_REGISTER_ULONG((volatile ULONG *)(Mca->Switch + Cluster * MCA_SWITCH_STRIDE + MCA_ADAPTER_B),
                         (min(Channels, 4) << MCA_ADAPTER_CHANNELS_SHIFT) |
                         (2 << MCA_ADAPTER_TX_CHANNELS_SHIFT) | (2 << MCA_ADAPTER_RX_CHANNELS_SHIFT) |
                         (Pad << MCA_ADAPTER_TX_LSB_PAD_SHIFT) | (Pad << MCA_ADAPTER_RX_MSB_PAD_SHIFT));

    Ratio = Slots * SlotWidth;
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_HIGH_PERIOD), Ratio / 2 - 1);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_LOW_PERIOD), (Ratio + 1) / 2 - 1);
    WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_MCLK_CONFIG), MCA_MCLK_DIVIDER_ONE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleMcaRoutePort(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Port,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable)
{
    if (!Mca->Clusters || Port >= Mca->ClusterCount || Cluster >= Mca->ClusterCount)
        return STATUS_INVALID_PARAMETER;
    if (Enable)
    {
        WRITE_REGISTER_ULONG(McaRegister(Mca, Port, MCA_PORT_DATA_SELECT), 1UL << (Cluster * 2));
        WRITE_REGISTER_ULONG(McaRegister(Mca, Port, MCA_PORT_CLOCK_SELECT), (Cluster + 1) << MCA_PORT_CLOCK_SHIFT);
        McaModify(Mca, Port, MCA_PORT_ENABLES, MCA_PORT_CLOCKS | MCA_PORT_TX_DATA, MCA_PORT_CLOCKS | MCA_PORT_TX_DATA);
    }
    else
    {
        McaModify(Mca, Port, MCA_PORT_ENABLES, MCA_PORT_CLOCKS | MCA_PORT_TX_DATA, 0);
        WRITE_REGISTER_ULONG(McaRegister(Mca, Port, MCA_PORT_DATA_SELECT), 0);
        WRITE_REGISTER_ULONG(McaRegister(Mca, Port, MCA_PORT_CLOCK_SELECT), 0);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleMcaEnableClocks(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable)
{
    if (!Mca->Clusters || Cluster >= Mca->ClusterCount)
        return;
    if (Enable)
    {
        WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_MCLK_SELECT), Cluster + 1);
        McaModify(Mca, Cluster, MCA_SYNC_STATUS, MCA_SYNC_ENABLE, MCA_SYNC_ENABLE);
        McaModify(Mca, Cluster, MCA_STATUS, MCA_STATUS_MCLK_ENABLE, MCA_STATUS_MCLK_ENABLE);
    }
    else
    {
        McaModify(Mca, Cluster, MCA_SYNC_STATUS, MCA_SYNC_ENABLE, 0);
        McaModify(Mca, Cluster, MCA_STATUS, MCA_STATUS_MCLK_ENABLE, 0);
    }
}

VOID
NTAPI
AppleMcaFollowPortClocks(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Port,
    _In_ BOOLEAN Enable)
{
    ASSERT(Cluster < Mca->ClusterCount);
    ASSERT(Port < Mca->ClusterCount);
    if (!Mca->Clusters || Cluster >= Mca->ClusterCount || Port >= Mca->ClusterCount)
        return;
    if (Enable)
    {
        WRITE_REGISTER_ULONG(McaRegister(Mca, Cluster, MCA_SYNC_MCLK_SELECT), Port + MCA_SYNC_PORT_CLOCK_BASE);
        McaModify(Mca, Cluster, MCA_SYNC_STATUS, MCA_SYNC_ENABLE, MCA_SYNC_ENABLE);
    }
    else
    {
        McaModify(Mca, Cluster, MCA_SYNC_STATUS, MCA_SYNC_ENABLE, 0);
    }
}

static
NTSTATUS
McaResetSerdes(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Unit,
    _In_ ULONG Config)
{
    if (!Mca->Clusters || Cluster >= Mca->ClusterCount)
        return STATUS_INVALID_PARAMETER;
    McaModify(Mca, Cluster, Config, MCA_CONFIG_SYNC_SELECT, 0);
    McaModify(Mca, Cluster, Config, MCA_CONFIG_SYNC_SELECT, MCA_CONFIG_SYNC_RESET << MCA_CONFIG_SYNC_SHIFT);
    McaModify(Mca, Cluster, Unit + MCA_SERDES_STATUS, MCA_SERDES_ENABLE | MCA_SERDES_RESET, MCA_SERDES_RESET);
    KeStallExecutionProcessor(MCA_SERDES_SETTLE_US);
    if (READ_REGISTER_ULONG(McaRegister(Mca, Cluster, Unit + MCA_SERDES_STATUS)) & MCA_SERDES_RESET)
        return STATUS_DEVICE_NOT_READY;
    McaModify(Mca, Cluster, Config, MCA_CONFIG_SYNC_SELECT, 0);
    McaModify(Mca, Cluster, Config, MCA_CONFIG_SYNC_SELECT, (Cluster + 1) << MCA_CONFIG_SYNC_SHIFT);
    KeStallExecutionProcessor(MCA_CLOCK_SETTLE_US);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleMcaResetTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster)
{
    return McaResetSerdes(Mca, Cluster, MCA_TX_SERDES, MCA_TX_SERDES + MCA_TX_CONFIG);
}

NTSTATUS
NTAPI
AppleMcaResetRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster)
{
    return McaResetSerdes(Mca, Cluster, MCA_RX_SERDES, MCA_RX_SERDES + MCA_RX_CONFIG);
}

VOID
NTAPI
AppleMcaEnableTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable)
{
    if (!Mca->Clusters || Cluster >= Mca->ClusterCount)
        return;
    McaModify(Mca, Cluster, MCA_TX_SERDES + MCA_SERDES_STATUS, MCA_SERDES_ENABLE | MCA_SERDES_RESET,
              Enable ? MCA_SERDES_ENABLE : 0);
}

VOID
NTAPI
AppleMcaEnableRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable)
{
    if (!Mca->Clusters || Cluster >= Mca->ClusterCount)
        return;
    McaModify(Mca, Cluster, MCA_RX_SERDES + MCA_SERDES_STATUS, MCA_SERDES_ENABLE | MCA_SERDES_RESET,
              Enable ? MCA_SERDES_ENABLE : 0);
}
