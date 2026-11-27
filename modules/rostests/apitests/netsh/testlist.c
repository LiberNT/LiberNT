#define STANDALONE
#include <apitest.h>

extern void func_commandline(void);

const struct test winetest_testlist[] =
{
    { "commandline", func_commandline },
    { 0, 0 }
};
