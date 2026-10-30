
#define STANDALONE
#include <wine/test.h>

extern void func_sw_extensions(void);
extern void func_sw_pixelformat(void);
extern void func_wgl_swap_control(void);
extern void func_wgl_desktop(void);
extern void func_wgl_streaming(void);
extern void func_wgl_fence_readback(void);
extern void func_wgl_hidden_present(void);
extern void func_wgl_context_rebind(void);
extern void func_wgl_icd_adapter(void);
extern void func_wgl_flat_shading(void);
extern void func_wgl_close_midrender(void);
extern void func_wgl_front_facing(void);
extern void func_wgl_scaled_attrib(void);
extern void func_wgl_depth_blit(void);
extern void func_wgl_render(void);

const struct test winetest_testlist[] =
{
    { "sw_extensions", func_sw_extensions },
    { "sw_pixelformat", func_sw_pixelformat },
    { "wgl_swap_control", func_wgl_swap_control },
    { "wgl_desktop", func_wgl_desktop },
    { "wgl_streaming", func_wgl_streaming },
    { "wgl_fence_readback", func_wgl_fence_readback },
    { "wgl_hidden_present", func_wgl_hidden_present },
    { "wgl_context_rebind", func_wgl_context_rebind },
    { "wgl_icd_adapter", func_wgl_icd_adapter },
    { "wgl_flat_shading", func_wgl_flat_shading },
    { "wgl_close_midrender", func_wgl_close_midrender },
    { "wgl_front_facing", func_wgl_front_facing },
    { "wgl_scaled_attrib", func_wgl_scaled_attrib },
    { "wgl_depth_blit", func_wgl_depth_blit },
    { "wgl_render", func_wgl_render },

    { 0, 0 }
};
