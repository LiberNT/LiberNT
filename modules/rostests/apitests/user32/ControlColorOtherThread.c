/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the WM_CTLCOLOR* messages of a control whose parent belongs to another thread
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

struct CHILD_PARAMS
{
    HWND hwndParent;
    LPCWSTR pszClass;
    DWORD dwStyle;
    HWND hwndChild;
};

static UINT g_cCtlColor;
static UINT g_uLastMsg;
static HWND g_hwndLastChild;
static DWORD g_dwLastSenderThread;
static HBRUSH g_hbrParent;

static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC)
    {
        g_cCtlColor++;
        g_uLastMsg = msg;
        g_hwndLastChild = (HWND)lParam;
        g_dwLastSenderThread = GetWindowThreadProcessId((HWND)lParam, NULL);
        return (LRESULT)g_hbrParent;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI ChildThread(LPVOID pv)
{
    struct CHILD_PARAMS *pParams = pv;
    HWND hwnd;

    hwnd = CreateWindowExW(0, pParams->pszClass, L"text", WS_CHILD | WS_VISIBLE | pParams->dwStyle, 0, 0, 100, 30,
                           pParams->hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    pParams->hwndChild = hwnd;
    if (hwnd)
    {
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        DestroyWindow(hwnd);
    }
    return 0;
}

static void TestChild(HWND hwndParent, LPCWSTR pszClass, DWORD dwStyle, UINT uExpectedMsg)
{
    struct CHILD_PARAMS params = { hwndParent, pszClass, dwStyle, NULL };
    DWORD dwThreadId;
    HANDLE hThread;

    winetest_push_context("%ls", pszClass);
    g_cCtlColor = 0;
    g_uLastMsg = 0;
    g_hwndLastChild = NULL;
    g_dwLastSenderThread = 0;
    hThread = CreateThread(NULL, 0, ChildThread, &params, 0, &dwThreadId);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        while (MsgWaitForMultipleObjects(1, &hThread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }
        CloseHandle(hThread);
    }
    ok(params.hwndChild != NULL, "the control was not created\n");
    ok(g_cCtlColor > 0, "the parent received no WM_CTLCOLOR* message\n");
    ok(g_uLastMsg == uExpectedMsg, "message 0x%x, expected 0x%x\n", g_uLastMsg, uExpectedMsg);
    ok(g_hwndLastChild == params.hwndChild, "lParam %p, expected %p\n", g_hwndLastChild, params.hwndChild);
    ok(g_dwLastSenderThread == dwThreadId, "the control belongs to thread %lu, expected %lu\n",
       g_dwLastSenderThread, dwThreadId);
    winetest_pop_context();
}

START_TEST(ControlColorOtherThread)
{
    WNDCLASSW wc = { 0 };
    HWND hwndParent;

    g_hbrParent = CreateSolidBrush(RGB(0x12, 0x34, 0x56));
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"ControlColorOtherThread";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    hwndParent = CreateWindowExW(0, wc.lpszClassName, L"ControlColorOtherThread", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 100, 100, 300, 200, NULL, NULL, wc.hInstance, NULL);
    ok(hwndParent != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (hwndParent)
    {
        UpdateWindow(hwndParent);
        TestChild(hwndParent, L"STATIC", SS_LEFT, WM_CTLCOLORSTATIC);
        TestChild(hwndParent, L"BUTTON", BS_PUSHBUTTON, WM_CTLCOLORBTN);
        DestroyWindow(hwndParent);
    }
    DeleteObject(g_hbrParent);
}
