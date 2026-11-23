/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the Apple silicon helper unit tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_AppleSoc(void);
extern void func_AgxFirmware(void);

const struct test winetest_testlist[] =
{
    { "AppleSoc", func_AppleSoc },
    { "AgxFirmware", func_AgxFirmware },
    { 0, 0 }
};
