/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the Run dialog is a taskbar window the shell is told about
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "shelltest.h"

typedef void (WINAPI *FN_RunFileDlg)(HWND, HICON, LPCWSTR, LPCWSTR, LPCWSTR, UINT);

#define MAX_SHELL_MESSAGES 64

static FN_RunFileDlg g_pRunFileDlg;
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

static DWORD WINAPI RunDialogThread(LPVOID pv)
{
    HWND hwndOwner = CreateWindowExW(0, WC_STATICW, NULL, WS_OVERLAPPED | WS_DISABLED | WS_CLIPSIBLINGS | WS_BORDER,
                                     0, 0, 40, 40, NULL, NULL, NULL, NULL);

    *(HWND *)pv = hwndOwner;
    g_pRunFileDlg(hwndOwner, NULL, NULL, NULL, NULL, 0);
    if (hwndOwner)
        DestroyWindow(hwndOwner);
    return 0;
}

static BOOL CALLBACK FindDialogProc(HWND hwnd, LPARAM lParam)
{
    WCHAR szClass[16];

    if (GetClassNameW(hwnd, szClass, _countof(szClass)) && !lstrcmpW(szClass, L"#32770") && IsWindowVisible(hwnd))
    {
        *(HWND *)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void PumpMessages(DWORD dwMilliseconds)
{
    DWORD dwStart = GetTickCount();
    MSG msg;

    while (GetTickCount() - dwStart < dwMilliseconds)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(20);
    }
}

static int FindShellMessage(WPARAM code, HWND hwnd)
{
    for (UINT i = 0; i < g_cShell; i++)
    {
        if (g_ShellCode[i] == code && g_ShellWnd[i] == hwnd)
            return (int)i;
    }
    return -1;
}

static void TestDialog(HWND hwndDlg, HWND hwndOwner)
{
    DWORD dwExStyle = (DWORD)GetWindowLongPtrW(hwndDlg, GWL_EXSTYLE);
    int iCreated, iActivated;

    PumpMessages(500);
    for (UINT i = 0; i < g_cShell; i++)
        trace("shell message %u: %Iu %p\n", i, g_ShellCode[i], g_ShellWnd[i]);
    trace("dialog %p style 0x%08lx exstyle 0x%08lx owner %p\n", hwndDlg, (DWORD)GetWindowLongPtrW(hwndDlg, GWL_STYLE),
          dwExStyle, GetWindow(hwndDlg, GW_OWNER));

    ok(GetWindow(hwndDlg, GW_OWNER) == hwndOwner, "owner %p, expected %p\n", GetWindow(hwndDlg, GW_OWNER), hwndOwner);
    ok(dwExStyle & WS_EX_APPWINDOW, "exstyle 0x%08lx has no WS_EX_APPWINDOW\n", dwExStyle);
    ok(GetForegroundWindow() == hwndDlg, "foreground %p, expected %p\n", GetForegroundWindow(), hwndDlg);
    iCreated = FindShellMessage(HSHELL_WINDOWCREATED, hwndDlg);
    iActivated = FindShellMessage(HSHELL_WINDOWACTIVATED, hwndDlg);
    ok(iCreated >= 0, "no HSHELL_WINDOWCREATED for the dialog\n");
    ok(iActivated >= 0, "no HSHELL_WINDOWACTIVATED for the dialog\n");
    ok(iCreated < iActivated, "HSHELL_WINDOWCREATED %d, HSHELL_WINDOWACTIVATED %d\n", iCreated, iActivated);
    ok(SendMessageW(hwndDlg, WM_GETICON, ICON_SMALL, 0) != 0, "the dialog has no small icon\n");
    ok(SendMessageW(hwndDlg, WM_GETICON, ICON_BIG, 0) == 0, "the dialog has a big icon\n");
}

START_TEST(RunFileDlg)
{
    WNDCLASSW wc = { 0 };
    HWND hwndHook, hwndOwner = NULL, hwndDlg = NULL;
    HANDLE hThread;
    DWORD dwThreadId;

    g_pRunFileDlg = (FN_RunFileDlg)GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(61));
    if (!g_pRunFileDlg)
    {
        skip("RunFileDlg is not exported\n");
        return;
    }

    wc.lpfnWndProc = HookWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RunFileDlgShellHook";
    RegisterClassW(&wc);
    hwndHook = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, NULL, WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                               wc.hInstance, NULL);
    g_uShellMsg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!hwndHook || !g_uShellMsg || !RegisterShellHookWindow(hwndHook))
    {
        skip("Cannot register a shell hook window\n");
        if (hwndHook)
            DestroyWindow(hwndHook);
        return;
    }

    hThread = CreateThread(NULL, 0, RunDialogThread, &hwndOwner, 0, &dwThreadId);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        for (int i = 0; i < 250 && !hwndDlg; i++)
        {
            PumpMessages(20);
            EnumThreadWindows(dwThreadId, FindDialogProc, (LPARAM)&hwndDlg);
        }
        ok(hwndDlg != NULL, "the Run dialog did not appear\n");
        if (hwndDlg)
        {
            TestDialog(hwndDlg, hwndOwner);
            PostMessageW(hwndDlg, WM_COMMAND, IDCANCEL, 0);
        }
        while (MsgWaitForMultipleObjects(1, &hThread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
            PumpMessages(20);
        CloseHandle(hThread);
    }

    DeregisterShellHookWindow(hwndHook);
    DestroyWindow(hwndHook);
}
