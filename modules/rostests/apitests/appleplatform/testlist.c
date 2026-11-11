/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the Apple silicon CPU performance and PWM driver unit tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_AppleCpufreq(void);
extern void func_AppleFpwm(void);

const struct test winetest_testlist[] =
{
    { "AppleCpufreq", func_AppleCpufreq },
    { "AppleFpwm", func_AppleFpwm },
    { 0, 0 }
};
