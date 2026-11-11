/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a 32-bit child of a 64-bit process starts under WoW64 and runs to its own exit code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/psfuncs.h>

START_TEST(Wow64ChildProcess)
{
#ifdef _WIN64
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    ULONG_PTR Peb32 = 0;
    DWORD ExitCode = 0, Wait;
    BOOL Wow64 = FALSE;
    NTSTATUS Status;
    UINT Length;

    Length = GetSystemWow64DirectoryW(Path, RTL_NUMBER_OF(Path));
    if (Length == 0 || Length >= RTL_NUMBER_OF(Path) - 16)
    {
        skip("No SysWOW64 directory (%lu)\n", GetLastError());
        return;
    }
    StringCchCatW(Path, RTL_NUMBER_OF(Path), L"\\cmd.exe");
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("No 32-bit cmd.exe\n");
        return;
    }
    StringCchPrintfW(CommandLine, RTL_NUMBER_OF(CommandLine), L"\"%s\" /c exit 7", Path);

    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(FALSE, "CreateProcessW failed with %lu\n", GetLastError());
        return;
    }

    ok(IsWow64Process(ProcessInfo.hProcess, &Wow64), "IsWow64Process failed with %lu\n", GetLastError());
    ok(Wow64 == TRUE, "Wow64 = %d\n", Wow64);

    Status = NtQueryInformationProcess(ProcessInfo.hProcess, ProcessWow64Information, &Peb32, sizeof(Peb32), NULL);
    ok(Status == STATUS_SUCCESS, "ProcessWow64Information returned 0x%lx\n", Status);
    ok(Peb32 != 0, "The 32-bit PEB is NULL\n");
    ok(Peb32 < 0x100000000ULL, "The 32-bit PEB 0x%Ix is above 4 GB\n", Peb32);

    ResumeThread(ProcessInfo.hThread);
    Wait = WaitForSingleObject(ProcessInfo.hProcess, 10000);
    ok(Wait == WAIT_OBJECT_0, "WaitForSingleObject returned %lu\n", Wait);
    ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed with %lu\n", GetLastError());
    ok(ExitCode == 7, "ExitCode = 0x%lx\n", ExitCode);

    if (Wait != WAIT_OBJECT_0) TerminateProcess(ProcessInfo.hProcess, 1);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
#else
    skip("Only a 64-bit process creates WoW64 children\n");
#endif
}
