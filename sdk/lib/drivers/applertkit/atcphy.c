/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB2 host bring-up of the Apple silicon Type-C PHY
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleatcphy.h>

#define AUSPLL_FSM_CTRL                     0x1014
#define AUSPLL_APB_CMD_OVERRIDE             0x2000
#define AUSPLL_APB_CMD_OVERRIDE_UNK28       (1UL << 28)
#define ACIOPHY_CFG0                        0x08
#define ACIOPHY_CFG0_COMMON_BIG_OV          (1UL << 1)
#define ACIOPHY_CFG0_COMMON_SMALL_OV        (1UL << 3)
#define ACIOPHY_CFG0_COMMON_CLAMP_OV        (1UL << 5)
#define ACIOPHY_CFG0_RX_SMALL_OV            (3UL << 8)
#define ACIOPHY_CFG0_RX_BIG_OV              (3UL << 12)
#define ACIOPHY_CFG0_RX_CLAMP_OV            (3UL << 16)
#define ACIOPHY_LANE_MODE                   0x48
#define ACIOPHY_LANE_MODE_MASK              0xFFFUL
#define ACIOPHY_LANE_MODE_ALL_OFF           0x6DBUL
#define ACIOPHY_CROSSBAR                    0x4C
#define ACIOPHY_CROSSBAR_PROTOCOL           0x1FUL
#define ACIOPHY_CROSSBAR_PROTOCOL_USB3      0x0AUL
#define ACIOPHY_CROSSBAR_DP_SINGLE_PMA      (0xFFFUL << 5)
#define ACIOPHY_CROSSBAR_DP_BOTH_PMA        (1UL << 17)
#define ACIOPHY_SLEEP_CTRL                  0x1B0
#define ACIOPHY_SLEEP_CTRL_TX_BIG_OV        (3UL << 2)
#define ACIOPHY_SLEEP_CTRL_TX_SMALL_OV      (3UL << 6)
#define ACIOPHY_SLEEP_CTRL_TX_CLAMP_OV      (3UL << 10)
#define CIO3PLL_CLK_CTRL                    0x2A00
#define CIO3PLL_CLK_PCLK_EN                 (1UL << 1)
#define CIO3PLL_CLK_REFCLK_EN               (1UL << 5)
#define LN0_AUSPMA_RX_TOP_PMAFSM            0x9010
#define LN1_AUSPMA_RX_TOP_PMAFSM            0x10010
#define LN_AUSPMA_RX_TOP_PMAFSM_PCS_OV      (1UL << 0)
#define ATCPHY_POWER_CTRL                   0x20000
#define ATCPHY_POWER_STAT                   0x20004
#define ATCPHY_POWER_SLEEP_SMALL            (1UL << 0)
#define ATCPHY_POWER_SLEEP_BIG              (1UL << 1)
#define ATCPHY_POWER_CLAMP_EN               (1UL << 2)
#define ATCPHY_POWER_APB_RESET_N            (1UL << 3)
#define ATCPHY_POWER_PHY_RESET_N            (1UL << 4)
#define ATCPHY_MISC                         0x20008
#define ATCPHY_MISC_RESET_N                 (1UL << 0)
#define ATCPHY_MISC_LANE_SWAP               (1UL << 2)
#define ATCPHY_CORE_SPAN                    0x20010

#define PIPEHANDLER_OVERRIDE                0x00
#define PIPEHANDLER_OVERRIDE_RXVALID        (1UL << 0)
#define PIPEHANDLER_OVERRIDE_RXDETECT       (1UL << 2)
#define PIPEHANDLER_OVERRIDE_VALUES         0x04
#define PIPEHANDLER_OVERRIDE_VAL_RXDETECT   (3UL << 1)
#define PIPEHANDLER_MUX_CTRL                0x0C
#define PIPEHANDLER_MUX_CTRL_DATA           0x07UL
#define PIPEHANDLER_MUX_CTRL_CLK            (0x07UL << 3)
#define PIPEHANDLER_MUX_DATA_DUMMY          2UL
#define PIPEHANDLER_MUX_CLK_DUMMY           (4UL << 3)
#define PIPEHANDLER_LOCK_REQ                0x10
#define PIPEHANDLER_LOCK_ACK                0x14
#define PIPEHANDLER_LOCK_EN                 (1UL << 0)
#define PIPEHANDLER_AON_GEN                 0x1C
#define PIPEHANDLER_AON_GEN_DWC3_RESET_N    (1UL << 0)
#define PIPEHANDLER_AON_GEN_DWC3_CLAMP_EN   (1UL << 4)
#define PIPEHANDLER_NONSELECTED_OVERRIDE    0x20
#define PIPEHANDLER_NATIVE_POWER_DOWN       0x0FUL
#define PIPEHANDLER_NATIVE_RESET            (1UL << 12)
#define PIPEHANDLER_DUMMY_PHY_EN            (1UL << 15)
#define PIPEHANDLER_SPAN                    0x24

#define USB2PHY_USBCTL                      0x00
#define USB2PHY_USBCTL_RUN                  2UL
#define USB2PHY_USBCTL_ISOLATION            4UL
#define USB2PHY_CTL                         0x04
#define USB2PHY_CTL_RESET                   (1UL << 0)
#define USB2PHY_CTL_PORT_RESET              (1UL << 1)
#define USB2PHY_CTL_APB_RESET_N             (1UL << 2)
#define USB2PHY_CTL_SIDDQ                   (1UL << 3)
#define USB2PHY_SIG                         0x08
#define USB2PHY_SIG_VBUS_FORCED             0x0FUL
#define USB2PHY_SIG_HOST                    (7UL << 12)
#define USB2PHY_MISCTUNE                    0x1C
#define USB2PHY_MISCTUNE_APBCLK_GATE_OFF    (1UL << 29)
#define USB2PHY_MISCTUNE_REFCLK_GATE_OFF    (1UL << 30)
#define USB2PHY_SPAN                        0x20

#define ATCPHY_STEP_US                      10
#define ATCPHY_WAKE_POLL_US                 100
#define ATCPHY_WAKE_TIMEOUT_US              100000
#define ATCPHY_SLEEP_TIMEOUT_US             1000
#define ATCPHY_LOCK_TIMEOUT_US              1000

static
ULONG
AtcRead(
    _In_ PAPPLE_ATCPHY_REGION Region,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Region->Base + Offset));
}

static
VOID
AtcWrite(
    _In_ PAPPLE_ATCPHY_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Region->Base + Offset), Value);
}

static
VOID
AtcMask(
    _In_ PAPPLE_ATCPHY_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Clear,
    _In_ ULONG Set)
{
    AtcWrite(Region, Offset, (AtcRead(Region, Offset) & ~Clear) | Set);
}

static
VOID
AtcMaskStep(
    _In_ PAPPLE_ATCPHY_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Clear,
    _In_ ULONG Set)
{
    AtcMask(Region, Offset, Clear, Set);
    KeStallExecutionProcessor(ATCPHY_STEP_US);
}

static
BOOLEAN
AtcWait(
    _In_ PAPPLE_ATCPHY_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG StepUs,
    _In_ ULONG TimeoutUs)
{
    ULONG Waited;

    for (Waited = 0;; Waited += StepUs)
    {
        if ((AtcRead(Region, Offset) & Mask) == Value)
            return TRUE;
        if (Waited >= TimeoutUs)
            return FALSE;
        KeStallExecutionProcessor(StepUs);
    }
}

static
BOOLEAN
AtcMapRegion(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _In_ ULONG MinimumSize,
    _Out_ PAPPLE_ATCPHY_REGION Region)
{
    ULONG Index = FdtDriverFindRegIndex(Tree, Node, Name);

    Region->Base = NULL;
    Region->Size = 0;
    if (Index == MAXULONG)
        return FALSE;
    Region->Base = FdtDriverMapReg(Tree, Node, Index, NULL, &Region->Size);
    if (Region->Base && Region->Size < MinimumSize)
    {
        MmUnmapIoSpace(Region->Base, Region->Size);
        Region->Base = NULL;
        Region->Size = 0;
    }
    return Region->Base != NULL;
}

static
ULONG
AtcApplyTunable(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _In_ PAPPLE_ATCPHY_REGION Region)
{
    const UCHAR *Property;
    ULONG Length, Count, Index, Offset, Mask, Value;

    Property = FdtGetProperty(&Tree->Fdt, Node, Name, &Length);
    if (!Property || !Length || Length % (3 * sizeof(ULONG)))
        return 0;
    Count = Length / (3 * sizeof(ULONG));
    for (Index = 0; Index < Count; ++Index)
    {
        Offset = FdtReadBigEndian32(Property + Index * 12);
        if ((Offset & 3) || Region->Size < sizeof(ULONG) || Offset > Region->Size - sizeof(ULONG))
            return 0;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        ULONG Old;

        Offset = FdtReadBigEndian32(Property + Index * 12);
        Mask = FdtReadBigEndian32(Property + Index * 12 + 4);
        Value = FdtReadBigEndian32(Property + Index * 12 + 8);
        Old = AtcRead(Region, Offset);
        if (((Old & ~Mask) | Value) != Old)
            AtcWrite(Region, Offset, (Old & ~Mask) | Value);
    }
    return Count;
}

static
VOID
AtcAssertDwc3Reset(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_AON_GEN, PIPEHANDLER_AON_GEN_DWC3_RESET_N, 0);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_AON_GEN, 0, PIPEHANDLER_AON_GEN_DWC3_CLAMP_EN);
}

static
VOID
AtcUsb2PowerOff(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcWrite(&Phy->Usb2Phy, USB2PHY_USBCTL, USB2PHY_USBCTL_ISOLATION);
    KeStallExecutionProcessor(ATCPHY_STEP_US);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, 0, USB2PHY_CTL_SIDDQ);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, 0, USB2PHY_CTL_PORT_RESET);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, 0, USB2PHY_CTL_RESET);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, USB2PHY_CTL_APB_RESET_N, 0);
    AtcMask(&Phy->Usb2Phy, USB2PHY_MISCTUNE, 0, USB2PHY_MISCTUNE_APBCLK_GATE_OFF);
    AtcMask(&Phy->Usb2Phy, USB2PHY_MISCTUNE, 0, USB2PHY_MISCTUNE_REFCLK_GATE_OFF);
}

static
NTSTATUS
AtcPowerOff(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, ATCPHY_POWER_PHY_RESET_N, 0);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, 0, ATCPHY_POWER_CLAMP_EN);
    AtcMask(&Phy->Core, ATCPHY_MISC, ATCPHY_MISC_RESET_N | ATCPHY_MISC_LANE_SWAP, 0);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, ATCPHY_POWER_APB_RESET_N, 0);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, ATCPHY_POWER_SLEEP_BIG, 0);
    if (!AtcWait(&Phy->Core, ATCPHY_POWER_STAT, ATCPHY_POWER_SLEEP_BIG, 0, ATCPHY_STEP_US, ATCPHY_SLEEP_TIMEOUT_US))
        return STATUS_IO_TIMEOUT;
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, ATCPHY_POWER_SLEEP_SMALL, 0);
    if (!AtcWait(&Phy->Core, ATCPHY_POWER_STAT, ATCPHY_POWER_SLEEP_SMALL, 0, ATCPHY_STEP_US, ATCPHY_SLEEP_TIMEOUT_US))
        return STATUS_IO_TIMEOUT;
    return STATUS_SUCCESS;
}

static
VOID
AtcSetPipeMuxDummy(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcMaskStep(&Phy->PipeHandler, PIPEHANDLER_MUX_CTRL, PIPEHANDLER_MUX_CTRL_CLK, 0);
    AtcMaskStep(&Phy->PipeHandler, PIPEHANDLER_MUX_CTRL, PIPEHANDLER_MUX_CTRL_DATA, PIPEHANDLER_MUX_DATA_DUMMY);
    AtcMaskStep(&Phy->PipeHandler, PIPEHANDLER_MUX_CTRL, PIPEHANDLER_MUX_CTRL_CLK, PIPEHANDLER_MUX_CLK_DUMMY);
}

static
VOID
AtcUsb2PowerOn(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_SIG, 0, USB2PHY_SIG_VBUS_FORCED);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, USB2PHY_CTL_SIDDQ, 0);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, USB2PHY_CTL_RESET, 0);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, USB2PHY_CTL_PORT_RESET, 0);
    AtcMaskStep(&Phy->Usb2Phy, USB2PHY_CTL, 0, USB2PHY_CTL_APB_RESET_N);
    AtcMask(&Phy->Usb2Phy, USB2PHY_MISCTUNE, USB2PHY_MISCTUNE_APBCLK_GATE_OFF, 0);
    AtcMask(&Phy->Usb2Phy, USB2PHY_MISCTUNE, USB2PHY_MISCTUNE_REFCLK_GATE_OFF, 0);
    AtcWrite(&Phy->Usb2Phy, USB2PHY_USBCTL, USB2PHY_USBCTL_RUN);
}

static
NTSTATUS
AtcPowerOn(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    AtcUsb2PowerOn(Phy);
    AtcMask(&Phy->Core, ATCPHY_MISC, 0, ATCPHY_MISC_RESET_N);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, 0, ATCPHY_POWER_SLEEP_SMALL);
    if (!AtcWait(&Phy->Core, ATCPHY_POWER_STAT, ATCPHY_POWER_SLEEP_SMALL, ATCPHY_POWER_SLEEP_SMALL,
                 ATCPHY_WAKE_POLL_US, ATCPHY_WAKE_TIMEOUT_US))
        return STATUS_IO_TIMEOUT;
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, 0, ATCPHY_POWER_SLEEP_BIG);
    if (!AtcWait(&Phy->Core, ATCPHY_POWER_STAT, ATCPHY_POWER_SLEEP_BIG, ATCPHY_POWER_SLEEP_BIG,
                 ATCPHY_WAKE_POLL_US, ATCPHY_WAKE_TIMEOUT_US))
        return STATUS_IO_TIMEOUT;
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, ATCPHY_POWER_CLAMP_EN, 0);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, 0, ATCPHY_POWER_APB_RESET_N);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAtcphyOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG ConsumerNode,
    _Out_ PAPPLE_ATCPHY Phy)
{
    const UCHAR *Phys;
    const VOID *Compatible;
    ULONG Length;

    RtlZeroMemory(Phy, sizeof(*Phy));
    Phys = FdtGetProperty(&Tree->Fdt, ConsumerNode, "phys", &Length);
    if (!Phys || Length < sizeof(ULONG))
        return STATUS_NOT_FOUND;
    Phy->Node = FdtDriverFindPhandle(Tree, FdtReadBigEndian32(Phys));
    if (Phy->Node == FDT_NO_NODE)
        return STATUS_NOT_FOUND;
    Compatible = FdtGetProperty(&Tree->Fdt, Phy->Node, "compatible", &Length);
    Phy->HasAusPllTop = !Compatible || !FdtStringListContains(Compatible, Length, "apple,t8122-atcphy");
    if (!AtcMapRegion(Tree, Phy->Node, "core", ATCPHY_CORE_SPAN, &Phy->Core) ||
        !AtcMapRegion(Tree, Phy->Node, "axi2af", sizeof(ULONG), &Phy->Axi2af) ||
        !AtcMapRegion(Tree, Phy->Node, "usb2phy", USB2PHY_SPAN, &Phy->Usb2Phy) ||
        !AtcMapRegion(Tree, Phy->Node, "pipehandler", PIPEHANDLER_SPAN, &Phy->PipeHandler))
    {
        AppleAtcphyClose(Phy);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAtcphyClose(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    PAPPLE_ATCPHY_REGION Regions[] = { &Phy->Core, &Phy->Axi2af, &Phy->Usb2Phy, &Phy->PipeHandler };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Regions); ++Index)
    {
        if (Regions[Index]->Base)
            MmUnmapIoSpace(Regions[Index]->Base, Regions[Index]->Size);
        Regions[Index]->Base = NULL;
        Regions[Index]->Size = 0;
    }
}

NTSTATUS
NTAPI
AppleAtcphyStartUsb2Host(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_ATCPHY Phy)
{
    NTSTATUS Status;

    AtcAssertDwc3Reset(Phy);
    AtcUsb2PowerOff(Phy);
    Status = AtcPowerOff(Phy);
    if (!NT_SUCCESS(Status))
        return Status;
    AtcSetPipeMuxDummy(Phy);

    Status = AtcPowerOn(Phy);
    if (!NT_SUCCESS(Status))
        return Status;
    Phy->TunableEntries[0] = AtcApplyTunable(Tree, Phy->Node, "apple,tunable-common-a", &Phy->Core);
    Phy->TunableEntries[1] = AtcApplyTunable(Tree, Phy->Node, "apple,tunable-axi2af", &Phy->Axi2af);
    Phy->TunableEntries[2] = AtcApplyTunable(Tree, Phy->Node, "apple,tunable-common-b", &Phy->Core);
    if (Phy->HasAusPllTop)
    {
        AtcMask(&Phy->Core, AUSPLL_FSM_CTRL, 0, 0x1FE000);
        AtcMask(&Phy->Core, AUSPLL_APB_CMD_OVERRIDE, 0, AUSPLL_APB_CMD_OVERRIDE_UNK28);
    }
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, 0, ACIOPHY_CFG0_COMMON_SMALL_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, 0, ACIOPHY_CFG0_COMMON_BIG_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, 0, ACIOPHY_CFG0_COMMON_CLAMP_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_SLEEP_CTRL, ACIOPHY_SLEEP_CTRL_TX_SMALL_OV, ACIOPHY_SLEEP_CTRL_TX_SMALL_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_SLEEP_CTRL, ACIOPHY_SLEEP_CTRL_TX_BIG_OV, ACIOPHY_SLEEP_CTRL_TX_BIG_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_SLEEP_CTRL, ACIOPHY_SLEEP_CTRL_TX_CLAMP_OV, ACIOPHY_SLEEP_CTRL_TX_CLAMP_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, ACIOPHY_CFG0_RX_BIG_OV, ACIOPHY_CFG0_RX_BIG_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, ACIOPHY_CFG0_RX_SMALL_OV, ACIOPHY_CFG0_RX_SMALL_OV);
    AtcMaskStep(&Phy->Core, ACIOPHY_CFG0, ACIOPHY_CFG0_RX_CLAMP_OV, ACIOPHY_CFG0_RX_CLAMP_OV);
    AtcMask(&Phy->Core, CIO3PLL_CLK_CTRL, 0, CIO3PLL_CLK_PCLK_EN);
    AtcMask(&Phy->Core, CIO3PLL_CLK_CTRL, 0, CIO3PLL_CLK_REFCLK_EN);

    AtcMask(&Phy->Core, ACIOPHY_LANE_MODE, ACIOPHY_LANE_MODE_MASK, ACIOPHY_LANE_MODE_ALL_OFF);
    AtcMask(&Phy->Core, ACIOPHY_CROSSBAR, ACIOPHY_CROSSBAR_PROTOCOL, ACIOPHY_CROSSBAR_PROTOCOL_USB3);
    AtcMask(&Phy->Core, ATCPHY_MISC, ATCPHY_MISC_LANE_SWAP, 0);
    AtcMask(&Phy->Core, ACIOPHY_CROSSBAR, ACIOPHY_CROSSBAR_DP_SINGLE_PMA | ACIOPHY_CROSSBAR_DP_BOTH_PMA, 0);
    AtcMaskStep(&Phy->Core, LN0_AUSPMA_RX_TOP_PMAFSM, LN_AUSPMA_RX_TOP_PMAFSM_PCS_OV, 0);
    AtcMaskStep(&Phy->Core, LN1_AUSPMA_RX_TOP_PMAFSM, LN_AUSPMA_RX_TOP_PMAFSM_PCS_OV, 0);
    AtcMask(&Phy->Core, ATCPHY_POWER_CTRL, 0, ATCPHY_POWER_PHY_RESET_N);

    AtcMask(&Phy->Usb2Phy, USB2PHY_SIG, 0, USB2PHY_SIG_HOST);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_AON_GEN, PIPEHANDLER_AON_GEN_DWC3_CLAMP_EN, 0);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_AON_GEN, 0, PIPEHANDLER_AON_GEN_DWC3_RESET_N);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAtcphySelectDummyPipe(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    BOOLEAN Locked;

    if ((AtcRead(&Phy->PipeHandler, PIPEHANDLER_LOCK_ACK) & PIPEHANDLER_LOCK_EN))
    {
        AtcMask(&Phy->PipeHandler, PIPEHANDLER_LOCK_REQ, PIPEHANDLER_LOCK_EN, 0);
        if (!AtcWait(&Phy->PipeHandler, PIPEHANDLER_LOCK_ACK, PIPEHANDLER_LOCK_EN, 0, ATCPHY_STEP_US, ATCPHY_LOCK_TIMEOUT_US))
            return STATUS_IO_TIMEOUT;
    }
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_OVERRIDE_VALUES, PIPEHANDLER_OVERRIDE_VAL_RXDETECT, 0);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_OVERRIDE, 0, PIPEHANDLER_OVERRIDE_RXVALID);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_OVERRIDE, 0, PIPEHANDLER_OVERRIDE_RXDETECT);

    Locked = (AtcRead(&Phy->PipeHandler, PIPEHANDLER_LOCK_REQ) & PIPEHANDLER_LOCK_EN) != 0;
    if (!Locked)
    {
        AtcMask(&Phy->PipeHandler, PIPEHANDLER_LOCK_REQ, 0, PIPEHANDLER_LOCK_EN);
        Locked = AtcWait(&Phy->PipeHandler, PIPEHANDLER_LOCK_ACK, PIPEHANDLER_LOCK_EN, PIPEHANDLER_LOCK_EN,
                         ATCPHY_STEP_US, ATCPHY_LOCK_TIMEOUT_US);
        if (!Locked)
            AtcMask(&Phy->PipeHandler, PIPEHANDLER_LOCK_REQ, PIPEHANDLER_LOCK_EN, 0);
    }
    AtcSetPipeMuxDummy(Phy);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_LOCK_REQ, PIPEHANDLER_LOCK_EN, 0);
    if (!AtcWait(&Phy->PipeHandler, PIPEHANDLER_LOCK_ACK, PIPEHANDLER_LOCK_EN, 0, ATCPHY_STEP_US, ATCPHY_LOCK_TIMEOUT_US))
        Locked = FALSE;
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_NONSELECTED_OVERRIDE, PIPEHANDLER_NATIVE_POWER_DOWN, 2);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_NONSELECTED_OVERRIDE, 0, PIPEHANDLER_NATIVE_RESET);
    AtcMask(&Phy->PipeHandler, PIPEHANDLER_NONSELECTED_OVERRIDE, 0, PIPEHANDLER_DUMMY_PHY_EN);
    return Locked ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

NTSTATUS
NTAPI
AppleAtcphyStopUsb2Host(
    _Inout_ PAPPLE_ATCPHY Phy)
{
    NTSTATUS PipeStatus, Status;

    AtcAssertDwc3Reset(Phy);
    PipeStatus = AppleAtcphySelectDummyPipe(Phy);
    AtcUsb2PowerOff(Phy);
    Status = AtcPowerOff(Phy);
    return NT_SUCCESS(PipeStatus) ? Status : PipeStatus;
}
