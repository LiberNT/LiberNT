/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for rpcrt4.dll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_EmbeddedPointers(void);
extern void func_ServerRestart(void);

const struct test winetest_testlist[] =
{
    { "EmbeddedPointers", func_EmbeddedPointers },
    { "ServerRestart", func_ServerRestart },
    { 0, 0 }
};
