/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the WaitForInputIdle timeout for a process that has not started
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(WaitForInputIdle)
{
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    WCHAR Path[MAX_PATH];
    DWORD Start, Elapsed, Result;
    UINT Length;

    Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    ok(Length != 0 && Length < ARRAYSIZE(Path) - 16, "GetSystemDirectoryW returned %u\n", Length);
    if (Length == 0 || Length >= ARRAYSIZE(Path) - 16)
        return;
    wcscat(Path, L"\\notepad.exe");

    if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW(%ls) failed with %lu\n", Path, GetLastError());
        return;
    }

    Start = GetTickCount();
    Result = WaitForInputIdle(ProcessInfo.hProcess, 500);
    Elapsed = GetTickCount() - Start;
    ok(Result == WAIT_TIMEOUT, "WaitForInputIdle returned 0x%lx, error %lu\n", Result, GetLastError());
    ok(Elapsed >= 480 && Elapsed < 650, "WaitForInputIdle waited %lu ms for a 500 ms timeout\n", Elapsed);

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
