/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the NTFS API tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_MissingSecurity(void);
extern void func_IndexMetadata(void);
extern void func_FullBaseGrowth(void);
extern void func_FreeClusters(void);

const struct test winetest_testlist[] =
{
    { "MissingSecurity", func_MissingSecurity },
    { "IndexMetadata", func_IndexMetadata },
    { "FullBaseGrowth", func_FullBaseGrowth },
    { "FreeClusters", func_FreeClusters },
    { 0, 0 }
};
