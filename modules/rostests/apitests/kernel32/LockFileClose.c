/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that closing a file releases the locks the process holds while another process keeps it open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
DWORD
LockFirstBytes(
    _In_ HANDLE File)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    if (LockFileEx(File, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 16, 0, &Overlapped))
        return ERROR_SUCCESS;

    return GetLastError();
}

START_TEST(LockFileClose)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH], Image[MAX_PATH], CommandLine[MAX_PATH * 2];
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode = MAXDWORD;
    char **Arguments;
    HANDLE File;
    int Count;

    Count = winetest_get_mainargs(&Arguments);
    if (Count >= 4 && !strcmp(Arguments[2], "child"))
    {
        File = (HANDLE)(ULONG_PTR)strtoul(Arguments[3], NULL, 16);
        ExitProcess(LockFirstBytes(File));
    }

    if (!GetModuleFileNameW(NULL, Image, ARRAYSIZE(Image)) ||
        !GetTempPathW(ARRAYSIZE(Directory), Directory) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%slockclose_%lu.tmp", Directory, GetCurrentProcessId())))
    {
        skip("no room for the file name: %lu\n", GetLastError());
        return;
    }

    File = CreateFileW(Path,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       &Inherit,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;

    ok_int(LockFirstBytes(File), ERROR_SUCCESS);

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" LockFileClose child %lx", Image,
                     (ULONG)(ULONG_PTR)File);
    if (!CreateProcessW(Image, CommandLine, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        CloseHandle(File);
        DeleteFileW(Path);
        return;
    }

    CloseHandle(File);
    ResumeThread(ProcessInfo.hThread);

    ok(WaitForSingleObject(ProcessInfo.hProcess, 10000) == WAIT_OBJECT_0, "the child did not exit\n");
    ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed with %lu\n", GetLastError());
    ok(ExitCode == ERROR_SUCCESS, "the child could not lock what the closed handle had locked (%lu)\n", ExitCode);

    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    ok(DeleteFileW(Path), "DeleteFileW failed with %lu\n", GetLastError());
}
