/*
 * PROJECT:         LiberNT API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Exercise console process creation against control-event delivery
 */

#include "precomp.h"

#define LEAF_COUNT 4
#define WAIT_MS 5000

enum
{
    WORKER_SUCCESS = 0,
    WORKER_NO_CONSOLE,
    WORKER_NO_HANDLER,
    WORKER_NO_THREAD,
    WORKER_LEAF_FAILED,
    WORKER_CTRL_LOST,
    WORKER_THREAD_STUCK,
};

typedef struct _CTRL_EVENT_CONTEXT
{
    HANDLE StopEvent;
    LONG Delivered;
    BOOL Lost;
} CTRL_EVENT_CONTEXT, *PCTRL_EVENT_CONTEXT;

static HANDLE CtrlHandledEvent;

static
BOOL
WINAPI
CtrlHandler(DWORD CtrlType)
{
    if (CtrlType != CTRL_BREAK_EVENT)
        return FALSE;
    SetEvent(CtrlHandledEvent);
    return TRUE;
}

static
DWORD
WINAPI
CtrlEventThread(PVOID Parameter)
{
    PCTRL_EVENT_CONTEXT Context = Parameter;

    do
    {
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetCurrentProcessId()) ||
            WaitForSingleObject(CtrlHandledEvent, WAIT_MS) != WAIT_OBJECT_0)
        {
            Context->Lost = TRUE;
            return 0;
        }
        Context->Delivered++;
    } while (WaitForSingleObject(Context->StopEvent, 0) == WAIT_TIMEOUT);

    return 0;
}

static
BOOL
PrepareConsoleHandles(VOID)
{
    SECURITY_ATTRIBUTES SecurityAttributes = { sizeof(SecurityAttributes), NULL, TRUE };
    HANDLE Input;
    HANDLE Output;

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &SecurityAttributes, OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE) return FALSE;

    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &SecurityAttributes, OPEN_EXISTING, 0, NULL);
    if (Output == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Input);
        return FALSE;
    }

    if (!SetStdHandle(STD_INPUT_HANDLE, Input) || !SetStdHandle(STD_OUTPUT_HANDLE, Output) || !SetStdHandle(STD_ERROR_HANDLE, Output))
    {
        CloseHandle(Output);
        CloseHandle(Input);
        return FALSE;
    }

    return TRUE;
}

static
BOOL
RunLeaf(PCWSTR Application)
{
    PROCESS_INFORMATION ProcessInfo = {0};
    STARTUPINFOW StartupInfo = {0};
    WCHAR CommandLine[MAX_PATH * 2];
    DWORD ExitCode = STILL_ACTIVE;
    DWORD WaitResult;

    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (FAILED(StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" ConsoleProcessInheritance leaf", Application)))
        return FALSE;
    if (!CreateProcessW(Application, CommandLine, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &StartupInfo, &ProcessInfo))
        return FALSE;

    WaitResult = WaitForSingleObject(ProcessInfo.hProcess, WAIT_MS);
    if (WaitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInfo.hProcess, WAIT_MS);
    }
    else if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
    {
        ExitCode = STILL_ACTIVE;
    }

    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return WaitResult == WAIT_OBJECT_0 && ExitCode == 0;
}

static
DWORD
RunWorker(VOID)
{
    CTRL_EVENT_CONTEXT Context = {0};
    WCHAR Application[MAX_PATH];
    BOOL LeavesRan = TRUE;
    HANDLE Thread;
    DWORD Index;

    if (!PrepareConsoleHandles() && (!AllocConsole() || !PrepareConsoleHandles()))
        return WORKER_NO_CONSOLE;

    CtrlHandledEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!CtrlHandledEvent || !Context.StopEvent || !SetConsoleCtrlHandler(CtrlHandler, TRUE))
        return WORKER_NO_HANDLER;

    SetLastError(ERROR_SUCCESS);
    if (!GetModuleFileNameW(NULL, Application, _countof(Application)) || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return WORKER_LEAF_FAILED;

    Thread = CreateThread(NULL, 0, CtrlEventThread, &Context, 0, NULL);
    if (!Thread)
        return WORKER_NO_THREAD;

    for (Index = 0; Index < LEAF_COUNT && LeavesRan; Index++)
        LeavesRan = RunLeaf(Application);

    SetEvent(Context.StopEvent);
    if (WaitForSingleObject(Thread, 2 * WAIT_MS) != WAIT_OBJECT_0)
        return WORKER_THREAD_STUCK;
    CloseHandle(Thread);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);

    if (!LeavesRan)
        return WORKER_LEAF_FAILED;
    if (Context.Lost || Context.Delivered == 0)
        return WORKER_CTRL_LOST;
    return WORKER_SUCCESS;
}

static
BOOL
ValidateInheritedConsoleHandles(VOID)
{
    DWORD Mode;

    if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &Mode)) return FALSE;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &Mode)) return FALSE;
    return TRUE;
}

START_TEST(ConsoleProcessInheritance)
{
    PROCESS_INFORMATION ProcessInfo = {0};
    STARTUPINFOW StartupInfo = {0};
    WCHAR Application[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    DWORD ExitCode = STILL_ACTIVE;
    DWORD WaitResult;
    int ArgumentCount;
    char **Arguments;
    BOOL Result;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "leaf")) ExitProcess(ValidateInheritedConsoleHandles() ? 0 : 2);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "worker")) ExitProcess(RunWorker());

    StartupInfo.cb = sizeof(StartupInfo);
    SetLastError(ERROR_SUCCESS);
    Result = GetModuleFileNameW(NULL, Application, _countof(Application)) != 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER;
    ok(Result, "GetModuleFileNameW failed with %lu\n", GetLastError());
    if (!Result) return;

    Result = SUCCEEDED(StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" ConsoleProcessInheritance worker", Application));
    ok(Result, "Failed to format the worker command line\n");
    if (!Result) return;

    Result = CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW failed with %lu\n", GetLastError());
    if (!Result) return;

    WaitResult = WaitForSingleObject(ProcessInfo.hProcess, (LEAF_COUNT + 3) * WAIT_MS);
    ok(WaitResult == WAIT_OBJECT_0, "Worker wait returned %#lx\n", WaitResult);
    if (WaitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInfo.hProcess, WAIT_MS);
    }

    Result = GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(Result, "GetExitCodeProcess failed with %lu\n", GetLastError());
    if (Result && WaitResult == WAIT_OBJECT_0) ok(ExitCode == WORKER_SUCCESS, "Worker exited with %lu\n", ExitCode);

    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
