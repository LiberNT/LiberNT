#define STANDALONE
#include <apitest.h>

extern void func_HasVariations(void);
extern void func_SystemFontFallback(void);

const struct test winetest_testlist[] =
{
    { "HasVariations", func_HasVariations },
    { "SystemFontFallback", func_SystemFontFallback },

    { 0, 0 }
};
