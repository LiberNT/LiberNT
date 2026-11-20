#define STANDALONE
#include <apitest.h>

extern void func_MapFont(void);

const struct test winetest_testlist[] =
{
    { "MapFont", func_MapFont },
    { 0, 0 }
};
