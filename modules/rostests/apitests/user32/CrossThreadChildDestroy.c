/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WM_DESTROY delivery to a child window whose parent belongs to another thread or process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HWND Parent;
static HWND Child;
static HANDLE ChildReady;
static HANDLE ParentGone;
static LONG ProcDestroy;
static LONG ProcNcDestroy;
static LONG HookDestroy;
static BOOL SecondDestroy;
static DWORD SecondDestroyError;

static HANDLE DestroyParentNow;
static HANDLE ParentDestroyed;

static LRESULT CALLBACK
ChildProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_DESTROY && InterlockedIncrement(&ProcDestroy) == 1 && DestroyParentNow)
    {
        MSG Msg;

        SetEvent(DestroyParentNow);
        while (MsgWaitForMultipleObjects(1, &ParentDestroyed, FALSE, 5000, QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1)
            PeekMessageW(&Msg, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    else if (Message == WM_NCDESTROY)
        InterlockedIncrement(&ProcNcDestroy);
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static LRESULT CALLBACK
CallWndHook(int Code, WPARAM wParam, LPARAM lParam)
{
    const CWPSTRUCT *Params = (const CWPSTRUCT *)lParam;

    if (Code == HC_ACTION && Params->message == WM_DESTROY && Params->hwnd == Child)
        InterlockedIncrement(&HookDestroy);
    return CallNextHookEx(NULL, Code, wParam, lParam);
}

static void
pump(DWORD Timeout)
{
    DWORD Deadline = GetTickCount() + Timeout;
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        if ((LONG)(Deadline - GetTickCount()) <= 0)
            break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    } while (TRUE);
}

static DWORD WINAPI
ChildThread(PVOID Param)
{
    HHOOK Hook;

    Hook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndHook, NULL, GetCurrentThreadId());
    ok(Hook != NULL, "SetWindowsHookExW failed, error %lu\n", GetLastError());
    Child = CreateWindowExW(0, L"CrossThreadChild", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
                            Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Child != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    SetEvent(ChildReady);

    while (WaitForSingleObject(ParentGone, 0) == WAIT_TIMEOUT)
        pump(20);
    pump(200);

    SetLastError(0xdeadbeef);
    SecondDestroy = DestroyWindow(Child);
    SecondDestroyError = GetLastError();
    pump(100);

    if (Hook)
        UnhookWindowsHookEx(Hook);
    return 0;
}

static DWORD WINAPI
DestroyingParentThread(PVOID Param)
{
    Parent = CreateWindowExW(0, L"CrossThreadParent", L"Parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 300, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(ChildReady);
    while (MsgWaitForMultipleObjects(1, &DestroyParentNow, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        pump(0);
    DestroyWindow(Parent);
    SetEvent(ParentDestroyed);
    pump(100);
    return 0;
}

static void
test_parent_destroyed_during_child_destroy(void)
{
    HANDLE Thread;
    HHOOK Hook;

    ProcDestroy = ProcNcDestroy = HookDestroy = 0;
    ResetEvent(ChildReady);
    DestroyParentNow = CreateEventW(NULL, TRUE, FALSE, NULL);
    ParentDestroyed = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, DestroyingParentThread, NULL, 0, NULL);
    WaitForSingleObject(ChildReady, 10000);

    Hook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndHook, NULL, GetCurrentThreadId());
    Child = CreateWindowExW(0, L"CrossThreadChild", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
                            Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Child != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    pump(50);
    DestroyWindow(Child);
    pump(100);
    WaitForSingleObject(Thread, 10000);

    ok(!IsWindow(Child), "the child window outlived its own destruction\n");
    ok(ProcDestroy == 2, "nested: the child window got %ld WM_DESTROY, expected 2\n", ProcDestroy);
    ok(ProcNcDestroy == 1, "nested: the child window got %ld WM_NCDESTROY, expected 1\n", ProcNcDestroy);
    ok(HookDestroy == 2, "nested: the WH_CALLWNDPROC hook saw %ld WM_DESTROY, expected 2\n", HookDestroy);

    if (Hook)
        UnhookWindowsHookEx(Hook);
    CloseHandle(Thread);
    CloseHandle(DestroyParentNow);
    CloseHandle(ParentDestroyed);
    DestroyParentNow = ParentDestroyed = NULL;
}

static void
child_process(HWND ParentWindow, HANDLE Ready)
{
    HHOOK Hook;
    DWORD Deadline;

    Hook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndHook, NULL, GetCurrentThreadId());
    Child = CreateWindowExW(0, L"CrossThreadChild", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
                            ParentWindow, NULL, GetModuleHandleW(NULL), NULL);
    if (!Hook || !Child)
        ExitProcess(0xfff);
    SetEvent(Ready);
    Deadline = GetTickCount() + 10000;
    while (IsWindow(Child) && (LONG)(Deadline - GetTickCount()) > 0)
        pump(20);
    pump(300);
    UnhookWindowsHookEx(Hook);
    ExitProcess((ProcNcDestroy << 8) | (ProcDestroy << 4) | HookDestroy);
}

static void
test_cross_process(void)
{
    SECURITY_ATTRIBUTES Security = { sizeof(Security), NULL, TRUE };
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    char Module[MAX_PATH], Command[MAX_PATH + 64];
    HANDLE Ready, Handles[2];
    DWORD Code = 0, Wait;

    Parent = CreateWindowExW(0, L"CrossThreadParent", L"Parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 300, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Parent != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    Ready = CreateEventW(&Security, TRUE, FALSE, NULL);
    GetModuleFileNameA(NULL, Module, ARRAY_SIZE(Module));
    sprintf(Command, "\"%s\" CrossThreadChildDestroy child %p %p", Module, Parent, Ready);
    if (!Parent || !Ready || !CreateProcessA(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Info))
    {
        ok(0, "failed to start the child process, error %lu\n", GetLastError());
        return;
    }

    Handles[0] = Ready;
    Handles[1] = Info.hProcess;
    do
    {
        pump(0);
        Wait = MsgWaitForMultipleObjects(2, Handles, FALSE, 10000, QS_ALLINPUT);
    } while (Wait == WAIT_OBJECT_0 + 2);
    ok(Wait == WAIT_OBJECT_0, "the child process did not create its window, wait %lu\n", Wait);

    ok(DestroyWindow(Parent), "DestroyWindow(parent) failed, error %lu\n", GetLastError());
    while (MsgWaitForMultipleObjects(1, &Info.hProcess, FALSE, 15000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        pump(0);
    GetExitCodeProcess(Info.hProcess, &Code);
    ok(Code != 0xfff, "the child process could not create its window\n");
    ok(((Code >> 4) & 0xf) == 1, "cross-process: the child window got %lu WM_DESTROY, expected 1\n", (Code >> 4) & 0xf);
    ok(((Code >> 8) & 0xf) == 1, "cross-process: the child window got %lu WM_NCDESTROY, expected 1\n", (Code >> 8) & 0xf);
    ok((Code & 0xf) == 1, "cross-process: the WH_CALLWNDPROC hook saw %lu WM_DESTROY, expected 1\n", Code & 0xf);

    CloseHandle(Info.hProcess);
    CloseHandle(Info.hThread);
    CloseHandle(Ready);
}

START_TEST(CrossThreadChildDestroy)
{
    WNDCLASSW Class = { 0 };
    HANDLE Thread;
    char **Argv;
    int Argc;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"CrossThreadParent";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());
    Class.lpfnWndProc = ChildProc;
    Class.lpszClassName = L"CrossThreadChild";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Argc = winetest_get_mainargs(&Argv);
    if (Argc == 5 && !strcmp(Argv[2], "child"))
    {
        HWND ParentWindow;
        HANDLE Ready;

        if (sscanf(Argv[3], "%p", &ParentWindow) != 1 || sscanf(Argv[4], "%p", &Ready) != 1)
            ExitProcess(0xfff);
        child_process(ParentWindow, Ready);
    }

    Parent = CreateWindowExW(0, L"CrossThreadParent", L"Parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 300, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Parent != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Parent)
        return;

    ChildReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    ParentGone = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ChildThread, NULL, 0, NULL);
    while (WaitForSingleObject(ChildReady, 0) == WAIT_TIMEOUT)
        pump(10);

    ok(DestroyWindow(Parent), "DestroyWindow(parent) failed, error %lu\n", GetLastError());
    SetEvent(ParentGone);
    while (MsgWaitForMultipleObjects(1, &Thread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        pump(0);

    ok(!IsWindow(Child), "the child window outlived its parent\n");
    ok(ProcDestroy == 1, "the child window got %ld WM_DESTROY, expected 1\n", ProcDestroy);
    ok(ProcNcDestroy == 1, "the child window got %ld WM_NCDESTROY, expected 1\n", ProcNcDestroy);
    ok(HookDestroy == 1, "the WH_CALLWNDPROC hook saw %ld WM_DESTROY, expected 1\n", HookDestroy);
    ok(!SecondDestroy, "DestroyWindow on the destroyed child succeeded\n");
    ok(SecondDestroyError == ERROR_INVALID_WINDOW_HANDLE, "DestroyWindow on the destroyed child set error %lu\n",
       SecondDestroyError);

    CloseHandle(Thread);
    CloseHandle(ParentGone);

    test_cross_process();
    test_parent_destroyed_during_child_destroy();
    CloseHandle(ChildReady);

    UnregisterClassW(L"CrossThreadChild", GetModuleHandleW(NULL));
    UnregisterClassW(L"CrossThreadParent", GetModuleHandleW(NULL));
}
