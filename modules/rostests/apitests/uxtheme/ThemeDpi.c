/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tabulates the per-DPI theme part sizes and margins to compare with Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdio.h>
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

typedef HTHEME (WINAPI *POPENTHEMEDATAFORDPI)(HWND, LPCWSTR, UINT);
typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);

#define DPI_COUNT 7

static const UINT Dpis[DPI_COUNT] = { 96, 120, 144, 168, 192, 240, 288 };

static const struct
{
    LPCWSTR Class;
    const char *Name;
    int Part;
    int State;
    BOOL Scales;
} Parts[] =
{
    { L"WINDOW", "caption", WP_CAPTION, CS_ACTIVE },
    { L"WINDOW", "close button", WP_CLOSEBUTTON, CBS_NORMAL },
    { L"WINDOW", "max button", WP_MAXBUTTON, MAXBS_NORMAL },
    { L"WINDOW", "min button", WP_MINBUTTON, MINBS_NORMAL },
    { L"WINDOW", "small close button", WP_SMALLCLOSEBUTTON, CBS_NORMAL },
    { L"WINDOW", "frame left", WP_FRAMELEFT, FS_ACTIVE },
    { L"WINDOW", "frame bottom", WP_FRAMEBOTTOM, FS_ACTIVE },
    { L"SCROLLBAR", "arrow", SBP_ARROWBTN, ABS_UPNORMAL },
    { L"SCROLLBAR", "thumb", SBP_THUMBBTNVERT, SCRBS_NORMAL },
    { L"SCROLLBAR", "gripper", SBP_GRIPPERVERT, SCRBS_NORMAL },
    { L"SCROLLBAR", "size box", SBP_SIZEBOX, SZB_RIGHTALIGN },
    { L"BUTTON", "push button", BP_PUSHBUTTON, PBS_NORMAL },
    { L"BUTTON", "radio button", BP_RADIOBUTTON, RBS_UNCHECKEDNORMAL, TRUE },
    { L"BUTTON", "check box", BP_CHECKBOX, CBS_UNCHECKEDNORMAL, TRUE },
    { L"TREEVIEW", "glyph", TVP_GLYPH, GLPS_CLOSED, TRUE },
    { L"Explorer::TreeView", "explorer glyph", TVP_GLYPH, GLPS_CLOSED, TRUE },
    { L"COMBOBOX", "drop-down button", CP_DROPDOWNBUTTON, CBXS_NORMAL },
    { L"SPIN", "up", SPNP_UP, UPS_NORMAL },
    { L"SPIN", "down", SPNP_DOWN, DNS_NORMAL },
    { L"TOOLTIP", "close", TTP_CLOSE, TTCS_NORMAL, TRUE },
    { L"MENU", "popup check", MENU_POPUPCHECK, MC_CHECKMARKNORMAL },
    { L"MENU", "popup submenu", MENU_POPUPSUBMENU, MSM_NORMAL },
    { L"MENU", "popup separator", MENU_POPUPSEPARATOR, 0 },
    { L"MENU", "popup gutter", MENU_POPUPGUTTER, 0 },
    { L"TRACKBAR", "thumb", TKP_THUMB, TUS_NORMAL },
    { L"HEADER", "sort arrow", HP_HEADERSORTARROW, HSAS_SORTEDUP },
    { L"STATUS", "gripper", SP_GRIPPER, 0 },
    { L"TAB", "tab item", TABP_TABITEM, TIS_NORMAL },
    { L"PROGRESS", "bar", PP_BAR, 0 },
    { L"REBAR", "chevron", RP_CHEVRON, CHEVS_NORMAL },
    { L"TOOLBAR", "split dropdown", TP_SPLITBUTTONDROPDOWN, TS_NORMAL },
};

static void
TraceSizes(const char *Class, const char *Name, const char *Kind, const SIZE *Sizes)
{
    trace("%s %s %s: %ldx%ld %ldx%ld %ldx%ld %ldx%ld %ldx%ld %ldx%ld %ldx%ld\n", Class, Name, Kind,
          Sizes[0].cx, Sizes[0].cy, Sizes[1].cx, Sizes[1].cy, Sizes[2].cx, Sizes[2].cy, Sizes[3].cx, Sizes[3].cy,
          Sizes[4].cx, Sizes[4].cy, Sizes[5].cx, Sizes[5].cy, Sizes[6].cx, Sizes[6].cy);
}

static void
TraceMargins(const char *Class, const char *Name, const char *Kind, const MARGINS *Margins)
{
    trace("%s %s %s: %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d\n",
          Class, Name, Kind,
          Margins[0].cxLeftWidth, Margins[0].cxRightWidth, Margins[0].cyTopHeight, Margins[0].cyBottomHeight,
          Margins[1].cxLeftWidth, Margins[1].cxRightWidth, Margins[1].cyTopHeight, Margins[1].cyBottomHeight,
          Margins[2].cxLeftWidth, Margins[2].cxRightWidth, Margins[2].cyTopHeight, Margins[2].cyBottomHeight,
          Margins[3].cxLeftWidth, Margins[3].cxRightWidth, Margins[3].cyTopHeight, Margins[3].cyBottomHeight,
          Margins[4].cxLeftWidth, Margins[4].cxRightWidth, Margins[4].cyTopHeight, Margins[4].cyBottomHeight,
          Margins[5].cxLeftWidth, Margins[5].cxRightWidth, Margins[5].cyTopHeight, Margins[5].cyBottomHeight,
          Margins[6].cxLeftWidth, Margins[6].cxRightWidth, Margins[6].cyTopHeight, Margins[6].cyBottomHeight);
}

static int
QueryEnum(HTHEME Theme, UINT Index, int Property)
{
    int Value = -1;

    if (FAILED(GetThemeEnumValue(Theme, Parts[Index].Part, Parts[Index].State, Property, &Value)))
        return -1;
    return Value;
}

static int
QueryInt(HTHEME Theme, UINT Index, int Property)
{
    int Value = -1;

    if (FAILED(GetThemeInt(Theme, Parts[Index].Part, Parts[Index].State, Property, &Value)))
        return -1;
    return Value;
}

static void
TraceProperties(POPENTHEMEDATAFORDPI pOpenThemeDataForDpi, const char *Class, UINT Index)
{
    static const int MinDpiIds[] = { TMT_MINDPI1, TMT_MINDPI2, TMT_MINDPI3, TMT_MINDPI4,
                                     TMT_MINDPI5, TMT_MINDPI6, TMT_MINDPI7 };
    static const int FileIds[] = { TMT_IMAGEFILE1, TMT_IMAGEFILE2, TMT_IMAGEFILE3, TMT_IMAGEFILE4,
                                   TMT_IMAGEFILE5, TMT_IMAGEFILE6, TMT_IMAGEFILE7 };
    WCHAR File[MAX_PATH];
    int MinDpi[7], Files = 0, i;
    HTHEME Theme = pOpenThemeDataForDpi(NULL, Parts[Index].Class, 96);

    if (!Theme)
        return;
    for (i = 0; i < 7; ++i)
    {
        MinDpi[i] = QueryInt(Theme, Index, MinDpiIds[i]);
        if (SUCCEEDED(GetThemeFilename(Theme, Parts[Index].Part, Parts[Index].State, FileIds[i], File,
                                       ARRAYSIZE(File))))
            Files |= 1 << i;
    }
    trace("%s %s properties: bg %d sizing %d truesize scaling %d stretch mark %d select %d count %d glyph %d "
          "files %#x mindpi %d %d %d %d %d %d %d\n",
          Class, Parts[Index].Name, QueryEnum(Theme, Index, TMT_BGTYPE), QueryEnum(Theme, Index, TMT_SIZINGTYPE),
          QueryEnum(Theme, Index, TMT_TRUESIZESCALINGTYPE), QueryInt(Theme, Index, TMT_TRUESIZESTRETCHMARK),
          QueryEnum(Theme, Index, TMT_IMAGESELECTTYPE), QueryInt(Theme, Index, TMT_IMAGECOUNT),
          QueryEnum(Theme, Index, TMT_GLYPHTYPE), Files, MinDpi[0], MinDpi[1], MinDpi[2], MinDpi[3], MinDpi[4],
          MinDpi[5], MinDpi[6]);
    CloseThemeData(Theme);
}

static void
CheckTrueSizeScaling(POPENTHEMEDATAFORDPI pOpenThemeDataForDpi, const char *Class, UINT Index, const SIZE *True,
                     const SIZE *Draw)
{
    static const int MinDpiIds[] = { TMT_MINDPI2, TMT_MINDPI3, TMT_MINDPI4, TMT_MINDPI5, TMT_MINDPI6, TMT_MINDPI7 };
    HTHEME Theme = pOpenThemeDataForDpi(NULL, Parts[Index].Class, 96);
    int Sizing, Scaling, Mark, i;
    BOOL Single = TRUE;
    SIZE Expected;
    UINT Dpi;

    if (!Theme)
        return;
    Sizing = QueryEnum(Theme, Index, TMT_SIZINGTYPE);
    Scaling = QueryEnum(Theme, Index, TMT_TRUESIZESCALINGTYPE);
    Mark = max(QueryInt(Theme, Index, TMT_TRUESIZESTRETCHMARK), 0);
    for (i = 0; i < ARRAYSIZE(MinDpiIds); ++i)
        Single &= QueryInt(Theme, Index, MinDpiIds[i]) <= 0;
    CloseThemeData(Theme);
    if (Sizing != ST_TRUESIZE || !Single || True[0].cx <= 0 || True[0].cy <= 0)
        return;
    for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
    {
        ok(True[Dpi].cx == True[0].cx && True[Dpi].cy == True[0].cy, "%s %s: true size %ldx%ld at %u DPI\n",
           Class, Parts[Index].Name, True[Dpi].cx, True[Dpi].cy, Dpis[Dpi]);
        Expected = True[0];
        if (Scaling == TSST_DPI && MulDiv(100, Dpis[Dpi], 96) >= Mark + 100)
        {
            Expected.cx = MulDiv(True[0].cx, Dpis[Dpi], 96);
            Expected.cy = MulDiv(True[0].cy, Dpis[Dpi], 96);
        }
        ok(Draw[Dpi].cx == Expected.cx && Draw[Dpi].cy == Expected.cy,
           "%s %s: draw size %ldx%ld at %u DPI, expected %ldx%ld\n", Class, Parts[Index].Name, Draw[Dpi].cx,
           Draw[Dpi].cy, Dpis[Dpi], Expected.cx, Expected.cy);
    }
}

static void
TestPartsForDpi(POPENTHEMEDATAFORDPI pOpenThemeDataForDpi)
{
    SIZE True[DPI_COUNT], Min[DPI_COUNT], Draw[DPI_COUNT];
    MARGINS Sizing[DPI_COUNT], Content[DPI_COUNT];
    char Class[64];
    HTHEME Theme;
    UINT Index, Dpi;
    RECT Bounds;
    HDC Dc;

    Dc = GetDC(NULL);
    for (Index = 0; Index < ARRAYSIZE(Parts); ++Index)
    {
        for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
        {
            ZeroMemory(&True[Dpi], sizeof(True[Dpi]));
            ZeroMemory(&Min[Dpi], sizeof(Min[Dpi]));
            ZeroMemory(&Draw[Dpi], sizeof(Draw[Dpi]));
            ZeroMemory(&Sizing[Dpi], sizeof(Sizing[Dpi]));
            ZeroMemory(&Content[Dpi], sizeof(Content[Dpi]));
            Theme = pOpenThemeDataForDpi(NULL, Parts[Index].Class, Dpis[Dpi]);
            if (!Theme)
                continue;
            SetRect(&Bounds, 0, 0, 200, 200);
            GetThemePartSize(Theme, Dc, Parts[Index].Part, Parts[Index].State, NULL, TS_TRUE, &True[Dpi]);
            GetThemePartSize(Theme, Dc, Parts[Index].Part, Parts[Index].State, NULL, TS_MIN, &Min[Dpi]);
            GetThemePartSize(Theme, Dc, Parts[Index].Part, Parts[Index].State, &Bounds, TS_DRAW, &Draw[Dpi]);
            GetThemeMargins(Theme, Dc, Parts[Index].Part, Parts[Index].State, TMT_SIZINGMARGINS, NULL,
                            &Sizing[Dpi]);
            GetThemeMargins(Theme, Dc, Parts[Index].Part, Parts[Index].State, TMT_CONTENTMARGINS, NULL,
                            &Content[Dpi]);
            CloseThemeData(Theme);
        }
        WideCharToMultiByte(CP_ACP, 0, Parts[Index].Class, -1, Class, sizeof(Class), NULL, NULL);
        TraceProperties(pOpenThemeDataForDpi, Class, Index);
        CheckTrueSizeScaling(pOpenThemeDataForDpi, Class, Index, True, Draw);
        if (Parts[Index].Scales)
            ok(Draw[4].cx * 2 > Draw[0].cx * 3 && Draw[4].cy * 2 > Draw[0].cy * 3,
               "%s %s: draw size %ldx%ld at 192 DPI does not scale from %ldx%ld\n", Class, Parts[Index].Name,
               Draw[4].cx, Draw[4].cy, Draw[0].cx, Draw[0].cy);
        TraceSizes(Class, Parts[Index].Name, "true", True);
        TraceSizes(Class, Parts[Index].Name, "min", Min);
        TraceSizes(Class, Parts[Index].Name, "draw", Draw);
        TraceMargins(Class, Parts[Index].Name, "sizing margins", Sizing);
        TraceMargins(Class, Parts[Index].Name, "content margins", Content);
    }
    ReleaseDC(NULL, Dc);
}

static void
TestSystemValues(PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext)
{
    static const struct
    {
        const char *Name;
        DPI_AWARENESS_CONTEXT Context;
    } Contexts[] =
    {
        { "unaware", DPI_AWARENESS_CONTEXT_UNAWARE },
        { "system", DPI_AWARENESS_CONTEXT_SYSTEM_AWARE },
        { "per-monitor v2", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 },
    };
    DPI_AWARENESS_CONTEXT Old;
    LOGFONTW Caption, Message, Icon;
    HTHEME Theme;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Contexts); ++Index)
    {
        Old = pSetThreadDpiAwarenessContext(Contexts[Index].Context);
        Theme = OpenThemeData(NULL, L"WINDOW");
        ZeroMemory(&Caption, sizeof(Caption));
        ZeroMemory(&Message, sizeof(Message));
        ZeroMemory(&Icon, sizeof(Icon));
        GetThemeSysFont(Theme, TMT_CAPTIONFONT, &Caption);
        GetThemeSysFont(Theme, TMT_MSGBOXFONT, &Message);
        GetThemeSysFont(Theme, TMT_ICONTITLEFONT, &Icon);
        trace("context %s: sys size scroll %d caption %d,%d small caption %d,%d menu %d,%d frame %d padded %d "
              "fonts caption %ld message %ld icon %ld\n",
              Contexts[Index].Name, GetThemeSysSize(Theme, SM_CXVSCROLL), GetThemeSysSize(Theme, SM_CXSIZE),
              GetThemeSysSize(Theme, SM_CYSIZE), GetThemeSysSize(Theme, SM_CXSMSIZE),
              GetThemeSysSize(Theme, SM_CYSMSIZE), GetThemeSysSize(Theme, SM_CXMENUSIZE),
              GetThemeSysSize(Theme, SM_CYMENUSIZE), GetThemeSysSize(Theme, SM_CXFRAME),
              GetThemeSysSize(Theme, SM_CXPADDEDBORDER), Caption.lfHeight, Message.lfHeight, Icon.lfHeight);
        trace("context %s: null sys size scroll %d caption %d\n", Contexts[Index].Name,
              GetThemeSysSize(NULL, SM_CXVSCROLL), GetThemeSysSize(NULL, SM_CYSIZE));
        if (Theme)
            CloseThemeData(Theme);
        pSetThreadDpiAwarenessContext(Old);
    }
}

START_TEST(ThemeDpi)
{
    HMODULE UxTheme = GetModuleHandleW(L"uxtheme.dll");
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    POPENTHEMEDATAFORDPI pOpenThemeDataForDpi;
    PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext;

    winetest_mute_threshold = 100000;
    if (!IsThemeActive())
    {
        skip("No visual style is active\n");
        return;
    }
    pOpenThemeDataForDpi = (POPENTHEMEDATAFORDPI)GetProcAddress(UxTheme, "OpenThemeDataForDpi");
    pSetThreadDpiAwarenessContext =
        (PSETTHREADDPIAWARENESSCONTEXT)GetProcAddress(User32, "SetThreadDpiAwarenessContext");
    ok(pOpenThemeDataForDpi != NULL, "OpenThemeDataForDpi is missing\n");
    ok(pSetThreadDpiAwarenessContext != NULL, "SetThreadDpiAwarenessContext is missing\n");
    if (pOpenThemeDataForDpi)
        TestPartsForDpi(pOpenThemeDataForDpi);
    if (pSetThreadDpiAwarenessContext)
        TestSystemValues(pSetThreadDpiAwarenessContext);
}
