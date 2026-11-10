
#define STANDALONE
#include <apitest.h>

extern void func_CloseThemeData(void);
extern void func_ColorPreference(void);
extern void func_DrawThemeParentBackground(void);
extern void func_DwmCaptionButton(void);
extern void func_GetThemeParseErrorInfo(void);
extern void func_CustomCaptionInset(void);
extern void func_NonClientActivate(void);
extern void func_NonClientCaption(void);
extern void func_NonClientHitTest(void);
extern void func_SetThemeAppProperties(void);
extern void func_SetWindowTheme(void);
extern void func_TextScaling(void);
extern void func_ThemeRendering(void);
extern void func_ThemeShellTransition(void);

const struct test winetest_testlist[] =
{
    { "CloseThemeData", func_CloseThemeData },
    { "ColorPreference", func_ColorPreference },
    { "DrawThemeParentBackground", func_DrawThemeParentBackground },
    { "DwmCaptionButton", func_DwmCaptionButton },
    { "GetThemeParseErrorInfo", func_GetThemeParseErrorInfo },
    { "CustomCaptionInset", func_CustomCaptionInset },
    { "NonClientActivate", func_NonClientActivate },
    { "NonClientCaption", func_NonClientCaption },
    { "NonClientHitTest", func_NonClientHitTest },
    { "SetWindowTheme", func_SetWindowTheme },
    { "TextScaling",    func_TextScaling },
    { "SetThemeAppProperties", func_SetThemeAppProperties },
    { "ThemeRendering", func_ThemeRendering },
    { "ThemeShellTransition", func_ThemeShellTransition },
    { 0, 0 }
};
