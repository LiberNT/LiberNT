/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the foreground of a process started by the process that received the last input
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

#define LAUNCHER    L"user32_apitest_fglaunch.exe"
#define READY_EVENT L"ForegroundLastInputReady"
#define GO_EVENT    L"ForegroundLastInputGo"
#define HIDDEN_EVENT L"ForegroundLastInputHidden"
#define TAKEN_EVENT L"ForegroundLastInputTaken"

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

static VOID
PressKey(VOID)
{
    INPUT Input[2] = { 0 };

    Input[0].type = INPUT_KEYBOARD;
    Input[0].ki.wVk = VK_F13;
    Input[1] = Input[0];
    Input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(ARRAYSIZE(Input), Input, sizeof(INPUT));
}

START_TEST(ForegroundLastInput)
{
    WCHAR Path[MAX_PATH], *Slash, Command[MAX_PATH + 2];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Launcher;
    WNDCLASSW Class = { 0 };
    HANDLE Ready, Go, Hidden, Taken;
    HWND hWnd;
    DWORD Result = 0;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    Slash = wcsrchr(Path, L'\\');
    if (!Slash || (Slash - Path) + 1 + wcslen(LAUNCHER) >= ARRAYSIZE(Path))
    {
        skip("No path for %ls\n", LAUNCHER);
        return;
    }
    lstrcpyW(Slash + 1, LAUNCHER);
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls is missing\n", Path);
        return;
    }

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = GetStockObject(WHITE_BRUSH);
    Class.lpszClassName = L"ForegroundLastInput";
    RegisterClassW(&Class);
    hWnd = CreateWindowExW(0, Class.lpszClassName, L"ForegroundLastInput other", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           40, 40, 300, 220, NULL, NULL, Class.hInstance, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    SetForegroundWindow(hWnd);
    Pump(500);

    Ready = CreateEventW(NULL, TRUE, FALSE, READY_EVENT);
    Go = CreateEventW(NULL, TRUE, FALSE, GO_EVENT);
    Hidden = CreateEventW(NULL, TRUE, FALSE, HIDDEN_EVENT);
    Taken = CreateEventW(NULL, TRUE, FALSE, TAKEN_EVENT);
    ok(Ready && Go && Hidden && Taken, "CreateEventW failed: %lu\n", GetLastError());
    if (!hWnd || !Ready || !Go || !Hidden || !Taken)
        return;

    StringCchPrintfW(Command, ARRAYSIZE(Command), L"\"%s\"", Path);
    if (!CreateProcessW(Path, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Launcher))
    {
        ok(FALSE, "CreateProcessW(%ls) failed: %lu\n", Path, GetLastError());
        return;
    }
    ok(WaitForSingleObject(Ready, 20000) == WAIT_OBJECT_0, "The launcher did not get ready\n");
    PressKey();
    Pump(500);
    SetEvent(Go);
    ok(WaitForSingleObject(Hidden, 20000) == WAIT_OBJECT_0, "The launcher did not hide its window\n");
    SetForegroundWindow(hWnd);
    Pump(500);
    ok(GetForegroundWindow() == hWnd, "The other window did not take the foreground left free by the launcher\n");
    SetEvent(Taken);
    ok(WaitForSingleObject(Launcher.hProcess, 30000) == WAIT_OBJECT_0, "The launcher did not exit\n");
    GetExitCodeProcess(Launcher.hProcess, &Result);
    CloseHandle(Launcher.hProcess);
    CloseHandle(Launcher.hThread);

    ok(!(Result & 0x180), "The launcher or the probe failed to start: 0x%lx\n", Result);
    ok(Result & 0x10, "The launcher window was not the foreground window\n");
    ok(Result & 0x20, "The launcher did not receive the key press\n");
    ok(Result & 0x40, "Another window was not the foreground window when the launcher started the probe\n");
    ok(Result & 0x01, "The window of the process started by the launcher is not the foreground window\n");

    CloseHandle(Ready);
    CloseHandle(Go);
    CloseHandle(Hidden);
    CloseHandle(Taken);
    DestroyWindow(hWnd);
}
