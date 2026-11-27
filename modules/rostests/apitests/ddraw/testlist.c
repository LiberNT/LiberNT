#define STANDALONE
#include <wine/test.h>

extern void func_AvailableVidMem(void);
extern void func_DirectDrawCreate(void);
extern void func_EnumDisplayModes(void);
extern void func_PrimaryBltLatency(void);
extern void func_QueryInterface(void);
extern void func_VerticalBlank(void);

const struct test winetest_testlist[] =
{
    { "AvailableVidMem", func_AvailableVidMem },
    { "DirectDrawCreate", func_DirectDrawCreate },
    { "EnumDisplayModes", func_EnumDisplayModes },
    { "PrimaryBltLatency", func_PrimaryBltLatency },
    { "QueryInterface", func_QueryInterface },
    { "VerticalBlank", func_VerticalBlank },

    { 0, 0 }
};
