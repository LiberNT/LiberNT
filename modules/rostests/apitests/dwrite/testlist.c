#define STANDALONE
#include <apitest.h>

extern void func_BidiLevels(void);
extern void func_GlyphJustification(void);
extern void func_HasVariations(void);
extern void func_SystemFontFallback(void);

const struct test winetest_testlist[] =
{
    { "BidiLevels", func_BidiLevels },
    { "GlyphJustification", func_GlyphJustification },
    { "HasVariations", func_HasVariations },
    { "SystemFontFallback", func_SystemFontFallback },

    { 0, 0 }
};
