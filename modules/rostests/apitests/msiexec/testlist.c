#define STANDALONE
#include <apitest.h>

extern void func_packageerror(void);
extern void func_restart(void);

const struct test winetest_testlist[] =
{
    { "packageerror", func_packageerror },
    { "restart", func_restart },
    { 0, 0 }
};
