/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the HAL unit tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_FdtPnp(void);

const struct test winetest_testlist[] =
{
    { "FdtPnp", func_FdtPnp },
    { 0, 0 }
};
