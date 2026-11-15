/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the version 6 edit control passes WM_IME_SETCONTEXT to the default IME window
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>
#include <imm.h>

static HWND g_hwndIme;
static UINT g_cActivate;
static UINT g_cDeactivate;

static LRESULT CALLBACK CallWndProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
    const CWPSTRUCT *pcwp = (const CWPSTRUCT *)lParam;

    if (nCode == HC_ACTION && pcwp->hwnd == g_hwndIme && pcwp->message == WM_IME_SETCONTEXT)
    {
        if (pcwp->wParam)
            ++g_cActivate;
        else
            ++g_cDeactivate;
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

START_TEST(EditImeContext)
{
    HWND hwndEdit;
    HHOOK hHook;

    hwndEdit = CreateWindowExW(0, WC_EDITW, L"", WS_POPUP, 0, 0, 100, 20, NULL, NULL, NULL, NULL);
    ok(hwndEdit != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hwndEdit)
        return;

    g_hwndIme = ImmGetDefaultIMEWnd(hwndEdit);
    if (!g_hwndIme)
    {
        skip("No default IME window\n");
        DestroyWindow(hwndEdit);
        return;
    }

    hHook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, NULL, GetCurrentThreadId());
    ok(hHook != NULL, "SetWindowsHookExW failed: %lu\n", GetLastError());
    if (!hHook)
    {
        DestroyWindow(hwndEdit);
        return;
    }

    SendMessageW(hwndEdit, WM_IME_SETCONTEXT, TRUE, ISC_SHOWUIALL);
    ok_int(g_cActivate, 1);
    ok_int(g_cDeactivate, 0);

    SendMessageW(hwndEdit, WM_IME_SETCONTEXT, FALSE, ISC_SHOWUIALL);
    ok_int(g_cActivate, 1);
    ok_int(g_cDeactivate, 1);

    UnhookWindowsHookEx(hHook);
    DestroyWindow(hwndEdit);
}
