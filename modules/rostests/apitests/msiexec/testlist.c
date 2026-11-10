#define STANDALONE
#include <apitest.h>

extern void func_restart(void);

const struct test winetest_testlist[] =
{
    { "restart", func_restart },
    { 0, 0 }
};
