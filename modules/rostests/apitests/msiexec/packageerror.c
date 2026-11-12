/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     msiexec and MsiInstallProduct with a package that cannot be opened
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <strsafe.h>

typedef struct
{
    DWORD dwProcessId;
    HWND hwnd;
} FIND_DIALOG;

static BOOL CALLBACK FindDialogProc(HWND hwnd, LPARAM lParam)
{
    FIND_DIALOG *pFind = (FIND_DIALOG *)lParam;
    WCHAR szClass[16];
    DWORD dwProcessId = 0;

    GetWindowThreadProcessId(hwnd, &dwProcessId);
    if (dwProcessId != pFind->dwProcessId || !IsWindowVisible(hwnd))
        return TRUE;
    if (!GetClassNameW(hwnd, szClass, ARRAYSIZE(szClass)) || wcscmp(szClass, L"#32770"))
        return TRUE;
    pFind->hwnd = hwnd;
    return FALSE;
}

static DWORD RunMsiexec(PCWSTR pszArgs, BOOL fExpectDialog)
{
    WCHAR szCmd[MAX_PATH * 2], szSystem[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    FIND_DIALOG find = { 0 };
    DWORD dwExit = 0xdeadbeef;
    int i;

    GetSystemDirectoryW(szSystem, ARRAYSIZE(szSystem));
    StringCbPrintfW(szCmd, sizeof(szCmd), L"\"%s\\msiexec.exe\" %s", szSystem, pszArgs);
    if (!CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcess failed with %lu\n", GetLastError());
        return dwExit;
    }
    find.dwProcessId = pi.dwProcessId;
    for (i = 0; i < 100 && WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT; i++)
    {
        EnumWindows(FindDialogProc, (LPARAM)&find);
        if (find.hwnd)
            break;
    }
    ok(!!find.hwnd == !!fExpectDialog, "%ls: dialog %p\n", pszArgs, find.hwnd);
    for (i = 0; find.hwnd && i < 50 && WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT; i++)
        PostMessageW(find.hwnd, WM_COMMAND, IDOK, 0);
    if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0)
    {
        ok(0, "%ls: msiexec did not exit\n", pszArgs);
        TerminateProcess(pi.hProcess, 1);
    }
    GetExitCodeProcess(pi.hProcess, &dwExit);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return dwExit;
}

START_TEST(packageerror)
{
    WCHAR szMissing[MAX_PATH], szNotMsi[MAX_PATH], szArgs[MAX_PATH * 2];
    UINT r;

    GetTempPathW(ARRAYSIZE(szMissing), szMissing);
    StringCbCatW(szMissing, sizeof(szMissing), L"msiexec_apitest_missing.msi");
    DeleteFileW(szMissing);
    GetSystemDirectoryW(szNotMsi, ARRAYSIZE(szNotMsi));
    StringCbCatW(szNotMsi, sizeof(szNotMsi), L"\\kernel32.dll");

    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    r = MsiInstallProductW(szMissing, NULL);
    ok_int(r, ERROR_FILE_NOT_FOUND);
    r = MsiInstallProductW(szNotMsi, NULL);
    ok_int(r, ERROR_INSTALL_PACKAGE_INVALID);

    StringCbPrintfW(szArgs, sizeof(szArgs), L"/i \"%s\" /qn", szMissing);
    ok_int(RunMsiexec(szArgs, FALSE), ERROR_INSTALL_PACKAGE_OPEN_FAILED);
    StringCbPrintfW(szArgs, sizeof(szArgs), L"/i \"%s\" /qn", szNotMsi);
    ok_int(RunMsiexec(szArgs, FALSE), ERROR_INSTALL_PACKAGE_INVALID);
    StringCbPrintfW(szArgs, sizeof(szArgs), L"/i \"%s\"", szMissing);
    ok_int(RunMsiexec(szArgs, TRUE), ERROR_INSTALL_PACKAGE_OPEN_FAILED);
    StringCbPrintfW(szArgs, sizeof(szArgs), L"/i \"%s\"", szNotMsi);
    ok_int(RunMsiexec(szArgs, TRUE), ERROR_INSTALL_PACKAGE_INVALID);
}
