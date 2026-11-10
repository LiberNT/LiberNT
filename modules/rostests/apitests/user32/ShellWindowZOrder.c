/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the shell window stays below the windows moved to the bottom
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define MOVE_FLAGS (SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)

static HWND hwndCounted;
static UINT ChangedCount;

static LRESULT CALLBACK
ColorWindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_WINDOWPOSCHANGED && hWnd == hwndCounted)
        ChangedCount++;
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static VOID
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static HWND
CreateColorWindow(PCWSTR ClassName, DWORD ExStyle, COLORREF Color, const RECT *Rect)
{
    WNDCLASSW Class = { 0 };

    Class.lpfnWndProc = ColorWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = CreateSolidBrush(Color);
    Class.lpszClassName = ClassName;
    RegisterClassW(&Class);
    return CreateWindowExW(ExStyle | WS_EX_TOOLWINDOW, ClassName, ClassName, WS_POPUP | WS_VISIBLE,
                           Rect->left, Rect->top, Rect->right - Rect->left, Rect->bottom - Rect->top,
                           NULL, NULL, Class.hInstance, NULL);
}

static VOID
DestroyColorWindow(HWND hWnd, PCWSTR ClassName)
{
    HBRUSH hbr;

    if (!hWnd)
        return;
    hbr = (HBRUSH)GetClassLongPtrW(hWnd, GCLP_HBRBACKGROUND);
    DestroyWindow(hWnd);
    UnregisterClassW(ClassName, GetModuleHandleW(NULL));
    DeleteObject(hbr);
}

static POINT
Center(HWND hWnd)
{
    RECT Rect;
    POINT Point;

    GetWindowRect(hWnd, &Rect);
    Point.x = (Rect.left + Rect.right) / 2;
    Point.y = (Rect.top + Rect.bottom) / 2;
    return Point;
}

static HWND
RootFromPoint(POINT Point)
{
    HWND hWnd = WindowFromPoint(Point);

    return hWnd ? GetAncestor(hWnd, GA_ROOT) : NULL;
}

static BOOL
ScreenShows(POINT Point, COLORREF Color)
{
    HDC hdc = GetDC(NULL);
    COLORREF Pixel = GetPixel(hdc, Point.x, Point.y);
    INT i;

    ReleaseDC(NULL, hdc);
    for (i = 0; i < 3; i++)
    {
        BYTE Want = (BYTE)(Color >> (i * 8));
        BYTE Got = (BYTE)(Pixel >> (i * 8));

        if (Want ? Got < 0x80 : Got > 0x40)
        {
            trace("Pixel at (%ld,%ld) is %06lx\n", Point.x, Point.y, Pixel);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
IsTopmost(HWND hWnd)
{
    return (GetWindowLongW(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

START_TEST(ShellWindowZOrder)
{
    HWND hwndShell = GetShellWindow(), hwndBottom, hwndTopmost, hwndFull;
    RECT WorkArea, BottomRect, TopmostRect, FullRect;
    const COLORREF Green = RGB(0, 200, 0), Blue = RGB(0, 0, 200), Red = RGB(200, 0, 0);

    if (!hwndShell)
    {
        skip("No shell window\n");
        return;
    }

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &WorkArea, 0);
    SetRect(&BottomRect, WorkArea.right - 160, WorkArea.bottom - 160, WorkArea.right - 40, WorkArea.bottom - 40);
    SetRect(&TopmostRect, WorkArea.right - 320, WorkArea.bottom - 160, WorkArea.right - 200, WorkArea.bottom - 40);
    SetRect(&FullRect, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    hwndBottom = CreateColorWindow(L"ShellWindowZOrderBottom", 0, Green, &BottomRect);
    ok(hwndBottom != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    hwndTopmost = CreateColorWindow(L"ShellWindowZOrderTopmost", WS_EX_TOPMOST, Blue, &TopmostRect);
    ok(hwndTopmost != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hwndBottom || !hwndTopmost)
    {
        DestroyColorWindow(hwndTopmost, L"ShellWindowZOrderTopmost");
        DestroyColorWindow(hwndBottom, L"ShellWindowZOrderBottom");
        return;
    }
    Pump(700);
    ok(RootFromPoint(Center(hwndBottom)) == hwndBottom, "The new window is not under its center\n");

    SetWindowPos(hwndBottom, HWND_BOTTOM, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(700);
    ok(GetWindow(hwndBottom, GW_HWNDNEXT) == hwndShell, "HWND_BOTTOM: the shell window is not right below the window\n");
    ok(GetWindow(hwndBottom, GW_HWNDLAST) == hwndShell, "HWND_BOTTOM: the shell window is not the last window\n");
    ok(RootFromPoint(Center(hwndBottom)) == hwndBottom, "HWND_BOTTOM: the window is not under its center\n");
    ok(ScreenShows(Center(hwndBottom), Green), "HWND_BOTTOM: the screen does not show the window\n");

    hwndCounted = hwndBottom;
    ChangedCount = 0;
    SetWindowPos(hwndBottom, hwndShell, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(300);
    ok(GetWindow(hwndBottom, GW_HWNDNEXT) == hwndShell, "After the shell window: the shell window is not right below the window\n");
    ok(GetWindow(hwndBottom, GW_HWNDLAST) == hwndShell, "After the shell window: the shell window is not the last window\n");
    ok(ChangedCount == 0, "After the shell window: WM_WINDOWPOSCHANGED was sent %u times\n", ChangedCount);

    ChangedCount = 0;
    SetWindowPos(hwndBottom, HWND_BOTTOM, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(300);
    ok(GetWindow(hwndBottom, GW_HWNDNEXT) == hwndShell, "HWND_BOTTOM again: the shell window is not right below the window\n");
    ok(ChangedCount == 0, "HWND_BOTTOM again: WM_WINDOWPOSCHANGED was sent %u times\n", ChangedCount);
    hwndCounted = NULL;

    SetWindowPos(hwndTopmost, HWND_BOTTOM, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(700);
    ok(!IsTopmost(hwndTopmost), "HWND_BOTTOM kept WS_EX_TOPMOST\n");
    ok(GetWindow(hwndTopmost, GW_HWNDNEXT) == hwndShell, "Topmost to HWND_BOTTOM: the shell window is not right below the window\n");
    ok(RootFromPoint(Center(hwndTopmost)) == hwndTopmost, "Topmost to HWND_BOTTOM: the window is not under its center\n");
    ok(ScreenShows(Center(hwndTopmost), Blue), "Topmost to HWND_BOTTOM: the screen does not show the window\n");

    SetWindowPos(hwndTopmost, HWND_TOPMOST, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(300);
    ok(IsTopmost(hwndTopmost), "HWND_TOPMOST did not set WS_EX_TOPMOST\n");

    hwndFull = CreateColorWindow(L"ShellWindowZOrderFull", 0, Red, &FullRect);
    ok(hwndFull != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (hwndFull)
    {
        Pump(700);
        ok(RootFromPoint(Center(hwndTopmost)) == hwndTopmost, "The topmost window is not over the full-screen window\n");
        ok(ScreenShows(Center(hwndTopmost), Blue), "The screen does not show the topmost window over the full-screen window\n");
        ok(RootFromPoint(Center(hwndBottom)) == hwndFull, "The full-screen window does not cover the bottom window\n");
        ok(ScreenShows(Center(hwndBottom), Red), "The screen does not show the full-screen window\n");

        SetWindowPos(hwndFull, HWND_BOTTOM, 0, 0, 0, 0, MOVE_FLAGS);
        Pump(700);
        ok(GetWindow(hwndFull, GW_HWNDLAST) == hwndShell, "Full-screen to HWND_BOTTOM: the shell window is not the last window\n");
        ok(RootFromPoint(Center(hwndBottom)) == hwndBottom, "The window above the lowered full-screen window is not under its center\n");
        ok(ScreenShows(Center(hwndBottom), Green), "The screen does not show the window above the lowered full-screen window\n");
        ok(RootFromPoint(Center(hwndTopmost)) == hwndTopmost, "The topmost window is not over the lowered full-screen window\n");
    }

    SetWindowPos(hwndShell, HWND_TOP, 0, 0, 0, 0, MOVE_FLAGS);
    Pump(300);
    ok(GetWindow(hwndShell, GW_HWNDNEXT) == NULL, "HWND_TOP moved the shell window up\n");
    ok(RootFromPoint(Center(hwndBottom)) == hwndBottom, "The shell window covers the bottom window after HWND_TOP\n");
    SetWindowPos(hwndShell, HWND_BOTTOM, 0, 0, 0, 0, MOVE_FLAGS);

    DestroyColorWindow(hwndFull, L"ShellWindowZOrderFull");
    DestroyColorWindow(hwndTopmost, L"ShellWindowZOrderTopmost");
    DestroyColorWindow(hwndBottom, L"ShellWindowZOrderBottom");
    Pump(300);
}
