#define STANDALONE
#include <apitest.h>

extern void func_HasVariations(void);

const struct test winetest_testlist[] =
{
    { "HasVariations", func_HasVariations },

    { 0, 0 }
};
