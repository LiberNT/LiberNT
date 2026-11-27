/*
 * PROJECT: LiberNT Realtek RTL8168/8111 Driver
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: 8168H MAC/PHY bring-up constants (hardware register values)
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 *
 * Values observed in Linux r8169, commit 8d7fe9885c62fc9a95bf55d9db244d9672a8aadc:
 *   r8169_main.c rtl_hw_start_8168h_1 L3869-L3926
 *   r8169_phy_config.c rtl8168h_2_hw_phy_config L796-L834
 */

#pragma once

#define RL_8168H_ERI_FIFO_RX 0x00080002u
#define RL_8168H_ERI_FIFO_TX 0x00100006u
#define RL_8168H_ERI_PAUSE_LO 0x38u
#define RL_8168H_ERI_PAUSE_HI 0x48u
#define RL_8168H_ERI_DC_SET 0x001cu
#define RL_8168H_ERI_EEE_CLR 0x1000u
#define RL_8168H_ERI_TX_LPI_CLR 0x0003u
#define RL_8168H_ERI_PKT_FILTER 0xdcu
#define RL_8168H_ERI_PKT_FILTER_PULSE 0x01u
#define RL_8168H_ERI_MAGIC 0x5f0u
#define RL_8168H_ERI_MAGIC_VAL 0x4f87u
#define RL_8168H_ERI_RX_EMPTY 0xc0u
#define RL_8168H_ERI_TX_EMPTY 0xb8u
#define RL_8168H_ERI_EEE_CTRL 0x1b0u
#define RL_8168H_ERI_L2L3 0x74u
#define RL_8168H_ERI_L2L3_CLR 0x07u
#define RL_8168H_ERI_FIFO_RX_ADDR 0xc8u
#define RL_8168H_ERI_FIFO_TX_ADDR 0xe8u
#define RL_8168H_ERI_PAUSE_LO_ADDR 0xccu
#define RL_8168H_ERI_PAUSE_HI_ADDR 0xd0u

#define RL_8168H_MACOCP_E056 0xe056u
#define RL_8168H_MACOCP_E056_CLR 0x00f0u
#define RL_8168H_MACOCP_E056_SET 0x0000u
#define RL_8168H_MACOCP_E052 0xe052u
#define RL_8168H_MACOCP_E052_CLR 0x6000u
#define RL_8168H_MACOCP_E052_SET 0x8008u
#define RL_8168H_MACOCP_E0D6 0xe0d6u
#define RL_8168H_MACOCP_E0D6_CLR 0x01ffu
#define RL_8168H_MACOCP_E0D6_SET 0x017fu
#define RL_8168H_MACOCP_D420 0xd420u
#define RL_8168H_MACOCP_D420_CLR 0x0fffu
#define RL_8168H_MACOCP_D420_SET 0x047fu
#define RL_8168H_MACOCP_D412 0xd412u
#define RL_8168H_MACOCP_D412_CLR 0x0fffu
#define RL_8168H_MACOCP_E63E 0xe63eu
#define RL_8168H_MACOCP_C094 0xc094u
#define RL_8168H_MACOCP_C09E 0xc09eu
#define RL_8168H_MACOCP_ADC_TRIG 0xdd02u
#define RL_8168H_MACOCP_ADC_TRIG_VAL 0x807du
#define RL_8168H_MACOCP_ADC_DATA 0xdd00u

#define RL_8168H_SAW_PAGE 0x0c42u
#define RL_8168H_SAW_REG 0x13u
#define RL_8168H_SAW_HZ 16000000u
#define RL_8168H_SAW_MASK 0x0fffu

#define RL_8168H_EPHY_1E_CLR 0x0800u
#define RL_8168H_EPHY_1E_SET 0x0001u
#define RL_8168H_EPHY_1D_SET 0x0800u
#define RL_8168H_EPHY_05_VAL 0x2089u
#define RL_8168H_EPHY_06_VAL 0x5881u
#define RL_8168H_EPHY_04_VAL 0x854au
#define RL_8168H_EPHY_01_VAL 0x068bu

#define RL_8168H_PHY_PAGE_A43 0x0a43u
#define RL_8168H_PHY_PAGE_A42 0x0a42u
#define RL_8168H_PHY_PAGE_A44 0x0a44u
#define RL_8168H_PHY_PAGE_BCF 0x0bcfu
#define RL_8168H_PHY_PAGE_BCD 0x0bcdu
#define RL_8168H_PHY_CHIN_EST 0x808au
#define RL_8168H_PHY_CHIN_EST_CLR 0x003fu
#define RL_8168H_PHY_CHIN_EST_SET 0x000au
#define RL_8168H_PHY_RTUNE 0x0811u
#define RL_8168H_PHY_RTUNE_SET 0x0800u
#define RL_8168H_PHY_PGA_SET 0x0002u
#define RL_8168H_PHY_GPHY10M_SET 0x0800u
#define RL_8168H_PHY_PFM_CLR 0x0080u
#define RL_8168H_PHY_PLL10M_CLR 0x0001u
#define RL_8168H_PHY_ALDPS_CLR 0x0004u
#define RL_8168H_PHY_ADC_INVALID 0x0fu
#define RL_8168H_PHY_TXLPF_BASE 3u

#define RL_8168H_GPHY_OCP 0x00b8u
#define RL_8168H_OCP_STD_PHY_BASE 0xa400u
#define RL_8168H_MCU_NOW_IS_OOB 0x80u
#define RL_8168H_MACOCP_LINK_LIST 0xe8deu
#define RL_8168H_MACOCP_LINK_LIST_HOLD 0x4000u
#define RL_8168H_MACOCP_LINK_LIST_GO 0x8000u
#define RL_8168H_RDESC_RXRWT 0x00400000u
#define RL_8168H_RDESC_RXRES 0x00200000u
#define RL_8168H_RDESC_RXRUNT 0x00100000u
#define RL_8168H_RDESC_RXCRC 0x00080000u
#define RL_8168H_RDESC_ERR_MASK (RL_8168H_RDESC_RXRWT | RL_8168H_RDESC_RXRES | \
                                 RL_8168H_RDESC_RXRUNT | RL_8168H_RDESC_RXCRC)
#define RL_8168H_RDESC_PID1 0x00040000u
#define RL_8168H_XID 0x540u
#define RL_8168H2_XID 0x541u
#define RL_8168H_HWREV 0x54000000u
#define RL_8168H2_HWREV 0x54100000u
#define RL_8168H_ASPM_ENTRY_LATENCY 0x27u
#define RL_8168H_PHY_EEE_CFG_BIT 0x0010u
#define RL_8168H_MMD_CTRL 0x0du
#define RL_8168H_MMD_DATA 0x0eu
#define RL_8168H_MMD_CTRL_NOINCR 0x4000u
#define RL_8168H_MDIO_MMD_AN 0x0007u
#define RL_8168H_MDIO_AN_EEE_ADV 0x003cu

static __inline USHORT
Rtl8168HPackAdcBiasNibble(
    _In_ USHORT HighBit,
    _In_ USHORT Low3)
{
    return (USHORT)(((HighBit & 1u) << 3) | (Low3 & 7u));
}

static __inline BOOLEAN
Rtl8168HAdcBiasFromMacWords(
    _In_ USHORT TrigWord,
    _In_ USHORT DataWord,
    _Out_ PUSHORT Packed)
{
    USHORT N3 = Rtl8168HPackAdcBiasNibble((USHORT)((TrigWord >> 7) & 1u),
                                          (USHORT)((DataWord >> 13) & 7u));
    USHORT N2 = (USHORT)((DataWord >> 9) & 0x0fu);
    USHORT N1 = (USHORT)((DataWord >> 5) & 0x0fu);
    USHORT N0 = Rtl8168HPackAdcBiasNibble((USHORT)((DataWord >> 4) & 1u),
                                          (USHORT)(DataWord & 7u));

    *Packed = (USHORT)((N3 << 12) | (N2 << 8) | (N1 << 4) | N0);
    return !(N3 == RL_8168H_PHY_ADC_INVALID &&
             N2 == RL_8168H_PHY_ADC_INVALID &&
             N1 == RL_8168H_PHY_ADC_INVALID &&
             N0 == RL_8168H_PHY_ADC_INVALID);
}

static __inline USHORT
Rtl8168HTxLpfCornerWord(
    _In_ USHORT RawNibble)
{
    USHORT Level = (USHORT)(RawNibble & 0x0fu);
    USHORT Corner = 0;

    if (Level > RL_8168H_PHY_TXLPF_BASE)
        Corner = (USHORT)(Level - RL_8168H_PHY_TXLPF_BASE);
    return (USHORT)(Corner | (Corner << 4) | (Corner << 8) | (Corner << 12));
}

static __inline USHORT
Rtl8168HSawTo1msCount(
    _In_ USHORT SawCount)
{
    if (SawCount == 0)
        return 0;
    return (USHORT)((RL_8168H_SAW_HZ / SawCount) & RL_8168H_SAW_MASK);
}
