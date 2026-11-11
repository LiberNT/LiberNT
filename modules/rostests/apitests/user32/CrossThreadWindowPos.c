/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that SetWindowPos on a window of another thread returns after that thread handled the change,
 *              unless SWP_ASYNCWINDOWPOS is given
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CROSS_CLASS L"CrossThreadWindowPos"
#define WM_CROSS_BLOCK (WM_APP + 1)

static HANDLE g_Ready;
static HANDLE g_Blocked;
static HANDLE g_Release;
static HANDLE g_PosChangedEvent;
static HWND g_Window;
static DWORD g_OwnerThreadId;
static volatile LONG g_PosChanged;
static volatile LONG g_InOwnerThread;
static volatile LONG g_ClientCx;

typedef struct _CROSS_RESULT
{
    BOOL Moved;
    LONG PosChangedAfterMove;
    LONG ClientCxAfterMove;
    BOOL Resized;
    LONG PosChangedAfterResize;
    LONG ClientCxAfterResize;
} CROSS_RESULT;

static CROSS_RESULT g_Result;

static LRESULT CALLBACK
CrossWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_WINDOWPOSCHANGED && hWnd == g_Window)
    {
        /* Late enough that a caller which does not wait finds nothing handled yet */
        Sleep(100);
        InterlockedExchange(&g_InOwnerThread, GetCurrentThreadId() == g_OwnerThreadId);
        InterlockedIncrement(&g_PosChanged);
        SetEvent(g_PosChangedEvent);
    }
    else if (Msg == WM_SIZE && hWnd == g_Window)
    {
        InterlockedExchange(&g_ClientCx, LOWORD(lParam));
    }
    else if (Msg == WM_CROSS_BLOCK && hWnd == g_Window)
    {
        SetEvent(g_Blocked);
        WaitForSingleObject(g_Release, 10000);
        return 0;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static DWORD WINAPI
OwnerThread(LPVOID Param)
{
    MSG Msg;

    UNREFERENCED_PARAMETER(Param);

    g_Window = CreateWindowExW(WS_EX_TOOLWINDOW, CROSS_CLASS, CROSS_CLASS, WS_POPUP | WS_VISIBLE,
                               10, 10, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(g_Ready);
    if (!g_Window)
        return 0;

    while (GetMessageW(&Msg, NULL, 0, 0) > 0)
        DispatchMessageW(&Msg);

    DestroyWindow(g_Window);
    return 0;
}

static DWORD WINAPI
CallerThread(LPVOID Param)
{
    UNREFERENCED_PARAMETER(Param);

    InterlockedExchange(&g_PosChanged, 0);
    g_Result.Moved = MoveWindow(g_Window, 20, 30, 200, 150, TRUE);
    g_Result.PosChangedAfterMove = InterlockedCompareExchange(&g_PosChanged, 0, 0);
    g_Result.ClientCxAfterMove = InterlockedCompareExchange(&g_ClientCx, 0, 0);

    InterlockedExchange(&g_PosChanged, 0);
    g_Result.Resized = SetWindowPos(g_Window, NULL, 0, 0, 250, 120,
                                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    g_Result.PosChangedAfterResize = InterlockedCompareExchange(&g_PosChanged, 0, 0);
    g_Result.ClientCxAfterResize = InterlockedCompareExchange(&g_ClientCx, 0, 0);
    return 0;
}

static BOOL g_AsyncResult;
static LONG g_AsyncPosChangedAtReturn;

static DWORD WINAPI
AsyncCallerThread(LPVOID Param)
{
    UNREFERENCED_PARAMETER(Param);

    g_AsyncResult = SetWindowPos(g_Window, NULL, 40, 50, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    g_AsyncPosChangedAtReturn = InterlockedCompareExchange(&g_PosChanged, 0, 0);
    return 0;
}

static void
TestAsyncWindowPos(void)
{
    HANDLE Caller;
    DWORD Wait;
    RECT Rect;

    ResetEvent(g_PosChangedEvent);
    InterlockedExchange(&g_PosChanged, 0);
    InterlockedExchange(&g_InOwnerThread, FALSE);
    ok_int(PostMessageW(g_Window, WM_CROSS_BLOCK, 0, 0), TRUE);
    Wait = WaitForSingleObject(g_Blocked, 5000);
    ok_long(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
        return;

    Caller = CreateThread(NULL, 0, AsyncCallerThread, NULL, 0, NULL);
    ok(Caller != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (Caller)
    {
        ok_long(WaitForSingleObject(Caller, 5000), WAIT_OBJECT_0);
        ok_int(g_AsyncResult, TRUE);
        ok_long(g_AsyncPosChangedAtReturn, 0);
        CloseHandle(Caller);
    }

    SetEvent(g_Release);
    ok_long(WaitForSingleObject(g_PosChangedEvent, 5000), WAIT_OBJECT_0);
    ok_long(g_InOwnerThread, TRUE);
    ok_int(GetWindowRect(g_Window, &Rect), TRUE);
    ok_long(Rect.left, 40);
    ok_long(Rect.top, 50);
}

START_TEST(CrossThreadWindowPos)
{
    WNDCLASSW Class = { 0 };
    HANDLE Owner, Caller;
    DWORD Wait;

    Class.lpfnWndProc = CrossWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = CROSS_CLASS;
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    g_Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_Blocked = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_Release = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_PosChangedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(g_Ready && g_Blocked && g_Release && g_PosChangedEvent, "CreateEventW failed, error %lu\n", GetLastError());
    if (!g_Ready || !g_Blocked || !g_Release || !g_PosChangedEvent)
        return;

    Owner = CreateThread(NULL, 0, OwnerThread, NULL, 0, &g_OwnerThreadId);
    ok(Owner != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (!Owner)
        return;
    ok_long(WaitForSingleObject(g_Ready, 5000), WAIT_OBJECT_0);
    ok(g_Window != NULL, "CreateWindowExW failed\n");
    if (!g_Window)
    {
        WaitForSingleObject(Owner, 5000);
        CloseHandle(Owner);
        return;
    }

    TestAsyncWindowPos();

    Caller = CreateThread(NULL, 0, CallerThread, NULL, 0, NULL);
    ok(Caller != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (Caller)
    {
        Wait = WaitForSingleObject(Caller, 5000);
        ok_long(Wait, WAIT_OBJECT_0);
        if (Wait == WAIT_OBJECT_0)
        {
            ok_int(g_Result.Moved, TRUE);
            ok_long(g_Result.PosChangedAfterMove, 1);
            ok_long(g_Result.ClientCxAfterMove, 200);
            ok_int(g_Result.Resized, TRUE);
            ok_long(g_Result.PosChangedAfterResize, 1);
            ok_long(g_Result.ClientCxAfterResize, 250);
            ok_long(g_InOwnerThread, TRUE);
        }
        CloseHandle(Caller);
    }

    PostThreadMessageW(g_OwnerThreadId, WM_QUIT, 0, 0);
    ok_long(WaitForSingleObject(Owner, 5000), WAIT_OBJECT_0);
    CloseHandle(Owner);
    CloseHandle(g_PosChangedEvent);
    CloseHandle(g_Release);
    CloseHandle(g_Blocked);
    CloseHandle(g_Ready);
    UnregisterClassW(CROSS_CLASS, Class.hInstance);
}
