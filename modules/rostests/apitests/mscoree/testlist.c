#define STANDALONE
#include <apitest.h>

extern void func_CorBindToRuntimeEx(void);

const struct test winetest_testlist[] =
{
    { "CorBindToRuntimeEx", func_CorBindToRuntimeEx },
    { 0, 0 }
};
