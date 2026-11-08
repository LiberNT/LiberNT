#define STANDALONE
#include <apitest.h>

extern void func_adapter_removal(void);
extern void func_format_support(void);
extern void func_stub_surface(void);
extern void func_d3d11_render_probe(void);
extern void func_dwm_render(void);
extern void func_completion_event(void);
extern void func_driver_error(void);
extern void func_memory_query_cost(void);
extern void func_d3d11_level9(void);
extern void func_factory_opengl(void);
extern void func_shared_fence(void);
extern void func_fence_neighbors(void);
extern void func_shared_name(void);
extern void func_gpu_priority(void);
extern void func_kmt_outputdupl(void);
extern void func_offer_resources(void);
extern void func_subresource_surface(void);
extern void func_feature_support(void);
extern void func_shared_dimensions(void);
extern void func_keyedmutex_kill(void);
extern void func_d3d10_bindings(void);
extern void func_d3d10_create(void);
extern void func_copy_regions(void);
extern void func_adapter_outputs(void);
extern void func_counters(void);
extern void func_query_order(void);
extern void func_composition(void);
extern void func_composition_damage(void);
extern void func_composition_source(void);
extern void func_hwnd_source(void);
extern void func_hwnd_damage(void);
extern void func_composition_lifetime(void);
extern void func_texture_dimensions(void);
extern void func_multisample(void);
extern void func_raster_coverage(void);
extern void func_null_texture(void);
extern void func_buffer_copy(void);
extern void func_copy_depth_stencil(void);
extern void func_volume_readback(void);
extern void func_viewport_levels(void);
extern void func_wait_vblank(void);
extern void func_sampler_default(void);

const struct test winetest_testlist[] =
{
    { "adapter_removal", func_adapter_removal },
    { "format_support", func_format_support },
    { "stub_surface", func_stub_surface },
    { "d3d11_render_probe", func_d3d11_render_probe },
    { "dwm_render", func_dwm_render },
    { "completion_event", func_completion_event },
    { "driver_error", func_driver_error },
    { "memory_query_cost", func_memory_query_cost },
    { "d3d11_level9", func_d3d11_level9 },
    { "factory_opengl", func_factory_opengl },
    { "shared_fence", func_shared_fence },
    { "fence_neighbors", func_fence_neighbors },
    { "shared_name", func_shared_name },
    { "gpu_priority", func_gpu_priority },
    { "kmt_outputdupl", func_kmt_outputdupl },
    { "offer_resources", func_offer_resources },
    { "subresource_surface", func_subresource_surface },
    { "feature_support", func_feature_support },
    { "shared_dimensions", func_shared_dimensions },
    { "keyedmutex_kill", func_keyedmutex_kill },
    { "d3d10_bindings", func_d3d10_bindings },
    { "d3d10_create", func_d3d10_create },
    { "copy_regions", func_copy_regions },
    { "adapter_outputs", func_adapter_outputs },
    { "counters", func_counters },
    { "query_order", func_query_order },
    { "composition", func_composition },
    { "composition_damage", func_composition_damage },
    { "composition_source", func_composition_source },
    { "hwnd_source", func_hwnd_source },
    { "hwnd_damage", func_hwnd_damage },
    { "composition_lifetime", func_composition_lifetime },
    { "texture_dimensions", func_texture_dimensions },
    { "multisample", func_multisample },
    { "raster_coverage", func_raster_coverage },
    { "null_texture", func_null_texture },
    { "buffer_copy", func_buffer_copy },
    { "copy_depth_stencil", func_copy_depth_stencil },
    { "volume_readback", func_volume_readback },
    { "viewport_levels", func_viewport_levels },
    { "wait_vblank", func_wait_vblank },
    { "sampler_default", func_sampler_default },
    { 0, 0 }
};
