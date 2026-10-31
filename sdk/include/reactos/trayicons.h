/*
 * PROJECT:     LiberNT shell
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared notification area icon metric
 */

#pragma once

#include <wingdi.h>
#include <uxtheme.h>
#include <reactos/colorscheme.h>

#define SHELL_TRAY_ICON_BASE 22
#define SHELL_TRAY_ICON_LIGHT 1000

#define SHELL_TMT_COMPOSITED 2204
#define SHELL_TMT_FILLCOLOR  3802

static inline INT
ShellTrayIconSize(VOID)
{
    INT nDpi = 0;
    HDC hdc = GetDC(NULL);

    if (hdc)
    {
        nDpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);
    }

    if (nDpi <= 0)
        nDpi = 96;

    return MulDiv(SHELL_TRAY_ICON_BASE, nDpi, 96);
}

static inline INT
ShellTrayIconRenderSize(VOID)
{
    return MulDiv(ShellTrayIconSize(), 4, 5);
}

static inline HTHEME
ShellOpenTaskbarMaterial(VOID)
{
    HTHEME hTheme;
    BOOL bComposited;

    if (DwmSettingsReadColorScheme() == DWM_COLOR_SCHEME_LIGHT)
    {
        hTheme = OpenThemeData(NULL, DWM_SHELL_MATERIAL(DWM_COLOR_SCHEME_LIGHT));
        bComposited = FALSE;
        if (hTheme && SUCCEEDED(GetThemeBool(hTheme, 0, 0, SHELL_TMT_COMPOSITED, &bComposited)) && bComposited)
            return hTheme;
        if (hTheme)
            CloseThemeData(hTheme);
    }
    return OpenThemeData(NULL, DWM_SHELL_MATERIAL(DWM_COLOR_SCHEME_DARK));
}

static inline BOOL
ShellColorIsLight(COLORREF cr)
{
    return (GetRValue(cr) * 299 + GetGValue(cr) * 587 + GetBValue(cr) * 114) / 1000 >= 128;
}

static inline BOOL
ShellTrayBackgroundIsLight(VOID)
{
    HTHEME hTheme;
    BOOL bComposited = FALSE;
    COLORREF cr = 0;

    if (!IsThemeActive())
        return ShellColorIsLight(GetSysColor(COLOR_3DFACE));
    hTheme = ShellOpenTaskbarMaterial();
    if (!hTheme)
        return FALSE;
    if (FAILED(GetThemeBool(hTheme, 0, 0, SHELL_TMT_COMPOSITED, &bComposited)) || !bComposited ||
        FAILED(GetThemeColor(hTheme, 0, 0, SHELL_TMT_FILLCOLOR, &cr)))
    {
        cr = 0;
    }
    CloseThemeData(hTheme);
    return ShellColorIsLight(cr);
}

static inline UINT
ShellTrayIconId(HINSTANCE hInstance, UINT nId)
{
    if (ShellTrayBackgroundIsLight() &&
        FindResourceW(hInstance, MAKEINTRESOURCEW(nId + SHELL_TRAY_ICON_LIGHT), (LPCWSTR)RT_GROUP_ICON))
        return nId + SHELL_TRAY_ICON_LIGHT;
    return nId;
}

static inline BOOL
ShellTrayColorsChanged(UINT uMsg, LPARAM lParam)
{
    return uMsg == WM_THEMECHANGED || uMsg == WM_SYSCOLORCHANGE ||
           (uMsg == WM_SETTINGCHANGE && lParam && !lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet"));
}
