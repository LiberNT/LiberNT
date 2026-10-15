#define STANDALONE
#include <apitest.h>

extern void func_MagnifierWindow(void);

const struct test winetest_testlist[] =
{
    { "MagnifierWindow", func_MagnifierWindow },
    { 0, 0 }
};
