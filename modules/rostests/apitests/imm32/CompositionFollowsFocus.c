/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that only moving the focus window or one of its ancestors renews the composition window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <imm.h>

static LONG g_Updates;
static WNDPROC g_EditProc;

static LRESULT CALLBACK ParentProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_IME_NOTIFY && wParam == IMN_SETCOMPOSITIONWINDOW)
        g_Updates++;
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_IME_NOTIFY && wParam == IMN_SETCOMPOSITIONWINDOW)
        g_Updates++;
    return CallWindowProcW(g_EditProc, hwnd, uMsg, wParam, lParam);
}

static void Flush(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static void MoveAndCount(int line, HWND hwnd, int x, int y, int cx, int cy, UINT flags, LONG expected)
{
    Flush();
    g_Updates = 0;
    SetWindowPos(hwnd, NULL, x, y, cx, cy, flags | SWP_NOZORDER | SWP_NOACTIVATE);
    Flush();
    ok_(__FILE__, line)(g_Updates == expected, "%ld composition window updates, expected %ld\n", g_Updates, expected);
}

START_TEST(CompositionFollowsFocus)
{
    COMPOSITIONFORM form = { CFS_POINT, { 10, 10 } };
    WNDCLASSW cls = { 0 };
    HWND parent, child, other, box, edit;
    HIMC hIMC;

    cls.lpfnWndProc = ParentProc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"CompositionFollowsFocus";
    RegisterClassW(&cls);
    parent = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 300, NULL, NULL,
                             cls.hInstance, NULL);
    ok(parent != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!parent)
        return;
    SetForegroundWindow(parent);
    SetFocus(parent);
    hIMC = ImmGetContext(parent);
    if (!hIMC || GetFocus() != parent)
    {
        skip("No input context on a focused window\n");
        DestroyWindow(parent);
        return;
    }

    Flush();
    g_Updates = 0;
    ok(ImmSetCompositionWindow(hIMC, &form), "ImmSetCompositionWindow failed\n");
    Flush();
    ok(g_Updates == 1, "%ld composition window updates after setting it\n", g_Updates);

    child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, parent, NULL, NULL, NULL);
    other = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP, 400, 400, 50, 50, NULL, NULL, NULL, NULL);
    ok(child != NULL && other != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    MoveAndCount(__LINE__, child, 20, 20, 0, 0, SWP_NOSIZE, 0);
    MoveAndCount(__LINE__, child, 0, 0, 60, 60, SWP_NOMOVE, 0);
    MoveAndCount(__LINE__, other, 420, 420, 0, 0, SWP_NOSIZE, 0);
    MoveAndCount(__LINE__, parent, 30, 30, 0, 0, SWP_NOSIZE, 1);
    MoveAndCount(__LINE__, parent, 0, 0, 320, 320, SWP_NOMOVE, 1);

    box = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 100, 100, 150, 150, parent, NULL, NULL, NULL);
    edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 5, 5, 80, 20, box, NULL, NULL, NULL);
    ok(box != NULL && edit != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    g_EditProc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetFocus(edit);
    Flush();
    ok(GetFocus() == edit, "the edit control has no focus\n");
    ok(ImmSetCompositionWindow(hIMC, &form), "ImmSetCompositionWindow failed\n");
    MoveAndCount(__LINE__, edit, 8, 8, 0, 0, SWP_NOSIZE, 1);
    MoveAndCount(__LINE__, box, 110, 110, 0, 0, SWP_NOSIZE, 1);
    MoveAndCount(__LINE__, child, 25, 25, 0, 0, SWP_NOSIZE, 0);
    MoveAndCount(__LINE__, parent, 35, 35, 0, 0, SWP_NOSIZE, 1);

    ImmReleaseContext(parent, hIMC);
    DestroyWindow(other);
    DestroyWindow(parent);
}
