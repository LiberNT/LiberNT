#define STANDALONE
#include <apitest.h>

extern void func_LcdBrightness(void);

const struct test winetest_testlist[] =
{
    { "LcdBrightness", func_LcdBrightness },
    { 0, 0 }
};
