#define STANDALONE
#include <apitest.h>

extern void func_Layer(void);

const struct test winetest_testlist[] =
{
    { "Layer", func_Layer },

    { 0, 0 }
};
