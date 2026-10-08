/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests window DC lookup and that a temporary display mode change gives a full, unpanned desktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

static BOOL PickMode(const DEVMODEW *Current, DEVMODEW *Picked)
{
    ULONGLONG CurrentArea = (ULONGLONG)Current->dmPelsWidth * Current->dmPelsHeight;
    ULONGLONG Area, PickedArea = 0;
    BOOL PickedLarger = FALSE, Larger;
    DEVMODEW Mode;
    DWORD Index;

    for (Index = 0; ; ++Index)
    {
        memset(&Mode, 0, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsW(NULL, Index, &Mode))
            break;
        if (Mode.dmBitsPerPel != Current->dmBitsPerPel ||
            (Mode.dmPelsWidth == Current->dmPelsWidth && Mode.dmPelsHeight == Current->dmPelsHeight) ||
            Mode.dmPelsWidth < 640 || Mode.dmPelsHeight < 480)
        {
            continue;
        }

        Area = (ULONGLONG)Mode.dmPelsWidth * Mode.dmPelsHeight;
        Larger = Area > CurrentArea;
        if (!PickedArea || (Larger && !PickedLarger) ||
            (Larger == PickedLarger && (Larger ? Area < PickedArea : Area > PickedArea)))
        {
            *Picked = Mode;
            PickedArea = Area;
            PickedLarger = Larger;
        }
    }
    return PickedArea != 0;
}

START_TEST(window_dc)
{
    static const WCHAR ClassName[] = L"AppSmokeOwnDc";
    UINT Index, High = 0, WrongWindow = 0, NotReleased = 0;
    WNDCLASSW Class;
    HWND Window;
    HDC Dc;

    memset(&Class, 0, sizeof(Class));
    Class.style = CS_OWNDC;
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    ok(RegisterClassW(&Class) != 0, "RegisterClass: %lu\n", GetLastError());

    for (Index = 0; Index < 96; ++Index)
    {
        Window = CreateWindowExW(0, ClassName, NULL, WS_POPUP, 0, 0, 32, 32, NULL, NULL, Class.hInstance, NULL);
        Dc = Window ? GetDC(Window) : NULL;
        if (!Dc)
        {
            ok(0, "Window %u has no DC: %lu\n", Index, GetLastError());
            if (Window)
                DestroyWindow(Window);
            break;
        }

        if ((ULONG_PTR)Dc & 0x80000000)
            ++High;
        if (WindowFromDC(Dc) != Window)
            ++WrongWindow;
        if (ReleaseDC(Window, Dc) != 1)
            ++NotReleased;
        DestroyWindow(Window);
    }

    ok(WrongWindow == 0, "WindowFromDC missed the window of %u of %u DCs\n", WrongWindow, Index);
    ok(NotReleased == 0, "ReleaseDC failed for %u of %u DCs\n", NotReleased, Index);
    trace("%u of %u DC handles had the top bit set\n", High, Index);
    UnregisterClassW(ClassName, Class.hInstance);
}

START_TEST(display_mode)
{
    DEVMODEW Current, Picked, Request, Applied;
    MONITORINFO Monitor;
    POINT Origin = { 0, 0 };
    DWORD Start, Elapsed;
    LONG Result;
    HDC Screen;

    memset(&Current, 0, sizeof(Current));
    Current.dmSize = sizeof(Current);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Current))
    {
        skip("No current display mode\n");
        return;
    }
    if (!PickMode(&Current, &Picked))
    {
        skip("The display has no second mode at %lu bits per pixel\n", Current.dmBitsPerPel);
        return;
    }

    memset(&Request, 0, sizeof(Request));
    Request.dmSize = sizeof(Request);
    Request.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    Request.dmPelsWidth = Picked.dmPelsWidth;
    Request.dmPelsHeight = Picked.dmPelsHeight;
    Request.dmBitsPerPel = Picked.dmBitsPerPel;
    trace("Switching %lux%lu to %lux%lu\n", Current.dmPelsWidth, Current.dmPelsHeight,
          Request.dmPelsWidth, Request.dmPelsHeight);

    Result = ChangeDisplaySettingsExW(NULL, &Request, NULL, CDS_TEST, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "CDS_TEST: %ld\n", Result);

    Start = GetTickCount();
    Result = ChangeDisplaySettingsExW(NULL, &Request, NULL, CDS_FULLSCREEN, NULL);
    Elapsed = GetTickCount() - Start;
    ok(Result == DISP_CHANGE_SUCCESSFUL, "CDS_FULLSCREEN: %ld\n", Result);
    trace("Mode change took %lu ms\n", Elapsed);
    if (Result != DISP_CHANGE_SUCCESSFUL)
        return;

    ok(GetSystemMetrics(SM_CXSCREEN) == (int)Request.dmPelsWidth &&
       GetSystemMetrics(SM_CYSCREEN) == (int)Request.dmPelsHeight,
       "Screen metrics are %dx%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    memset(&Applied, 0, sizeof(Applied));
    Applied.dmSize = sizeof(Applied);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Applied), "No current mode after the change\n");
    ok(Applied.dmPelsWidth == Request.dmPelsWidth && Applied.dmPelsHeight == Request.dmPelsHeight,
       "The current mode is %lux%lu\n", Applied.dmPelsWidth, Applied.dmPelsHeight);
    ok(!(Applied.dmFields & DM_PANNINGWIDTH) || !Applied.dmPanningWidth ||
       Applied.dmPanningWidth >= Applied.dmPelsWidth,
       "The desktop pans a %lu pixel wide view over %lu pixels\n", Applied.dmPanningWidth, Applied.dmPelsWidth);
    ok(!(Applied.dmFields & DM_PANNINGHEIGHT) || !Applied.dmPanningHeight ||
       Applied.dmPanningHeight >= Applied.dmPelsHeight,
       "The desktop pans a %lu pixel high view over %lu pixels\n", Applied.dmPanningHeight, Applied.dmPelsHeight);

    Screen = GetDC(NULL);
    ok(GetDeviceCaps(Screen, HORZRES) == (int)Request.dmPelsWidth &&
       GetDeviceCaps(Screen, VERTRES) == (int)Request.dmPelsHeight,
       "The screen DC is %dx%d\n", GetDeviceCaps(Screen, HORZRES), GetDeviceCaps(Screen, VERTRES));
    ReleaseDC(NULL, Screen);

    memset(&Monitor, 0, sizeof(Monitor));
    Monitor.cbSize = sizeof(Monitor);
    ok(GetMonitorInfoW(MonitorFromPoint(Origin, MONITOR_DEFAULTTOPRIMARY), &Monitor), "No primary monitor\n");
    ok(Monitor.rcMonitor.right - Monitor.rcMonitor.left == (LONG)Request.dmPelsWidth &&
       Monitor.rcMonitor.bottom - Monitor.rcMonitor.top == (LONG)Request.dmPelsHeight,
       "The primary monitor is %ldx%ld\n", Monitor.rcMonitor.right - Monitor.rcMonitor.left,
       Monitor.rcMonitor.bottom - Monitor.rcMonitor.top);

    Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Restoring the mode: %ld\n", Result);
    ok(GetSystemMetrics(SM_CXSCREEN) == (int)Current.dmPelsWidth &&
       GetSystemMetrics(SM_CYSCREEN) == (int)Current.dmPelsHeight,
       "Screen metrics after the restore are %dx%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}
