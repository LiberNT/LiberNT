/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Themed WM_NCHITTEST reports the client where it covers the caption
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

static BOOL g_ClientCaption;

static LRESULT CALLBACK
HitTestWndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_NCCALCSIZE && wParam && g_ClientCaption)
    {
        NCCALCSIZE_PARAMS *Params = (NCCALCSIZE_PARAMS *)lParam;
        LONG Top = Params->rgrc[0].top;

        DefWindowProcW(Window, Message, wParam, lParam);
        Params->rgrc[0].top = Top;
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static LRESULT
icon_hit_test(BOOL ClientCaption)
{
    RECT Frame = { 0, 0, 0, 0 }, WindowRect;
    POINT Point;
    LRESULT Hit;
    HWND Window;

    g_ClientCaption = ClientCaption;
    Window = CreateWindowExW(0, L"NonClientHitTest", L"Hit test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return HTERROR;

    AdjustWindowRectEx(&Frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
    GetWindowRect(Window, &WindowRect);
    if (ClientCaption)
    {
        POINT Origin = { 0, 0 };

        SetWindowPos(Window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ClientToScreen(Window, &Origin);
        ok(Origin.y == WindowRect.top, "client top %ld, window top %ld\n", Origin.y, WindowRect.top);
    }
    Point.x = WindowRect.left - Frame.left + 4;
    Point.y = WindowRect.top - Frame.top - GetSystemMetrics(SM_CYCAPTION) / 2;
    Hit = SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(Point.x, Point.y));
    DestroyWindow(Window);
    return Hit;
}

START_TEST(NonClientHitTest)
{
    WNDCLASSW Class = { 0 };
    LRESULT Hit;

    Class.lpfnWndProc = HitTestWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = L"NonClientHitTest";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Hit = icon_hit_test(FALSE);
    ok(Hit == HTSYSMENU, "caption icon hit %ld, expected HTSYSMENU\n", (LONG)Hit);

    Hit = icon_hit_test(TRUE);
    ok(Hit == HTCLIENT, "client over the caption icon hit %ld, expected HTCLIENT\n", (LONG)Hit);

    UnregisterClassW(L"NonClientHitTest", GetModuleHandleW(NULL));
}
