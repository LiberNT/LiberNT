/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests ShellExecuteExW on a URL ending in .exe
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include "shelltest.h"
#include <strsafe.h>
#include <string.h>

#define TEST_PROTO_KEY L"Software\\Classes\\lnturlexetest"
#define TEST_CMD_KEY   TEST_PROTO_KEY L"\\shell\\open\\command"

static void TestUrlExe(LPCWSTR Verb)
{
    WCHAR szTempDir[MAX_PATH];
    WCHAR szTempFile[MAX_PATH];
    WCHAR szCmdPath[MAX_PATH];
    WCHAR szCommand[MAX_PATH * 2 + 32];
    HKEY hProtoKey = NULL;
    HKEY hCmdKey = NULL;
    LSTATUS Status;
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    BOOL bRet;
    HANDLE hFile;
    CHAR szOutput[512];
    DWORD cbRead = 0;
    DWORD cchTemp;
    UINT cchSys;

    cchTemp = GetTempPathW(ARRAYSIZE(szTempDir), szTempDir);
    if (cchTemp == 0 || cchTemp >= ARRAYSIZE(szTempDir))
    {
        skip("GetTempPathW failed (%lu)\n", GetLastError());
        return;
    }

    if (!GetTempFileNameW(szTempDir, L"lue", 0, szTempFile))
    {
        skip("GetTempFileNameW failed (%lu)\n", GetLastError());
        return;
    }

    cchSys = GetSystemDirectoryW(szCmdPath, ARRAYSIZE(szCmdPath));
    if (cchSys == 0 || cchSys >= ARRAYSIZE(szCmdPath) || !PathAppendW(szCmdPath, L"cmd.exe"))
    {
        DeleteFileW(szTempFile);
        skip("Failed to locate cmd.exe (%lu)\n", GetLastError());
        return;
    }

    if (FAILED(StringCchPrintfW(szCommand, ARRAYSIZE(szCommand),
                                L"\"%s\" /c echo %%1 > \"%s\"", szCmdPath, szTempFile)))
    {
        DeleteFileW(szTempFile);
        skip("Failed to format command\n");
        return;
    }

    Status = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_PROTO_KEY, 0, NULL, 0,
                             KEY_WRITE, NULL, &hProtoKey, NULL);
    ok_long(Status, ERROR_SUCCESS);
    if (Status != ERROR_SUCCESS)
        goto cleanup;

    Status = RegSetValueExW(hProtoKey, L"URL Protocol", 0, REG_SZ,
                            (const BYTE *)L"", sizeof(WCHAR));
    RegCloseKey(hProtoKey);
    ok_long(Status, ERROR_SUCCESS);
    if (Status != ERROR_SUCCESS)
        goto cleanup;

    Status = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_CMD_KEY, 0, NULL, 0,
                             KEY_WRITE, NULL, &hCmdKey, NULL);
    ok_long(Status, ERROR_SUCCESS);
    if (Status != ERROR_SUCCESS)
        goto cleanup;

    Status = RegSetValueExW(hCmdKey, NULL, 0, REG_SZ, (const BYTE *)szCommand,
                            (DWORD)((lstrlenW(szCommand) + 1) * sizeof(WCHAR)));
    RegCloseKey(hCmdKey);
    ok_long(Status, ERROR_SUCCESS);
    if (Status != ERROR_SUCCESS)
        goto cleanup;

    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = Verb;
    sei.lpFile = L"lnturlexetest://foo/bar.exe";
    sei.nShow = SW_HIDE;

    bRet = ShellExecuteExW(&sei);
    ok(bRet, "Verb %ls: ShellExecuteExW failed (%lu)\n", Verb ? Verb : L"(null)", GetLastError());
    ok(sei.hProcess != NULL, "Expected non-NULL sei.hProcess\n");
    if (bRet && sei.hProcess)
    {
        ok_long(WaitForSingleObject(sei.hProcess, 10000), WAIT_OBJECT_0);
        CloseHandle(sei.hProcess);
    }

    ZeroMemory(szOutput, sizeof(szOutput));
    hFile = CreateFileW(szTempFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "CreateFileW failed (%lu)\n", GetLastError());
    if (hFile != INVALID_HANDLE_VALUE)
    {
        bRet = ReadFile(hFile, szOutput, sizeof(szOutput) - 1, &cbRead, NULL);
        ok(bRet, "ReadFile failed (%lu)\n", GetLastError());
        CloseHandle(hFile);
        szOutput[cbRead] = '\0';
        ok(strstr(szOutput, "lnturlexetest://foo/bar.exe") != NULL,
           "Verb %ls: unexpected captured output: '%s'\n", Verb ? Verb : L"(null)", szOutput);
    }

cleanup:
    SHDeleteKeyW(HKEY_CURRENT_USER, TEST_PROTO_KEY);
    DeleteFileW(szTempFile);
}

START_TEST(ShellExecuteUrlExe)
{
    TestUrlExe(L"open");
    TestUrlExe(NULL);
}
