/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL8852B MAC, SDIO and system register definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Register addresses and fields are hardware facts taken from the Realtek
 * rtw89 driver register map (GPL-2.0 OR BSD-3-Clause) and the Realtek
 * RTL8852BS halmac register map.
 */

#pragma once

#define RTW_BIT(n)                          (1UL << (n))

#define R_AX_SYS_ISO_CTRL                   0x0000
#define B_AX_PWC_EV2EF_B15                  RTW_BIT(15)
#define B_AX_PWC_EV2EF_B14                  RTW_BIT(14)
#define B_AX_ISO_EB2CORE                    RTW_BIT(8)

#define R_AX_SYS_FUNC_EN                    0x0002
#define B_AX_FEN_BB_GLB_RSTN                RTW_BIT(1)
#define B_AX_FEN_BBRSTB                     RTW_BIT(0)

#define R_AX_SYS_PW_CTRL                    0x0004
#define B_AX_SOP_EASWR                      RTW_BIT(30)
#define B_AX_SOP_EDSWR                      RTW_BIT(28)
#define B_AX_DIS_WLBT_PDNSUSEN_SOPC         RTW_BIT(18)
#define B_AX_RDY_SYSPWR                     RTW_BIT(17)
#define B_AX_EN_WLON                        RTW_BIT(16)
#define B_AX_APDM_HPDN                      RTW_BIT(15)
#define B_AX_AFSM_PCIE_SUS_EN               RTW_BIT(12)
#define B_AX_AFSM_WLSUS_EN                  RTW_BIT(11)
#define B_AX_APFM_SWLPS                     RTW_BIT(10)
#define B_AX_APFM_OFFMAC                    RTW_BIT(9)
#define B_AX_APFN_ONMAC                     RTW_BIT(8)

#define R_AX_SYS_WL_EFUSE_CTRL              0x000A
#define B_AX_AUTOLOAD_SUS                   RTW_BIT(5)

#define R_AX_SYS_SWR_CTRL1                  0x0010
#define B_AX_SYM_CTRL_SPS_PWMFREQ           RTW_BIT(10)

#define R_AX_SYS_ADIE_PAD_PWR_CTRL          0x0018
#define B_AX_SYM_PADPDN_WL_PTA_1P3          RTW_BIT(6)
#define B_AX_SYM_PADPDN_WL_RFC_1P3          RTW_BIT(5)

#define R_AX_SYS_CLK_CTRL                   0x0008
#define B_AX_CPU_CLK_EN                     RTW_BIT(14)

#define R_AX_RSV_CTRL                       0x001C
#define B_AX_R_DIS_PRST                     RTW_BIT(6)

#define R_AX_AFE_LDO_CTRL                   0x0020
#define B_AX_AON_OFF_PC_EN                  RTW_BIT(23)

#define R_AX_AFE_CTRL1                      0x0024
#define B_AX_CMAC_CLK_SEL                   RTW_BIT(21)

#define R_AX_EFUSE_CTRL                     0x0030
#define B_AX_EF_RDY                         RTW_BIT(29)
#define B_AX_EF_ADDR_SHIFT                  16
#define B_AX_EF_ADDR_MASK                   0x07FF0000UL

#define R_AX_EFUSE_CTRL_1_V1                0x0038
#define B_AX_EF_BURST                       RTW_BIT(19)

#define R_AX_GPIO_MUXCFG                    0x0040
#define B_AX_BOOT_MODE                      RTW_BIT(19)

#define R_AX_HCI_OPT_CTRL                   0x0074
#define B_AX_SDIO_DATA_PAD_SMT              RTW_BIT(19)

#define R_AX_PLATFORM_ENABLE                0x0088
#define B_AX_APB_WRAP_EN                    RTW_BIT(2)
#define B_AX_WCPU_EN                        RTW_BIT(1)
#define B_AX_PLATFORM_EN                    RTW_BIT(0)

#define R_AX_WLLPS_CTRL                     0x0090
#define B_AX_DIS_WLBT_LPSEN_LOPC            RTW_BIT(1)

#define R_AX_SCOREBOARD                     0x00AC
#define MAC_AX_NOTIFY_TP_MAJOR              0x81
#define MAC_AX_NOTIFY_PWR_MAJOR             0x80

#define R_AX_PMC_DBG_CTRL2                  0x00CC
#define B_AX_SYSON_DIS_PMCR_AX_WRMSK        RTW_BIT(2)

#define R_AX_HALT_H2C_CTRL                  0x0160
#define R_AX_HALT_C2H_CTRL                  0x0164
#define R_AX_HALT_H2C                       0x0168
#define R_AX_HALT_C2H                       0x016C

#define R_AX_WCPU_FW_CTRL                   0x01E0
#define B_AX_WCPU_FWDL_STS_SHIFT            5
#define B_AX_WCPU_FWDL_STS_MASK             0x000000E0UL
#define B_AX_FWDL_PATH_RDY                  RTW_BIT(2)
#define B_AX_H2C_PATH_RDY                   RTW_BIT(1)
#define B_AX_WCPU_FWDL_EN                   RTW_BIT(0)
#define RTW_FWDL_INITIAL_STATE              0
#define RTW_FWDL_CHECKSUM_FAIL              2
#define RTW_FWDL_SECURITY_FAIL              3
#define RTW_FWDL_CV_NOT_MATCH               4
#define RTW_FWDL_WCPU_FW_INIT_RDY           7

#define R_AX_BOOT_REASON                    0x01E6
#define B_AX_BOOT_REASON_MASK               0x0007

#define R_AX_UDM1                           0x01F4
#define R_AX_UDM2                           0x01F8

#define R_AX_SYS_CFG1                       0x00F0
#define B_AX_CHIP_VER_SHIFT                 12
#define B_AX_CHIP_VER_MASK                  0x0000F000UL

#define R_AX_SYS_STATUS1                    0x00F4
#define B_AX_AUTO_WLPON                     RTW_BIT(10)

#define R_AX_SPS_DIG_ON_CTRL0               0x0200
#define B_AX_VOL_L1_MASK                    0x0000000FUL
#define B_AX_VREFPFM_L_SHIFT                22
#define B_AX_VREFPFM_L_MASK                 0x03C00000UL
#define B_AX_REG_ZCDC_H_MASK                0x00060000UL

#define R_AX_WLAN_XTAL_SI_CTRL              0x0270
#define B_AX_WL_XTAL_SI_CMD_POLL            RTW_BIT(31)
#define B_AX_WL_XTAL_SI_MODE_SHIFT          24
#define B_AX_WL_XTAL_SI_BITMASK_SHIFT       16
#define B_AX_WL_XTAL_SI_DATA_SHIFT          8
#define XTAL_SI_NORMAL_WRITE                0x00
#define XTAL_SI_NORMAL_READ                 0x01

#define R_AX_EECS_EESK_FUNC_SEL             0x02D8
#define B_AX_PINMUX_EESK_FUNC_SEL_SHIFT     4
#define B_AX_PINMUX_EESK_FUNC_SEL_MASK      0x000000F0UL
#define PINMUX_EESK_FUNC_SEL_BT_LOG         0x1

#define R_AX_WLRF_CTRL                      0x02F0
#define B_AX_AFC_AFEDIG                     RTW_BIT(17)

#define R_AX_SEC_CTRL                       0x0C00
#define B_AX_SEC_IDMEM_SIZE_CONFIG_MASK     0x00030000UL

#define R_AX_IC_PWR_STATE                   0x03F0
#define B_AX_WLMAC_PWR_STE_SHIFT            8
#define B_AX_WLMAC_PWR_STE_MASK             0x00000300UL
#define MAC_AX_MAC_OFF                      0
#define MAC_AX_MAC_ON                       1
#define MAC_AX_MAC_LPS                      2

#define R_AX_SPS_DIG_OFF_CTRL0              0x0400
#define B_AX_C1_L1_MASK                     0x00000003UL
#define B_AX_C3_L1_SHIFT                    4
#define B_AX_C3_L1_MASK                     0x00000030UL

#define R_AX_SDIO_TX_CTRL                   0x1000
#define B_AX_CMD53_W_MIX                    RTW_BIT(14)
#define B_AX_CMD53_TX_FORMAT                RTW_BIT(13)
#define B_AX_RXINT_READ_MASK_DIS            RTW_BIT(3)

#define R_AX_SDIO_INDIRECT_ADDR             0x1040
#define R_AX_SDIO_INDIRECT_DATA             0x1044
#define R_AX_SDIO_INDIRECT_CTRL             0x1048
#define B_AX_INDIRECT_RDY                   RTW_BIT(31)
#define B_AX_INDIRECT_REG_R                 RTW_BIT(3)
#define B_AX_INDIRECT_REG_W                 RTW_BIT(2)

#define R_AX_SDIO_BUS_CTRL                  0x1084
#define B_AX_HCI_RESUME_RDY                 RTW_BIT(17)
#define B_AX_HCI_SUS_REQ                    RTW_BIT(16)
#define B_AX_EN_RPT_TXCRC                   RTW_BIT(9)

#define R_AX_SDIO_HIMR                      0x1100
#define R_AX_SDIO_HISR                      0x1104
#define R_AX_SDIO_RX_REQ_LEN                0x1108
#define B_AX_RX_REQ_LEN_MASK                0x0003FFFFUL
#define R_AX_SDIO_TXPG_WP                   0x1110
#define B_AX_SDIO_ACH12_AVAL_PG_SHIFT       16
#define B_AX_SDIO_AVAL_PG_MASK              0x1FFF

#define RTW_SDIO_TX_BASE                    0x10000
#define RTW_SDIO_TX_CHANNEL_SHIFT           12
#define RTW_SDIO_TX_LENGTH_MASK             0x0FFF
#define RTW_SDIO_RX_FIFO                    0x1F00

#define R_AX_HCI_FUNC_EN                    0x8380
#define B_AX_HCI_RXDMA_EN                   RTW_BIT(1)
#define B_AX_HCI_TXDMA_EN                   RTW_BIT(0)

#define R_AX_DMAC_FUNC_EN                   0x8400
#define B_AX_MAC_FUNC_EN                    RTW_BIT(30)
#define B_AX_DMAC_FUNC_EN                   RTW_BIT(29)
#define B_AX_MPDU_PROC_EN                   RTW_BIT(28)
#define B_AX_WD_RLS_EN                      RTW_BIT(27)
#define B_AX_DLE_WDE_EN                     RTW_BIT(26)
#define B_AX_TXPKT_CTRL_EN                  RTW_BIT(25)
#define B_AX_STA_SCH_EN                     RTW_BIT(24)
#define B_AX_DLE_PLE_EN                     RTW_BIT(23)
#define B_AX_PKT_BUF_EN                     RTW_BIT(22)
#define B_AX_DMAC_TBL_EN                    RTW_BIT(21)
#define B_AX_PKT_IN_EN                      RTW_BIT(20)
#define B_AX_DLE_CPUIO_EN                   RTW_BIT(19)
#define B_AX_DISPATCHER_EN                  RTW_BIT(18)
#define B_AX_BBRPT_EN                       RTW_BIT(17)
#define B_AX_MAC_SEC_EN                     RTW_BIT(16)
#define B_AX_DMACREG_GCKEN                  RTW_BIT(15)

#define R_AX_BOOT_DBG                       0x83F0

#define R_AX_DMAC_CLK_EN                    0x8404
#define B_AX_DLE_WDE_CLK_EN                 RTW_BIT(26)
#define B_AX_DLE_PLE_CLK_EN                 RTW_BIT(23)
#define B_AX_DISPATCHER_CLK_EN              RTW_BIT(18)

#define R_AX_HCI_FC_CTRL                    0x8A00
#define B_AX_HCI_FC_CH12_FULL_COND_MASK     0x00000C00UL
#define B_AX_HCI_FC_CH12_EN                 RTW_BIT(3)
#define B_AX_HCI_FC_EN                      RTW_BIT(0)

#define R_AX_CH_PAGE_CTRL                   0x8A04
#define B_AX_PREC_PAGE_CH12_SHIFT           16

#define R_AX_WDE_PKTBUF_CFG                 0x8C08
#define R_AX_WDE_QTA0_CFG                   0x8C40
#define R_AX_WDE_QTA1_CFG                   0x8C44
#define R_AX_WDE_QTA3_CFG                   0x8C4C
#define R_AX_WDE_QTA4_CFG                   0x8C50
#define R_AX_WDE_INI_STATUS                 0x8D00
#define R_AX_PLE_PKTBUF_CFG                 0x9008
#define R_AX_PLE_QTA0_CFG                   0x9040
#define R_AX_PLE_INI_STATUS                 0x9100
#define B_AX_DLE_FREE_PAGE_NUM_SHIFT        16
#define B_AX_DLE_START_BOUND_SHIFT          8
#define B_AX_DLE_QTA_MAX_SHIFT              16
#define RTW_DLE_MGN_INI_RDY                 0x00000003UL
#define RTW_DLE_BOUND_UNIT                  (8 * 1024)
#define RTW_WDE_PAGE_SEL_64                 0
#define RTW_PLE_PAGE_SEL_128                1

#define R_AX_CMAC_FUNC_EN                   0xC000
#define B_AX_CMAC_EN                        RTW_BIT(30)
#define B_AX_CMAC_TXEN                      RTW_BIT(29)
#define B_AX_CMAC_RXEN                      RTW_BIT(28)
#define B_AX_FORCE_CMACREG_GCKEN            RTW_BIT(15)
#define B_AX_PHYINTF_EN                     RTW_BIT(5)
#define B_AX_CMAC_DMA_EN                    RTW_BIT(4)
#define B_AX_PTCLTOP_EN                     RTW_BIT(3)
#define B_AX_SCHEDULER_EN                   RTW_BIT(2)
#define B_AX_TMAC_EN                        RTW_BIT(1)
#define B_AX_RMAC_EN                        RTW_BIT(0)

#define R_AX_CK_EN                          0xC004
#define R_AX_CMAC_REG_END                   0xFFFF
#define CMAC_CLK_ALLEN                      0xFFFFFFFFUL
#define MAC_AX_R32_DEAD                     0xDEADBEEFUL

#define XTAL_SI_XTAL_XMD_2                  0x24
#define XTAL_SI_XTAL_XMD_4                  0x26
#define XTAL_SI_CV                          0x41
#define XTAL_SI_WL_RFC_S0                   0x80
#define XTAL_SI_WL_RFC_S1                   0x81
#define XTAL_SI_ANAPAR_WL                   0x90
#define XTAL_SI_SRAM_CTRL                   0xA1

#define B_AX_DMAC_CRPRT                     RTW_BIT(31)
#define B_AX_MAC_SEC_CLK_EN                 RTW_BIT(16)
#define B_AX_DLE_CPUIO_CLK_EN               RTW_BIT(19)
#define B_AX_PKT_IN_CLK_EN                  RTW_BIT(20)
#define B_AX_STA_SCH_CLK_EN                 RTW_BIT(24)
#define B_AX_TXPKT_CTRL_CLK_EN              RTW_BIT(25)
#define B_AX_WD_RLS_CLK_EN                  RTW_BIT(27)
#define B_AX_BBRPT_CLK_EN                   RTW_BIT(17)
#define B_AX_CMAC_CRPRT                     RTW_BIT(31)
#define B_AX_CMAC_CKEN                      RTW_BIT(30)
#define B_AX_PHYINTF_CKEN                   RTW_BIT(5)
#define B_AX_CMAC_DMA_CKEN                  RTW_BIT(4)
#define B_AX_PTCLTOP_CKEN                   RTW_BIT(3)
#define B_AX_SCHEDULER_CKEN                 RTW_BIT(2)
#define B_AX_TMAC_CKEN                      RTW_BIT(1)
#define B_AX_RMAC_CKEN                      RTW_BIT(0)
#define B_AX_OCP_L1_MASK                    0xE000
#define R_AX_ACH0_PAGE_CTRL                 0x8A10
#define R_AX_ACH0_PAGE_INFO                 0x8A50
#define R_AX_PUB_PAGE_CTRL1                 0x8A90
#define R_AX_PUB_PAGE_CTRL2                 0x8A94
#define R_AX_PUB_PAGE_INFO1                 0x8A98
#define R_AX_PUB_PAGE_INFO2                 0x8A9C
#define R_AX_PUB_PAGE_INFO3                 0x8A8C
#define R_AX_WP_PAGE_CTRL1                  0x8AA0
#define R_AX_WP_PAGE_CTRL2                  0x8AA4
#define R_AX_WP_PAGE_INFO1                  0x8AA8
#define B_AX_GRP                            RTW_BIT(31)
#define B_AX_HCI_FC_MODE_MASK               6
#define R_AX_SS_CTRL                        0x9E10
#define B_AX_SS_EN                          RTW_BIT(0)
#define B_AX_SS_INIT_DONE_1                 RTW_BIT(31)
#define B_AX_SS_WARM_INIT_FLG               RTW_BIT(29)
#define B_AX_SS_NONEMPTY_SS2FINFO_EN        RTW_BIT(28)
#define R_AX_ACTION_FWD0                    0x9C04
#define TRXCFG_MPDU_PROC_ACT_FRWD           0x02A95A95UL
#define R_AX_TF_FWD                         0x9C14
#define TRXCFG_MPDU_PROC_TF_FRWD            0xAA55
#define R_AX_MPDU_PROC                      0x9C00
#define B_AX_APPEND_FCS                     RTW_BIT(0)
#define B_AX_A_ICV_ERR                      RTW_BIT(1)
#define R_AX_CUT_AMSDU_CTRL                 0x9C40
#define TRXCFG_MPDU_PROC_CUT_CTRL           0x010E05F0UL
#define R_AX_SEC_ENG_CTRL                   0x9D00
#define B_AX_CLK_EN_CGCMP                   RTW_BIT(10)
#define B_AX_CLK_EN_WAPI                    RTW_BIT(9)
#define B_AX_CLK_EN_WEP_TKIP                RTW_BIT(8)
#define B_AX_SEC_TX_ENC                     RTW_BIT(0)
#define B_AX_SEC_RX_DEC                     RTW_BIT(1)
#define B_AX_MC_DEC                         RTW_BIT(3)
#define B_AX_BC_DEC                         RTW_BIT(2)
#define B_AX_TX_PARTIAL_MODE                RTW_BIT(11)
#define B_AX_UC_MGNT_DEC                    RTW_BIT(4)
#define B_AX_BMC_MGNT_DEC                   RTW_BIT(5)
#define R_AX_SEC_MPDU_PROC                  0x9D04
#define B_AX_APPEND_ICV                     RTW_BIT(1)
#define B_AX_APPEND_MIC                     RTW_BIT(0)
#define R_AX_PREBKF_CFG_1                   0xC33C
#define B_AX_SIFS_MACTXEN_T1_MASK           0x007F
#define SIFS_MACTXEN_T1                     0x0047
#define R_AX_SCH_EXT_CTRL                   0xC3FC
#define B_AX_PORT_RST_TSF_ADV               RTW_BIT(1)
#define R_AX_CCA_CFG_0                      0xC340
#define B_AX_BTCCA_EN                       RTW_BIT(5)
#define R_AX_PREBKF_CFG_0                   0xC338
#define B_AX_PREBKF_TIME_MASK               0x001F
#define SCH_PREBKF_24US                     0x0018
#define R_AX_ADDR_CAM_CTRL                  0xCE34
#define B_AX_ADDR_CAM_RANGE_MASK            0x00FF0000UL
#define B_AX_ADDR_CAM_CLR                   RTW_BIT(8)
#define B_AX_ADDR_CAM_EN                    RTW_BIT(0)
#define R_AX_MGNT_FLTR                      0xCE28
#define R_AX_CTRL_FLTR                      0xCE24
#define R_AX_DATA_FLTR                      0xCE2C
#define RX_FLTR_FRAME_TO_HOST               0x55555555UL
#define R_AX_RX_FLTR_OPT                    0xCE20
#define B_AX_SNIFFER_MODE                   RTW_BIT(0)
#define B_AX_A_A1_MATCH                     RTW_BIT(1)
#define B_AX_A_BC                           RTW_BIT(2)
#define B_AX_A_MC                           RTW_BIT(3)
#define B_AX_A_UC_CAM_MATCH                 RTW_BIT(4)
#define B_AX_A_BC_CAM_MATCH                 RTW_BIT(5)
#define B_AX_A_MC_LIST_CAM_MATCH            RTW_BIT(6)
#define B_AX_A_BCN_CHK_EN                   RTW_BIT(7)
#define B_AX_A_BCN_CHK_RULE_MASK            0x0300
#define B_AX_A_PWR_MGNT                     RTW_BIT(10)
#define B_AX_A_CRC32_ERR                    RTW_BIT(11)
#define B_AX_A_FTM_REQ                      RTW_BIT(14)
#define B_AX_RX_MPDU_MAX_LEN_MASK           0x003F0000UL
#define B_AX_UID_FILTER_MASK                0xFF000000UL
#define R_AX_PLCP_HDR_FLTR                  0xCE04
#define B_AX_CCK_CRC_CHK                    RTW_BIT(0)
#define B_AX_CCK_SIG_CHK                    RTW_BIT(1)
#define B_AX_LSIG_PARITY_CHK_EN             RTW_BIT(2)
#define B_AX_SIGA_CRC_CHK                   RTW_BIT(3)
#define B_AX_VHT_SU_SIGB_CRC_CHK            RTW_BIT(4)
#define B_AX_VHT_MU_SIGB_CRC_CHK            RTW_BIT(5)
#define B_AX_HE_SIGB_CRC_CHK                RTW_BIT(6)
#define R_AX_CCA_CONTROL                    0xC390
#define B_AX_TB_CHK_TX_NAV                  RTW_BIT(31)
#define B_AX_TB_CHK_BASIC_NAV               RTW_BIT(30)
#define B_AX_TB_CHK_BTCCA                   RTW_BIT(29)
#define B_AX_TB_CHK_EDCCA                   RTW_BIT(28)
#define B_AX_TB_CHK_CCA_S80                 RTW_BIT(27)
#define B_AX_TB_CHK_CCA_S40                 RTW_BIT(26)
#define B_AX_TB_CHK_CCA_S20                 RTW_BIT(25)
#define B_AX_TB_CHK_CCA_P20                 RTW_BIT(24)
#define B_AX_SIFS_CHK_BTCCA                 RTW_BIT(21)
#define B_AX_SIFS_CHK_EDCCA                 RTW_BIT(20)
#define B_AX_SIFS_CHK_CCA_S80               RTW_BIT(19)
#define B_AX_SIFS_CHK_CCA_S40               RTW_BIT(18)
#define B_AX_SIFS_CHK_CCA_S20               RTW_BIT(17)
#define B_AX_SIFS_CHK_CCA_P20               RTW_BIT(16)
#define B_AX_CTN_CHK_TXNAV                  RTW_BIT(8)
#define B_AX_CTN_CHK_INTRA_NAV              RTW_BIT(7)
#define B_AX_CTN_CHK_BASIC_NAV              RTW_BIT(6)
#define B_AX_CTN_CHK_BTCCA                  RTW_BIT(5)
#define B_AX_CTN_CHK_EDCCA                  RTW_BIT(4)
#define B_AX_CTN_CHK_CCA_S80                RTW_BIT(3)
#define B_AX_CTN_CHK_CCA_S40                RTW_BIT(2)
#define B_AX_CTN_CHK_CCA_S20                RTW_BIT(1)
#define B_AX_CTN_CHK_CCA_P20                RTW_BIT(0)
#define R_AX_RSP_CHK_SIG                    0xCC00
#define B_AX_RSP_CHK_TXNAV                  RTW_BIT(19)
#define B_AX_RSP_CHK_INTRA_NAV              RTW_BIT(20)
#define B_AX_RSP_CHK_BASIC_NAV              RTW_BIT(21)
#define R_AX_TRXPTCL_RESP_0                 0xCC04
#define B_AX_RSP_CHK_SEC_CCA_80             RTW_BIT(28)
#define B_AX_RSP_CHK_SEC_CCA_40             RTW_BIT(27)
#define B_AX_RSP_CHK_SEC_CCA_20             RTW_BIT(26)
#define B_AX_RSP_CHK_BTCCA                  RTW_BIT(25)
#define B_AX_RSP_CHK_EDCCA                  RTW_BIT(24)
#define B_AX_RSP_CHK_CCA                    RTW_BIT(23)
#define B_AX_WMAC_SPEC_SIFS_CCK_MASK        0x00FF
#define WMAC_SPEC_SIFS_CCK                  0x000A
#define B_AX_WMAC_SPEC_SIFS_OFDM_MASK       0xFF00
#define WMAC_SPEC_SIFS_OFDM_52B             0x0011
#define R_AX_WMAC_NAV_CTL                   0xCC80
#define B_AX_WMAC_PLCP_UP_NAV_EN            RTW_BIT(17)
#define B_AX_WMAC_TF_UP_NAV_EN              RTW_BIT(16)
#define B_AX_WMAC_NAV_UPPER_EN              RTW_BIT(26)
#define B_AX_WMAC_NAV_UPPER_MASK            0xFF00
#define NAV_25MS                            0x00C4
#define R_AX_RX_SR_CTRL                     0xCE4A
#define B_AX_SR_EN                          RTW_BIT(0)
#define R_AX_BSSID_SRC_CTRL                 0xCE4B
#define B_AX_PLCP_SRC_EN                    RTW_BIT(0)
#define R_AX_MAC_LOOPBACK                   0xCC20
#define B_AX_MACLBK_EN                      RTW_BIT(0)
#define R_AX_TCR0                           0xCA00
#define B_AX_TCR_UDF_THSD_MASK              0x007F0000UL
#define TCR_UDF_THSD                        6
#define R_AX_TXD_FIFO_CTRL                  0xCA1C
#define B_AX_TXDFIFO_HIGH_MCS_THRE_MASK     0xF000
#define TXDFIFO_HIGH_MCS_THRE               7
#define B_AX_TXDFIFO_LOW_MCS_THRE_MASK      0x0F00
#define TXDFIFO_LOW_MCS_THRE                7
#define R_AX_RXTRIG_TEST_USER_2             0xCCB0
#define B_AX_RXTRIG_FCSCHK_EN               RTW_BIT(20)
#define R_AX_TRXPTCL_RRSR_CTL_0             0xCC08
#define B_AX_WMAC_RESP_REF_RATE_SEL         RTW_BIT(9)
#define B_AX_WMAC_RESP_RSC_MASK             0x0C00
#define R_AX_RESPBA_CAM_CTRL                0xCE3C
#define B_AX_BACAM_RST_MASK                 3
#define S_AX_BACAM_RST_ALL                  2
#define B_AX_SSN_SEL                        RTW_BIT(2)
#define R_AX_DLK_PROTECT_CTL                0xCE02
#define B_AX_RX_DLK_DATA_TIME_MASK          0x00F0
#define B_AX_RX_DLK_CCA_TIME_MASK           0xFF00
#define R_AX_RCR                            0xCE00
#define B_AX_CH_EN_MASK                     0x000F
#define R_AX_TX_SUB_CARRIER_VALUE           0xC088
#define R_AX_PTCL_RRSR1                     0xC090
#define B_AX_RRSR_RATE_EN_MASK              0x0F00
#define RRSR_OFDM_CCK_EN                    3
#define R_AX_PTCL_COMMON_SETTING_0          0xC600
#define B_AX_CMAC_TX_MODE_0                 RTW_BIT(0)
#define B_AX_CMAC_TX_MODE_1                 RTW_BIT(1)
#define B_AX_PTCL_TRIGGER_SS_EN_0           RTW_BIT(2)
#define B_AX_PTCL_TRIGGER_SS_EN_1           RTW_BIT(3)
#define B_AX_PTCL_TRIGGER_SS_EN_UL          RTW_BIT(4)
#define R_AX_PTCLRPT_FULL_HDL               0xC660
#define B_AX_SPE_RPT_PATH_MASK              0x0030
#define FWD_TO_WLCPU                        1
#define R_AX_AGG_LEN_VHT_0                  0xC618
#define B_AX_AMPDU_MAX_LEN_VHT_MASK         0x000FFFFFUL
#define R_AX_RXDMA_CTRL_0                   0xC804
#define R_AX_WDRLS_CFG                      0x9408
#define B_AX_WDRLS_MODE_MASK                3
#define R_AX_RLSRPT0_CFG0                   0x9410
#define B_AX_RLSRPT0_FLTR_MAP_MASK          0x0F000000UL
#define R_AX_RLSRPT0_CFG1                   0x9414
#define B_AX_RLSRPT0_AGGNUM_MASK            0x00FF
#define B_AX_RLSRPT0_TO_MASK                0x00FF0000UL
#define R_AX_PHYREG_SET                     0x8040
#define PHYREG_SET_XYN_CYCLE                0x000E
#define R_AX_RXDMA_SETTING                  0x8908
#define B_AX_HS0ISR_IND_EN                  RTW_BIT(16)
#define B_AX_RX_REQUEST_INT_EN              RTW_BIT(0)
#define R_AX_HIMR0                          0x01A0
#define R_AX_HISR0                          0x01A4
#define B_AX_HALT_C2H_INT_EN                RTW_BIT(21)
#define R_AX_RXAGG_0                        0x8900
#define B_AX_RXAGG_EN                       RTW_BIT(31)
#define B_AX_RXAGG_DMA_STORE                RTW_BIT(30)
#define B_AX_RXAGG_SW_EN                    RTW_BIT(29)
#define B_AX_RXAGG_PKTNUM_TH_SHIFT          16
#define B_AX_RXAGG_TIMEOUT_TH_SHIFT         8
#define B_AX_PLE_BURST_READ                 RTW_BIT(24)
#define RTW_RX_FULL_MODE                    0x003F
#define RTW_HCIFC_SDIO                      2
#define RTW_RPR_MODE_STF                    1
#define B_AX_MAX_PG_MASK                    0x1FFF0000UL
#define B_AX_PUBPG_G1_MASK                  0x1FFF0000UL

#define R_AX_SDIO_TXPG_0                    0x1114
#define R_AX_PORT_CFG_P0                    0xC400
#define B_AX_PORT_FUNC_EN                   RTW_BIT(2)
#define B_AX_BRK_SETUP                      RTW_BIT(16)
#define B_AX_TSFTR_RST                      RTW_BIT(5)
#define B_AX_TXBCN_RPT_EN                   RTW_BIT(1)
#define B_AX_RXBCN_RPT_EN                   RTW_BIT(0)
#define B_AX_NET_TYPE_MASK                  0x0C00
#define B_AX_TBTT_PROHIB_EN                 RTW_BIT(13)
#define B_AX_RX_BSSID_FIT_EN                RTW_BIT(4)
#define B_AX_TSF_UDT_EN                     RTW_BIT(3)
#define B_AX_BCNTX_EN                       RTW_BIT(12)
#define R_AX_BCN_CNT_TMR_P0                 0xC434
#define R_AX_TBTT_PROHIB_P0                 0xC404
#define B_AX_TBTT_SETUP_MASK                0x00FF
#define B_AX_TBTT_HOLD_MASK                 0x0FFF0000UL
#define R_AX_BCN_AREA_P0                    0xC408
#define B_AX_BCN_MSK_AREA_MASK              0x0FFF0000UL
#define R_AX_BCNERLYINT_CFG_P0              0xC40C
#define B_AX_BCNERLY_MASK                   0x0FFF
#define R_AX_TBTTERLYINT_CFG_P0             0xC40E
#define B_AX_TBTTERLY_MASK                  0x0FFF
#define R_AX_TBTT_AGG_P0                    0xC412
#define B_AX_TBTT_AGG_NUM_MASK              0xFF00
#define R_AX_BCN_SPACE_CFG_P0               0xC414
#define B_AX_BCN_SPACE_MASK                 0xFFFF
#define R_AX_DTIM_CTRL_P0                   0xC426
#define B_AX_DTIM_NUM_MASK                  0xFF00
#define R_AX_MD_TSFT_STMP_CTL               0xCA08
#define B_AX_UPD_HGQMD                      RTW_BIT(1)
#define B_AX_UPD_TIMIE                      RTW_BIT(0)
#define R_AX_PTCL_BSS_COLOR_0               0xC6A0
#define B_AX_BSS_COLOB_AX_PORT_0_MASK       0x003F
#define R_AX_MBSSID_CTRL                    0xC568
#define B_AX_P0MB_ALL_MASK                  0x00FFFFFEUL
#define R_AX_MBSSID_DROP_0                  0xC63C
#define B_AX_PORT_DROP_4_0_MASK             0x001F0000UL
#define R_AX_P0MB_HGQ_WINDOW_CFG_0          0xC590
#define R_AX_BCN_PSR_RPT_P0                 0xCE84
#define B_AX_BCAID_P0_MASK                  0x07FF
#define R_AX_FILTER_MODEL_ADDR              0x0C04
#define R_AX_INDIR_ACCESS_ENTRY             0x00040000UL
#define DMAC_TBL_BASE_ADDR                  0x18800000UL
#define CMAC_TBL_BASE_ADDR                  0x18840000UL
#define CCTL_INFO_SIZE                      0x0020
