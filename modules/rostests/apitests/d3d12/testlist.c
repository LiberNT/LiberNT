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
extern void func_device(void);
extern void func_factory(void);
extern void func_features(void);
extern void func_heaps(void);
extern void func_interfaces(void);
extern void func_pipeline(void);
extern void func_pipelib(void);
extern void func_queue(void);
extern void func_relaxed(void);
extern void func_renderpass(void);
extern void func_rootsig(void);
extern void func_sampler(void);
extern void func_topology(void);
extern void func_uav(void);
extern void func_versions(void);
extern void func_video(void);
extern void func_viewinst(void);
extern void func_vrs(void);

const struct test winetest_testlist[] =
{
    { "allocator", func_allocator },
    { "budget", func_budget },
    { "blend", func_blend },
    { "bundle", func_bundle },
    { "device", func_device },
    { "factory", func_factory },
    { "features", func_features },
    { "heaps", func_heaps },
    { "interfaces", func_interfaces },
    { "pipeline", func_pipeline },
    { "pipelib", func_pipelib },
    { "queue", func_queue },
    { "relaxed", func_relaxed },
    { "renderpass", func_renderpass },
    { "rootsig", func_rootsig },
    { "sampler", func_sampler },
    { "topology", func_topology },
    { "uav", func_uav },
    { "versions", func_versions },
    { "video", func_video },
    { "viewinst", func_viewinst },
    { "vrs", func_vrs },
    { 0, 0 }
};
