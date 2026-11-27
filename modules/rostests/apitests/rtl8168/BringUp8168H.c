/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin RTL8168H MAC bring-up constants against published hw facts
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>
#include "rtl8168_hwvals.h"

#define RL_RXCFG_EARLYOFFV2 0x00000800u
#define RL_RXCFG_MAXDMA_UNLIMITED 0x00000700u
#define RL_RXCFG_FIFO_NOTHRESH 0x0000E000u
#define RL_RXCFG_RX128_INT 0x00008000u
#define RL_RXCFG_RX_MULTI_EN 0x00004000u
#define RL_RXCFG_BASE (RL_RXCFG_MAXDMA_UNLIMITED | RL_RXCFG_FIFO_NOTHRESH)
#define RL_RXCFG_GPLUS_BASE (RL_RXCFG_BASE | RL_RXCFG_EARLYOFFV2 | \
                             RL_RXCFG_RX128_INT | RL_RXCFG_RX_MULTI_EN)
#define RTL8168_RX_MAX_SIZE 0x3FFFu

START_TEST(BringUp8168H)
{
    ok(RL_8168H_ERI_FIFO_RX == 0x00080002u, "rx fifo eri value\n");
    ok(RL_8168H_ERI_FIFO_TX == 0x00100006u, "tx fifo eri value\n");
    ok(RL_8168H_ERI_PAUSE_LO == 0x38u, "pause low\n");
    ok(RL_8168H_ERI_PAUSE_HI == 0x48u, "pause high\n");
    ok(RL_8168H_ERI_DC_SET == 0x001cu, "packet filter dc bits\n");
    ok(RL_8168H_ERI_EEE_CLR == 0x1000u, "eee mac clear bit12\n");
    ok(RL_8168H_ERI_TX_LPI_CLR == 0x0003u, "tx-lpi eri bits\n");
    ok(RL_8168H_MACOCP_E056_CLR == 0x00f0u, "e056 clear mask\n");
    ok(RL_8168H_MACOCP_E056_SET == 0x0000u, "e056 set value\n");
    ok(RL_8168H_MACOCP_E052_CLR == 0x6000u, "e052 clear mask\n");
    ok(RL_8168H_MACOCP_E052_SET == 0x8008u, "e052 set value\n");
    ok(RL_RXCFG_GPLUS_BASE == 0x0000ef00u, "8168G+ rxcfg base\n");
    ok(RTL8168_RX_MAX_SIZE == 0x3FFFu, "rx max size\n");
    ok(RL_8168H_GPHY_OCP == 0x00b8u, "gphy ocp mmio\n");
    ok(RL_8168H_OCP_STD_PHY_BASE == 0xa400u, "std phy ocp base\n");
    ok(RL_8168H_MCU_NOW_IS_OOB == 0x80u, "mcu now-is-oob\n");
    ok(RL_8168H_MACOCP_LINK_LIST == 0xe8deu, "link-list ocp\n");
    ok(RL_8168H_MACOCP_LINK_LIST_HOLD == 0x4000u, "link-list hold\n");
    ok(RL_8168H_MACOCP_LINK_LIST_GO == 0x8000u, "link-list go\n");
    ok(RL_8168H_RDESC_ERR_MASK == 0x00780000u, "rx error bits\n");
    ok((RL_8168H_RDESC_ERR_MASK & RL_8168H_RDESC_PID1) == 0,
       "udp pid1 must not be an rx error\n");
    ok(RL_8168H_ASPM_ENTRY_LATENCY == 0x27u, "aspm l0/l1 entry latency\n");
    ok(RL_8168H_PHY_EEE_CFG_BIT == 0x0010u, "phy eee cfg clear bit\n");
    ok(RL_8168H_MDIO_MMD_AN == 0x0007u, "mmd an device\n");
    ok(RL_8168H_MDIO_AN_EEE_ADV == 0x003cu, "eee advert mmd reg\n");
    ok(RL_8168H_XID == 0x540u, "8168h xid\n");
    ok(RL_8168H2_XID == 0x541u, "8168h-2 xid\n");
    ok(RL_8168H_HWREV == 0x54000000u, "8168h hwrev\n");
    ok(RL_8168H2_HWREV == 0x54100000u, "8168h-2 hwrev\n");
    ok(Rtl8168HSawTo1msCount(0) == 0, "saw zero yields zero\n");
    ok(Rtl8168HSawTo1msCount(16000) == 1000, "saw 16k -> 1ms count\n");
    ok(Rtl8168HTxLpfCornerWord(0x0002) == 0x0000, "txlpf below base\n");
    ok(Rtl8168HTxLpfCornerWord(0x0005) == 0x2222, "txlpf corner fill\n");
    {
        USHORT Packed = 0;
        ok(Rtl8168HAdcBiasFromMacWords(0x0080, 0xffff, &Packed) == FALSE,
           "all-ones adc bias invalid\n");
        ok(Rtl8168HAdcBiasFromMacWords(0x0000, 0x0000, &Packed) == TRUE,
           "zero adc bias valid\n");
        ok(Packed == 0x0000, "zero adc bias packs to zero\n");
    }
}
