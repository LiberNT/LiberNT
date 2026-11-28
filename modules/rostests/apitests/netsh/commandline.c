/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     netsh reports a command it does not know when run from the command line
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <stdio.h>
#include <string.h>

static DWORD RunNetsh(PCWSTR Arguments, PSTR Output, DWORD OutputSize)
{
    SECURITY_ATTRIBUTES Security = { sizeof(Security), NULL, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    WCHAR CommandLine[MAX_PATH + 64], System[MAX_PATH];
    HANDLE Read, Write;
    DWORD Total = 0, Got, ExitCode = MAXDWORD;

    Output[0] = 0;
    if (!CreatePipe(&Read, &Write, &Security, 0))
        return MAXDWORD;
    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);
    GetSystemDirectoryW(System, ARRAYSIZE(System));
    _snwprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\\netsh.exe\" %ls", System, Arguments);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = 0;
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    Startup.hStdOutput = Write;
    Startup.hStdError = Write;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
    {
        CloseHandle(Read);
        CloseHandle(Write);
        return MAXDWORD;
    }
    CloseHandle(Write);
    while (Total + 1 < OutputSize && ReadFile(Read, Output + Total, OutputSize - Total - 1, &Got, NULL) && Got)
        Total += Got;
    Output[Total] = 0;
    CloseHandle(Read);
    if (WaitForSingleObject(Process.hProcess, 5000) == WAIT_OBJECT_0)
        GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return ExitCode;
}

START_TEST(commandline)
{
    CHAR Output[1024];
    DWORD ExitCode;

    if (PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_ENGLISH)
    {
        skip("The message check needs an English user interface\n");
        return;
    }

    ExitCode = RunNetsh(L"lntnosuchcommand", Output, sizeof(Output));
    ok(ExitCode == 1, "exit code %lu\n", ExitCode);
    ok(strstr(Output, "The following command was not found: lntnosuchcommand.") != NULL, "output \"%s\"\n", Output);

    ExitCode = RunNetsh(L"interface lntnosuchcommand", Output, sizeof(Output));
    ok(ExitCode == 1, "exit code %lu\n", ExitCode);
    ok(strstr(Output, "The following command was not found: interface lntnosuchcommand.") != NULL, "output \"%s\"\n",
       Output);
}
