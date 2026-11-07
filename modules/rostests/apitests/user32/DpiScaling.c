/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tabulates the per-DPI system metrics, parameters and frame sizes to compare with Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

typedef INT (WINAPI *PGETSYSTEMMETRICSFORDPI)(INT, UINT);
typedef BOOL (WINAPI *PSYSTEMPARAMETERSINFOFORDPI)(UINT, UINT, PVOID, UINT, UINT);
typedef BOOL (WINAPI *PADJUSTWINDOWRECTEXFORDPI)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFORSYSTEM)(VOID);
typedef UINT (WINAPI *PGETDPIFORWINDOW)(HWND);

#define DPI_COUNT 7
#define METRIC_LIMIT 100

static const UINT Dpis[DPI_COUNT] = { 96, 120, 144, 168, 192, 240, 288 };

static PGETSYSTEMMETRICSFORDPI pGetSystemMetricsForDpi;
static PSYSTEMPARAMETERSINFOFORDPI pSystemParametersInfoForDpi;
static PADJUSTWINDOWRECTEXFORDPI pAdjustWindowRectExForDpi;
static PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext;
static PGETDPIFORSYSTEM pGetDpiForSystem;
static PGETDPIFORWINDOW pGetDpiForWindow;

static void
TraceRow(const char *Name, const INT *Values)
{
    trace("%s: %d %d %d %d %d %d %d\n", Name, Values[0], Values[1], Values[2], Values[3], Values[4], Values[5],
          Values[6]);
}

static void
TestMetricsForDpi(void)
{
    INT Values[DPI_COUNT];
    char Name[32];
    UINT Index, Dpi;
    BOOL Any;

    for (Index = 0; Index < METRIC_LIMIT; ++Index)
    {
        Any = FALSE;
        for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
        {
            Values[Dpi] = pGetSystemMetricsForDpi(Index, Dpis[Dpi]);
            Any |= Values[Dpi] != 0;
        }
        if (!Any)
            continue;
        sprintf(Name, "metric %u", Index);
        TraceRow(Name, Values);
    }

    SetLastError(0xdeadbeef);
    ok(pGetSystemMetricsForDpi(SM_CXVSCROLL, 0) == 0, "SM_CXVSCROLL at DPI 0 is not 0\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "metric at DPI 0 error %lu\n", GetLastError());
}

static INT
ScaleRound(INT Value, UINT Dpi)
{
    if (Value < 0)
        return -MulDiv(-Value, Dpi, 96);
    return MulDiv(Value, Dpi, 96);
}

static INT
CaptionCharWidth(const LOGFONTW *LogFont)
{
    TEXTMETRICW Tm;
    HFONT Font = CreateFontIndirectW(LogFont), Old;
    HDC Dc = GetDC(NULL);
    LONG Width;

    Old = SelectObject(Dc, Font);
    Width = GdiGetCharDimensions(Dc, &Tm, NULL);
    SelectObject(Dc, Old);
    DeleteObject(Font);
    ReleaseDC(NULL, Dc);
    return Width;
}

static INT
ReadWindowMetric(PCWSTR Name)
{
    WCHAR Value[32];
    DWORD Size = sizeof(Value), Type;
    HKEY Key;
    LONG Error;
    INT Metric;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ, &Key))
        return -1;
    Error = RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Value, &Size);
    RegCloseKey(Key);
    if (Error || Type != REG_SZ)
        return -1;
    Metric = _wtoi(Value);
    return Metric < 0 ? MulDiv(-Metric, 96, 1440) : Metric;
}

static void
TestMetricRulesForDpi(void)
{
    static const INT Scaled[] =
    {
        SM_CXVSCROLL, SM_CYHSCROLL, SM_CYVTHUMB, SM_CXHTHUMB, SM_CYVSCROLL, SM_CXHSCROLL, SM_CYSIZE,
        SM_CXSMSIZE, SM_CYSMSIZE, SM_CXMENUSIZE, SM_CYMENUSIZE, SM_CXICONSPACING, SM_CYICONSPACING,
        SM_CXPADDEDBORDER,
    };
    static const INT Fixed[] =
    {
        SM_CXSCREEN, SM_CYSCREEN, SM_CXBORDER, SM_CYBORDER, SM_CXDLGFRAME, SM_CYDLGFRAME, SM_CXEDGE, SM_CYEDGE,
        SM_CXDOUBLECLK, SM_CYDOUBLECLK, SM_CXMINIMIZED, SM_CYMINIMIZED, SM_CXMINSPACING, SM_CYMINSPACING,
        SM_CXFULLSCREEN, SM_CYFULLSCREEN, SM_CXMAXTRACK, SM_CYMAXTRACK,
    };
    static const INT Cursor[DPI_COUNT] = { 32, 32, 48, 48, 64, 64, 96 };
    NONCLIENTMETRICSW Base, Ncm;
    MINIMIZEDMETRICS Mm;
    UINT Dpi, Index;
    INT Frame;

    ZeroMemory(&Base, sizeof(Base));
    Base.cbSize = sizeof(Base);
    pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Base), &Base, 0, 96);
    Frame = ReadWindowMetric(L"BorderWidth");
    ok(Frame < 0 || Base.iBorderWidth == Frame, "border at 96 DPI is %d, the registry holds %d\n", Base.iBorderWidth,
       Frame);
    Frame = ReadWindowMetric(L"PaddedBorderWidth");
    ok(Frame < 0 || Base.iPaddedBorderWidth == Frame, "padded border at 96 DPI is %d, the registry holds %d\n",
       Base.iPaddedBorderWidth, Frame);
    for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
    {
        for (Index = 0; Index < ARRAYSIZE(Scaled); ++Index)
            ok(pGetSystemMetricsForDpi(Scaled[Index], Dpis[Dpi]) ==
               ScaleRound(pGetSystemMetricsForDpi(Scaled[Index], 96), Dpis[Dpi]),
               "metric %d at %u DPI is %d, expected %d scaled from 96 DPI\n", Scaled[Index], Dpis[Dpi],
               pGetSystemMetricsForDpi(Scaled[Index], Dpis[Dpi]), pGetSystemMetricsForDpi(Scaled[Index], 96));
        for (Index = 0; Index < ARRAYSIZE(Fixed); ++Index)
            ok(pGetSystemMetricsForDpi(Fixed[Index], Dpis[Dpi]) == pGetSystemMetricsForDpi(Fixed[Index], 96),
               "metric %d at %u DPI is %d, the 96 DPI value is %d\n", Fixed[Index], Dpis[Dpi],
               pGetSystemMetricsForDpi(Fixed[Index], Dpis[Dpi]), pGetSystemMetricsForDpi(Fixed[Index], 96));
        ok(pGetSystemMetricsForDpi(SM_CXICON, Dpis[Dpi]) == ScaleRound(32, Dpis[Dpi]), "SM_CXICON at %u DPI is %d\n",
           Dpis[Dpi], pGetSystemMetricsForDpi(SM_CXICON, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CXSMICON, Dpis[Dpi]) == ScaleRound(16, Dpis[Dpi]),
           "SM_CXSMICON at %u DPI is %d\n", Dpis[Dpi], pGetSystemMetricsForDpi(SM_CXSMICON, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CXCURSOR, Dpis[Dpi]) == Cursor[Dpi] &&
           pGetSystemMetricsForDpi(SM_CYCURSOR, Dpis[Dpi]) == Cursor[Dpi], "cursor at %u DPI is %dx%d\n",
           Dpis[Dpi], pGetSystemMetricsForDpi(SM_CXCURSOR, Dpis[Dpi]), pGetSystemMetricsForDpi(SM_CYCURSOR, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CYCAPTION, Dpis[Dpi]) == pGetSystemMetricsForDpi(SM_CYSIZE, Dpis[Dpi]) + 1,
           "SM_CYCAPTION at %u DPI is %d\n", Dpis[Dpi], pGetSystemMetricsForDpi(SM_CYCAPTION, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CYSMCAPTION, Dpis[Dpi]) == pGetSystemMetricsForDpi(SM_CYSMSIZE, Dpis[Dpi]) + 1,
           "SM_CYSMCAPTION at %u DPI is %d\n", Dpis[Dpi], pGetSystemMetricsForDpi(SM_CYSMCAPTION, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CYMENU, Dpis[Dpi]) == pGetSystemMetricsForDpi(SM_CYMENUSIZE, Dpis[Dpi]) + 1,
           "SM_CYMENU at %u DPI is %d\n", Dpis[Dpi], pGetSystemMetricsForDpi(SM_CYMENU, Dpis[Dpi]));

        ZeroMemory(&Ncm, sizeof(Ncm));
        Ncm.cbSize = sizeof(Ncm);
        pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0, Dpis[Dpi]);
        ok(Ncm.iBorderWidth == ScaleRound(Base.iBorderWidth, Dpis[Dpi]) &&
           Ncm.iPaddedBorderWidth == ScaleRound(Base.iPaddedBorderWidth, Dpis[Dpi]),
           "border %d padded %d at %u DPI, %d and %d at 96 DPI\n", Ncm.iBorderWidth, Ncm.iPaddedBorderWidth,
           Dpis[Dpi], Base.iBorderWidth, Base.iPaddedBorderWidth);
        ok(Ncm.lfCaptionFont.lfHeight == ScaleRound(Base.lfCaptionFont.lfHeight, Dpis[Dpi]) &&
           Ncm.lfMessageFont.lfHeight == ScaleRound(Base.lfMessageFont.lfHeight, Dpis[Dpi]),
           "caption font %ld message font %ld at %u DPI\n", Ncm.lfCaptionFont.lfHeight,
           Ncm.lfMessageFont.lfHeight, Dpis[Dpi]);
        Frame = pGetSystemMetricsForDpi(SM_CXFRAME, Dpis[Dpi]);
        ok(Frame == 3 + Ncm.iBorderWidth, "SM_CXFRAME at %u DPI is %d, border %d\n", Dpis[Dpi], Frame,
           Ncm.iBorderWidth);
        ok(pGetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpis[Dpi]) == Ncm.iPaddedBorderWidth,
           "SM_CXPADDEDBORDER at %u DPI is %d, padded %d\n", Dpis[Dpi],
           pGetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpis[Dpi]), Ncm.iPaddedBorderWidth);
        ok(pGetSystemMetricsForDpi(SM_CYMIN, Dpis[Dpi]) ==
           pGetSystemMetricsForDpi(SM_CYCAPTION, Dpis[Dpi]) + 2 * (Frame + Ncm.iPaddedBorderWidth),
           "SM_CYMIN at %u DPI is %d\n", Dpis[Dpi], pGetSystemMetricsForDpi(SM_CYMIN, Dpis[Dpi]));
        ok(pGetSystemMetricsForDpi(SM_CXMIN, Dpis[Dpi]) ==
           4 * pGetSystemMetricsForDpi(SM_CYSIZE, Dpis[Dpi]) + 4 + 4 * CaptionCharWidth(&Ncm.lfCaptionFont) +
           2 * (Frame + Ncm.iPaddedBorderWidth),
           "SM_CXMIN at %u DPI is %d, caption character width %d\n", Dpis[Dpi],
           pGetSystemMetricsForDpi(SM_CXMIN, Dpis[Dpi]), CaptionCharWidth(&Ncm.lfCaptionFont));
        ok(pGetSystemMetricsForDpi(SM_CXMINTRACK, Dpis[Dpi]) == pGetSystemMetricsForDpi(SM_CXMIN, Dpis[Dpi]) &&
           pGetSystemMetricsForDpi(SM_CYMINTRACK, Dpis[Dpi]) == pGetSystemMetricsForDpi(SM_CYMIN, Dpis[Dpi]),
           "minimum track size at %u DPI differs from the minimum size\n", Dpis[Dpi]);
    }

    ZeroMemory(&Mm, sizeof(Mm));
    Mm.cbSize = sizeof(Mm);
    SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(Mm), &Mm, 0);
    ok(pGetSystemMetricsForDpi(SM_CXMINIMIZED, 96) == Mm.iWidth + 6, "SM_CXMINIMIZED %d, minimized width %d\n",
       pGetSystemMetricsForDpi(SM_CXMINIMIZED, 96), Mm.iWidth);
}

static void
TestParametersForDpi(void)
{
    INT Border[DPI_COUNT], ScrollW[DPI_COUNT], ScrollH[DPI_COUNT], CapW[DPI_COUNT], CapH[DPI_COUNT];
    INT SmCapW[DPI_COUNT], SmCapH[DPI_COUNT], MenuW[DPI_COUNT], MenuH[DPI_COUNT], Padded[DPI_COUNT];
    INT CapFont[DPI_COUNT], SmCapFont[DPI_COUNT], MenuFont[DPI_COUNT], StatusFont[DPI_COUNT], MsgFont[DPI_COUNT];
    INT HorzSpacing[DPI_COUNT], VertSpacing[DPI_COUNT], IconFont[DPI_COUNT], TitleFont[DPI_COUNT];
    NONCLIENTMETRICSW Ncm;
    MINIMIZEDMETRICS Mm;
    ICONMETRICSW Im;
    LOGFONTW Font;
    RECT Rect;
    UINT Dpi;
    BOOL Result;

    for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
    {
        ZeroMemory(&Ncm, sizeof(Ncm));
        Ncm.cbSize = sizeof(Ncm);
        Result = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0, Dpis[Dpi]);
        ok(Result, "SPI_GETNONCLIENTMETRICS at %u DPI failed with %lu\n", Dpis[Dpi], GetLastError());
        Border[Dpi] = Ncm.iBorderWidth;
        ScrollW[Dpi] = Ncm.iScrollWidth;
        ScrollH[Dpi] = Ncm.iScrollHeight;
        CapW[Dpi] = Ncm.iCaptionWidth;
        CapH[Dpi] = Ncm.iCaptionHeight;
        SmCapW[Dpi] = Ncm.iSmCaptionWidth;
        SmCapH[Dpi] = Ncm.iSmCaptionHeight;
        MenuW[Dpi] = Ncm.iMenuWidth;
        MenuH[Dpi] = Ncm.iMenuHeight;
        Padded[Dpi] = Ncm.iPaddedBorderWidth;
        CapFont[Dpi] = Ncm.lfCaptionFont.lfHeight;
        SmCapFont[Dpi] = Ncm.lfSmCaptionFont.lfHeight;
        MenuFont[Dpi] = Ncm.lfMenuFont.lfHeight;
        StatusFont[Dpi] = Ncm.lfStatusFont.lfHeight;
        MsgFont[Dpi] = Ncm.lfMessageFont.lfHeight;

        ZeroMemory(&Im, sizeof(Im));
        Im.cbSize = sizeof(Im);
        Result = pSystemParametersInfoForDpi(SPI_GETICONMETRICS, sizeof(Im), &Im, 0, Dpis[Dpi]);
        ok(Result, "SPI_GETICONMETRICS at %u DPI failed with %lu\n", Dpis[Dpi], GetLastError());
        HorzSpacing[Dpi] = Im.iHorzSpacing;
        VertSpacing[Dpi] = Im.iVertSpacing;
        IconFont[Dpi] = Im.lfFont.lfHeight;

        ZeroMemory(&Font, sizeof(Font));
        Result = pSystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(Font), &Font, 0, Dpis[Dpi]);
        ok(Result, "SPI_GETICONTITLELOGFONT at %u DPI failed with %lu\n", Dpis[Dpi], GetLastError());
        TitleFont[Dpi] = Font.lfHeight;
    }
    TraceRow("ncm border", Border);
    TraceRow("ncm scroll width", ScrollW);
    TraceRow("ncm scroll height", ScrollH);
    TraceRow("ncm caption width", CapW);
    TraceRow("ncm caption height", CapH);
    TraceRow("ncm small caption width", SmCapW);
    TraceRow("ncm small caption height", SmCapH);
    TraceRow("ncm menu width", MenuW);
    TraceRow("ncm menu height", MenuH);
    TraceRow("ncm padded border", Padded);
    TraceRow("ncm caption font", CapFont);
    TraceRow("ncm small caption font", SmCapFont);
    TraceRow("ncm menu font", MenuFont);
    TraceRow("ncm status font", StatusFont);
    TraceRow("ncm message font", MsgFont);
    TraceRow("icon horizontal spacing", HorzSpacing);
    TraceRow("icon vertical spacing", VertSpacing);
    TraceRow("icon font", IconFont);
    TraceRow("icon title font", TitleFont);

    SetLastError(0xdeadbeef);
    Result = pSystemParametersInfoForDpi(SPI_GETWORKAREA, 0, &Rect, 0, 192);
    ok(!Result, "SPI_GETWORKAREA was accepted by SystemParametersInfoForDpi\n");
    trace("SPI_GETWORKAREA for DPI: %d error %lu\n", Result, GetLastError());

    ZeroMemory(&Ncm, sizeof(Ncm));
    Ncm.cbSize = sizeof(Ncm);
    SetLastError(0xdeadbeef);
    Result = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0, 0);
    trace("SPI_GETNONCLIENTMETRICS at DPI 0: %d error %lu scroll %d\n", Result, GetLastError(), Ncm.iScrollWidth);
    ok(Result, "SPI_GETNONCLIENTMETRICS at DPI 0 failed with %lu\n", GetLastError());
    ok(Ncm.iScrollWidth == pGetSystemMetricsForDpi(SM_CXVSCROLL, pGetDpiForSystem()),
       "SPI_GETNONCLIENTMETRICS at DPI 0 gave scroll width %d\n", Ncm.iScrollWidth);

    ZeroMemory(&Mm, sizeof(Mm));
    Mm.cbSize = sizeof(Mm);
    Result = SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(Mm), &Mm, 0);
    trace("minimized metrics: %d width %d horizontal gap %d vertical gap %d arrange %d\n", Result, Mm.iWidth,
          Mm.iHorzGap, Mm.iVertGap, Mm.iArrange);
}

static void
TestMenuFontForDpi(void)
{
    INT Height[DPI_COUNT], Ascent[DPI_COUNT], Descent[DPI_COUNT], Internal[DPI_COUNT], External[DPI_COUNT];
    INT Average[DPI_COUNT], CaptionAverage[DPI_COUNT];
    NONCLIENTMETRICSW Ncm;
    TEXTMETRICW Tm;
    HFONT Font, Old;
    UINT Dpi;
    HDC Dc = GetDC(NULL);

    for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
    {
        ZeroMemory(&Ncm, sizeof(Ncm));
        Ncm.cbSize = sizeof(Ncm);
        pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0, Dpis[Dpi]);
        ZeroMemory(&Tm, sizeof(Tm));
        Font = CreateFontIndirectW(&Ncm.lfMenuFont);
        Old = SelectObject(Dc, Font);
        GetTextMetricsW(Dc, &Tm);
        SelectObject(Dc, Old);
        DeleteObject(Font);
        ok(pGetSystemMetricsForDpi(SM_CXMENUCHECK, Dpis[Dpi]) == ((Tm.tmHeight + Tm.tmExternalLeading - 1) | 1),
           "SM_CXMENUCHECK at %u DPI is %d, menu font height %ld\n", Dpis[Dpi],
           pGetSystemMetricsForDpi(SM_CXMENUCHECK, Dpis[Dpi]), Tm.tmHeight);
        Height[Dpi] = Tm.tmHeight;
        Ascent[Dpi] = Tm.tmAscent;
        Descent[Dpi] = Tm.tmDescent;
        Internal[Dpi] = Tm.tmInternalLeading;
        External[Dpi] = Tm.tmExternalLeading;
        Average[Dpi] = Tm.tmAveCharWidth;
        ZeroMemory(&Tm, sizeof(Tm));
        Font = CreateFontIndirectW(&Ncm.lfCaptionFont);
        Old = SelectObject(Dc, Font);
        GetTextMetricsW(Dc, &Tm);
        SelectObject(Dc, Old);
        DeleteObject(Font);
        CaptionAverage[Dpi] = Tm.tmAveCharWidth;
    }
    ReleaseDC(NULL, Dc);
    trace("menu font face %ls\n", Ncm.lfMenuFont.lfFaceName);
    TraceRow("menu font height", Height);
    TraceRow("menu font ascent", Ascent);
    TraceRow("menu font descent", Descent);
    TraceRow("menu font internal leading", Internal);
    TraceRow("menu font external leading", External);
    TraceRow("menu font average width", Average);
    TraceRow("caption font average width", CaptionAverage);
}

static void
TestFramesForDpi(void)
{
    static const struct
    {
        const char *Name;
        DWORD Style;
        DWORD ExStyle;
        BOOL Menu;
    } Frames[] =
    {
        { "overlapped", WS_OVERLAPPEDWINDOW, 0, FALSE },
        { "overlapped menu", WS_OVERLAPPEDWINDOW, 0, TRUE },
        { "caption", WS_CAPTION | WS_SYSMENU, 0, FALSE },
        { "dialog", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, FALSE },
        { "tool window", WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, FALSE },
        { "popup border", WS_POPUP | WS_BORDER, 0, FALSE },
        { "popup thick", WS_POPUP | WS_THICKFRAME, 0, FALSE },
        { "child client edge", WS_CHILD, WS_EX_CLIENTEDGE, FALSE },
        { "child static edge", WS_CHILD, WS_EX_STATICEDGE, FALSE },
    };
    INT Left[DPI_COUNT], Top[DPI_COUNT], Right[DPI_COUNT], Bottom[DPI_COUNT];
    char Name[64];
    UINT Frame, Dpi;
    RECT Rect;

    for (Frame = 0; Frame < ARRAYSIZE(Frames); ++Frame)
    {
        for (Dpi = 0; Dpi < DPI_COUNT; ++Dpi)
        {
            SetRect(&Rect, 0, 0, 100, 100);
            ok(pAdjustWindowRectExForDpi(&Rect, Frames[Frame].Style, Frames[Frame].Menu, Frames[Frame].ExStyle,
                                         Dpis[Dpi]),
               "AdjustWindowRectExForDpi %s at %u DPI failed with %lu\n", Frames[Frame].Name, Dpis[Dpi],
               GetLastError());
            Left[Dpi] = -Rect.left;
            Top[Dpi] = -Rect.top;
            Right[Dpi] = Rect.right - 100;
            Bottom[Dpi] = Rect.bottom - 100;
        }
        sprintf(Name, "frame %s left", Frames[Frame].Name);
        TraceRow(Name, Left);
        sprintf(Name, "frame %s top", Frames[Frame].Name);
        TraceRow(Name, Top);
        sprintf(Name, "frame %s right", Frames[Frame].Name);
        TraceRow(Name, Right);
        sprintf(Name, "frame %s bottom", Frames[Frame].Name);
        TraceRow(Name, Bottom);
    }
}

static void
TestThreadContexts(void)
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
    NONCLIENTMETRICSW Ncm;
    LOGFONTW Font;
    RECT Window, Client, Adjusted;
    UINT Index, WindowDpi;
    HWND hWnd;
    HDC Dc;
    INT Pixels;

    for (Index = 0; Index < ARRAYSIZE(Contexts); ++Index)
    {
        Old = pSetThreadDpiAwarenessContext(Contexts[Index].Context);
        ok(Old != NULL, "SetThreadDpiAwarenessContext %s failed with %lu\n", Contexts[Index].Name, GetLastError());
        Dc = GetDC(NULL);
        Pixels = GetDeviceCaps(Dc, LOGPIXELSY);
        ReleaseDC(NULL, Dc);
        ZeroMemory(&Ncm, sizeof(Ncm));
        Ncm.cbSize = sizeof(Ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0);
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(Font), &Font, 0);
        trace("context %s: dpi %u logpixels %d vscroll %d caption %d size %d icon %d smicon %d screen %d frame %d "
              "ncm scroll %d caption %d font %ld title font %ld border %d padded %d sm padded %d\n",
              Contexts[Index].Name, pGetDpiForSystem(), Pixels, GetSystemMetrics(SM_CXVSCROLL),
              GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CXSIZE), GetSystemMetrics(SM_CXICON),
              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CXFRAME),
              Ncm.iScrollWidth, Ncm.iCaptionHeight, Ncm.lfCaptionFont.lfHeight, Font.lfHeight, Ncm.iBorderWidth,
              Ncm.iPaddedBorderWidth, GetSystemMetrics(SM_CXPADDEDBORDER));
        ok(GetSystemMetrics(SM_CXVSCROLL) == pGetSystemMetricsForDpi(SM_CXVSCROLL, pGetDpiForSystem()),
           "context %s: SM_CXVSCROLL %d differs from the value for %u DPI %d\n", Contexts[Index].Name,
           GetSystemMetrics(SM_CXVSCROLL), pGetDpiForSystem(),
           pGetSystemMetricsForDpi(SM_CXVSCROLL, pGetDpiForSystem()));
        ok(Ncm.iScrollWidth == GetSystemMetrics(SM_CXVSCROLL), "context %s: ncm scroll %d, SM_CXVSCROLL %d\n",
           Contexts[Index].Name, Ncm.iScrollWidth, GetSystemMetrics(SM_CXVSCROLL));
        ok(Pixels == (INT)pGetDpiForSystem(), "context %s: LOGPIXELSY %d, GetDpiForSystem %u\n",
           Contexts[Index].Name, Pixels, pGetDpiForSystem());

        hWnd = CreateWindowExW(0, L"static", L"DpiScaling", WS_OVERLAPPEDWINDOW, 10, 10, 300, 200, NULL, NULL,
                               NULL, NULL);
        ok(hWnd != NULL, "CreateWindowExW %s failed with %lu\n", Contexts[Index].Name, GetLastError());
        if (hWnd)
        {
            WindowDpi = pGetDpiForWindow(hWnd);
            GetWindowRect(hWnd, &Window);
            GetClientRect(hWnd, &Client);
            SetRect(&Adjusted, 0, 0, Client.right, Client.bottom);
            pAdjustWindowRectExForDpi(&Adjusted, WS_OVERLAPPEDWINDOW, FALSE, 0, WindowDpi);
            trace("context %s: window dpi %u window %ldx%ld client %ldx%ld adjusted %ldx%ld\n",
                  Contexts[Index].Name, WindowDpi, Window.right - Window.left, Window.bottom - Window.top,
                  Client.right, Client.bottom, Adjusted.right - Adjusted.left, Adjusted.bottom - Adjusted.top);
            ok(Window.right - Window.left == 300 && Window.bottom - Window.top == 200,
               "context %s: window is %ldx%ld\n", Contexts[Index].Name, Window.right - Window.left,
               Window.bottom - Window.top);
            ok(Adjusted.right - Adjusted.left == 300 && Adjusted.bottom - Adjusted.top == 200,
               "context %s: client %ldx%ld adjusts to %ldx%ld at %u DPI\n", Contexts[Index].Name, Client.right,
               Client.bottom, Adjusted.right - Adjusted.left, Adjusted.bottom - Adjusted.top, WindowDpi);
            DestroyWindow(hWnd);
        }
        pSetThreadDpiAwarenessContext(Old);
    }
}

START_TEST(DpiScaling)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");

    winetest_mute_threshold = 100000;
    pGetSystemMetricsForDpi = (PGETSYSTEMMETRICSFORDPI)GetProcAddress(User32, "GetSystemMetricsForDpi");
    pSystemParametersInfoForDpi = (PSYSTEMPARAMETERSINFOFORDPI)GetProcAddress(User32, "SystemParametersInfoForDpi");
    pAdjustWindowRectExForDpi = (PADJUSTWINDOWRECTEXFORDPI)GetProcAddress(User32, "AdjustWindowRectExForDpi");
    pSetThreadDpiAwarenessContext =
        (PSETTHREADDPIAWARENESSCONTEXT)GetProcAddress(User32, "SetThreadDpiAwarenessContext");
    pGetDpiForSystem = (PGETDPIFORSYSTEM)GetProcAddress(User32, "GetDpiForSystem");
    pGetDpiForWindow = (PGETDPIFORWINDOW)GetProcAddress(User32, "GetDpiForWindow");
    ok(pGetSystemMetricsForDpi && pSystemParametersInfoForDpi && pAdjustWindowRectExForDpi &&
       pSetThreadDpiAwarenessContext && pGetDpiForSystem && pGetDpiForWindow,
       "The per-DPI user32 functions are missing\n");
    if (!pGetSystemMetricsForDpi || !pSystemParametersInfoForDpi || !pAdjustWindowRectExForDpi ||
        !pSetThreadDpiAwarenessContext || !pGetDpiForSystem || !pGetDpiForWindow)
        return;

    TestMetricsForDpi();
    TestMetricRulesForDpi();
    TestParametersForDpi();
    TestMenuFontForDpi();
    TestFramesForDpi();
    TestThreadContexts();
}
