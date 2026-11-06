/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a maximized window follows a display mode change
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static VOID
PumpFor(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    while (GetTickCount() - Start < Milliseconds)
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        Sleep(20);
    }
}

static VOID
GetInsets(HWND Window, RECT *Insets, RECT *Work)
{
    MONITORINFO Info = { sizeof(Info) };
    RECT Rect;

    GetWindowRect(Window, &Rect);
    GetMonitorInfoW(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &Info);
    *Work = Info.rcWork;
    Insets->left = Rect.left - Info.rcWork.left;
    Insets->top = Rect.top - Info.rcWork.top;
    Insets->right = Rect.right - Info.rcWork.right;
    Insets->bottom = Rect.bottom - Info.rcWork.bottom;
}

static VOID
GetWorkMargins(RECT *Margins)
{
    MONITORINFO Info = { sizeof(Info) };

    GetMonitorInfoW(MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &Info);
    Margins->left = Info.rcWork.left - Info.rcMonitor.left;
    Margins->top = Info.rcWork.top - Info.rcMonitor.top;
    Margins->right = Info.rcMonitor.right - Info.rcWork.right;
    Margins->bottom = Info.rcMonitor.bottom - Info.rcWork.bottom;
}

static VOID
CheckMaximized(HWND Window, PCSTR Step, const RECT *Expected, const DEVMODEW *Mode)
{
    RECT Insets, Work;

    GetInsets(Window, &Insets, &Work);
    ok(IsZoomed(Window), "%s: the window is not maximized\n", Step);
    ok(Work.right - Work.left <= (LONG)Mode->dmPelsWidth && Work.bottom - Work.top <= (LONG)Mode->dmPelsHeight,
       "%s: work area %s on a %lux%lu screen\n", Step, wine_dbgstr_rect(&Work), Mode->dmPelsWidth, Mode->dmPelsHeight);
    ok(EqualRect(&Insets, Expected), "%s: window insets %s from work area %s, expected %s\n",
       Step, wine_dbgstr_rect(&Insets), wine_dbgstr_rect(&Work), wine_dbgstr_rect(Expected));
}

START_TEST(DisplayChangeMaximized)
{
    DEVMODEW Current = { 0 }, Mode = { 0 }, Target = { 0 };
    RECT Insets, Work, Smaller, Before, After, Margins, NewMargins;
    HWND Window;
    ULONG Index;
    LONG Result;

    Current.dmSize = Mode.dmSize = sizeof(DEVMODEW);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Current), "No current settings\n");
    for (Index = 0; EnumDisplaySettingsW(NULL, Index, &Mode); Index++)
    {
        if (Mode.dmBitsPerPel == Current.dmBitsPerPel &&
            Mode.dmPelsWidth < Current.dmPelsWidth && Mode.dmPelsHeight < Current.dmPelsHeight)
            break;
    }
    if (Mode.dmPelsWidth >= Current.dmPelsWidth || Mode.dmPelsHeight >= Current.dmPelsHeight)
    {
        skip("The display has no smaller mode\n");
        return;
    }
    Target.dmSize = sizeof(Target);
    Target.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    Target.dmBitsPerPel = Mode.dmBitsPerPel;
    Target.dmPelsWidth = Mode.dmPelsWidth;
    Target.dmPelsHeight = Mode.dmPelsHeight;

    Window = CreateWindowW(L"STATIC", L"DisplayChangeMaximized", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowW failed with %lu\n", GetLastError());
    if (!Window)
        return;
    ShowWindow(Window, SW_MAXIMIZE);
    PumpFor(1000);
    GetInsets(Window, &Insets, &Work);
    ok(IsZoomed(Window), "The window is not maximized\n");
    trace("Maximized insets %s in work area %s\n", wine_dbgstr_rect(&Insets), wine_dbgstr_rect(&Work));

    GetWindowRect(Window, &Before);
    Smaller = Work;
    Smaller.bottom -= 40;
    ok(SystemParametersInfoW(SPI_SETWORKAREA, 0, &Smaller, 0), "SPI_SETWORKAREA failed with %lu\n", GetLastError());
    PumpFor(1000);
    GetWindowRect(Window, &After);
    ok(EqualRect(&Before, &After), "Smaller work area: window %s, expected %s\n",
       wine_dbgstr_rect(&After), wine_dbgstr_rect(&Before));
    ok(SystemParametersInfoW(SPI_SETWORKAREA, 0, &Smaller, SPIF_SENDCHANGE), "SPI_SETWORKAREA failed with %lu\n", GetLastError());
    PumpFor(1000);
    GetWindowRect(Window, &After);
    trace("Smaller work area with SPIF_SENDCHANGE: window %s\n", wine_dbgstr_rect(&After));
    ok(SystemParametersInfoW(SPI_SETWORKAREA, 0, &Work, SPIF_SENDCHANGE), "SPI_SETWORKAREA failed with %lu\n", GetLastError());
    PumpFor(1000);
    CheckMaximized(Window, "restored work area", &Insets, &Current);

    GetWorkMargins(&Margins);
    Result = ChangeDisplaySettingsExW(NULL, &Target, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Dynamic change returned %ld\n", Result);
    if (Result == DISP_CHANGE_SUCCESSFUL)
    {
        GetWorkMargins(&NewMargins);
        ok(EqualRect(&NewMargins, &Margins), "Work area margins %s after the change, expected %s\n",
           wine_dbgstr_rect(&NewMargins), wine_dbgstr_rect(&Margins));
        GetWindowRect(Window, &After);
        GetInsets(Window, &Smaller, &Work);
        trace("Right after the change: window %s, work area %s\n", wine_dbgstr_rect(&After), wine_dbgstr_rect(&Work));
        PumpFor(3000);
        CheckMaximized(Window, "smaller mode", &Insets, &Target);

        Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
        ok(Result == DISP_CHANGE_SUCCESSFUL, "Reset returned %ld\n", Result);
        PumpFor(3000);
        CheckMaximized(Window, "reset", &Insets, &Current);
    }

    DestroyWindow(Window);
    ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
}
