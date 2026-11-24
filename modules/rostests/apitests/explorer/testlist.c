#define STANDALONE
#include <apitest.h>

extern void func_SecondaryTaskbars(void);
extern void func_VolumeOsd(void);

const struct test winetest_testlist[] =
{
    { "SecondaryTaskbars", func_SecondaryTaskbars },
    { "VolumeOsd", func_VolumeOsd },
    { 0, 0 }
};
