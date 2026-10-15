/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 test list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_allocator(void);
extern void func_budget(void);
extern void func_blend(void);
extern void func_bundle(void);
extern void func_features(void);
extern void func_interfaces(void);
extern void func_queue(void);
extern void func_renderpass(void);
extern void func_rootsig(void);
extern void func_sampler(void);
extern void func_versions(void);

const struct test winetest_testlist[] =
{
    { "allocator", func_allocator },
    { "budget", func_budget },
    { "blend", func_blend },
    { "bundle", func_bundle },
    { "features", func_features },
    { "interfaces", func_interfaces },
    { "queue", func_queue },
    { "renderpass", func_renderpass },
    { "rootsig", func_rootsig },
    { "sampler", func_sampler },
    { "versions", func_versions },
    { 0, 0 }
};
