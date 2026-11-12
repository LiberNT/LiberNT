/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which window takes the foreground when the foreground window is hidden or destroyed
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>
#include "fghide.h"

#define HELPER L"user32_apitest_fghide.exe"

static HWND hwndThreadControl;
static HANDLE hSleeping;
static WNDPROC OwnedProc;
static UINT OwnedActivations;

static LRESULT CALLBACK
CountActivationProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE)
        OwnedActivations++;
    return CallWindowProcW(OwnedProc, hWnd, Message, wParam, lParam);
}

static LRESULT CALLBACK
ControlProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    HWND hwndNew;

    switch (Message)
    {
        case FGHIDE_CREATE:
            hwndNew = CreateWindowExW(0, FGHIDE_WINDOW_CLASS, L"ForegroundAfterHide thread", WS_OVERLAPPEDWINDOW,
                                      (INT)wParam, (INT)lParam, 240, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
            ShowWindow(hwndNew, SW_SHOWNOACTIVATE);
            return (LRESULT)hwndNew;
        case FGHIDE_DESTROY:
            return DestroyWindow((HWND)wParam);
        case FGHIDE_ACTIVATE:
            return SetForegroundWindow((HWND)wParam);
        case FGHIDE_SLEEP:
            SetEvent(hSleeping);
            Sleep((DWORD)wParam);
            return 0;
        case FGHIDE_DISABLE:
            return EnableWindow((HWND)wParam, FALSE);
        case FGHIDE_MINIMIZE:
            return ShowWindow((HWND)wParam, SW_SHOWMINNOACTIVE);
    }
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static DWORD WINAPI
ControlThread(LPVOID Parameter)
{
    MSG Message;

    hwndThreadControl = CreateWindowExW(0, FGHIDE_CONTROL_CLASS, L"", WS_POPUP, 0, 0, 1, 1,
                                        NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent((HANDLE)Parameter);
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
        DispatchMessageW(&Message);
    return 0;
}

static VOID
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static HWND
WaitForForeground(VOID)
{
    INT i;

    for (i = 0; i < 100 && !GetForegroundWindow(); i++)
        Pump(20);
    Pump(200);
    return GetForegroundWindow();
}

static HWND
StartHelper(PCWSTR Path, PCWSTR Name, PROCESS_INFORMATION *Process)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    WCHAR Command[MAX_PATH + 16];
    HANDLE Ready = CreateEventW(NULL, FALSE, FALSE, FGHIDE_READY_EVENT);
    HWND hwndControl = NULL;

    StringCchPrintfW(Command, ARRAYSIZE(Command), L"\"%s\" %s", Path, Name);
    if (Ready && CreateProcessW(Path, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, Process))
    {
        if (WaitForSingleObject(Ready, 10000) == WAIT_OBJECT_0)
            hwndControl = FindWindowW(FGHIDE_CONTROL_CLASS, Name);
    }
    if (Ready)
        CloseHandle(Ready);
    return hwndControl;
}

static HWND
CreateIn(HWND hwndControl, INT x, INT y)
{
    return (HWND)SendMessageW(hwndControl, FGHIDE_CREATE, x, y);
}

static VOID
GiveForeground(HWND hwndControl, HWND hWnd)
{
    AllowSetForegroundWindow(ASFW_ANY);
    SendMessageW(hwndControl, FGHIDE_ACTIVATE, (WPARAM)hWnd, 0);
    Pump(300);
}

static HWND
CreateOwnWindow(INT x, INT y, BOOL Activate)
{
    HWND hWnd = CreateWindowExW(0, FGHIDE_WINDOW_CLASS, L"ForegroundAfterHide", WS_OVERLAPPEDWINDOW,
                                x, y, 240, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);

    ShowWindow(hWnd, Activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    if (Activate)
        SetForegroundWindow(hWnd);
    Pump(300);
    return hWnd;
}

static VOID
AllowBack(HWND hwndHelper1, HWND hwndHelper2)
{
    SendMessageW(hwndHelper1, FGHIDE_ALLOW, GetCurrentProcessId(), 0);
    SendMessageW(hwndHelper2, FGHIDE_ALLOW, GetCurrentProcessId(), 0);
}

static HWND
NextVisible(HWND hWnd)
{
    do
    {
        hWnd = GetWindow(hWnd, GW_HWNDNEXT);
    } while (hWnd && !IsWindowVisible(hWnd));
    return hWnd;
}

static HWND
TakeForeground(HWND hwndHelper1, HWND hwndHelper2, HWND hwndBelow)
{
    HWND hWnd;

    AllowBack(hwndHelper1, hwndHelper2);
    hWnd = CreateOwnWindow(100, 100, TRUE);
    ok(GetForegroundWindow() == hWnd, "The test window did not take the foreground\n");
    if (hwndBelow)
    {
        SetWindowPos(hwndBelow, hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Pump(200);
        ok(NextVisible(hWnd) == hwndBelow, "The window is not the visible window right below the test window\n");
    }
    return hWnd;
}

static HWND
CreateOwnedForeground(HWND hwndOwner)
{
    HWND hWnd = CreateWindowExW(0, FGHIDE_WINDOW_CLASS, L"ForegroundAfterHide owned", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                160, 160, 240, 160, hwndOwner, NULL, GetModuleHandleW(NULL), NULL);

    ShowWindow(hWnd, SW_SHOWNORMAL);
    SetForegroundWindow(hWnd);
    Pump(300);
    ok(GetForegroundWindow() == hWnd, "The owned window did not take the foreground\n");
    SetForegroundWindow(hwndOwner);
    Pump(300);
    ok(GetForegroundWindow() == hwndOwner, "The owner did not take the foreground back\n");
    return hWnd;
}

static VOID
RunCases(HWND hwndHelper1, HWND hwndHelper2)
{
    HWND hWnd, hwndPrev, hwndOther, hwndNext, hwndOwned;

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hwndOther = CreateIn(hwndHelper1, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndOther);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide: the previous foreground window is not the foreground window\n");
    ok(GetActiveWindow() == NULL, "Hide: the thread kept an active window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndOther, 0);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndOther = CreateIn(hwndThreadControl, 300, 300);
    GiveForeground(hwndThreadControl, hwndOther);
    hwndPrev = CreateIn(hwndHelper1, 200, 200);
    GiveForeground(hwndHelper1, hwndPrev);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndOther);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide: the most recent foreground window is not the foreground window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    SendMessageW(hwndThreadControl, FGHIDE_DESTROY, (WPARAM)hwndOther, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hwndOther = CreateOwnWindow(200, 200, FALSE);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndOther);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide: a window of the same thread took the foreground\n");
    ok(GetActiveWindow() == NULL, "Hide: the thread activated its other window\n");
    DestroyWindow(hWnd);
    DestroyWindow(hwndOther);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateOwnWindow(200, 200, TRUE);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide: the previous foreground window of the thread is not the foreground window\n");
    ok(GetActiveWindow() == hwndPrev, "Hide: the previous window of the thread is not active\n");
    DestroyWindow(hWnd);
    DestroyWindow(hwndPrev);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper1, 300, 300);
    GiveForeground(hwndHelper1, hwndPrev);
    hwndNext = CreateIn(hwndHelper2, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndNext);
    SendMessageW(hwndHelper1, FGHIDE_HIDE, (WPARAM)hwndPrev, 0);
    Pump(200);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndNext, "Hide: the next window in the z-order is not the foreground window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndNext, 0);
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hwndOther = CreateIn(hwndHelper1, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndOther);
    DestroyWindow(hWnd);
    ok(WaitForForeground() == hwndPrev, "Destroy: the previous foreground window is not the foreground window\n");
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndOther, 0);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hwndOther = CreateIn(hwndHelper1, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndOther);
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    Pump(300);
    ok(!IsWindowVisible(hWnd), "SetWindowPos hide: the window is still visible\n");
    ok(GetForegroundWindow() == hWnd, "SetWindowPos hide: the hidden window lost the foreground\n");
    ok(GetActiveWindow() == hWnd, "SetWindowPos hide: the hidden window is not active\n");
    DestroyWindow(hWnd);
    ok(WaitForForeground() == hwndPrev, "SetWindowPos hide: the previous foreground window did not get the foreground back on destroy\n");
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndOther, 0);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    GiveForeground(hwndHelper2, hwndPrev);
    AllowBack(hwndHelper1, hwndHelper2);
    SetForegroundWindow(hWnd);
    Pump(300);
    ok(GetForegroundWindow() == hWnd, "The test window did not take the foreground back\n");
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide after a switch back: the previous foreground window is not the foreground window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndOther = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndOther);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    hwndPrev = CreateIn(hwndHelper1, 200, 200);
    GiveForeground(hwndHelper1, hwndPrev);
    ok(GetForegroundWindow() == hwndPrev, "The window of the first helper did not take the foreground\n");
    AllowBack(hwndHelper1, hwndHelper2);
    SetForegroundWindow(hWnd);
    Pump(300);
    ok(GetForegroundWindow() == hWnd, "The still active test window did not take the foreground back\n");
    SetWindowPos(hwndOther, hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Pump(200);
    ok(NextVisible(hWnd) == hwndOther, "The older foreground window is not right below the test window\n");
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndPrev, "Hide after a switch back to the active window: the previous foreground window is not the foreground window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper1, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndOther, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndThreadControl, 300, 300);
    GiveForeground(hwndThreadControl, hwndPrev);
    hwndNext = CreateIn(hwndHelper2, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndNext);
    SendMessageW(hwndThreadControl, FGHIDE_DISABLE, (WPARAM)hwndPrev, 0);
    Pump(200);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndNext, "Hide: the disabled previous foreground window was not skipped\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndNext, 0);
    SendMessageW(hwndThreadControl, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndThreadControl, 300, 300);
    GiveForeground(hwndThreadControl, hwndPrev);
    hwndNext = CreateIn(hwndHelper2, 200, 200);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, hwndNext);
    SendMessageW(hwndThreadControl, FGHIDE_MINIMIZE, (WPARAM)hwndPrev, 0);
    Pump(300);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndNext, "Hide: the minimized previous foreground window was not skipped\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndNext, 0);
    SendMessageW(hwndThreadControl, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    hwndOwned = CreateOwnedForeground(hWnd);
    ShowWindow(hWnd, SW_HIDE);
    ok(WaitForForeground() == hwndOwned, "Hide owner: the owned previous foreground window is not the foreground window\n");
    ok(GetActiveWindow() == hwndOwned, "Hide owner: the owned previous foreground window is not active\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndHelper2, 300, 300);
    GiveForeground(hwndHelper2, hwndPrev);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    hwndOwned = CreateOwnedForeground(hWnd);
    OwnedActivations = 0;
    OwnedProc = (WNDPROC)SetWindowLongPtrW(hwndOwned, GWLP_WNDPROC, (LONG_PTR)CountActivationProc);
    DestroyWindow(hWnd);
    ok(WaitForForeground() == hwndPrev, "Destroy owner: the window below the owner is not the foreground window\n");
    ok(OwnedActivations == 0, "Destroy owner: the owned window was activated %u times\n", OwnedActivations);
    SendMessageW(hwndHelper2, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);

    hwndPrev = CreateIn(hwndThreadControl, 200, 200);
    GiveForeground(hwndThreadControl, hwndPrev);
    hWnd = TakeForeground(hwndHelper1, hwndHelper2, NULL);
    PostMessageW(hwndThreadControl, FGHIDE_SLEEP, 1500, 0);
    ok(WaitForSingleObject(hSleeping, 5000) == WAIT_OBJECT_0, "Busy: the thread did not start sleeping\n");
    ShowWindow(hWnd, SW_HIDE);
    ok(GetForegroundWindow() == NULL, "Busy: there is a foreground window right after the hide\n");
    Pump(300);
    ok(GetForegroundWindow() == NULL, "Busy: there is a foreground window while the thread is busy\n");
    Pump(2000);
    ok(GetForegroundWindow() == hwndPrev, "Busy: the previous foreground window is not the foreground window\n");
    DestroyWindow(hWnd);
    SendMessageW(hwndThreadControl, FGHIDE_DESTROY, (WPARAM)hwndPrev, 0);
    Pump(300);
}

START_TEST(ForegroundAfterHide)
{
    WCHAR Path[MAX_PATH], *Slash;
    WNDCLASSW Class = { 0 };
    PROCESS_INFORMATION Process1 = { 0 }, Process2 = { 0 };
    HWND hwndHelper1, hwndHelper2;
    HANDLE Ready, Thread;
    DWORD ThreadId;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    Slash = wcsrchr(Path, L'\\');
    if (!Slash || (Slash - Path) + 1 + wcslen(HELPER) >= ARRAYSIZE(Path))
    {
        skip("No path for %ls\n", HELPER);
        return;
    }
    StringCchCopyW(Slash + 1, ARRAYSIZE(Path) - (Slash + 1 - Path), HELPER);
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls is missing\n", Path);
        return;
    }

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = FGHIDE_WINDOW_CLASS;
    RegisterClassW(&Class);
    Class.lpfnWndProc = ControlProc;
    Class.lpszClassName = FGHIDE_CONTROL_CLASS;
    RegisterClassW(&Class);

    hwndHelper1 = StartHelper(Path, L"1", &Process1);
    hwndHelper2 = StartHelper(Path, L"2", &Process2);
    Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    hSleeping = CreateEventW(NULL, FALSE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ControlThread, Ready, 0, &ThreadId);
    if (Thread)
        WaitForSingleObject(Ready, 10000);
    ok(hwndHelper1 && hwndHelper2 && hwndThreadControl, "The helpers did not start\n");
    if (hwndHelper1 && hwndHelper2 && hwndThreadControl)
        RunCases(hwndHelper1, hwndHelper2);

    if (hwndThreadControl)
        PostMessageW(hwndThreadControl, WM_QUIT, 0, 0);
    if (Thread)
    {
        WaitForSingleObject(Thread, 5000);
        CloseHandle(Thread);
    }
    if (Ready)
        CloseHandle(Ready);
    if (hSleeping)
        CloseHandle(hSleeping);
    if (Process1.hProcess)
    {
        PostThreadMessageW(Process1.dwThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(Process1.hProcess, 5000);
        CloseHandle(Process1.hProcess);
        CloseHandle(Process1.hThread);
    }
    if (Process2.hProcess)
    {
        PostThreadMessageW(Process2.dwThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(Process2.hProcess, 5000);
        CloseHandle(Process2.hProcess);
        CloseHandle(Process2.hThread);
    }
    UnregisterClassW(FGHIDE_CONTROL_CLASS, GetModuleHandleW(NULL));
    UnregisterClassW(FGHIDE_WINDOW_CLASS, GetModuleHandleW(NULL));
}
