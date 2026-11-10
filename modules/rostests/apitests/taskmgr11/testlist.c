/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Task Manager 11 test list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_battery(void);
extern void func_hardware_text(void);
extern void func_performance_layout(void);

const struct test winetest_testlist[] =
{
    { "battery", func_battery },
    { "hardware_text", func_hardware_text },
    { "performance_layout", func_performance_layout },
    { 0, 0 }
};
