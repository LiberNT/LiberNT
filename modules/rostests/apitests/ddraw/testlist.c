#define STANDALONE
#include <wine/test.h>

extern void func_DirectDrawCreate(void);
extern void func_PrimaryBltLatency(void);

const struct test winetest_testlist[] =
{
    { "DirectDrawCreate", func_DirectDrawCreate },
    { "PrimaryBltLatency", func_PrimaryBltLatency },

    { 0, 0 }
};
