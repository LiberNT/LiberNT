#define STANDALONE
#include <apitest.h>

extern void func_NciConnectionName(void);

const struct test winetest_testlist[] =
{
    { "NciConnectionName", func_NciConnectionName },
    { 0, 0 }
};
