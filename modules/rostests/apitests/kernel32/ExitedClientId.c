/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a thread or process ID keeps naming its object after exit, until the object goes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/psfuncs.h>

#define THREAD_EXIT_CODE 0x2a
#define PROCESS_EXIT_CODE 7
#define OBJECT_SETTLE_MS 500

static DWORD WINAPI ReturnExitCode(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return THREAD_EXIT_CODE;
}

static void Test_ExitedThread(void)
{
    HANDLE Thread, Opened;
    DWORD ThreadId = 0, Code = 0;

    Thread = CreateThread(NULL, 0, ReturnExitCode, NULL, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return;

    ok_long(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);

    Opened = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, ThreadId);
    ok(Opened != NULL, "OpenThread of an exited thread failed with %lu\n", GetLastError());
    if (Opened != NULL)
    {
        ok(GetExitCodeThread(Opened, &Code), "GetExitCodeThread failed with %lu\n", GetLastError());
        ok_hex(Code, THREAD_EXIT_CODE);
        ok_long(GetThreadId(Opened), ThreadId);
        CloseHandle(Opened);
    }

    CloseHandle(Thread);
    Sleep(OBJECT_SETTLE_MS);

    SetLastError(0xdeadbeef);
    Opened = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, ThreadId);
    if (Opened == NULL)
    {
        /* Access is denied when the ID already went to an object of someone else's */
        ok(GetLastError() == ERROR_INVALID_PARAMETER || GetLastError() == ERROR_ACCESS_DENIED,
           "Unexpected error %lu\n", GetLastError());
    }
    else
    {
        /* The ID went to a new thread */
        Code = 0;
        ok(GetExitCodeThread(Opened, &Code), "GetExitCodeThread failed with %lu\n", GetLastError());
        ok(Code != THREAD_EXIT_CODE, "OpenThread handed out the deleted thread\n");
        CloseHandle(Opened);
    }
}

static void Test_ExitedProcess(void)
{
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    WCHAR Application[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    KAFFINITY Affinity;
    HANDLE Opened;
    DWORD Code = 0;

    RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
    RtlZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    GetModuleFileNameW(NULL, Application, _countof(Application));
    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" ExitedClientId child", Application);
    if (!CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        return;
    }

    ok_long(WaitForSingleObject(ProcessInfo.hProcess, 10000), WAIT_OBJECT_0);

    Opened = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, ProcessInfo.dwProcessId);
    ok(Opened != NULL, "OpenProcess of an exited process failed with %lu\n", GetLastError());
    if (Opened != NULL)
    {
        ok(GetExitCodeProcess(Opened, &Code), "GetExitCodeProcess failed with %lu\n", GetLastError());
        ok_hex(Code, PROCESS_EXIT_CODE);
        ok_long(GetProcessId(Opened), ProcessInfo.dwProcessId);
        Affinity = 1;
        ok_hex(NtSetInformationProcess(Opened, ProcessAffinityMask, &Affinity, sizeof(Affinity)), STATUS_PROCESS_IS_TERMINATING);
        CloseHandle(Opened);
    }

    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    Sleep(OBJECT_SETTLE_MS);

    SetLastError(0xdeadbeef);
    Opened = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessInfo.dwProcessId);
    if (Opened == NULL)
    {
        /* Access is denied when the ID already went to an object of someone else's */
        ok(GetLastError() == ERROR_INVALID_PARAMETER || GetLastError() == ERROR_ACCESS_DENIED,
           "Unexpected error %lu\n", GetLastError());
    }
    else
    {
        /* The ID went to a new process */
        Code = 0;
        ok(GetExitCodeProcess(Opened, &Code), "GetExitCodeProcess failed with %lu\n", GetLastError());
        ok(Code != PROCESS_EXIT_CODE, "OpenProcess handed out the deleted process\n");
        CloseHandle(Opened);
    }
}

START_TEST(ExitedClientId)
{
    char **Arguments;
    int ArgumentCount;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "child"))
        ExitProcess(PROCESS_EXIT_CODE);

    Test_ExitedThread();
    Test_ExitedProcess();
}
