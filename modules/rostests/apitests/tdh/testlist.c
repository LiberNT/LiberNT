#define STANDALONE
#include <apitest.h>

extern void func_TdhProperty(void);

const struct test winetest_testlist[] =
{
    { "TdhProperty", func_TdhProperty },
    { 0, 0 }
};
