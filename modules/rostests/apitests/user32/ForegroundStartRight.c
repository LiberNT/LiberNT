/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the process that received the last input keeps the foreground right after losing the foreground
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

#define CLASS_NAME L"ForegroundStartRightClass"

static void PumpMessages(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static void PumpFor(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();

    while (GetTickCount() - Start < Milliseconds)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
        PumpMessages();
    }
}

static HWND CreateShownWindow(PCWSTR Title)
{
    HWND hwnd;

    hwnd = CreateWindowExW(0, CLASS_NAME, Title, WS_OVERLAPPEDWINDOW,
                           120, 120, 240, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    PumpMessages();
    return hwnd;
}

static int RunTypist(PCSTR Mode)
{
    INPUT input[2] = { 0 };
    UINT Count = 2;

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_F24;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (!strcmp(Mode, "down"))
        Count = 1;
    else if (!strcmp(Mode, "up"))
    {
        input[0] = input[1];
        Count = 1;
    }
    else if (!strcmp(Mode, "click"))
    {
        input[0].type = INPUT_MOUSE;
        input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        input[1].type = INPUT_MOUSE;
        input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    }
    return SendInput(Count, input, sizeof(INPUT)) == Count ? 1 : 0;
}

static int RunHolder(HANDLE hQuit, HANDLE hYield)
{
    HWND hwnd = CreateShownWindow(L"Holder");
    HANDLE Handles[2] = { hQuit, hYield };
    DWORD Wait;

    for (;;)
    {
        Wait = MsgWaitForMultipleObjects(2, Handles, FALSE, 30000, QS_ALLINPUT);
        if (Wait == WAIT_OBJECT_0 + 1)
        {
            ResetEvent(hYield);
            SetForegroundWindow(FindWindowW(CLASS_NAME, L"Main"));
        }
        else if (Wait == WAIT_OBJECT_0 + 2)
            PumpMessages();
        else
            break;
    }
    DestroyWindow(hwnd);
    return 0;
}

static int RunShower(HANDLE hGo)
{
    HWND hwnd;
    BOOL Foreground;

    if (hGo)
        WaitForSingleObject(hGo, 15000);
    hwnd = CreateShownWindow(L"Shower");

    PumpFor(200);
    Foreground = (GetForegroundWindow() == hwnd);
    DestroyWindow(hwnd);
    return Foreground ? 1 : 0;
}

static BOOL StartChild(PCWSTR Mode, HANDLE hQuit, HANDLE hYield, PROCESS_INFORMATION *ProcessInfo)
{
    WCHAR FileName[MAX_PATH], CommandLine[MAX_PATH + 64];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" ForegroundStartRight %ls %Ix %Ix",
                    FileName, Mode, (ULONG_PTR)hQuit, (ULONG_PTR)hYield);
    return CreateProcessW(FileName, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, ProcessInfo);
}

static int WaitChild(PROCESS_INFORMATION *ProcessInfo)
{
    DWORD ExitCode = 2;

    while (MsgWaitForMultipleObjects(1, &ProcessInfo->hProcess, FALSE, 15000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        PumpMessages();
    GetExitCodeProcess(ProcessInfo->hProcess, &ExitCode);
    CloseHandle(ProcessInfo->hThread);
    CloseHandle(ProcessInfo->hProcess);
    PumpFor(300);
    return (int)ExitCode;
}

static int RunChild(PCWSTR Mode)
{
    PROCESS_INFORMATION ProcessInfo;

    if (!StartChild(Mode, NULL, NULL, &ProcessInfo))
        return -1;
    return WaitChild(&ProcessInfo);
}

static int ShowAfterInput(PCWSTR InputMode)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    PROCESS_INFORMATION ProcessInfo;
    HANDLE hGo = CreateEventW(&Inherit, TRUE, FALSE, NULL);
    int Result = -1;

    if (StartChild(L"show", hGo, NULL, &ProcessInfo))
    {
        PumpFor(500);
        ok_int(RunChild(InputMode), 1);
        SetEvent(hGo);
        Result = WaitChild(&ProcessInfo);
    }
    CloseHandle(hGo);
    return Result;
}

START_TEST(ForegroundStartRight)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    PROCESS_INFORMATION Holder;
    WNDCLASSW wc = { 0 };
    HWND hwnd, hwndHolder;
    RECT Rect;
    HANDLE hQuit, hYield;
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    if (Count >= 5 && !strcmp(Arguments[2], "hold"))
        ExitProcess(RunHolder((HANDLE)(ULONG_PTR)_strtoui64(Arguments[3], NULL, 16),
                              (HANDLE)(ULONG_PTR)_strtoui64(Arguments[4], NULL, 16)));
    if (Count >= 4 && !strcmp(Arguments[2], "show"))
        ExitProcess(RunShower((HANDLE)(ULONG_PTR)_strtoui64(Arguments[3], NULL, 16)));
    if (Count >= 3 && (!strcmp(Arguments[2], "type") || !strcmp(Arguments[2], "down") ||
                       !strcmp(Arguments[2], "up") || !strcmp(Arguments[2], "click")))
        ExitProcess(RunTypist(Arguments[2]));

    hwnd = CreateShownWindow(L"Main");
    if (GetForegroundWindow() != hwnd)
    {
        skip("The test process could not take the foreground\n");
        DestroyWindow(hwnd);
        return;
    }

    GetWindowRect(hwnd, &Rect);
    SetCursorPos((Rect.left + Rect.right) / 2, (Rect.top + Rect.bottom) / 2);
    ok_int(ShowAfterInput(L"type"), 0);
    ok(GetForegroundWindow() == hwnd, "The test window lost the foreground\n");
    ok_int(ShowAfterInput(L"click"), 0);
    ok(GetForegroundWindow() == hwnd, "The test window lost the foreground\n");
    ok_int(RunChild(L"down"), 1);
    ok_int(ShowAfterInput(L"up"), 1);
    SetForegroundWindow(hwnd);
    PumpFor(300);
    if (GetForegroundWindow() != hwnd)
    {
        skip("The test process could not take the foreground back\n");
        DestroyWindow(hwnd);
        return;
    }

    hQuit = CreateEventW(&Inherit, TRUE, FALSE, NULL);
    hYield = CreateEventW(&Inherit, TRUE, FALSE, NULL);
    if (!StartChild(L"hold", hQuit, hYield, &Holder))
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        CloseHandle(hYield);
        CloseHandle(hQuit);
        DestroyWindow(hwnd);
        return;
    }
    PumpFor(1000);
    hwndHolder = GetForegroundWindow();
    ok(hwndHolder != NULL && hwndHolder != hwnd, "The holder did not take the foreground\n");

    ok_int(RunChild(L"type"), 1);
    ok_int(RunChild(L"show"), 1);

    SetEvent(hYield);
    PumpFor(500);
    ok(GetForegroundWindow() == hwnd, "The holder did not give the foreground back\n");

    ok_int(RunChild(L"type"), 1);
    ok_int(SetForegroundWindow(hwndHolder), TRUE);
    PumpFor(200);
    ok(GetForegroundWindow() == hwndHolder, "The holder is not the foreground window\n");
    ok_int(RunChild(L"show"), 1);

    SetEvent(hQuit);
    WaitForSingleObject(Holder.hProcess, 15000);
    CloseHandle(Holder.hThread);
    CloseHandle(Holder.hProcess);
    CloseHandle(hYield);
    CloseHandle(hQuit);
    DestroyWindow(hwnd);
    PumpMessages();
    UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
}
