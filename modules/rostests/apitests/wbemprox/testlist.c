/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list of the WMI repository and decoupled provider tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_Decoupled(void);
extern void func_Repository(void);

const struct test winetest_testlist[] =
{
    { "Decoupled", func_Decoupled },
    { "Repository", func_Repository },
    { 0, 0 }
};
