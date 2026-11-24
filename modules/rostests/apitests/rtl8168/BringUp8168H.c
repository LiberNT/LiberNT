/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin RTL8168H MAC bring-up constants against published hw facts
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>
#include "rtl8168_hwvals.h"

START_TEST(BringUp8168H)
{
    ok(RL_8168H_ERI_FIFO_RX == 0x00080002u, "rx fifo eri value\n");
    ok(RL_8168H_ERI_FIFO_TX == 0x00100006u, "tx fifo eri value\n");
    ok(RL_8168H_ERI_PAUSE_LO == 0x38u, "pause low\n");
    ok(RL_8168H_ERI_PAUSE_HI == 0x48u, "pause high\n");
    ok(RL_8168H_ERI_DC_SET == 0x001cu, "packet filter dc bits\n");
    ok(RL_8168H_ERI_EEE_SET == 0x0003u, "eee mac set\n");
    ok(RL_8168H_ERI_EEE_CLR == 0x1000u, "eee mac clear bit12\n");
    ok(RL_8168H_MACOCP_E056_CLR == 0x00f0u, "e056 clear mask\n");
    ok(RL_8168H_MACOCP_E056_SET == 0x0070u, "e056 set value\n");
    ok(RL_8168H_MACOCP_E052_CLR == 0x6000u, "e052 clear mask\n");
    ok(RL_8168H_MACOCP_E052_SET == 0x8008u, "e052 set value\n");
}
