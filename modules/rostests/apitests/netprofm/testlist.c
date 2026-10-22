#define STANDALONE
#include <apitest.h>

extern void func_GetCost(void);

const struct test winetest_testlist[] =
{
    { "GetCost", func_GetCost },

    { 0, 0 }
};
