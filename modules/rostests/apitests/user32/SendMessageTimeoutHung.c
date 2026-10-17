/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SendMessageTimeout on a window whose thread stopped pumping
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HANDLE Ready, Release;
static HWND HungWindow;

static
DWORD
WINAPI
HungThread(
    _In_ PVOID Parameter)
{
    WNDCLASSW Class = { 0 };

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"LiberNTHungProbe";
    RegisterClassW(&Class);
    HungWindow = CreateWindowExW(0, Class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10,
                                 NULL, NULL, Class.hInstance, NULL);
    SetEvent(Ready);
    WaitForSingleObject(Release, INFINITE);
    DestroyWindow(HungWindow);
    return 0;
}

static
VOID
TestOtherProcess(VOID)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 64];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD_PTR Result;
    LRESULT Ret;
    HWND Window = NULL;
    DWORD Start, Elapsed, Error, Tries;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    _snwprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" SendMessageTimeoutHung", Path);
    SetEnvironmentVariableW(L"LIBERNT_HUNG_CHILD", L"1");
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        SetEnvironmentVariableW(L"LIBERNT_HUNG_CHILD", NULL);
        ok(0, "CreateProcess failed %lu\n", GetLastError());
        return;
    }
    SetEnvironmentVariableW(L"LIBERNT_HUNG_CHILD", NULL);
    for (Tries = 0; Tries < 100 && !(Window = FindWindowW(L"LiberNTHungChild", NULL)); Tries++)
        Sleep(50);
    ok(Window != NULL, "Child window not found\n");
    if (Window)
    {
        printf("Waiting for the child window to be reported hung\n");
        fflush(stdout);
        Sleep(5500);
        ok(IsHungAppWindow(Window), "Child window is not hung\n");
        printf("Sending to the hung child window\n");
        fflush(stdout);
        SetLastError(0xdeadbeef);
        Result = 0x55555555;
        Start = GetTickCount();
        Ret = SendMessageTimeoutW(Window, WM_USER, 0, 0, SMTO_ABORTIFHUNG, 3000, &Result);
        Error = GetLastError();
        Elapsed = GetTickCount() - Start;
        ok_long((LONG)Ret, 0);
        ok_long(Error, ERROR_TIMEOUT);
        ok(Elapsed >= 2500, "Returned after %lu ms\n", Elapsed);
        SetLastError(0xdeadbeef);
        Start = GetTickCount();
        Ret = SendMessageTimeoutW(Window, WM_USER, 0, 0, SMTO_NORMAL, 300, &Result);
        Error = GetLastError();
        Elapsed = GetTickCount() - Start;
        ok_long((LONG)Ret, 0);
        ok_long(Error, ERROR_TIMEOUT);
        ok(Elapsed >= 250, "Returned after %lu ms\n", Elapsed);
    }
    TerminateProcess(Process.hProcess, 0);
    WaitForSingleObject(Process.hProcess, 5000);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
}

static
VOID
RunHungChild(VOID)
{
    WNDCLASSW Class = { 0 };

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"LiberNTHungChild";
    RegisterClassW(&Class);
    CreateWindowExW(0, Class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, NULL, NULL, Class.hInstance, NULL);
    Sleep(20000);
}

START_TEST(SendMessageTimeoutHung)
{
    DWORD_PTR Result;
    LRESULT Ret;
    HANDLE Thread;
    DWORD Start, Elapsed, Error;

    if (GetEnvironmentVariableW(L"LIBERNT_HUNG_CHILD", NULL, 0))
    {
        RunHungChild();
        ExitProcess(0);
    }

    Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, HungThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed\n");
    if (!Thread)
        return;
    WaitForSingleObject(Ready, INFINITE);
    ok(HungWindow != NULL, "CreateWindowExW failed\n");

    SetLastError(0xdeadbeef);
    Result = 0x55555555;
    Start = GetTickCount();
    Ret = SendMessageTimeoutW(HungWindow, WM_USER, 0, 0, SMTO_NORMAL, 200, &Result);
    Error = GetLastError();
    Elapsed = GetTickCount() - Start;
    ok_long((LONG)Ret, 0);
    ok_long(Error, ERROR_TIMEOUT);
    ok(Result == 0, "Result 0x%Ix\n", Result);
    ok(Elapsed >= 150, "Returned after %lu ms\n", Elapsed);

    printf("Waiting for the window to be reported hung\n");
    fflush(stdout);
    Sleep(5500);
    ok(IsHungAppWindow(HungWindow), "Window is not hung\n");
    printf("Sending to the hung window\n");
    fflush(stdout);

    SetLastError(0xdeadbeef);
    Result = 0x55555555;
    Start = GetTickCount();
    Ret = SendMessageTimeoutW(HungWindow, WM_USER, 0, 0, SMTO_ABORTIFHUNG, 3000, &Result);
    Error = GetLastError();
    Elapsed = GetTickCount() - Start;
    ok_long((LONG)Ret, 0);
    ok_long(Error, ERROR_TIMEOUT);
    ok(Elapsed >= 2500, "Returned after %lu ms\n", Elapsed);

    printf("Testing a window of another process\n");
    fflush(stdout);
    SetEvent(Release);
    WaitForSingleObject(Thread, 10000);
    CloseHandle(Thread);
    CloseHandle(Ready);
    CloseHandle(Release);

    TestOtherProcess();
}
