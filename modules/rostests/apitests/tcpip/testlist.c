/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Register TCP/IP API tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_fragment_reassembly(void);
extern void func_raw_receive(void);
extern void func_raw_send(void);

const struct test winetest_testlist[] =
{
    { "fragment_reassembly", func_fragment_reassembly },
    { "raw_receive", func_raw_receive },
    { "raw_send", func_raw_send },
    { 0, 0 }
};
