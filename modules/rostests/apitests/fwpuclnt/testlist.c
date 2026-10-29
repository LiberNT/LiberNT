#define STANDALONE
#include <apitest.h>

extern void func_ale(void);

const struct test winetest_testlist[] =
{
    { "ale", func_ale },
    { 0, 0 }
};
