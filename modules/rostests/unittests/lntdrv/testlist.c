#define STANDALONE
#include <apitest.h>

extern void func_CatalogRules(void);
extern void func_DeviceRules(void);

const struct test winetest_testlist[] =
{
    { "CatalogRules", func_CatalogRules },
    { "DeviceRules", func_DeviceRules },
    { 0, 0 }
};
