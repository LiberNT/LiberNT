
#define STANDALONE
#include <wine/test.h>

extern void func_createflags(void);
extern void func_exmodes(void);
extern void func_hal_render(void);

const struct test winetest_testlist[] =
{
    { "createflags", func_createflags },
    { "exmodes", func_exmodes },
    { "hal_render", func_hal_render },

    { 0, 0 }
};
