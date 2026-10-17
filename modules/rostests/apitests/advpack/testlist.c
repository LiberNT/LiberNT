/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for advpack.dll functions
 * COPYRIGHT:   Copyright 2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_DelNode(void);
extern void func_RegInstall(void);

const struct test winetest_testlist[] =
{
    { "DelNode", func_DelNode },
    { "RegInstall", func_RegInstall },
    { 0, 0 }
};
