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
extern void func_legacysync(void);
extern void func_swapchain(void);
extern void func_exitshared(void);
extern void func_exitshared_child(void);
extern void func_synclife(void);
extern void func_queryregistry(void);

const struct test winetest_testlist[] =
{
    { "sharedaccess", func_sharedaccess },
    { "syncabi",      func_syncabi },
    { "legacysync",   func_legacysync },
    { "swapchain",    func_swapchain },
    { "exitshared",   func_exitshared },
    { "exitshared_child", func_exitshared_child },
    { "synclife",     func_synclife },
    { "queryregistry", func_queryregistry },
    { 0, 0 }
};
