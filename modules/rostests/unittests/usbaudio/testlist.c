#define STANDALONE
#include <apitest.h>

extern void func_UacStream(void);

const struct test winetest_testlist[] =
{
    { "UacStream", func_UacStream },

    { 0, 0 }
};
