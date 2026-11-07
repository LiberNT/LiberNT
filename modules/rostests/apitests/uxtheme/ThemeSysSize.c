/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the DPI scaling of GetThemeSysSize and the theme metrics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>

static const int ThemeMetrics[] =
{
    SM_CXVSCROLL, SM_CYHSCROLL, SM_CXSIZE, SM_CYSIZE,
    SM_CXSMSIZE, SM_CYSMSIZE, SM_CXMENUSIZE, SM_CYMENUSIZE,
};

static UINT
ScreenDpi(void)
{
    HDC Dc = GetDC(NULL);
    UINT Dpi = Dc ? GetDeviceCaps(Dc, LOGPIXELSY) : 0;

    if (Dc)
        ReleaseDC(NULL, Dc);
    return Dpi;
}

START_TEST(ThemeSysSize)
{
    int Aware[ARRAYSIZE(ThemeMetrics)], Unaware[ARRAYSIZE(ThemeMetrics)];
    DPI_AWARENESS_CONTEXT Old;
    UINT AwareDpi, UnawareDpi, Index;
    int Metric;
    HTHEME Theme;

    if (!IsThemeActive())
    {
        skip("No visual style is active\n");
        return;
    }
    Theme = OpenThemeData(NULL, L"WINDOW");
    ok(Theme != NULL, "OpenThemeData failed with %lu\n", GetLastError());
    if (!Theme)
        return;

    Old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    ok(Old != NULL, "SetThreadDpiAwarenessContext failed with %lu\n", GetLastError());
    AwareDpi = ScreenDpi();
    for (Index = 0; Index < ARRAYSIZE(ThemeMetrics); ++Index)
        Aware[Index] = GetThemeSysSize(Theme, ThemeMetrics[Index]);
    Metric = GetSystemMetrics(SM_CXVSCROLL);
    ok(Aware[0] == Metric, "theme scroll bar width %d, system metric %d at %u DPI\n", Aware[0], Metric, AwareDpi);

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    UnawareDpi = ScreenDpi();
    for (Index = 0; Index < ARRAYSIZE(ThemeMetrics); ++Index)
        Unaware[Index] = GetThemeSysSize(Theme, ThemeMetrics[Index]);
    Metric = GetSystemMetrics(SM_CXVSCROLL);
    ok(Unaware[0] == Metric, "unaware theme scroll bar width %d, system metric %d at %u DPI\n",
       Unaware[0], Metric, UnawareDpi);

    if (Old)
        SetThreadDpiAwarenessContext(Old);

    ok(AwareDpi >= 96 && UnawareDpi >= 96, "screen DPI %u aware, %u unaware\n", AwareDpi, UnawareDpi);
    if (AwareDpi < 96 || UnawareDpi < 96)
    {
        CloseThemeData(Theme);
        return;
    }
    for (Index = 0; Index < ARRAYSIZE(ThemeMetrics); ++Index)
    {
        ok(Aware[Index] > 0, "metric %d: theme size %d\n", ThemeMetrics[Index], Aware[Index]);
        ok(abs(MulDiv(Unaware[Index], AwareDpi, UnawareDpi) - Aware[Index]) <= 1,
           "metric %d: %d at %u DPI does not scale to %d at %u DPI\n",
           ThemeMetrics[Index], Unaware[Index], UnawareDpi, Aware[Index], AwareDpi);
    }

    CloseThemeData(Theme);
}
