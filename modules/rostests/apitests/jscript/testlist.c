#define STANDALONE
#include <apitest.h>

extern void func_NumberConversion(void);

const struct test winetest_testlist[] =
{
    { "NumberConversion", func_NumberConversion },
    { 0, 0 }
};
