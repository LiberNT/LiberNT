#define STANDALONE
#include <apitest.h>

extern void func_EditVerbs(void);

const struct test winetest_testlist[] =
{
    { "EditVerbs", func_EditVerbs },
    { 0, 0 }
};
