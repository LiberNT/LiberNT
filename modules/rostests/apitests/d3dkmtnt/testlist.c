/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the D3DKMT NT-handle object tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#define STANDALONE
#include <apitest.h>

extern void func_sharedaccess(void);
extern void func_syncabi(void);

const struct test winetest_testlist[] =
{
    { "sharedaccess", func_sharedaccess },
    { "syncabi",      func_syncabi },
    { 0, 0 }
};
