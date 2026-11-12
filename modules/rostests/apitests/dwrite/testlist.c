#define STANDALONE
#include <apitest.h>

extern void func_BidiLevels(void);
extern void func_FontFallbackLocale(void);
extern void func_FontFallbackSource(void);
extern void func_GlyphJustification(void);
extern void func_HasVariations(void);
extern void func_SystemFontFallback(void);

const struct test winetest_testlist[] =
{
    { "BidiLevels", func_BidiLevels },
    { "FontFallbackLocale", func_FontFallbackLocale },
    { "FontFallbackSource", func_FontFallbackSource },
    { "GlyphJustification", func_GlyphJustification },
    { "HasVariations", func_HasVariations },
    { "SystemFontFallback", func_SystemFontFallback },

    { 0, 0 }
};
