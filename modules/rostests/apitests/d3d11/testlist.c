#define STANDALONE
#include <wine/test.h>

extern void func_hal_render(void);
extern void func_hal_resource(void);
extern void func_winrt_interop(void);

const struct test winetest_testlist[] =
{
    { "hal_render", func_hal_render },
    { "hal_resource", func_hal_resource },
    { "winrt_interop", func_winrt_interop },

    { 0, 0 }
};
