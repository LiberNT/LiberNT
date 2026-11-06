/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test a suspended process initialized by a thread other than its initial thread
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define CHILD_EXIT_CODE 42

static
BOOL
StartSuspendedChild(
    _In_ PCSTR Program,
    _Out_ PPROCESS_INFORMATION ProcessInfo)
{
    CHAR CommandLine[MAX_PATH + 32];
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    BOOL Success;

    StringCbPrintfA(CommandLine, sizeof(CommandLine), "\"%s\" RemoteThreadInit child", Program);
    Success = CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
                             &StartupInfo, ProcessInfo);
    ok(Success, "CreateProcessA failed, error %lu\n", GetLastError());
    return Success;
}

static
VOID
FinishChild(
    _In_ PPROCESS_INFORMATION ProcessInfo,
    _In_ DWORD ExpectedSuspendCount)
{
    DWORD ExitCode = 0xdeadbeef;
    DWORD Result;

    ok(GetExitCodeProcess(ProcessInfo->hProcess, &ExitCode), "GetExitCodeProcess failed, error %lu\n",
       GetLastError());
    ok(ExitCode == STILL_ACTIVE, "Child exit code %#lx before its initial thread ran\n", ExitCode);

    Result = ResumeThread(ProcessInfo->hThread);
    ok(Result == ExpectedSuspendCount, "ResumeThread returned %lu, expected %lu\n", Result,
       ExpectedSuspendCount);
    while (Result != (DWORD)-1 && Result > 1)
        Result = ResumeThread(ProcessInfo->hThread);

    Result = WaitForSingleObject(ProcessInfo->hProcess, 10000);
    ok(Result == WAIT_OBJECT_0, "Child wait returned %lu\n", Result);
    if (Result != WAIT_OBJECT_0)
    {
        ok(TerminateProcess(ProcessInfo->hProcess, 1), "TerminateProcess failed, error %lu\n", GetLastError());
        WaitForSingleObject(ProcessInfo->hProcess, 10000);
    }

    ExitCode = 0xdeadbeef;
    GetExitCodeProcess(ProcessInfo->hProcess, &ExitCode);
    ok(ExitCode == CHILD_EXIT_CODE, "Child exit code %#lx\n", ExitCode);

    CloseHandle(ProcessInfo->hThread);
    CloseHandle(ProcessInfo->hProcess);
}

static
VOID
WaitRemoteThread(
    _In_ HANDLE Thread)
{
    DWORD ExitCode = 0xdeadbeef;
    DWORD Result;

    Result = WaitForSingleObject(Thread, 10000);
    ok(Result == WAIT_OBJECT_0, "Remote thread wait returned %lu\n", Result);
    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed, error %lu\n", GetLastError());
    ok(ExitCode == 0, "Remote thread exit code %#lx\n", ExitCode);
    CloseHandle(Thread);
}

static
VOID
TestRemoteThread(
    _In_ PCSTR Program,
    _In_ BOOL Native)
{
    PROCESS_INFORMATION ProcessInfo;
    PVOID Start;
    HANDLE Thread = NULL;
    CLIENT_ID ClientId;
    NTSTATUS Status;

    if (!StartSuspendedChild(Program, &ProcessInfo))
        return;

    Start = (PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitThread");
    if (Native)
    {
        Status = RtlCreateUserThread(ProcessInfo.hProcess, NULL, FALSE, 0, 0, 0,
                                     (PTHREAD_START_ROUTINE)Start, NULL, &Thread, &ClientId);
        ok(Status == STATUS_SUCCESS, "RtlCreateUserThread returned %#lx\n", Status);
    }
    else
    {
        Thread = CreateRemoteThread(ProcessInfo.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)Start, NULL, 0, NULL);
        ok(Thread != NULL, "CreateRemoteThread failed, error %lu\n", GetLastError());
    }
    if (Thread)
        WaitRemoteThread(Thread);

    FinishChild(&ProcessInfo, 1);
}

static
VOID
TestDebuggerReattach(
    _In_ PCSTR Program)
{
    PROCESS_INFORMATION ProcessInfo;
    BOOL Success;
    ULONG Round;

    if (!StartSuspendedChild(Program, &ProcessInfo))
        return;

    for (Round = 0; Round < 2; Round++)
    {
        Success = DebugActiveProcess(ProcessInfo.dwProcessId);
        ok(Success, "Round %lu: DebugActiveProcess failed, error %lu\n", Round, GetLastError());
        if (!Success)
            break;

        Success = DebugActiveProcessStop(ProcessInfo.dwProcessId);
        ok(Success, "Round %lu: DebugActiveProcessStop failed, error %lu\n", Round, GetLastError());

        Sleep(1000);
    }

    FinishChild(&ProcessInfo, 1);
}

START_TEST(RemoteThreadInit)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
        ExitProcess(CHILD_EXIT_CODE);

    TestRemoteThread(argv[0], FALSE);
    TestRemoteThread(argv[0], TRUE);
    TestDebuggerReattach(argv[0]);
}
