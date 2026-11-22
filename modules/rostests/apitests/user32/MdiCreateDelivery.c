/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that WM_MDICREATE reaches a window procedure intact on every delivery path
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

static const char *g_Path;
static LONG g_Received;

static const MDICREATESTRUCTW g_Mdi =
{
    L"SomeClass", L"Some title", (HANDLE)(ULONG_PTR)0x11110000, 11, 22, 333, 444, 0x00cf0000, (LPARAM)0x1234abcd
};

static LRESULT CALLBACK MdiCreateProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_MDICREATE)
    {
        const MDICREATESTRUCTW *Mdi = (const MDICREATESTRUCTW *)lParam;

        g_Received++;
        ok(Mdi != NULL, "%s: lParam is NULL\n", g_Path);
        if (!Mdi)
            return 0;
        ok(Mdi->szClass && !IS_INTRESOURCE(Mdi->szClass) && !wcscmp(Mdi->szClass, L"SomeClass"),
           "%s: szClass %p\n", g_Path, Mdi->szClass);
        ok(Mdi->szTitle && !wcscmp(Mdi->szTitle, L"Some title"), "%s: szTitle %p\n", g_Path, Mdi->szTitle);
        ok(Mdi->hOwner == g_Mdi.hOwner, "%s: hOwner %p\n", g_Path, Mdi->hOwner);
        ok(Mdi->x == 11 && Mdi->y == 22 && Mdi->cx == 333 && Mdi->cy == 444,
           "%s: position %d,%d %dx%d\n", g_Path, Mdi->x, Mdi->y, Mdi->cx, Mdi->cy);
        ok(Mdi->style == 0x00cf0000, "%s: style %08lx\n", g_Path, Mdi->style);
        ok(Mdi->lParam == g_Mdi.lParam, "%s: lParam field %Ix\n", g_Path, (ULONG_PTR)Mdi->lParam);
        return 0x5150;
    }
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static LRESULT CALLBACK CallWndHook(int Code, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(NULL, Code, wParam, lParam);
}

static DWORD WINAPI SenderThread(PVOID Parameter)
{
    return (DWORD)SendMessageW((HWND)Parameter, WM_MDICREATE, 0, (LPARAM)&g_Mdi);
}

START_TEST(MdiCreateDelivery)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    HHOOK hHook;
    HANDLE hThread;
    DWORD ExitCode = 0;
    MSG msg;

    wc.lpfnWndProc = MdiCreateProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MdiCreateDelivery";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    g_Path = "direct";
    ok(SendMessageW(hwnd, WM_MDICREATE, 0, (LPARAM)&g_Mdi) == 0x5150, "direct: wrong result\n");

    g_Path = "hooked";
    hHook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndHook, NULL, GetCurrentThreadId());
    ok(hHook != NULL, "SetWindowsHookExW failed, error %lu\n", GetLastError());
    ok(SendMessageW(hwnd, WM_MDICREATE, 0, (LPARAM)&g_Mdi) == 0x5150, "hooked: wrong result\n");
    if (hHook)
        UnhookWindowsHookEx(hHook);

    g_Path = "thread";
    hThread = CreateThread(NULL, 0, SenderThread, hwnd, 0, NULL);
    ok(hThread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (hThread)
    {
        while (MsgWaitForMultipleObjects(1, &hThread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }
        GetExitCodeThread(hThread, &ExitCode);
        ok(ExitCode == 0x5150, "thread: result %lx\n", ExitCode);
        CloseHandle(hThread);
    }

    ok(g_Received == 3, "the window procedure got %ld WM_MDICREATE messages\n", g_Received);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
