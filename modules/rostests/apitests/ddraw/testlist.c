#define STANDALONE
#include <wine/test.h>

extern void func_DirectDrawCreate(void);
extern void func_PrimaryBltLatency(void);
extern void func_QueryInterface(void);

const struct test winetest_testlist[] =
{
    { "DirectDrawCreate", func_DirectDrawCreate },
    { "PrimaryBltLatency", func_PrimaryBltLatency },
    { "QueryInterface", func_QueryInterface },

    { 0, 0 }
};
