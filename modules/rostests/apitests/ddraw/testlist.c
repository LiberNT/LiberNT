#define STANDALONE
#include <wine/test.h>

extern void func_PrimaryBltLatency(void);

const struct test winetest_testlist[] =
{
    { "PrimaryBltLatency", func_PrimaryBltLatency },

    { 0, 0 }
};
