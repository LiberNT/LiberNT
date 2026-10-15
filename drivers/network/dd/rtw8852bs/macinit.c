/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MAC data path, scheduler and protocol initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

#define RTW_DMA_CH_COUNT                12
#define RTW_DMA_CH_UNUSED               ((1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10) | (1u << 11))

typedef struct _RTW_HFC_CHANNEL
{
    USHORT Min;
    USHORT Max;
    BOOLEAN Group1;
} RTW_HFC_CHANNEL;

static const RTW_HFC_CHANNEL RtwHfcChannels[RTW_DMA_CH_COUNT] =
{
    { 2, 102, FALSE }, { 2, 102, FALSE }, { 2, 102, FALSE }, { 2, 102, FALSE },
    { 0, 0, TRUE }, { 0, 0, TRUE }, { 0, 0, TRUE }, { 0, 0, TRUE },
    { 2, 102, FALSE }, { 2, 102, FALSE }, { 0, 0, TRUE }, { 0, 0, TRUE },
};

static
ULONG
RtwFieldShift(
    _In_ ULONG Mask)
{
    ULONG Shift = 0;

    while (Shift < 32 && !(Mask & (1UL << Shift)))
        Shift++;
    return Shift;
}

static
VOID
RtwWrite8Mask(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ UCHAR Mask,
    _In_ UCHAR Value)
{
    UCHAR Current = RtwRead8(Adapter, Address);

    RtwWrite8(Adapter, Address, (UCHAR)((Current & ~Mask) | ((Value << RtwFieldShift(Mask)) & Mask)));
}

static
VOID
RtwWrite16Mask(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Mask,
    _In_ USHORT Value)
{
    USHORT Current = RtwRead16(Adapter, Address);

    RtwWrite16(Adapter, Address, (USHORT)((Current & ~Mask) | ((Value << RtwFieldShift(Mask)) & Mask)));
}

static
NTSTATUS
RtwEnableBbRf(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;

    RtwWrite8(Adapter, R_AX_SYS_FUNC_EN, RtwRead8(Adapter, R_AX_SYS_FUNC_EN) | B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
    RtwWrite32Mask(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);
    RtwWrite32Set(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite32Clear(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite32Set(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

    Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S0, 0xC7, 0xFF);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S1, 0xC7, 0xFF);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite8(Adapter, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);
    return STATUS_SUCCESS;
}

static
VOID
RtwSysInit(
    _In_ PRTW_ADAPTER Adapter)
{
    RtwWrite32(Adapter, R_AX_DMAC_FUNC_EN,
               B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MAC_SEC_EN |
               B_AX_DISPATCHER_EN | B_AX_DLE_CPUIO_EN | B_AX_PKT_IN_EN |
               B_AX_DMAC_TBL_EN | B_AX_PKT_BUF_EN | B_AX_STA_SCH_EN |
               B_AX_TXPKT_CTRL_EN | B_AX_WD_RLS_EN | B_AX_MPDU_PROC_EN |
               B_AX_DMAC_CRPRT);
    RtwWrite32(Adapter, R_AX_DMAC_CLK_EN,
               B_AX_MAC_SEC_CLK_EN | B_AX_DISPATCHER_CLK_EN |
               B_AX_DLE_CPUIO_CLK_EN | B_AX_PKT_IN_CLK_EN |
               B_AX_STA_SCH_CLK_EN | B_AX_TXPKT_CTRL_CLK_EN |
               B_AX_WD_RLS_CLK_EN | B_AX_BBRPT_CLK_EN);

    RtwWrite32Set(Adapter, R_AX_CK_EN,
                  B_AX_CMAC_CKEN | B_AX_PHYINTF_CKEN | B_AX_CMAC_DMA_CKEN |
                  B_AX_PTCLTOP_CKEN | B_AX_SCHEDULER_CKEN | B_AX_TMAC_CKEN |
                  B_AX_RMAC_CKEN);
    RtwWrite32Set(Adapter, R_AX_CMAC_FUNC_EN,
                  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
                  B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN | B_AX_PTCLTOP_EN |
                  B_AX_SCHEDULER_EN | B_AX_TMAC_EN | B_AX_RMAC_EN |
                  B_AX_CMAC_CRPRT);

    RtwWrite32Set(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_OCP_L1_MASK);
}

static
NTSTATUS
RtwInitDle(
    _In_ PRTW_ADAPTER Adapter)
{
    static const USHORT WdeQuota[] = { 112, 8, 0, 6 };
    static const ULONG WdeQuotaRegister[] =
    {
        R_AX_WDE_QTA0_CFG, R_AX_WDE_QTA1_CFG, R_AX_WDE_QTA3_CFG, R_AX_WDE_QTA4_CFG,
    };
    static const USHORT PleMin[] = { 320, 0, 16, 16, 26, 0, 178, 0, 16, 1, 8 };
    static const USHORT PleMax[] = { 320, 0, 123, 16, 133, 0, 285, 0, 123, 1, 24 };
    const ULONG WdeLinkPages = 126, WdeUnlinkPages = 2, WdePageSize = 64;
    const ULONG PleLinkPages = 688;
    ULONG Value, Bound, i;
    NTSTATUS Status;

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
        RtwWrite32(Adapter, WdeQuotaRegister[i], WdeQuota[i] | ((ULONG)WdeQuota[i] << B_AX_DLE_QTA_MAX_SHIFT));
    for (i = 0; i < RTL_NUMBER_OF(PleMin); i++)
        RtwWrite32(Adapter, R_AX_PLE_QTA0_CFG + i * 4, PleMin[i] | ((ULONG)PleMax[i] << B_AX_DLE_QTA_MAX_SHIFT));

    RtwWrite32Set(Adapter, R_AX_DMAC_FUNC_EN, B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);

    Status = RtwPoll32(Adapter, R_AX_WDE_INI_STATUS, RTW_DLE_MGN_INI_RDY, RTW_DLE_MGN_INI_RDY, 2000);
    if (NT_SUCCESS(Status))
        Status = RtwPoll32(Adapter, R_AX_PLE_INI_STATUS, RTW_DLE_MGN_INI_RDY, RTW_DLE_MGN_INI_RDY, 2000);
    if (!NT_SUCCESS(Status))
        DPRINT1("RTW8852BS: DLE not ready WDE 0x%08lx PLE 0x%08lx\n",
                RtwRead32(Adapter, R_AX_WDE_INI_STATUS), RtwRead32(Adapter, R_AX_PLE_INI_STATUS));
    return Status;
}

static
VOID
RtwInitHfc(
    _In_ PRTW_ADAPTER Adapter)
{
    const ULONG PublicGroup0 = 112, PublicGroup1 = 0, PublicMax = 112, WpThreshold = 0;
    const ULONG Ch011Precost = 1, H2cPrecost = 40;
    ULONG Channel, Value;

    RtwWrite32Clear(Adapter, R_AX_HCI_FC_CTRL, B_AX_HCI_FC_EN | B_AX_HCI_FC_CH12_EN);

    for (Channel = 0; Channel < RTW_DMA_CH_COUNT; Channel++)
    {
        if (RTW_DMA_CH_UNUSED & (1u << Channel))
            continue;
        Value = RtwHfcChannels[Channel].Min |
                ((ULONG)RtwHfcChannels[Channel].Max << RtwFieldShift(B_AX_MAX_PG_MASK)) |
                (RtwHfcChannels[Channel].Group1 ? B_AX_GRP : 0);
        RtwWrite32(Adapter, R_AX_ACH0_PAGE_CTRL + Channel * 4, Value);
    }

    RtwWrite32(Adapter, R_AX_PUB_PAGE_CTRL1, PublicGroup0 | (PublicGroup1 << RtwFieldShift(B_AX_PUBPG_G1_MASK)));
    RtwWrite32(Adapter, R_AX_WP_PAGE_CTRL2, WpThreshold);

    RtwWrite32(Adapter, R_AX_CH_PAGE_CTRL, Ch011Precost | (H2cPrecost << B_AX_PREC_PAGE_CH12_SHIFT));
    RtwWrite32(Adapter, R_AX_PUB_PAGE_CTRL2, PublicMax);
    RtwWrite32(Adapter, R_AX_WP_PAGE_CTRL1, 0);

    Value = RtwRead32(Adapter, R_AX_HCI_FC_CTRL);
    Value &= ~(B_AX_HCI_FC_MODE_MASK | 0x00000FF0UL);
    Value |= RTW_HCIFC_SDIO << RtwFieldShift(B_AX_HCI_FC_MODE_MASK);
    RtwWrite32(Adapter, R_AX_HCI_FC_CTRL, Value);

    RtwWrite32Set(Adapter, R_AX_HCI_FC_CTRL, B_AX_HCI_FC_EN | B_AX_HCI_FC_CH12_EN);
    KeStallExecutionProcessor(10);
}

static
NTSTATUS
RtwInitDmac(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG Value;

    Status = RtwInitDle(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwInitHfc(Adapter);

    RtwWrite8(Adapter, R_AX_SS_CTRL, RtwRead8(Adapter, R_AX_SS_CTRL) | B_AX_SS_EN);
    Status = RtwPoll32(Adapter, R_AX_SS_CTRL, B_AX_SS_INIT_DONE_1, B_AX_SS_INIT_DONE_1, 2000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: station scheduler did not initialize\n");
        return Status;
    }
    RtwWrite32Set(Adapter, R_AX_SS_CTRL, B_AX_SS_WARM_INIT_FLG);
    RtwWrite32Clear(Adapter, R_AX_SS_CTRL, B_AX_SS_NONEMPTY_SS2FINFO_EN);

    RtwWrite32(Adapter, R_AX_ACTION_FWD0, TRXCFG_MPDU_PROC_ACT_FRWD);
    RtwWrite32(Adapter, R_AX_TF_FWD, TRXCFG_MPDU_PROC_TF_FRWD);
    RtwWrite32Set(Adapter, R_AX_MPDU_PROC, B_AX_APPEND_FCS | B_AX_A_ICV_ERR);
    RtwWrite32(Adapter, R_AX_CUT_AMSDU_CTRL, TRXCFG_MPDU_PROC_CUT_CTRL);

    Value = RtwRead32(Adapter, R_AX_SEC_ENG_CTRL);
    Value |= B_AX_CLK_EN_CGCMP | B_AX_CLK_EN_WAPI | B_AX_CLK_EN_WEP_TKIP |
             B_AX_SEC_TX_ENC | B_AX_SEC_RX_DEC | B_AX_MC_DEC | B_AX_BC_DEC;
    Value &= ~B_AX_TX_PARTIAL_MODE;
    RtwWrite32(Adapter, R_AX_SEC_ENG_CTRL, Value);
    RtwWrite32Set(Adapter, R_AX_SEC_MPDU_PROC, B_AX_APPEND_ICV | B_AX_APPEND_MIC);

    return STATUS_SUCCESS;
}

static
NTSTATUS
RtwInitCmac(
    _In_ PRTW_ADAPTER Adapter)
{
    const ULONG RxMaxLength = min(127UL * 128, 11454UL + 512) / 512;
    NTSTATUS Status;
    ULONG Value;

    RtwWrite32Mask(Adapter, R_AX_PREBKF_CFG_1, B_AX_SIFS_MACTXEN_T1_MASK, SIFS_MACTXEN_T1);
    RtwWrite32Set(Adapter, R_AX_SCH_EXT_CTRL, B_AX_PORT_RST_TSF_ADV);
    RtwWrite32Clear(Adapter, R_AX_CCA_CFG_0, B_AX_BTCCA_EN);
    RtwWrite32Mask(Adapter, R_AX_PREBKF_CFG_0, B_AX_PREBKF_TIME_MASK, SCH_PREBKF_24US);

    RtwWrite32Set(Adapter, R_AX_ADDR_CAM_CTRL,
                  (0x7FUL << RtwFieldShift(B_AX_ADDR_CAM_RANGE_MASK)) | B_AX_ADDR_CAM_CLR | B_AX_ADDR_CAM_EN);
    Status = RtwPoll32(Adapter, R_AX_ADDR_CAM_CTRL, B_AX_ADDR_CAM_CLR, 0, 2000);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: address CAM did not reset\n");
        return Status;
    }

    RtwWrite32(Adapter, R_AX_MGNT_FLTR, RX_FLTR_FRAME_TO_HOST);
    RtwWrite32(Adapter, R_AX_CTRL_FLTR, RX_FLTR_FRAME_TO_HOST);
    RtwWrite32(Adapter, R_AX_DATA_FLTR, RX_FLTR_FRAME_TO_HOST);
    Adapter->RxFilter = B_AX_A_A1_MATCH | B_AX_A_BC | B_AX_A_MC | B_AX_A_UC_CAM_MATCH |
                        B_AX_A_BC_CAM_MATCH | B_AX_A_PWR_MGNT | B_AX_A_FTM_REQ |
                        (3UL << RtwFieldShift(B_AX_UID_FILTER_MASK)) | B_AX_A_BCN_CHK_EN;
    RtwWrite32(Adapter, R_AX_RX_FLTR_OPT, Adapter->RxFilter);
    RtwWrite16(Adapter, R_AX_PLCP_HDR_FLTR,
               B_AX_CCK_CRC_CHK | B_AX_CCK_SIG_CHK | B_AX_LSIG_PARITY_CHK_EN |
               B_AX_SIGA_CRC_CHK | B_AX_VHT_SU_SIGB_CRC_CHK | B_AX_VHT_MU_SIGB_CRC_CHK |
               B_AX_HE_SIGB_CRC_CHK);

    Value = RtwRead32(Adapter, R_AX_CCA_CONTROL);
    Value |= B_AX_TB_CHK_BASIC_NAV | B_AX_TB_CHK_BTCCA | B_AX_TB_CHK_EDCCA | B_AX_TB_CHK_CCA_P20 |
             B_AX_SIFS_CHK_BTCCA | B_AX_SIFS_CHK_CCA_P20 | B_AX_CTN_CHK_INTRA_NAV |
             B_AX_CTN_CHK_BASIC_NAV | B_AX_CTN_CHK_BTCCA | B_AX_CTN_CHK_EDCCA |
             B_AX_CTN_CHK_CCA_S80 | B_AX_CTN_CHK_CCA_S40 | B_AX_CTN_CHK_CCA_S20 |
             B_AX_CTN_CHK_CCA_P20;
    Value &= ~(B_AX_TB_CHK_TX_NAV | B_AX_TB_CHK_CCA_S80 | B_AX_TB_CHK_CCA_S40 |
               B_AX_TB_CHK_CCA_S20 | B_AX_SIFS_CHK_CCA_S80 | B_AX_SIFS_CHK_CCA_S40 |
               B_AX_SIFS_CHK_CCA_S20 | B_AX_CTN_CHK_TXNAV | B_AX_SIFS_CHK_EDCCA);
    RtwWrite32(Adapter, R_AX_CCA_CONTROL, Value);
    RtwWrite32Clear(Adapter, R_AX_RSP_CHK_SIG, B_AX_RSP_CHK_TXNAV | B_AX_RSP_CHK_INTRA_NAV | B_AX_RSP_CHK_BASIC_NAV);
    RtwWrite32Clear(Adapter, R_AX_TRXPTCL_RESP_0,
                    B_AX_RSP_CHK_SEC_CCA_80 | B_AX_RSP_CHK_SEC_CCA_40 | B_AX_RSP_CHK_SEC_CCA_20 |
                    B_AX_RSP_CHK_BTCCA | B_AX_RSP_CHK_EDCCA | B_AX_RSP_CHK_CCA);

    RtwWrite32Set(Adapter, R_AX_WMAC_NAV_CTL, B_AX_WMAC_PLCP_UP_NAV_EN | B_AX_WMAC_TF_UP_NAV_EN | B_AX_WMAC_NAV_UPPER_EN);
    RtwWrite32Mask(Adapter, R_AX_WMAC_NAV_CTL, B_AX_WMAC_NAV_UPPER_MASK, NAV_25MS);

    RtwWrite8(Adapter, R_AX_RX_SR_CTRL, RtwRead8(Adapter, R_AX_RX_SR_CTRL) & ~B_AX_SR_EN);
    RtwWrite8(Adapter, R_AX_BSSID_SRC_CTRL, RtwRead8(Adapter, R_AX_BSSID_SRC_CTRL) | B_AX_PLCP_SRC_EN);

    RtwWrite32Clear(Adapter, R_AX_MAC_LOOPBACK, B_AX_MACLBK_EN);
    RtwWrite32Mask(Adapter, R_AX_TCR0, B_AX_TCR_UDF_THSD_MASK, TCR_UDF_THSD);
    RtwWrite32Mask(Adapter, R_AX_TXD_FIFO_CTRL, B_AX_TXDFIFO_HIGH_MCS_THRE_MASK, TXDFIFO_HIGH_MCS_THRE);
    RtwWrite32Mask(Adapter, R_AX_TXD_FIFO_CTRL, B_AX_TXDFIFO_LOW_MCS_THRE_MASK, TXDFIFO_LOW_MCS_THRE);

    Value = RtwRead32(Adapter, R_AX_TRXPTCL_RESP_0);
    Value = (Value & ~(B_AX_WMAC_SPEC_SIFS_CCK_MASK | B_AX_WMAC_SPEC_SIFS_OFDM_MASK)) |
            WMAC_SPEC_SIFS_CCK | (WMAC_SPEC_SIFS_OFDM_52B << RtwFieldShift(B_AX_WMAC_SPEC_SIFS_OFDM_MASK));
    RtwWrite32(Adapter, R_AX_TRXPTCL_RESP_0, Value);
    RtwWrite32Set(Adapter, R_AX_RXTRIG_TEST_USER_2, B_AX_RXTRIG_FCSCHK_EN);
    RtwWrite32Clear(Adapter, R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_REF_RATE_SEL);
    RtwWrite32Mask(Adapter, R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_RSC_MASK, 2);

    RtwWrite32Mask(Adapter, R_AX_RESPBA_CAM_CTRL, B_AX_BACAM_RST_MASK, S_AX_BACAM_RST_ALL);
    Status = RtwPoll32(Adapter, R_AX_RESPBA_CAM_CTRL, B_AX_BACAM_RST_MASK, 0, 1000);
    if (!NT_SUCCESS(Status))
        DPRINT1("RTW8852BS: BA CAM did not reset\n");
    RtwWrite8(Adapter, R_AX_RESPBA_CAM_CTRL, RtwRead8(Adapter, R_AX_RESPBA_CAM_CTRL) | B_AX_SSN_SEL);
    RtwWrite16Mask(Adapter, R_AX_DLK_PROTECT_CTL, B_AX_RX_DLK_DATA_TIME_MASK, 15);
    RtwWrite16Mask(Adapter, R_AX_DLK_PROTECT_CTL, B_AX_RX_DLK_CCA_TIME_MASK, 32);
    RtwWrite8Mask(Adapter, R_AX_RCR, B_AX_CH_EN_MASK, 0x1);
    RtwWrite32Mask(Adapter, R_AX_RX_FLTR_OPT, B_AX_RX_MPDU_MAX_LEN_MASK, RxMaxLength);
    Adapter->RxFilter = (Adapter->RxFilter & ~B_AX_RX_MPDU_MAX_LEN_MASK) | (RxMaxLength << RtwFieldShift(B_AX_RX_MPDU_MAX_LEN_MASK));
    RtwWrite8(Adapter, R_AX_PLCP_HDR_FLTR, RtwRead8(Adapter, R_AX_PLCP_HDR_FLTR) & ~B_AX_VHT_SU_SIGB_CRC_CHK);

    RtwWrite32Clear(Adapter, R_AX_TX_SUB_CARRIER_VALUE, 0x0FFF);
    RtwWrite32Mask(Adapter, R_AX_PTCL_RRSR1, B_AX_RRSR_RATE_EN_MASK, RRSR_OFDM_CCK_EN);

    RtwWrite8(Adapter, R_AX_PTCL_COMMON_SETTING_0,
              (RtwRead8(Adapter, R_AX_PTCL_COMMON_SETTING_0) | B_AX_CMAC_TX_MODE_0 | B_AX_CMAC_TX_MODE_1) &
              ~(B_AX_PTCL_TRIGGER_SS_EN_0 | B_AX_PTCL_TRIGGER_SS_EN_1 | B_AX_PTCL_TRIGGER_SS_EN_UL));
    RtwWrite8Mask(Adapter, R_AX_PTCLRPT_FULL_HDL, B_AX_SPE_RPT_PATH_MASK, FWD_TO_WLCPU);
    RtwWrite32Mask(Adapter, R_AX_AGG_LEN_VHT_0, B_AX_AMPDU_MAX_LEN_VHT_MASK, 0x3FF80);

    RtwWrite8(Adapter, R_AX_RXDMA_CTRL_0, RtwRead8(Adapter, R_AX_RXDMA_CTRL_0) & ~RTW_RX_FULL_MODE);
    return STATUS_SUCCESS;
}

NTSTATUS
RtwMacInit(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = RtwEnableBbRf(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwSysInit(Adapter);

    Status = RtwInitDmac(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtwInitCmac(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite32Mask(Adapter, R_AX_WDRLS_CFG, B_AX_WDRLS_MODE_MASK, RTW_RPR_MODE_STF);
    RtwWrite32Clear(Adapter, R_AX_RLSRPT0_CFG0, B_AX_RLSRPT0_FLTR_MAP_MASK);
    RtwWrite32Mask(Adapter, R_AX_RLSRPT0_CFG1, B_AX_RLSRPT0_AGGNUM_MASK, 30);
    RtwWrite32Mask(Adapter, R_AX_RLSRPT0_CFG1, B_AX_RLSRPT0_TO_MASK, 255);

    RtwWrite32Clear(Adapter, R_AX_RXDMA_SETTING, B_AX_PLE_BURST_READ);
    RtwWrite32(Adapter, R_AX_RXAGG_0, B_AX_RXAGG_DMA_STORE | (RtwRead32(Adapter, R_AX_RXAGG_0) & B_AX_RXAGG_SW_EN));

    RtwWrite32(Adapter, R_AX_SDIO_HISR, RtwRead32(Adapter, R_AX_SDIO_HISR));
    RtwWrite32(Adapter, R_AX_HISR0, RtwRead32(Adapter, R_AX_HISR0));
    return STATUS_SUCCESS;
}
