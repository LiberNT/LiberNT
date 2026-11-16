/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the shell hook messages when a window activated while hidden is shown
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define MAX_SHELL_MESSAGES 32

static UINT g_uShellMsg;
static UINT g_cShell;
static WPARAM g_ShellCode[MAX_SHELL_MESSAGES];
static HWND g_ShellWnd[MAX_SHELL_MESSAGES];

static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_uShellMsg && msg)
    {
        if (g_cShell < MAX_SHELL_MESSAGES)
        {
            g_ShellCode[g_cShell] = wParam;
            g_ShellWnd[g_cShell] = (HWND)lParam;
            g_cShell++;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void PumpMessages(void)
{
    MSG msg;
    int i;

    for (i = 0; i < 10; i++)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(20);
    }
}

static int FindShellMessage(WPARAM code, HWND hwnd)
{
    UINT i;

    for (i = 0; i < g_cShell; i++)
    {
        if (g_ShellCode[i] == code && g_ShellWnd[i] == hwnd)
            return (int)i;
    }
    return -1;
}

static void TestShowActive(HWND hwnd, int nCmdShow)
{
    int iCreated, iActivated;
    UINT i;

    SetForegroundWindow(hwnd);
    PumpMessages();
    ok(GetForegroundWindow() == hwnd, "foreground %p, expected %p\n", GetForegroundWindow(), hwnd);
    ok(GetActiveWindow() == hwnd, "active %p, expected %p\n", GetActiveWindow(), hwnd);
    ok(!IsWindowVisible(hwnd), "the window is visible\n");

    g_cShell = 0;
    ShowWindow(hwnd, nCmdShow);
    PumpMessages();
    for (i = 0; i < g_cShell; i++)
        trace("shell message %u: %Iu %p\n", i, g_ShellCode[i], g_ShellWnd[i]);
    iCreated = FindShellMessage(HSHELL_WINDOWCREATED, hwnd);
    iActivated = FindShellMessage(HSHELL_WINDOWACTIVATED, hwnd);
    ok(iCreated >= 0, "no HSHELL_WINDOWCREATED\n");
    ok(iActivated >= 0, "no HSHELL_WINDOWACTIVATED\n");
    ok(iCreated < iActivated, "HSHELL_WINDOWCREATED %d, HSHELL_WINDOWACTIVATED %d\n", iCreated, iActivated);
}

static void TestClass(LPCWSTR pszClass, BOOL fOwned, int nCmdShow)
{
    HWND hwndOwner = NULL, hwnd;

    winetest_push_context("%ls %s %d", pszClass, fOwned ? "owned" : "unowned", nCmdShow);
    if (fOwned)
    {
        hwndOwner = CreateWindowExW(0, L"STATIC", NULL, WS_OVERLAPPED | WS_DISABLED | WS_BORDER, 0, 0, 40, 40, NULL,
                                    NULL, NULL, NULL);
        ok(hwndOwner != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    }
    hwnd = CreateWindowExW(fOwned ? WS_EX_APPWINDOW : 0, pszClass, L"ShellHookShowActive",
                           WS_POPUP | WS_CAPTION | WS_SYSMENU, 100, 100, 200, 100, hwndOwner, NULL,
                           GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (hwnd)
    {
        TestShowActive(hwnd, nCmdShow);
        DestroyWindow(hwnd);
    }
    if (hwndOwner)
        DestroyWindow(hwndOwner);
    PumpMessages();
    winetest_pop_context();
}

START_TEST(ShellHookShowActive)
{
    WNDCLASSW wc = { 0 };
    HWND hwndHook;

    wc.lpfnWndProc = HookWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"ShellHookShowActive";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    wc.style = CS_SAVEBITS;
    wc.lpszClassName = L"ShellHookShowActiveSaveBits";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed with %lu\n", GetLastError());

    hwndHook = CreateWindowExW(WS_EX_TOOLWINDOW, L"ShellHookShowActive", L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                               wc.hInstance, NULL);
    ok(hwndHook != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    g_uShellMsg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!hwndHook || !g_uShellMsg || !RegisterShellHookWindow(hwndHook))
    {
        skip("Cannot register a shell hook window\n");
        if (hwndHook)
            DestroyWindow(hwndHook);
        return;
    }

    TestClass(L"ShellHookShowActive", FALSE, SW_SHOWNORMAL);
    TestClass(L"ShellHookShowActiveSaveBits", FALSE, SW_SHOWNORMAL);
    TestClass(L"ShellHookShowActiveSaveBits", TRUE, SW_SHOWNORMAL);
    TestClass(L"ShellHookShowActiveSaveBits", FALSE, SW_SHOW);
    TestClass(L"#32770", FALSE, SW_SHOWNORMAL);
    TestClass(L"#32770", TRUE, SW_SHOWNORMAL);

    DeregisterShellHookWindow(hwndHook);
    DestroyWindow(hwndHook);
}
