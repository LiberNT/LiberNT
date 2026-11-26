#define STANDALONE
#include <apitest.h>

extern void func_GetCost(void);
extern void func_InternetConnectivity(void);

const struct test winetest_testlist[] =
{
    { "GetCost", func_GetCost },
    { "InternetConnectivity", func_InternetConnectivity },

    { 0, 0 }
};
