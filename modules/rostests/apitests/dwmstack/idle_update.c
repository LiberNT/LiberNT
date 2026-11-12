/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a window drawn after an idle period reaches the screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

#define UPDATE_X        60
#define UPDATE_Y        60
#define UPDATE_WIDTH    120
#define UPDATE_HEIGHT   80
#define UPDATE_IDLE_MS  600
#define UPDATE_LIMIT_MS 1000

static COLORREF g_PaintColor;

static LRESULT CALLBACK
UpdateWndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        HDC Dc = BeginPaint(Window, &Paint);
        HBRUSH Brush = CreateSolidBrush(g_PaintColor);

        FillRect(Dc, &Paint.rcPaint, Brush);
        DeleteObject(Brush);
        EndPaint(Window, &Paint);
        return 0;
    }
    if (Message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static void
UpdatePump(void)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

static BOOL
ScreenMatches(COLORREF Color)
{
    HDC Screen = GetDC(NULL);
    COLORREF Pixel = GetPixel(Screen, UPDATE_X + UPDATE_WIDTH / 2, UPDATE_Y + UPDATE_HEIGHT / 2);

    ReleaseDC(NULL, Screen);
    return Pixel == Color;
}

static DWORD
WaitForScreen(COLORREF Color)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        UpdatePump();
        if (ScreenMatches(Color))
            return GetTickCount() - Start;
        if (GetTickCount() - Start >= UPDATE_LIMIT_MS)
            return MAXDWORD;
        Sleep(5);
    }
}

static void
IdleThenRepaint(HWND Window, COLORREF Color)
{
    DWORD Latency;

    Sleep(UPDATE_IDLE_MS);
    g_PaintColor = Color;
    InvalidateRect(Window, NULL, FALSE);
    UpdateWindow(Window);
    Latency = WaitForScreen(Color);
    ok(Latency != MAXDWORD, "a repaint after %u ms of idle never reached the screen\n", UPDATE_IDLE_MS);
    trace("repaint after idle reached the screen in %lu ms\n", Latency);
}

static void
IdleThenDraw(HWND Window, COLORREF Color)
{
    DWORD Latency;
    HBRUSH Brush;
    RECT Client;
    HDC Dc;

    Sleep(UPDATE_IDLE_MS);
    g_PaintColor = Color;
    GetClientRect(Window, &Client);
    Dc = GetDC(Window);
    Brush = CreateSolidBrush(Color);
    FillRect(Dc, &Client, Brush);
    DeleteObject(Brush);
    ReleaseDC(Window, Dc);
    Latency = WaitForScreen(Color);
    ok(Latency != MAXDWORD, "a GetDC draw after %u ms of idle never reached the screen\n", UPDATE_IDLE_MS);
    trace("GetDC draw after idle reached the screen in %lu ms\n", Latency);
}

START_TEST(idle_update)
{
    WNDCLASSW Class = {0};
    HWND Window;

    Class.lpfnWndProc = UpdateWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DwmIdleUpdate";
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    g_PaintColor = RGB(255, 0, 0);
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, Class.lpszClassName,
                             L"idle update", WS_POPUP | WS_VISIBLE, UPDATE_X, UPDATE_Y,
                             UPDATE_WIDTH, UPDATE_HEIGHT, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    UpdateWindow(Window);
    ok(WaitForScreen(RGB(255, 0, 0)) != MAXDWORD, "the first paint never reached the screen\n");

    IdleThenRepaint(Window, RGB(0, 0, 255));
    IdleThenDraw(Window, RGB(0, 255, 0));
    IdleThenRepaint(Window, RGB(255, 255, 0));

    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
