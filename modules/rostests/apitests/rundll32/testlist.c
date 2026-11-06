#define STANDALONE
#include <apitest.h>

extern void func_ordinal(void);
extern void func_wow64(void);

const struct test winetest_testlist[] =
{
    { "ordinal", func_ordinal },
    { "wow64", func_wow64 },
    { 0, 0 }
};
