/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the coordinates a DPI-unaware process sees under DPI virtualization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFORSYSTEM)(VOID);
typedef UINT (WINAPI *PGETDPIFORWINDOW)(HWND);

static PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext;
static PGETDPIFORSYSTEM pGetDpiForSystem;
static PGETDPIFORWINDOW pGetDpiForWindow;
static INT CreateCx = -1, SizeCx = -1, SizeCy = -1;

static LRESULT CALLBACK
VirtualWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
        case WM_CREATE:
            CreateCx = ((CREATESTRUCTW *)lParam)->cx;
            break;
        case WM_SIZE:
            SizeCx = LOWORD(lParam);
            SizeCy = HIWORD(lParam);
            break;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static UINT
GetAwareSystemDpi(void)
{
    DPI_AWARENESS_CONTEXT Previous;
    UINT Dpi;

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    if (!Previous)
        return 0;
    Dpi = pGetDpiForSystem();
    pSetThreadDpiAwarenessContext(Previous);
    return Dpi;
}

static void
ok_scaled_(const char *file, int line, LONG Logical, LONG Physical, UINT Dpi, const char *What)
{
    LONG Expected = MulDiv(Logical, Dpi, USER_DEFAULT_SCREEN_DPI);

    ok_(file, line)(labs(Physical - Expected) <= 1, "%s: physical %ld, expected %ld from logical %ld\n",
                    What, Physical, Expected, Logical);
}
#define ok_scaled(Logical, Physical, Dpi, What) ok_scaled_(__FILE__, __LINE__, Logical, Physical, Dpi, What)

static void
TestMetrics(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    NONCLIENTMETRICSW Unaware = {sizeof(Unaware)}, Aware = {sizeof(Aware)};
    HDC hDC;
    INT LogPixels, HorzRes, DesktopHorzRes, ScreenCx, AwareScreenCx;

    hDC = GetDC(NULL);
    LogPixels = GetDeviceCaps(hDC, LOGPIXELSY);
    HorzRes = GetDeviceCaps(hDC, HORZRES);
    DesktopHorzRes = GetDeviceCaps(hDC, DESKTOPHORZRES);
    ReleaseDC(NULL, hDC);
    ScreenCx = GetSystemMetrics(SM_CXSCREEN);

    ok_int(LogPixels, USER_DEFAULT_SCREEN_DPI);
    ok_int(pGetDpiForSystem(), USER_DEFAULT_SCREEN_DPI);
    ok_scaled(HorzRes, DesktopHorzRes, Dpi, "HORZRES");
    ok_int(ScreenCx, HorzRes);
    ok(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Unaware), &Unaware, 0), "SPI_GETNONCLIENTMETRICS failed\n");

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    AwareScreenCx = GetSystemMetrics(SM_CXSCREEN);
    ok(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Aware), &Aware, 0), "SPI_GETNONCLIENTMETRICS failed\n");
    pSetThreadDpiAwarenessContext(Previous);

    ok_int(AwareScreenCx, DesktopHorzRes);
    ok_scaled(Unaware.iCaptionHeight, Aware.iCaptionHeight, Dpi, "iCaptionHeight");
    ok_scaled(Unaware.lfMessageFont.lfHeight, Aware.lfMessageFont.lfHeight, Dpi, "lfMessageFont.lfHeight");
}

static void
TestWindow(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    WNDCLASSW Class = {0};
    RECT Window, Client, AwareWindow;
    POINT Origin = {0, 0}, Mapped = {0, 0}, Cursor, AwareCursor;
    HWND hWnd;

    Class.lpfnWndProc = VirtualWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DpiVirtualizationTest";
    RegisterClassW(&Class);

    hWnd = CreateWindowExW(0, Class.lpszClassName, L"DPI virtualization", WS_OVERLAPPEDWINDOW,
                           100, 100, 400, 300, NULL, NULL, Class.hInstance, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed\n");
    if (!hWnd)
        return;
    ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hWnd);

    ok_int(CreateCx, 400);
    ok_int(pGetDpiForWindow(hWnd), USER_DEFAULT_SCREEN_DPI);
    GetWindowRect(hWnd, &Window);
    ok(Window.left == 100 && Window.top == 100 && Window.right == 500 && Window.bottom == 400,
       "Window rect %ld,%ld,%ld,%ld\n", Window.left, Window.top, Window.right, Window.bottom);
    GetClientRect(hWnd, &Client);
    ok(Client.right == SizeCx && Client.bottom == SizeCy, "Client %ldx%ld, WM_SIZE %dx%d\n",
       Client.right, Client.bottom, SizeCx, SizeCy);
    ClientToScreen(hWnd, &Origin);
    MapWindowPoints(hWnd, NULL, &Mapped, 1);
    ok(Origin.x == Mapped.x && Origin.y == Mapped.y, "ClientToScreen %ld,%ld, MapWindowPoints %ld,%ld\n",
       Origin.x, Origin.y, Mapped.x, Mapped.y);
    ok(Origin.x > Window.left && Origin.x < Window.left + 20, "Client origin %ld for window left %ld\n",
       Origin.x, Window.left);
    ok(WindowFromPoint((POINT){Origin.x + 50, Origin.y + 50}) == hWnd, "WindowFromPoint missed the window\n");

    SetWindowPos(hWnd, NULL, 120, 110, 420, 320, SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(hWnd, &Window);
    ok(Window.left == 120 && Window.top == 110 && Window.right == 540 && Window.bottom == 430,
       "Window rect after SetWindowPos %ld,%ld,%ld,%ld\n", Window.left, Window.top, Window.right, Window.bottom);
    GetClientRect(hWnd, &Client);
    ok(Client.right == SizeCx && Client.bottom == SizeCy, "Client %ldx%ld, WM_SIZE %dx%d\n",
       Client.right, Client.bottom, SizeCx, SizeCy);

    SetCursorPos(Origin.x + 30, Origin.y + 40);
    GetCursorPos(&Cursor);

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    GetWindowRect(hWnd, &AwareWindow);
    GetCursorPos(&AwareCursor);
    pSetThreadDpiAwarenessContext(Previous);

    ok_scaled(Window.left, AwareWindow.left, Dpi, "aware window left");
    ok_scaled(Window.top, AwareWindow.top, Dpi, "aware window top");
    ok_scaled(Window.right - Window.left, AwareWindow.right - AwareWindow.left, Dpi, "aware window width");
    ok_scaled(Window.bottom - Window.top, AwareWindow.bottom - AwareWindow.top, Dpi, "aware window height");
    ok(Cursor.x == Origin.x + 30 && Cursor.y == Origin.y + 40, "Cursor %ld,%ld\n", Cursor.x, Cursor.y);
    ok_scaled(Cursor.x, AwareCursor.x, Dpi, "aware cursor x");
    ok_scaled(Cursor.y, AwareCursor.y, Dpi, "aware cursor y");

    DestroyWindow(hWnd);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}

START_TEST(DpiVirtualization)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    UINT Dpi;

    pSetThreadDpiAwarenessContext = (PSETTHREADDPIAWARENESSCONTEXT)GetProcAddress(User32, "SetThreadDpiAwarenessContext");
    pGetDpiForSystem = (PGETDPIFORSYSTEM)GetProcAddress(User32, "GetDpiForSystem");
    pGetDpiForWindow = (PGETDPIFORWINDOW)GetProcAddress(User32, "GetDpiForWindow");
    if (!pSetThreadDpiAwarenessContext || !pGetDpiForSystem || !pGetDpiForWindow)
    {
        skip("DPI awareness APIs are not exported\n");
        return;
    }
    if (IsProcessDPIAware())
    {
        skip("The test process is DPI aware\n");
        return;
    }

    Dpi = GetAwareSystemDpi();
    if (Dpi == USER_DEFAULT_SCREEN_DPI || Dpi == 0)
    {
        skip("The system DPI is %u; DPI virtualization is not used\n", Dpi);
        return;
    }

    TestMetrics(Dpi);
    TestWindow(Dpi);
}
