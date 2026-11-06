/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 test list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_allocator(void);
extern void func_barrier(void);
extern void func_budget(void);
extern void func_bgwork(void);
extern void func_blend(void);
extern void func_bundle(void);
extern void func_capsuse(void);
extern void func_ddibuild(void);
extern void func_device(void);
extern void func_dred(void);
extern void func_factory(void);
extern void func_fencekill(void);
extern void func_mesh(void);
extern void func_feedback(void);
extern void func_features(void);
extern void func_formatsupport(void);
extern void func_heaps(void);
extern void func_interfaces(void);
extern void func_pipeline(void);
extern void func_pipelib(void);
extern void func_queue(void);
extern void func_raster(void);
extern void func_relaxed(void);
extern void func_renderpass(void);
extern void func_rootsig(void);
extern void func_rootstate(void);
extern void func_sampler(void);
extern void func_sharedfence(void);
extern void func_stablepower(void);
extern void func_swapchain(void);
extern void func_swapformat(void);
extern void func_swizzle(void);
extern void func_topology(void);
extern void func_uav(void);
extern void func_versions(void);
extern void func_video(void);
extern void func_viewinst(void);
extern void func_vrs(void);

const struct test winetest_testlist[] =
{
    { "allocator", func_allocator },
    { "barrier", func_barrier },
    { "budget", func_budget },
    { "bgwork", func_bgwork },
    { "blend", func_blend },
    { "bundle", func_bundle },
    { "capsuse", func_capsuse },
    { "ddibuild", func_ddibuild },
    { "device", func_device },
    { "dred", func_dred },
    { "factory", func_factory },
    { "fencekill", func_fencekill },
    { "mesh", func_mesh },
    { "feedback", func_feedback },
    { "features", func_features },
    { "formatsupport", func_formatsupport },
    { "heaps", func_heaps },
    { "interfaces", func_interfaces },
    { "pipeline", func_pipeline },
    { "pipelib", func_pipelib },
    { "queue", func_queue },
    { "raster", func_raster },
    { "relaxed", func_relaxed },
    { "renderpass", func_renderpass },
    { "rootsig", func_rootsig },
    { "rootstate", func_rootstate },
    { "sampler", func_sampler },
    { "sharedfence", func_sharedfence },
    { "stablepower", func_stablepower },
    { "swapchain", func_swapchain },
    { "swapformat", func_swapformat },
    { "swizzle", func_swizzle },
    { "topology", func_topology },
    { "uav", func_uav },
    { "versions", func_versions },
    { "video", func_video },
    { "viewinst", func_viewinst },
    { "vrs", func_vrs },
    { 0, 0 }
};
