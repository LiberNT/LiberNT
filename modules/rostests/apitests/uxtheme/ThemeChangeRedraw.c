/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that per-window theme changes keep the client area valid
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static LONG g_ThemeChanged;

static LRESULT CALLBACK
RedrawWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_THEMECHANGED)
        InterlockedIncrement(&g_ThemeChanged);
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
FlushWindow(HWND hWnd)
{
    MSG msg;

    UpdateWindow(hWnd);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ValidateRect(hWnd, NULL);
}

static void
TestItemsViewClass(void)
{
    HTHEME hTheme;

    hTheme = OpenThemeData(NULL, L"ItemsView");
    ok(hTheme != NULL, "OpenThemeData(NULL, ItemsView) failed, error %lu\n", GetLastError());
    if (hTheme)
    {
        COLORREF Color = CLR_INVALID;
        HRESULT hr = GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &Color);

        trace("ItemsView FILLCOLOR hr 0x%lx color 0x%06lx\n", hr, Color);
        hr = GetThemeColor(hTheme, 0, 0, TMT_TEXTCOLOR, &Color);
        trace("ItemsView TEXTCOLOR hr 0x%lx color 0x%06lx\n", hr, Color);
        CloseThemeData(hTheme);
    }
}

static void
TestSetWindowThemeKeepsClient(HWND hWnd)
{
    RECT Update;
    HRESULT hr;

    FlushWindow(hWnd);
    ok(!GetUpdateRect(hWnd, &Update, FALSE), "client was not validated\n");

    g_ThemeChanged = 0;
    hr = SetWindowTheme(hWnd, L"Explorer", NULL);
    ok(hr == S_OK, "SetWindowTheme(Explorer) returned 0x%lx\n", hr);
    ok(g_ThemeChanged == 1, "WM_THEMECHANGED count %ld\n", g_ThemeChanged);
    ok(!GetUpdateRect(hWnd, &Update, FALSE), "SetWindowTheme(Explorer) invalidated (%ld,%ld)-(%ld,%ld)\n",
       Update.left, Update.top, Update.right, Update.bottom);

    FlushWindow(hWnd);
    g_ThemeChanged = 0;
    hr = SetWindowTheme(hWnd, NULL, NULL);
    ok(hr == S_OK, "SetWindowTheme(NULL) returned 0x%lx\n", hr);
    ok(g_ThemeChanged == 1, "WM_THEMECHANGED count %ld\n", g_ThemeChanged);
    ok(!GetUpdateRect(hWnd, &Update, FALSE), "SetWindowTheme(NULL) invalidated (%ld,%ld)-(%ld,%ld)\n",
       Update.left, Update.top, Update.right, Update.bottom);
}

static void
TestDwmAttributeKeepsClient(HWND hWnd)
{
    RECT Update;
    BOOL Dark = TRUE, Value = FALSE;
    HRESULT hr;

    hr = DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));
    ok(hr == S_OK, "first DwmSetWindowAttribute returned 0x%lx\n", hr);
    FlushWindow(hWnd);

    hr = DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));
    ok(hr == S_OK, "repeated DwmSetWindowAttribute returned 0x%lx\n", hr);
    ok(!GetUpdateRect(hWnd, &Update, FALSE), "repeated dark mode attribute invalidated (%ld,%ld)-(%ld,%ld)\n",
       Update.left, Update.top, Update.right, Update.bottom);

    hr = DwmGetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &Value, sizeof(Value));
    ok(hr == S_OK, "DwmGetWindowAttribute returned 0x%lx\n", hr);
    ok(Value == TRUE, "dark mode attribute is %d\n", Value);
}

START_TEST(ThemeChangeRedraw)
{
    WNDCLASSW Class = { 0 };
    HWND hWnd;

    if (!IsThemeActive())
    {
        skip("Theme not active\n");
        return;
    }

    TestItemsViewClass();

    Class.lpfnWndProc = RedrawWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = L"ThemeChangeRedraw";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    hWnd = CreateWindowExW(0, Class.lpszClassName, L"ThemeChangeRedraw",
                           WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE,
                           40, 40, 320, 240, NULL, NULL, Class.hInstance, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hWnd)
        return;

    TestSetWindowThemeKeepsClient(hWnd);
    TestDwmAttributeKeepsClient(hWnd);

    DestroyWindow(hWnd);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
