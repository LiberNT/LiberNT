#define STANDALONE
#include <apitest.h>

extern void func_native(void);
extern void func_wow64(void);

const struct test winetest_testlist[] =
{
    { "native", func_native },
    { "wow64", func_wow64 },
    { 0, 0 }
};
