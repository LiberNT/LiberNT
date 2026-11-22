#define STANDALONE
#include <apitest.h>

extern void func_D3DMark(void);
extern void func_D3DStats(void);

const struct test winetest_testlist[] =
{
    { "D3DMark", func_D3DMark },
    { "D3DStats", func_D3DStats },
    { 0, 0 }
};
