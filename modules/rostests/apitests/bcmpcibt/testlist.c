/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the Broadcom PCIe Bluetooth protocol unit tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_BcmPciBt(void);

const struct test winetest_testlist[] =
{
    { "BcmPciBt", func_BcmPciBt },
    { 0, 0 }
};
