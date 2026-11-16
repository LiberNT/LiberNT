/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests ShellExecuteEx with a program path that uses forward slashes
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "shelltest.h"
#include <strsafe.h>

static void WriteCommandLine(LPCSTR pszFile)
{
    HANDLE hFile = CreateFileA(pszFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    LPCWSTR pszCmdLine = GetCommandLineW();
    DWORD cbWritten;

    if (hFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(hFile, pszCmdLine, lstrlenW(pszCmdLine) * sizeof(WCHAR), &cbWritten, NULL);
    CloseHandle(hFile);
}

static void ReadCommandLine(LPCWSTR pszFile, LPWSTR pszCmdLine, DWORD cch)
{
    HANDLE hFile = CreateFileW(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD cbRead = 0;

    pszCmdLine[0] = UNICODE_NULL;
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    ReadFile(hFile, pszCmdLine, (cch - 1) * sizeof(WCHAR), &cbRead, NULL);
    pszCmdLine[cbRead / sizeof(WCHAR)] = UNICODE_NULL;
    CloseHandle(hFile);
}

static void TestSlashPath(LPCWSTR pszSelf, LPCWSTR pszOutput)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    WCHAR szFile[MAX_PATH], szParams[MAX_PATH + 32], szCmdLine[3 * MAX_PATH], szExpected[MAX_PATH];
    BOOL ret;

    StringCchCopyW(szFile, _countof(szFile), pszSelf);
    for (WCHAR *p = szFile; *p; p++)
    {
        if (*p == L'\\')
            *p = L'/';
    }
    StringCchPrintfW(szParams, _countof(szParams), L"ShellExecuteSlash child \"%s\"", pszOutput);
    DeleteFileW(pszOutput);

    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = szFile;
    sei.lpParameters = szParams;
    sei.nShow = SW_HIDE;
    ret = ShellExecuteExW(&sei);
    ok(ret, "ShellExecuteExW(%ls) failed with %lu\n", szFile, GetLastError());
    if (!ret || !sei.hProcess)
        return;
    ok_long(WaitForSingleObject(sei.hProcess, 30000), WAIT_OBJECT_0);
    CloseHandle(sei.hProcess);

    ReadCommandLine(pszOutput, szCmdLine, _countof(szCmdLine));
    trace("command line %ls\n", szCmdLine);
    StringCchPrintfW(szExpected, _countof(szExpected), L"\"%s\" ", pszSelf);
    ok(!wcsncmp(szCmdLine, szExpected, wcslen(szExpected)), "command line %ls, expected it to start with %ls\n",
       szCmdLine, szExpected);
    DeleteFileW(pszOutput);
}

START_TEST(ShellExecuteSlash)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    WCHAR szSelf[MAX_PATH], szOutput[MAX_PATH];

    if (argc >= 4 && !strcmp(argv[2], "child"))
    {
        WriteCommandLine(argv[3]);
        return;
    }

    GetModuleFileNameW(NULL, szSelf, _countof(szSelf));
    GetTempPathW(_countof(szOutput), szOutput);
    StringCchCatW(szOutput, _countof(szOutput), L"ShellExecuteSlash.txt");
    TestSlashPath(szSelf, szOutput);
}
