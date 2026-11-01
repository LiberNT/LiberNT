#define STANDALONE
#include <apitest.h>

extern void func_Negotiate(void);

const struct test winetest_testlist[] =
{
    { "Negotiate", func_Negotiate },
    { 0, 0 }
};
