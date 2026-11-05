/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Color Converter DSP API test registration
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_convert(void);

const struct test winetest_testlist[] =
{
    { "convert", func_convert },
    { 0, 0 }
};
