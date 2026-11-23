#define STANDALONE
#include <apitest.h>

extern void func_sqlite(void);

const struct test winetest_testlist[] =
{
    { "sqlite", func_sqlite },
    { 0, 0 }
};
