#define STANDALONE
#include <apitest.h>

extern void func_RxFrame(void);

const struct test winetest_testlist[] =
{
    { "RxFrame", func_RxFrame },
    { 0, 0 }
};
