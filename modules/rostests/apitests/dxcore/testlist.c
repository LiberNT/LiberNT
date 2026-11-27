#define STANDALONE
#include <apitest.h>

extern void func_DXCoreCreateAdapterFactory(void);
extern void func_DisplayOnly(void);

const struct test winetest_testlist[] =
{
    { "DXCoreCreateAdapterFactory", func_DXCoreCreateAdapterFactory },
    { "DisplayOnly", func_DisplayOnly },
    { 0, 0 }
};
