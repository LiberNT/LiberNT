#define STANDALONE
#include <apitest.h>

extern void func_BackgroundApply(void);

const struct test winetest_testlist[] =
{
    { "BackgroundApply", func_BackgroundApply },
    { 0, 0 }
};
