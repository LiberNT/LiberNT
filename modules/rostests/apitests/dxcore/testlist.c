#define STANDALONE
#include <apitest.h>

extern void func_DXCoreCreateAdapterFactory(void);

const struct test winetest_testlist[] =
{
    { "DXCoreCreateAdapterFactory", func_DXCoreCreateAdapterFactory },
    { 0, 0 }
};
