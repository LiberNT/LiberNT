#define STANDALONE
#include <apitest.h>

extern void func_DriverLoad(void);

const struct test winetest_testlist[] =
{
    { "DriverLoad", func_DriverLoad },
    { 0, 0 }
};
