/*
 * PROJECT:     LiberNT shell
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared notification area icon metric
 */

#pragma once

#include <wingdi.h>

#define SHELL_TRAY_ICON_BASE 22
#define SHELL_TRAY_ICON_LIGHT 1000

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

static inline BOOL
ShellSystemUsesLightTheme(VOID)
{
    DWORD Value, Size = sizeof(Value);

    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &Value, &Size) == ERROR_SUCCESS)
        return Value != 0;
    Size = sizeof(Value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ReactOS\\DWM", L"ColorScheme", RRF_RT_REG_DWORD, NULL,
                        &Value, &Size) == ERROR_SUCCESS && Value == 0;
}

static inline UINT
ShellTrayIconId(HINSTANCE hInstance, UINT nId)
{
    if (ShellSystemUsesLightTheme() &&
        FindResourceW(hInstance, MAKEINTRESOURCEW(nId + SHELL_TRAY_ICON_LIGHT), (LPCWSTR)RT_GROUP_ICON))
        return nId + SHELL_TRAY_ICON_LIGHT;
    return nId;
}
