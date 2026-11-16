#define STANDALONE
#include <apitest.h>

extern void func_Tbs(void);

const struct test winetest_testlist[] =
{
    { "Tbs", func_Tbs },
    { 0, 0 }
};
