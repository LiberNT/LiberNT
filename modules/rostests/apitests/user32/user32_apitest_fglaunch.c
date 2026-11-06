/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GUI launcher and probe for the ForegroundLastInput test
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <windows.h>

#define READY_EVENT L"ForegroundLastInputReady"
#define GO_EVENT    L"ForegroundLastInputGo"
#define HIDDEN_EVENT L"ForegroundLastInputHidden"
#define TAKEN_EVENT L"ForegroundLastInputTaken"

static LONG KeyDowns;

static LRESULT CALLBACK
WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_KEYDOWN)
        InterlockedIncrement(&KeyDowns);
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static VOID
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    do
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static HWND
CreateTestWindow(PCWSTR Title, INT X, INT Y)
{
    WNDCLASSW Class = { 0 };

    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = L"user32_apitest_fglaunch";
    RegisterClassW(&Class);
    return CreateWindowExW(0, Class.lpszClassName, Title, WS_OVERLAPPEDWINDOW, X, Y, 240, 160, NULL, NULL,
                           Class.hInstance, NULL);
}

static DWORD
RunProbe(VOID)
{
    HWND hWnd = CreateTestWindow(L"ForegroundLastInput probe", 420, 320);
    DWORD Result = 0;

    if (!hWnd)
        return 0x80;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    Pump(1000);
    if (GetForegroundWindow() == hWnd)
        Result |= 1;
    DestroyWindow(hWnd);
    return Result;
}

static DWORD
RunLauncher(VOID)
{
    WCHAR Module[MAX_PATH], Command[MAX_PATH + 16];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Probe;
    HANDLE Ready, Go, Hidden, Taken;
    HWND hWnd;
    DWORD Result = 0, ProbeResult = 0x80;

    Ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, READY_EVENT);
    Go = OpenEventW(SYNCHRONIZE, FALSE, GO_EVENT);
    Hidden = OpenEventW(EVENT_MODIFY_STATE, FALSE, HIDDEN_EVENT);
    Taken = OpenEventW(SYNCHRONIZE, FALSE, TAKEN_EVENT);
    hWnd = CreateTestWindow(L"ForegroundLastInput launcher", 120, 120);
    if (!Ready || !Go || !Hidden || !Taken || !hWnd)
        return 0x100;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    SetForegroundWindow(hWnd);
    Pump(500);
    if (GetForegroundWindow() == hWnd)
        Result |= 0x10;
    SetEvent(Ready);
    while (MsgWaitForMultipleObjects(1, &Go, FALSE, 20000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        Pump(10);
    Pump(300);
    if (KeyDowns != 0)
        Result |= 0x20;

    ShowWindow(hWnd, SW_HIDE);
    Pump(500);
    SetEvent(Hidden);
    while (MsgWaitForMultipleObjects(1, &Taken, FALSE, 20000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        Pump(10);
    Pump(300);
    if (GetForegroundWindow() != hWnd && GetForegroundWindow() != NULL)
        Result |= 0x40;

    GetModuleFileNameW(NULL, Module, ARRAYSIZE(Module));
    wsprintfW(Command, L"\"%s\" probe", Module);
    if (CreateProcessW(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Probe))
    {
        WaitForSingleObject(Probe.hProcess, 20000);
        GetExitCodeProcess(Probe.hProcess, &ProbeResult);
        CloseHandle(Probe.hProcess);
        CloseHandle(Probe.hThread);
    }
    DestroyWindow(hWnd);
    return Result | (ProbeResult & 0x8F);
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    if (!lstrcmpiW(lpCmdLine, L"probe"))
        return (int)RunProbe();
    return (int)RunLauncher();
}
