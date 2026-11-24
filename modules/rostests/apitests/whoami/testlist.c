#define STANDALONE
#include <apitest.h>

extern void func_groups(void);

const struct test winetest_testlist[] =
{
    { "groups", func_groups },
    { 0, 0 }
};
