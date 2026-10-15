/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power sequencing, chip identification and eFuse access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
RtwWriteXtalSi(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Offset,
    _In_ UCHAR Value,
    _In_ UCHAR Mask)
{
    ULONG Command;
    ULONG Result;
    NTSTATUS Status;

    Command = Offset |
              ((ULONG)Value << B_AX_WL_XTAL_SI_DATA_SHIFT) |
              ((ULONG)Mask << B_AX_WL_XTAL_SI_BITMASK_SHIFT) |
              ((ULONG)XTAL_SI_NORMAL_WRITE << B_AX_WL_XTAL_SI_MODE_SHIFT) |
              B_AX_WL_XTAL_SI_CMD_POLL;
    RtwWrite32(Adapter, R_AX_WLAN_XTAL_SI_CTRL, Command);

    Status = RtwPoll32(Adapter, R_AX_WLAN_XTAL_SI_CTRL, B_AX_WL_XTAL_SI_CMD_POLL, 0, 50000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: XTAL SI write 0x%02x timed out\n", Offset);
        return Status;
    }

    Result = RtwRead32(Adapter, R_AX_WLAN_XTAL_SI_CTRL);
    if ((UCHAR)Result != Offset || (UCHAR)(Result >> B_AX_WL_XTAL_SI_DATA_SHIFT) != Value)
        DPRINT("RTW8852BS: XTAL SI write 0x%02x readback 0x%08lx\n", Offset, Result);
    return STATUS_SUCCESS;
}

NTSTATUS
RtwReadXtalSi(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Offset,
    _Out_ PUCHAR Value)
{
    ULONG Command;
    ULONG Result;
    NTSTATUS Status;

    Command = Offset |
              ((ULONG)XTAL_SI_NORMAL_READ << B_AX_WL_XTAL_SI_MODE_SHIFT) |
              B_AX_WL_XTAL_SI_CMD_POLL;
    RtwWrite32(Adapter, R_AX_WLAN_XTAL_SI_CTRL, Command);

    Status = RtwPoll32(Adapter, R_AX_WLAN_XTAL_SI_CTRL, B_AX_WL_XTAL_SI_CMD_POLL, 0, 50000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: XTAL SI read 0x%02x timed out\n", Offset);
        *Value = 0;
        return Status;
    }

    Result = RtwRead32(Adapter, R_AX_WLAN_XTAL_SI_CTRL);
    *Value = (UCHAR)(Result >> B_AX_WL_XTAL_SI_DATA_SHIFT);
    return STATUS_SUCCESS;
}

static
UCHAR
RtwMacPowerState(
    _In_ PRTW_ADAPTER Adapter)
{
    return (UCHAR)((RtwRead32(Adapter, R_AX_IC_PWR_STATE) & B_AX_WLMAC_PWR_STE_MASK) >> B_AX_WLMAC_PWR_STE_SHIFT);
}

static
NTSTATUS
RtwPowerOnSequence(
    _In_ PRTW_ADAPTER Adapter)
{
    static const struct
    {
        UCHAR Offset;
        UCHAR Value;
        UCHAR Mask;
    } XtalSteps[] =
    {
        { XTAL_SI_ANAPAR_WL, 0x04, 0x04 },
        { XTAL_SI_ANAPAR_WL, 0x08, 0x08 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x10 },
        { XTAL_SI_ANAPAR_WL, 0x01, 0x01 },
        { XTAL_SI_ANAPAR_WL, 0x02, 0x02 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x80 },
        { XTAL_SI_SRAM_CTRL, 0x00, 0x02 },
        { XTAL_SI_XTAL_XMD_2, 0x00, 0x70 },
        { XTAL_SI_XTAL_XMD_4, 0x00, 0x0F },
    };
    NTSTATUS Status;
    ULONG Value;
    ULONG i;

    RtwWrite32Clear(Adapter, R_AX_SDIO_BUS_CTRL, B_AX_HCI_SUS_REQ);
    Status = RtwPoll32(Adapter, R_AX_SDIO_BUS_CTRL, B_AX_HCI_RESUME_RDY, B_AX_HCI_RESUME_RDY, 20000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: SDIO resume did not complete\n");
        return Status;
    }

    RtwWrite32Clear(Adapter, R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN | B_AX_AFSM_PCIE_SUS_EN);
    RtwWrite32Set(Adapter, R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
    RtwWrite32Set(Adapter, R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
    RtwWrite32Clear(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

    Status = RtwPoll32(Adapter, R_AX_SYS_PW_CTRL, B_AX_RDY_SYSPWR, B_AX_RDY_SYSPWR, 20000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: system power did not become ready\n");
        return Status;
    }

    RtwWrite32Set(Adapter, R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN);
    Status = RtwPoll32(Adapter, R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN, B_AX_AON_OFF_PC_EN, 20000);
    if (!NT_SUCCESS(Status))
        return Status;

    Value = RtwRead32(Adapter, R_AX_SPS_DIG_OFF_CTRL0);
    Value = (Value & ~(B_AX_C1_L1_MASK | B_AX_C3_L1_MASK)) | 0x1 | (0x3 << B_AX_C3_L1_SHIFT);
    RtwWrite32(Adapter, R_AX_SPS_DIG_OFF_CTRL0, Value);

    RtwWrite32Set(Adapter, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
    RtwWrite32Set(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);
    Status = RtwPoll32(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC, 0, 20000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: MAC power-on request did not complete\n");
        return Status;
    }

    for (i = 0; i < 2; i++)
    {
        RtwWrite8(Adapter, R_AX_PLATFORM_ENABLE, RtwRead8(Adapter, R_AX_PLATFORM_ENABLE) | B_AX_PLATFORM_EN);
        RtwWrite8(Adapter, R_AX_PLATFORM_ENABLE, RtwRead8(Adapter, R_AX_PLATFORM_ENABLE) & ~B_AX_PLATFORM_EN);
    }
    RtwWrite8(Adapter, R_AX_PLATFORM_ENABLE, RtwRead8(Adapter, R_AX_PLATFORM_ENABLE) | B_AX_PLATFORM_EN);

    RtwWrite32Set(Adapter, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);
    Status = RtwWriteXtalSi(Adapter, XTAL_SI_ANAPAR_WL, 0x40, 0x40);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite32Set(Adapter, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);
    Status = RtwWriteXtalSi(Adapter, XTAL_SI_ANAPAR_WL, 0x20, 0x20);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < RTL_NUMBER_OF(XtalSteps); i++)
    {
        Status = RtwWriteXtalSi(Adapter, XtalSteps[i].Offset, XtalSteps[i].Value, XtalSteps[i].Mask);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    RtwWrite32Set(Adapter, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    RtwWrite32Set(Adapter, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
    RtwWrite32Clear(Adapter, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);
    RtwSleep(1);
    RtwWrite32Clear(Adapter, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
    RtwWrite32Clear(Adapter, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);

    return STATUS_SUCCESS;
}

static
VOID
RtwEnableFunctions(
    _In_ PRTW_ADAPTER Adapter)
{
    if (!Adapter->PowerCalibrated)
    {
        RtwWrite32Mask(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_VOL_L1_MASK, 0x9);
        RtwWrite32Mask(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_VREFPFM_L_MASK, 0xA);
    }

    RtwWrite32Set(Adapter, R_AX_DMAC_FUNC_EN,
                  B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
                  B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
                  B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
                  B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
                  B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
                  B_AX_DMACREG_GCKEN);
    RtwWrite32Set(Adapter, R_AX_CMAC_FUNC_EN,
                  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
                  B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN |
                  B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN | B_AX_TMAC_EN |
                  B_AX_RMAC_EN);
    RtwWrite32Mask(Adapter, R_AX_EECS_EESK_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_MASK,
                   PINMUX_EESK_FUNC_SEL_BT_LOG);
}

static
NTSTATUS
RtwReadPhysicalEfuse(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Start,
    _In_ ULONG Count,
    _Out_writes_bytes_(Count) PUCHAR Map)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Control;
    ULONG Address;

    RtwWrite8(Adapter, R_AX_PMC_DBG_CTRL2, RtwRead8(Adapter, R_AX_PMC_DBG_CTRL2) | B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) | (USHORT)B_AX_PWC_EV2EF_B14);
    RtwSleep(1);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) | (USHORT)B_AX_PWC_EV2EF_B15);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) & (USHORT)~B_AX_ISO_EB2CORE);
    if (Adapter->ChipVersion == 0)
        RtwWrite32Set(Adapter, R_AX_EFUSE_CTRL_1_V1, B_AX_EF_BURST);

    for (Address = Start; Address < Start + Count; Address++)
    {
        Control = (Address << B_AX_EF_ADDR_SHIFT) & B_AX_EF_ADDR_MASK;
        RtwWrite32(Adapter, R_AX_EFUSE_CTRL, Control & ~B_AX_EF_RDY);
        Status = RtwPoll32(Adapter, R_AX_EFUSE_CTRL, B_AX_EF_RDY, B_AX_EF_RDY, 1000000);
        if (!NT_SUCCESS(Status))
            break;
        *Map++ = (UCHAR)RtwRead32(Adapter, R_AX_EFUSE_CTRL);
    }

    if (Adapter->ChipVersion == 0)
        RtwWrite32Clear(Adapter, R_AX_EFUSE_CTRL_1_V1, B_AX_EF_BURST);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) | (USHORT)B_AX_ISO_EB2CORE);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) & (USHORT)~B_AX_PWC_EV2EF_B15);
    RtwSleep(1);
    RtwWrite16(Adapter, R_AX_SYS_ISO_CTRL, RtwRead16(Adapter, R_AX_SYS_ISO_CTRL) & (USHORT)~B_AX_PWC_EV2EF_B14);
    RtwWrite8(Adapter, R_AX_PMC_DBG_CTRL2, RtwRead8(Adapter, R_AX_PMC_DBG_CTRL2) & ~B_AX_SYSON_DIS_PMCR_AX_WRMSK);

    return Status;
}

static
NTSTATUS
RtwBuildLogicalEfuse(
    _In_reads_bytes_(RTW_EFUSE_PHYSICAL_SIZE) const UCHAR *Physical,
    _Out_writes_bytes_(RTW_EFUSE_LOGICAL_SIZE) PUCHAR Logical)
{
    ULONG Index = RTW_EFUSE_SEC_CTRL_SIZE;
    ULONG Limit = RTW_EFUSE_PHYSICAL_SIZE - RTW_EFUSE_SEC_CTRL_SIZE;
    ULONG Block, LogicalIndex, Word;
    UCHAR Header1, Header2, WordEnable;

    RtlFillMemory(Logical, RTW_EFUSE_LOGICAL_SIZE, 0xFF);

    while (Index + 1 < Limit)
    {
        Header1 = Physical[Index];
        Header2 = Physical[Index + 1];
        if (Header1 == 0xFF || Header2 == 0xFF)
            break;

        Block = ((Header2 & 0xF0) >> 4) | ((ULONG)(Header1 & 0x0F) << 4);
        WordEnable = Header2 & 0x0F;
        Index += 2;

        for (Word = 0; Word < 4; Word++)
        {
            if (WordEnable & (1u << Word))
                continue;

            LogicalIndex = (Block << 3) + (Word << 1);
            if (Index + 1 > Limit - 1 || LogicalIndex + 1 > RTW_EFUSE_LOGICAL_SIZE - 1)
                return STATUS_DEVICE_DATA_ERROR;

            Logical[LogicalIndex] = Physical[Index];
            Logical[LogicalIndex + 1] = Physical[Index + 1];
            Index += 2;
        }
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
RtwIsSecureBoot(
    _In_ UCHAR Byte1,
    _In_ UCHAR Byte2)
{
    UCHAR DeviceType = Byte1 & 0x0F;
    UCHAR ExternalPn = (UCHAR)(0xFF - Byte1);
    UCHAR Customer = (UCHAR)(0x0F - (Byte2 & 0x0F));
    UCHAR Serial = (UCHAR)(0x07 - ((Byte2 >> 4) & 0x07));

    if (Byte1 == 0xFF && Byte2 == 0xFF)
        return FALSE;
    if (ExternalPn == 0 && ((Customer == 0 && Serial == 0) || (Customer == 1 && Serial == 1)))
        return TRUE;
    if (Byte1 == 0xFF && Byte2 == 0x6E)
        return TRUE;
    return DeviceType == 0xF || DeviceType == 0xC || DeviceType == 0xA || DeviceType == 0x9 || DeviceType == 0x6;
}

NTSTATUS
RtwReadEfuse(
    _In_ PRTW_ADAPTER Adapter)
{
    PUCHAR Physical;
    NTSTATUS Status;

    Adapter->EfuseValid = (RtwRead16(Adapter, R_AX_SYS_WL_EFUSE_CTRL) & B_AX_AUTOLOAD_SUS) != 0;

    Physical = RtwAllocate(RTW_EFUSE_PHYSICAL_SIZE);
    if (!Adapter->LogicalEfuse)
        Adapter->LogicalEfuse = RtwAllocate(RTW_EFUSE_LOGICAL_SIZE);
    if (!Physical || !Adapter->LogicalEfuse)
    {
        RtwFree(Physical);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = RtwReadPhysicalEfuse(Adapter, 0, RTW_EFUSE_PHYSICAL_SIZE, Physical);
    if (NT_SUCCESS(Status))
        Status = RtwBuildLogicalEfuse(Physical, Adapter->LogicalEfuse);
    RtwFree(Physical);
    if (NT_SUCCESS(Status))
        Status = RtwReadPhysicalEfuse(Adapter, RTW_EFUSE_PHYCAP_OFFSET, RTW_EFUSE_PHYCAP_SIZE, Adapter->PhyCap);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: eFuse read failed 0x%08lx\n", Status);
        return Status;
    }

    Adapter->SecureInfo[0] = Adapter->PhyCap[RTW_EFUSE_SECURE_OFFSET - RTW_EFUSE_PHYCAP_OFFSET];
    Adapter->SecureInfo[1] = Adapter->PhyCap[RTW_EFUSE_SECURE_OFFSET + 1 - RTW_EFUSE_PHYCAP_OFFSET];
    Adapter->SecureBoot = RtwIsSecureBoot(Adapter->SecureInfo[0], Adapter->SecureInfo[1]);

    RtlCopyMemory(Adapter->PermanentAddress,
                  Adapter->LogicalEfuse + RTW_EFUSE_SDIO_MAC_OFFSET,
                  RTW_ADDRESS_LENGTH);
    RtlCopyMemory(Adapter->CurrentAddress, Adapter->PermanentAddress, RTW_ADDRESS_LENGTH);
    Adapter->PowerCalibrated = Adapter->PhyCap[RTW_EFUSE_PWR_K_OFFSET - RTW_EFUSE_PHYCAP_OFFSET] == RTW_EFUSE_PWR_K_VALUE;
    return STATUS_SUCCESS;
}

typedef struct _RTW_DLE_QUOTA
{
    USHORT Min;
    USHORT Max;
} RTW_DLE_QUOTA;

static
NTSTATUS
RtwWaitDleReady(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = RtwPoll32(Adapter, R_AX_WDE_INI_STATUS, RTW_DLE_MGN_INI_RDY, RTW_DLE_MGN_INI_RDY, 2000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: WDE not ready 0x%08lx\n", RtwRead32(Adapter, R_AX_WDE_INI_STATUS));
        return Status;
    }

    Status = RtwPoll32(Adapter, R_AX_PLE_INI_STATUS, RTW_DLE_MGN_INI_RDY, RTW_DLE_MGN_INI_RDY, 2000);
    if (!NT_SUCCESS(Status))
        DPRINT1("RTW8852BS: PLE not ready 0x%08lx\n", RtwRead32(Adapter, R_AX_PLE_INI_STATUS));
    return Status;
}

static
NTSTATUS
RtwInitDleForDownload(
    _In_ PRTW_ADAPTER Adapter)
{
    static const RTW_DLE_QUOTA WdeQuota[] =
    {
        { 0, 0 }, { 8, 0 }, { 0, 0 }, { 0, 0 },
    };
    static const ULONG WdeQuotaRegister[] =
    {
        R_AX_WDE_QTA0_CFG, R_AX_WDE_QTA1_CFG, R_AX_WDE_QTA3_CFG, R_AX_WDE_QTA4_CFG,
    };
    static const RTW_DLE_QUOTA PleQuota[] =
    {
        { 0, 0 }, { 0, 0 }, { 16, 16 }, { 48, 48 }, { 0, 0 }, { 0, 0 },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    };
    const ULONG WdeLinkPages = 0, WdeUnlinkPages = 1024, WdePageSize = 64;
    const ULONG PleLinkPages = 64;
    ULONG Value, Bound, i;

    RtwWrite32Clear(Adapter, R_AX_DMAC_FUNC_EN, B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);
    RtwWrite32Set(Adapter, R_AX_DMAC_CLK_EN, B_AX_DLE_WDE_CLK_EN | B_AX_DLE_PLE_CLK_EN);

    Value = RtwRead32(Adapter, R_AX_WDE_PKTBUF_CFG);
    Value &= ~(0x3UL | (0x3FUL << B_AX_DLE_START_BOUND_SHIFT) | (0x1FFFUL << B_AX_DLE_FREE_PAGE_NUM_SHIFT));
    Value |= RTW_WDE_PAGE_SEL_64 | (WdeLinkPages << B_AX_DLE_FREE_PAGE_NUM_SHIFT);
    RtwWrite32(Adapter, R_AX_WDE_PKTBUF_CFG, Value);

    Bound = (WdeLinkPages + WdeUnlinkPages) * WdePageSize / RTW_DLE_BOUND_UNIT;
    Value = RtwRead32(Adapter, R_AX_PLE_PKTBUF_CFG);
    Value &= ~(0x3UL | (0x3FUL << B_AX_DLE_START_BOUND_SHIFT) | (0x1FFFUL << B_AX_DLE_FREE_PAGE_NUM_SHIFT));
    Value |= RTW_PLE_PAGE_SEL_128 | (Bound << B_AX_DLE_START_BOUND_SHIFT) | (PleLinkPages << B_AX_DLE_FREE_PAGE_NUM_SHIFT);
    RtwWrite32(Adapter, R_AX_PLE_PKTBUF_CFG, Value);

    for (i = 0; i < RTL_NUMBER_OF(WdeQuota); i++)
        RtwWrite32(Adapter, WdeQuotaRegister[i], WdeQuota[i].Min | ((ULONG)WdeQuota[i].Max << B_AX_DLE_QTA_MAX_SHIFT));
    for (i = 0; i < RTL_NUMBER_OF(PleQuota); i++)
        RtwWrite32(Adapter, R_AX_PLE_QTA0_CFG + i * 4, PleQuota[i].Min | ((ULONG)PleQuota[i].Max << B_AX_DLE_QTA_MAX_SHIFT));

    RtwWrite32Set(Adapter, R_AX_DMAC_FUNC_EN, B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);
    return RtwWaitDleReady(Adapter);
}

NTSTATUS
RtwMacPreInit(
    _In_ PRTW_ADAPTER Adapter)
{
    const ULONG H2cPrecost = 40;
    NTSTATUS Status;

    RtwWrite32Set(Adapter, R_AX_HCI_FUNC_EN, B_AX_HCI_TXDMA_EN | B_AX_HCI_RXDMA_EN);
    RtwWrite32(Adapter, R_AX_DMAC_FUNC_EN, B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_DISPATCHER_EN | B_AX_PKT_BUF_EN);
    RtwWrite32(Adapter, R_AX_DMAC_CLK_EN, B_AX_DISPATCHER_CLK_EN);

    Status = RtwInitDleForDownload(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite32Clear(Adapter, R_AX_HCI_FC_CTRL, B_AX_HCI_FC_EN | B_AX_HCI_FC_CH12_EN);
    RtwWrite32(Adapter, R_AX_CH_PAGE_CTRL, H2cPrecost << B_AX_PREC_PAGE_CH12_SHIFT);
    RtwWrite32Mask(Adapter, R_AX_HCI_FC_CTRL, B_AX_HCI_FC_CH12_FULL_COND_MASK, 0);
    RtwWrite32(Adapter, R_AX_HCI_FC_CTRL,
               (RtwRead32(Adapter, R_AX_HCI_FC_CTRL) & ~B_AX_HCI_FC_EN) | B_AX_HCI_FC_CH12_EN);

    RtwWrite32Set(Adapter, R_AX_HCI_OPT_CTRL, B_AX_SDIO_DATA_PAD_SMT);
    RtwWrite32(Adapter, R_AX_SDIO_TX_CTRL,
               (RtwRead32(Adapter, R_AX_SDIO_TX_CTRL) & ~B_AX_CMD53_TX_FORMAT) | B_AX_RXINT_READ_MASK_DIS);
    RtwWrite16(Adapter, R_AX_SDIO_BUS_CTRL, RtwRead16(Adapter, R_AX_SDIO_BUS_CTRL) | (USHORT)B_AX_EN_RPT_TXCRC);

    return STATUS_SUCCESS;
}

NTSTATUS
RtwPowerOn(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG Himr;
    ULONG Poll;
    UCHAR State;

    Adapter->PowerOn = FALSE;

    if (RtwRead32(Adapter, R_AX_GPIO_MUXCFG) & B_AX_BOOT_MODE)
    {
        RtwWrite32Clear(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);
        RtwWrite32Clear(Adapter, R_AX_SYS_STATUS1, B_AX_AUTO_WLPON);
        RtwWrite32Clear(Adapter, R_AX_GPIO_MUXCFG, B_AX_BOOT_MODE);
        RtwWrite32Clear(Adapter, R_AX_RSV_CTRL, B_AX_R_DIS_PRST);
    }

    Himr = RtwRead32(Adapter, R_AX_SDIO_HIMR);
    RtwWrite32(Adapter, R_AX_SDIO_HIMR, 0);

    RtwWrite32Clear(Adapter, R_AX_AFE_CTRL1, B_AX_CMAC_CLK_SEL);
    for (Poll = 0; Poll < 2000; Poll++)
    {
        State = RtwMacPowerState(Adapter);
        if (State != MAC_AX_MAC_LPS)
            break;
        KeStallExecutionProcessor(10);
    }

    if (State == MAC_AX_MAC_ON)
    {
        DPRINT1("RTW8852BS: MAC already on, resetting power\n");
        RtwWrite32(Adapter, R_AX_SDIO_HIMR, Himr);
        RtwPowerOff(Adapter);
        RtwWrite32(Adapter, R_AX_SDIO_HIMR, 0);
    }
    else if (State == MAC_AX_MAC_LPS)
    {
        DPRINT1("RTW8852BS: MAC stuck in low power state\n");
        RtwWrite32(Adapter, R_AX_SDIO_HIMR, Himr);
        return STATUS_DEVICE_NOT_READY;
    }

    Status = RtwPowerOnSequence(Adapter);
    if (!NT_SUCCESS(Status))
    {
        RtwWrite32(Adapter, R_AX_SDIO_HIMR, Himr);
        return Status;
    }

    Adapter->PowerOn = TRUE;
    RtwWrite8(Adapter, R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_TP_MAJOR);
    RtwWrite32(Adapter, R_AX_SDIO_HIMR, Himr);
    return STATUS_SUCCESS;
}

VOID
RtwPowerOff(
    _In_ PRTW_ADAPTER Adapter)
{
    static const struct
    {
        UCHAR Offset;
        UCHAR Value;
        UCHAR Mask;
    } XtalSteps[] =
    {
        { XTAL_SI_ANAPAR_WL, 0x10, 0x10 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x08 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x04 },
        { XTAL_SI_WL_RFC_S0, 0x00, 0x01 },
        { XTAL_SI_WL_RFC_S1, 0x00, 0x01 },
        { XTAL_SI_ANAPAR_WL, 0x80, 0x80 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x02 },
        { XTAL_SI_ANAPAR_WL, 0x00, 0x01 },
    };
    ULONG i;

    Adapter->PowerOn = FALSE;

    for (i = 0; i < RTL_NUMBER_OF(XtalSteps); i++)
    {
        if (!NT_SUCCESS(RtwWriteXtalSi(Adapter, XtalSteps[i].Offset, XtalSteps[i].Value, XtalSteps[i].Mask)))
            return;
    }

    RtwWrite32Set(Adapter, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
    RtwWrite32Clear(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite8(Adapter, R_AX_SYS_FUNC_EN, RtwRead8(Adapter, R_AX_SYS_FUNC_EN) & ~(B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB));

    RtwWrite32Clear(Adapter, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);
    if (!NT_SUCCESS(RtwWriteXtalSi(Adapter, XTAL_SI_ANAPAR_WL, 0x00, 0x20)))
        return;
    RtwWrite32Clear(Adapter, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);
    if (!NT_SUCCESS(RtwWriteXtalSi(Adapter, XTAL_SI_ANAPAR_WL, 0x00, 0x40)))
        return;

    RtwWrite32Set(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);
    if (!NT_SUCCESS(RtwPoll32(Adapter, R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC, 0, 20000)))
    {
        DPRINT1("RTW8852BS: MAC power-off request did not complete\n");
        return;
    }

    RtwWrite32Clear(Adapter, R_AX_SYS_PW_CTRL, B_AX_SOP_EDSWR | B_AX_SOP_EASWR);
    RtwWrite32Set(Adapter, R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
    RtwWrite32Mask(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x3);
    RtwWrite32(Adapter, R_AX_SYS_PW_CTRL,
               (RtwRead32(Adapter, R_AX_SYS_PW_CTRL) | B_AX_AFSM_WLSUS_EN) & ~B_AX_AFSM_PCIE_SUS_EN);
    RtwWrite32Set(Adapter, R_AX_SDIO_BUS_CTRL, B_AX_HCI_SUS_REQ);
    RtwWrite8(Adapter, R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_PWR_MAJOR);
}

NTSTATUS
RtwHardwareProbe(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    UCHAR Value;

    Adapter->ChipVersion = (UCHAR)((RtwRead32(Adapter, R_AX_SYS_CFG1) & B_AX_CHIP_VER_MASK) >> B_AX_CHIP_VER_SHIFT);
    if (Adapter->IoError)
        return STATUS_DEVICE_NOT_CONNECTED;

    Status = RtwPowerOn(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: power on failed 0x%08lx\n", Status);
        return Status;
    }

    if (NT_SUCCESS(RtwReadXtalSi(Adapter, XTAL_SI_CV, &Value)))
        Adapter->AnalogVersion = Value & 0x0F;

    Status = RtwReadEfuse(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwEnableFunctions(Adapter);

    DPRINT1("RTW8852BS: chip cut %u analog %u efuse %s power-k %s secure %02x%02x%s MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            Adapter->ChipVersion, Adapter->AnalogVersion,
            Adapter->EfuseValid ? "autoloaded" : "blank",
            Adapter->PowerCalibrated ? "yes" : "no",
            Adapter->SecureInfo[0], Adapter->SecureInfo[1], Adapter->SecureBoot ? " (signed)" : "",
            Adapter->PermanentAddress[0], Adapter->PermanentAddress[1],
            Adapter->PermanentAddress[2], Adapter->PermanentAddress[3],
            Adapter->PermanentAddress[4], Adapter->PermanentAddress[5]);
    return STATUS_SUCCESS;
}
