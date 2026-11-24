/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Register RTL8168 helper API tests
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_EtherCrc32(void);

const struct test winetest_testlist[] =
{
    { "EtherCrc32", func_EtherCrc32 },
    { 0, 0 }
};
