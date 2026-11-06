/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Status bar resize repaint test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <apitest.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <commctrl.h>

static LRESULT CALLBACK resize_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

START_TEST(StatusBarResize)
{
    static const WCHAR name[] = L"StatusResizeTestWindow";
    WNDCLASSW cls = {0};
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    HWND parent, status;
    RECT old, moved;
    POINT point;
    HRGN region;
    int before, after;
    BOOL in_before, in_after;

    InitCommonControlsEx(&icc);
    cls.lpfnWndProc = resize_wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = name;
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    ok(RegisterClassW(&cls) != 0, "RegisterClassW failed %lu\n", GetLastError());
    parent = CreateWindowW(name, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           100, 100, 480, 400, NULL, NULL, cls.hInstance, NULL);
    ok(parent != NULL, "CreateWindowW parent failed %lu\n", GetLastError());
    if (!parent) return;
    status = CreateWindowExW(0, STATUSCLASSNAMEW, L"Processes: 24",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 0, 0, parent, (HMENU)2001, cls.hInstance, NULL);
    ok(status != NULL, "CreateWindowExW status failed %lu\n", GetLastError());
    if (!status) { DestroyWindow(parent); return; }
    ShowWindow(parent, SW_SHOW);
    UpdateWindow(parent);
    UpdateWindow(status);
    GetWindowRect(status, &old);
    point.x = old.left + 4;
    point.y = old.top + 4;
    ScreenToClient(parent, &point);
    SetWindowPos(parent, NULL, 0, 0, 480, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    region = CreateRectRgn(0, 0, 0, 0);
    before = GetUpdateRgn(parent, region, FALSE);
    in_before = PtInRegion(region, point.x, point.y);
    ok(!in_before, "old status area was already invalidated before moving\n");
    SendMessageW(status, WM_SIZE, SIZE_RESTORED, 0);
    GetWindowRect(status, &moved);
    ok(moved.top > old.bottom, "status did not move away from its old area\n");
    after = GetUpdateRgn(parent, region, FALSE);
    in_after = PtInRegion(region, point.x, point.y);
    trace("status old point=(%ld,%ld) before=%d/%d after=%d/%d\n", point.x, point.y, before, in_before, after, in_after);
    ok(in_after, "old status bar area was not invalidated after moving\n");
    DeleteObject(region);
    DestroyWindow(parent);
    UnregisterClassW(name, cls.hInstance);
}
