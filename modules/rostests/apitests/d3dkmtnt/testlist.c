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
extern void func_gdi32names(void);
extern void func_win32unames(void);
extern void func_allocabi(void);
extern void func_nestedptr(void);

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
    { "gdi32names",   func_gdi32names },
    { "win32unames",  func_win32unames },
    { "allocabi",     func_allocabi },
    { "nestedptr",    func_nestedptr },
    { 0, 0 }
};
