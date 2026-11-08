#define STANDALONE
#include <apitest.h>

extern void func_CatalogV2(void);

const struct test winetest_testlist[] =
{
    { "CatalogV2", func_CatalogV2 },
    { 0, 0 }
};
