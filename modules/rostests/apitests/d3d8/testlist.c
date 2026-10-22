#define STANDALONE
#include <apitest.h>

extern void func_LinePattern(void);

const struct test winetest_testlist[] =
{
    { "LinePattern", func_LinePattern },

    { 0, 0 }
};
