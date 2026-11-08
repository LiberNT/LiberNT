#define STANDALONE
#include <wine/test.h>

extern void func_hal_render(void);

const struct test winetest_testlist[] =
{
    { "hal_render", func_hal_render },

    { 0, 0 }
};
