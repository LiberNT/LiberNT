/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the dark mode opt-in of an application and of its windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

typedef int (WINAPI *PFN_SET_PREFERRED_APP_MODE)(int);
typedef BOOLEAN (WINAPI *PFN_ALLOW_DARK_MODE_FOR_WINDOW)(HWND, BOOLEAN);
typedef BOOLEAN (WINAPI *PFN_IS_DARK_MODE_ALLOWED_FOR_WINDOW)(HWND);

enum { AppModeDefault, AppModeAllowDark, AppModeForceDark, AppModeForceLight };

static COLORREF
ItemsViewColor(HWND hWnd, int Property)
{
    COLORREF Color = CLR_INVALID;
    HTHEME hTheme = OpenThemeData(hWnd, L"ItemsView");

    if (!hTheme)
        return CLR_INVALID;
    if (FAILED(GetThemeColor(hTheme, 0, 0, Property, &Color)))
        Color = CLR_INVALID;
    CloseThemeData(hTheme);
    return Color;
}

START_TEST(PreferredAppMode)
{
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    PFN_SET_PREFERRED_APP_MODE pSetPreferredAppMode;
    PFN_ALLOW_DARK_MODE_FOR_WINDOW pAllowDarkModeForWindow;
    PFN_IS_DARK_MODE_ALLOWED_FOR_WINDOW pIsDarkModeAllowedForWindow;
    COLORREF DarkText, LightText, DarkFill, LightFill;
    HWND hWnd;
    int Previous;

    pSetPreferredAppMode = (PFN_SET_PREFERRED_APP_MODE)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
    pAllowDarkModeForWindow = (PFN_ALLOW_DARK_MODE_FOR_WINDOW)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
    pIsDarkModeAllowedForWindow = (PFN_IS_DARK_MODE_ALLOWED_FOR_WINDOW)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(137));
    if (!pSetPreferredAppMode || !pAllowDarkModeForWindow || !pIsDarkModeAllowedForWindow)
    {
        skip("Dark mode ordinals are not exported\n");
        return;
    }

    Previous = pSetPreferredAppMode(AppModeAllowDark);
    ok(Previous == AppModeDefault, "first SetPreferredAppMode returned %d\n", Previous);
    Previous = pSetPreferredAppMode(AppModeForceDark);
    ok(Previous == AppModeAllowDark, "SetPreferredAppMode returned %d, expected the previous mode\n", Previous);

    hWnd = CreateWindowExW(0, L"STATIC", L"PreferredAppMode", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                           NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hWnd)
        return;

    ok(!pIsDarkModeAllowedForWindow(hWnd), "a new window allows dark mode\n");
    ok(pAllowDarkModeForWindow(hWnd, TRUE), "AllowDarkModeForWindow(TRUE) failed\n");
    ok(pIsDarkModeAllowedForWindow(hWnd), "IsDarkModeAllowedForWindow is FALSE after allowing\n");
    ok(pAllowDarkModeForWindow(hWnd, FALSE), "AllowDarkModeForWindow(FALSE) failed\n");
    ok(!pIsDarkModeAllowedForWindow(hWnd), "IsDarkModeAllowedForWindow is TRUE after disallowing\n");

    if (IsThemeActive())
    {
        DarkText = ItemsViewColor(NULL, TMT_TEXTCOLOR);
        DarkFill = ItemsViewColor(NULL, TMT_FILLCOLOR);
        pSetPreferredAppMode(AppModeForceLight);
        LightText = ItemsViewColor(NULL, TMT_TEXTCOLOR);
        LightFill = ItemsViewColor(NULL, TMT_FILLCOLOR);
        trace("ItemsView dark text %06lx fill %06lx, light text %06lx fill %06lx\n",
              DarkText, DarkFill, LightText, LightFill);
        ok(DarkText != CLR_INVALID && LightText != CLR_INVALID, "ItemsView has no text color\n");
        ok(DarkText != LightText, "ForceDark and ForceLight give the same ItemsView text color\n");
        ok(DarkFill != LightFill, "ForceDark and ForceLight give the same ItemsView fill color\n");
    }
    else
    {
        skip("Theme not active\n");
    }

    pSetPreferredAppMode(AppModeDefault);
    DestroyWindow(hWnd);
}
