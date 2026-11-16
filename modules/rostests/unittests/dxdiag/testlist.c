#define STANDALONE
#include <apitest.h>

extern void func_D3DMark(void);

const struct test winetest_testlist[] =
{
    { "D3DMark", func_D3DMark },
    { 0, 0 }
};
