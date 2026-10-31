#define STANDALONE
#include <apitest.h>

extern void func_VtScreen(void);

const struct test winetest_testlist[] =
{
    { "VtScreen", func_VtScreen },
    { 0, 0 }
};
