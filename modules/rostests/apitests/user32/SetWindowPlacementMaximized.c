/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that SetWindowPlacement keeps a maximized top-level window where maximizing put it
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

static void Pump(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static void TestPlacement(const char *Name, BOOL SetMaxPosition, LONG x, LONG y)
{
    WINDOWPLACEMENT wp = { sizeof(wp) };
    RECT rcBefore, rcAfter;
    HWND hwnd;

    hwnd = CreateWindowExW(0, L"SetWindowPlacementMaximized", NULL, WS_OVERLAPPEDWINDOW, 100, 100, 500, 400,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "%s: CreateWindowExW failed, error %lu\n", Name, GetLastError());
    if (!hwnd)
        return;
    ShowWindow(hwnd, SW_MAXIMIZE);
    Pump();
    GetWindowRect(hwnd, &rcBefore);

    ok(GetWindowPlacement(hwnd, &wp), "%s: GetWindowPlacement failed\n", Name);
    ok(wp.showCmd == SW_SHOWMAXIMIZED, "%s: showCmd %u\n", Name, wp.showCmd);
    ok(wp.ptMaxPosition.x == -1 && wp.ptMaxPosition.y == -1, "%s: ptMaxPosition (%ld,%ld) before\n", Name,
       wp.ptMaxPosition.x, wp.ptMaxPosition.y);
    if (SetMaxPosition)
    {
        wp.ptMaxPosition.x = x;
        wp.ptMaxPosition.y = y;
    }
    ok(SetWindowPlacement(hwnd, &wp), "%s: SetWindowPlacement failed, error %lu\n", Name, GetLastError());
    Pump();

    GetWindowRect(hwnd, &rcAfter);
    ok(EqualRect(&rcBefore, &rcAfter), "%s: window moved from (%ld,%ld)-(%ld,%ld) to (%ld,%ld)-(%ld,%ld)\n", Name,
       rcBefore.left, rcBefore.top, rcBefore.right, rcBefore.bottom,
       rcAfter.left, rcAfter.top, rcAfter.right, rcAfter.bottom);
    ok(IsZoomed(hwnd), "%s: the window is no longer maximized\n", Name);
    ok(GetWindowPlacement(hwnd, &wp), "%s: GetWindowPlacement failed\n", Name);
    ok(wp.ptMaxPosition.x == -1 && wp.ptMaxPosition.y == -1, "%s: ptMaxPosition (%ld,%ld) after\n", Name,
       wp.ptMaxPosition.x, wp.ptMaxPosition.y);
    DestroyWindow(hwnd);
}

START_TEST(SetWindowPlacementMaximized)
{
    WNDCLASSW wc = { 0 };

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SetWindowPlacementMaximized";
    RegisterClassW(&wc);

    TestPlacement("round trip", FALSE, 0, 0);
    TestPlacement("origin", TRUE, 0, 0);
    TestPlacement("offset", TRUE, 200, 150);

    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
