/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a fast fail ends the process without reaching any exception handler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define FAST_FAIL_TEST_CODE 7
#define HANDLER_REACHED 0x0BADF00D

static
LONG
CALLBACK
ChildVectoredHandler(
    _In_ PEXCEPTION_POINTERS Info)
{
    UNREFERENCED_PARAMETER(Info);
    TerminateProcess(GetCurrentProcess(), HANDLER_REACHED);
    return EXCEPTION_CONTINUE_SEARCH;
}

static
VOID
RunChild(VOID)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1, ChildVectoredHandler);
    __fastfail(FAST_FAIL_TEST_CODE);
}

START_TEST(FastFail)
{
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    CHAR CommandLine[MAX_PATH * 2];
    DWORD Wait, ExitCode = 0;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if ((argc >= 3) && (strcmp(argv[2], "child") == 0))
    {
        RunChild();
        return;
    }

    sprintf(CommandLine, "\"%s\" FastFail child", argv[0]);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcessA failed with %lu\n", GetLastError());
        return;
    }

    Wait = WaitForSingleObject(Process.hProcess, 30000);
    ok(Wait == WAIT_OBJECT_0, "Child still running after 30 s (wait 0x%lx)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 1);

    ok(GetExitCodeProcess(Process.hProcess, &ExitCode), "GetExitCodeProcess failed with %lu\n", GetLastError());
    ok(ExitCode != HANDLER_REACHED, "A vectored exception handler saw the fast fail\n");
    ok(ExitCode == (DWORD)STATUS_STACK_BUFFER_OVERRUN, "Child exit code 0x%lx\n", ExitCode);

    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}
