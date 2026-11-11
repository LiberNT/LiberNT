/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that an exited process no longer holds its image file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(ExitedProcessImage)
{
    WCHAR Source[MAX_PATH], Directory[MAX_PATH], Image[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    HANDLE Held = NULL;
    char **Arguments;
    int Count;

    Count = winetest_get_mainargs(&Arguments);
    if (Count >= 3 && !strcmp(Arguments[2], "exit"))
        ExitProcess(0);

    if (!GetModuleFileNameW(NULL, Source, ARRAYSIZE(Source)) ||
        !GetTempPathW(ARRAYSIZE(Directory), Directory) ||
        FAILED(StringCchPrintfW(Image, ARRAYSIZE(Image), L"%sexited_image_%lu.exe",
                                Directory, GetCurrentProcessId())))
    {
        skip("no room for the image copy: %lu\n", GetLastError());
        return;
    }

    ok(CopyFileW(Source, Image, FALSE), "CopyFileW failed with %lu\n", GetLastError());
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" ExitedProcessImage exit", Image);
    if (!CreateProcessW(Image, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        DeleteFileW(Image);
        return;
    }

    /* Somebody else still holding the process, as the subsystem does, must not keep its image in use */
    ok(DuplicateHandle(GetCurrentProcess(), ProcessInfo.hProcess, GetCurrentProcess(), &Held,
                       PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0),
       "DuplicateHandle failed with %lu\n", GetLastError());
    ok(WaitForSingleObject(ProcessInfo.hProcess, 30000) == WAIT_OBJECT_0, "the child did not exit\n");
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    ok(CopyFileW(Source, Image, FALSE), "overwriting an exited process's image failed with %lu\n", GetLastError());
    ok(DeleteFileW(Image), "deleting an exited process's image failed with %lu\n", GetLastError());

    if (Held) CloseHandle(Held);
    DeleteFileW(Image);
}
