/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that moving or hiding a hidden child leaves its parent's painting alone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HWND g_Parent;
static LONG g_EraseCount;
static LONG g_NcPaintCount;

static LRESULT CALLBACK
HiddenChildWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (hWnd == g_Parent)
    {
        if (Msg == WM_ERASEBKGND)
            g_EraseCount++;
        else if (Msg == WM_NCPAINT)
            g_NcPaintCount++;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
FlushMessages(void)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&Msg);
}

START_TEST(HiddenChildWindowPos)
{
    WNDCLASSW Class = { 0 };
    HWND Top, Hidden, Visible;
    RECT Rect;

    Class.lpfnWndProc = HiddenChildWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = L"HiddenChildWindowPos";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Top = CreateWindowExW(0, Class.lpszClassName, L"HiddenChildWindowPos",
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                          50, 50, 400, 300, NULL, NULL, Class.hInstance, NULL);
    ok(Top != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Top)
        return;
    g_Parent = CreateWindowExW(0, Class.lpszClassName, NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                               0, 0, 300, 200, Top, NULL, Class.hInstance, NULL);
    Hidden = CreateWindowExW(0, Class.lpszClassName, NULL, WS_CHILD,
                             0, 0, 100, 20, g_Parent, NULL, Class.hInstance, NULL);
    Visible = CreateWindowExW(0, Class.lpszClassName, NULL, WS_CHILD | WS_VISIBLE,
                              10, 40, 100, 50, g_Parent, NULL, Class.hInstance, NULL);
    ok(g_Parent && Hidden && Visible, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!g_Parent || !Hidden || !Visible)
    {
        DestroyWindow(Top);
        return;
    }
    UpdateWindow(Top);
    FlushMessages();

    /* A pending repaint of the parent stays pending: nothing on screen changed */
    InvalidateRect(g_Parent, NULL, TRUE);
    g_EraseCount = g_NcPaintCount = 0;
    ok(SetWindowPos(Hidden, NULL, 0, 0, 300, 0, SWP_HIDEWINDOW | SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok(SetWindowPos(Hidden, NULL, 5, 5, 200, 30, SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok_long(g_EraseCount, 0);
    ok_long(g_NcPaintCount, 0);
    ok(GetUpdateRect(g_Parent, &Rect, FALSE), "The parent's pending update was lost\n");
    FlushMessages();

    /* A validated parent is not invalidated by a hidden child */
    ValidateRect(g_Parent, NULL);
    ok(SetWindowPos(Hidden, NULL, 20, 20, 150, 40, SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok(SetWindowPos(Hidden, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok(!GetUpdateRect(g_Parent, &Rect, FALSE), "The parent was invalidated (%ld,%ld)-(%ld,%ld)\n",
       Rect.left, Rect.top, Rect.right, Rect.bottom);

    /* Growing a visible child of a parent that clips its children uncovers nothing of the parent */
    InvalidateRect(g_Parent, NULL, TRUE);
    g_EraseCount = g_NcPaintCount = 0;
    ok(SetWindowPos(Visible, NULL, 0, 0, 150, 80, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok_long(g_EraseCount, 0);
    ok_long(g_NcPaintCount, 0);
    FlushMessages();

    /* Hiding a visible child uncovers part of the parent */
    ValidateRect(g_Parent, NULL);
    ok(SetWindowPos(Visible, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
       "SetWindowPos failed, error %lu\n", GetLastError());
    ok(GetUpdateRect(g_Parent, &Rect, FALSE), "Hiding a visible child left the parent valid\n");

    DestroyWindow(Top);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
