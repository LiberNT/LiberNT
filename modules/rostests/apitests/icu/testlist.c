#define STANDALONE
#include <apitest.h>

extern void func_ustring(void);

const struct test winetest_testlist[] =
{
    { "ustring", func_ustring },
    { 0, 0 }
};
