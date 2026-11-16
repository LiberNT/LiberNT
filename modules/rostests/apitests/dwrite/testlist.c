#define STANDALONE
#include <apitest.h>

extern void func_BidiLevels(void);
extern void func_FontFallbackLocale(void);
extern void func_FontFallbackSource(void);
extern void func_GlyphJustification(void);
extern void func_GlyphRunSizes(void);
extern void func_HasVariations(void);
extern void func_RenderingModeTransform(void);
extern void func_SystemFontFallback(void);
extern void func_TrailingWhitespace(void);

const struct test winetest_testlist[] =
{
    { "BidiLevels", func_BidiLevels },
    { "FontFallbackLocale", func_FontFallbackLocale },
    { "FontFallbackSource", func_FontFallbackSource },
    { "GlyphJustification", func_GlyphJustification },
    { "GlyphRunSizes", func_GlyphRunSizes },
    { "HasVariations", func_HasVariations },
    { "RenderingModeTransform", func_RenderingModeTransform },
    { "SystemFontFallback", func_SystemFontFallback },
    { "TrailingWhitespace", func_TrailingWhitespace },

    { 0, 0 }
};
