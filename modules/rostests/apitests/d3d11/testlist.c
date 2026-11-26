#define STANDALONE
#include <wine/test.h>

extern void func_cb_offset(void);
extern void func_clear_view(void);
extern void func_hal_render(void);
extern void func_hal_resource(void);
extern void func_map_default(void);
extern void func_null_swapchain_desc(void);
extern void func_warp_device(void);
extern void func_winrt_interop(void);

const struct test winetest_testlist[] =
{
    { "cb_offset", func_cb_offset },
    { "clear_view", func_clear_view },
    { "hal_render", func_hal_render },
    { "hal_resource", func_hal_resource },
    { "map_default", func_map_default },
    { "null_swapchain_desc", func_null_swapchain_desc },
    { "warp_device", func_warp_device },
    { "winrt_interop", func_winrt_interop },

    { 0, 0 }
};
