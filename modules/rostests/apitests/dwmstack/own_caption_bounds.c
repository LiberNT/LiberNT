/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A window that draws its own caption shows no resize borders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <dwmapi.h>

typedef HRESULT (WINAPI *PFN_GET_ATTRIBUTE)(HWND, DWORD, PVOID, DWORD);

static LRESULT CALLBACK
OwnCaptionProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCCALCSIZE && wparam)
    {
        NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lparam;
        LONG top = params->rgrc[0].top;
        LRESULT result = DefWindowProcW(hwnd, msg, wparam, lparam);

        params->rgrc[0].top = top;
        return result;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

START_TEST(own_caption_bounds)
{
    WNDCLASSW wc = {0};
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    PFN_GET_ATTRIBUTE get_attribute;
    RECT window, client, bounds;
    POINT origin = {0, 0};
    HRESULT hr;
    HWND hwnd;

    ok(dwmapi != NULL, "LoadLibrary(dwmapi.dll) failed: %lu\n", GetLastError());
    if (!dwmapi)
        return;
    get_attribute = (PFN_GET_ATTRIBUTE)GetProcAddress(dwmapi, "DwmGetWindowAttribute");
    ok(get_attribute != NULL, "DwmGetWindowAttribute is not exported\n");
    if (!get_attribute)
        return;

    wc.lpfnWndProc = OwnCaptionProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"OwnCaptionBounds";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    ok(RegisterClassW(&wc) != 0, "RegisterClass failed: %lu\n", GetLastError());
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"own caption", WS_OVERLAPPEDWINDOW, 100, 100, 400, 300,
                           NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!hwnd)
        return;
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x, origin.y);
    ok(client.top == window.top, "client starts at %ld, window at %ld\n", client.top, window.top);
    ok(client.left > window.left && client.right < window.right && client.bottom < window.bottom,
       "client (%ld,%ld)-(%ld,%ld) has no frame inside window (%ld,%ld)-(%ld,%ld)\n",
       client.left, client.top, client.right, client.bottom, window.left, window.top, window.right, window.bottom);

    hr = get_attribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    ok(hr == S_OK, "DWMWA_EXTENDED_FRAME_BOUNDS returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(bounds.top == window.top, "bounds top %ld, window top %ld\n", bounds.top, window.top);
        ok(bounds.left == client.left - GetSystemMetrics(SM_CXBORDER), "bounds left %ld, client left %ld\n",
           bounds.left, client.left);
        ok(bounds.right == client.right + GetSystemMetrics(SM_CXBORDER), "bounds right %ld, client right %ld\n",
           bounds.right, client.right);
        ok(bounds.bottom == client.bottom + GetSystemMetrics(SM_CYBORDER), "bounds bottom %ld, client bottom %ld\n",
           bounds.bottom, client.bottom);
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
