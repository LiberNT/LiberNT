#define STANDALONE
#include <apitest.h>

extern void func_SizedList(void);

const struct test winetest_testlist[] =
{
    { "SizedList", func_SizedList },

    { 0, 0 }
};
