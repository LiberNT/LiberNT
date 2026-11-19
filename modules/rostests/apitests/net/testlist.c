#define STANDALONE
#include <apitest.h>

extern void func_NetUserCommand(void);

const struct test winetest_testlist[] =
{
    { "NetUserCommand", func_NetUserCommand },
    { 0, 0 }
};
