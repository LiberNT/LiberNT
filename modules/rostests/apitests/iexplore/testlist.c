#define STANDALONE
#include <apitest.h>

extern void func_Registration(void);

const struct test winetest_testlist[] =
{
    { "Registration", func_Registration },

    { 0, 0 }
};
