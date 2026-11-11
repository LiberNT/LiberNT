/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that working set limits set through a handle apply to that process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <psapi.h>

START_TEST(ProcessWorkingSetSize)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 40];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Child;
    SIZE_T OwnMin, OwnMax, Min, Max, NewMin, NewMax;
    BOOL Success;

    ok(GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)) != 0, "GetModuleFileNameW failed, error %lu\n", GetLastError());
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" ProcessWorkingSetSize child", Path);
    Success = CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Child);
    ok(Success, "CreateProcessW failed, error %lu\n", GetLastError());
    if (!Success) return;

    Success = GetProcessWorkingSetSize(GetCurrentProcess(), &OwnMin, &OwnMax);
    ok(Success, "GetProcessWorkingSetSize(self) failed, error %lu\n", GetLastError());
    Success = GetProcessWorkingSetSize(Child.hProcess, &Min, &Max);
    ok(Success, "GetProcessWorkingSetSize(child) failed, error %lu\n", GetLastError());

    NewMin = OwnMin + 0x100000;
    NewMax = OwnMax + 0x400000;
    Success = SetProcessWorkingSetSize(Child.hProcess, NewMin, NewMax);
    ok(Success, "SetProcessWorkingSetSize(child) failed, error %lu\n", GetLastError());

    Min = Max = 0;
    Success = GetProcessWorkingSetSize(Child.hProcess, &Min, &Max);
    ok(Success, "GetProcessWorkingSetSize(child) failed, error %lu\n", GetLastError());
    ok(Min == NewMin, "Child minimum %Iu, expected %Iu\n", Min, NewMin);
    ok(Max == NewMax, "Child maximum %Iu, expected %Iu\n", Max, NewMax);

    Min = Max = 0;
    Success = GetProcessWorkingSetSize(GetCurrentProcess(), &Min, &Max);
    ok(Success, "GetProcessWorkingSetSize(self) failed, error %lu\n", GetLastError());
    ok(Min == OwnMin, "Own minimum changed to %Iu from %Iu\n", Min, OwnMin);
    ok(Max == OwnMax, "Own maximum changed to %Iu from %Iu\n", Max, OwnMax);

    Success = EmptyWorkingSet(Child.hProcess);
    ok(Success, "EmptyWorkingSet(child) failed, error %lu\n", GetLastError());

    TerminateProcess(Child.hProcess, 0);
    CloseHandle(Child.hThread);
    CloseHandle(Child.hProcess);
}
