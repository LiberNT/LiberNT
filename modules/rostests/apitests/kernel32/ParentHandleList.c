/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test a handle list attribute together with a parent process attribute
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

static WCHAR s_szExe[MAX_PATH];

static HANDLE DuplicateIntoParent(HANDLE hParent, HANDLE hEvent, BOOL fInherit)
{
    HANDLE hValue;
    DWORD dwFlags;
    UINT i;

    for (i = 0; i < 4096; i++)
    {
        hValue = NULL;
        if (!DuplicateHandle(GetCurrentProcess(), hEvent, hParent, &hValue, 0, fInherit, DUPLICATE_SAME_ACCESS))
            return NULL;
        if (!GetHandleInformation(hValue, &dwFlags))
            return hValue;
    }
    return NULL;
}

static BOOL CreateWithParent(HANDLE hParent, HANDLE hValue, DWORD *pdwError, DWORD *pdwExit)
{
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    WCHAR szCmd[MAX_PATH * 2];
    SIZE_T cb = 0;
    BOOL fOk;

    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    InitializeProcThreadAttributeList(NULL, 2, 0, &cb);
    si.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList(si.lpAttributeList, 2, 0, &cb) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &hParent,
                                   sizeof(hParent), NULL, NULL) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &hValue,
                                   sizeof(hValue), NULL, NULL))
    {
        *pdwError = GetLastError();
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        return FALSE;
    }

    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ParentHandleList check %Ix", s_szExe, (ULONG_PTR)hValue);
    fOk = CreateProcessW(NULL, szCmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, NULL, NULL,
                         &si.StartupInfo, &pi);
    *pdwError = fOk ? ERROR_SUCCESS : GetLastError();
    if (fOk)
    {
        if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0)
            TerminateProcess(pi.hProcess, 0xdead);
        GetExitCodeProcess(pi.hProcess, pdwExit);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    return fOk;
}

START_TEST(ParentHandleList)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR szCmd[MAX_PATH * 2];
    HANDLE hEvent, hValue;
    DWORD dwError, dwExit = 0xdead;
    char **argv;

    if (winetest_get_mainargs(&argv) >= 4)
    {
        if (!strcmp(argv[2], "check"))
            ExitProcess(WaitForSingleObject((HANDLE)(ULONG_PTR)_strtoui64(argv[3], NULL, 16), 0) == WAIT_TIMEOUT);
        Sleep(30000);
        return;
    }

    GetModuleFileNameW(NULL, s_szExe, _countof(s_szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ParentHandleList wait parent", s_szExe);
    if (!CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        skip("CreateProcessW failed %lu\n", GetLastError());
        return;
    }
    hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    hValue = DuplicateIntoParent(pi.hProcess, hEvent, TRUE);
    ok(hValue != NULL, "no handle value that is free in the caller\n");
    if (hValue)
    {
        ok(CreateWithParent(pi.hProcess, hValue, &dwError, &dwExit), "CreateProcessW failed %lu\n", dwError);
        ok_long(dwExit, 1);
    }

    hValue = DuplicateIntoParent(pi.hProcess, hEvent, FALSE);
    ok(hValue != NULL, "no handle value that is free in the caller\n");
    if (hValue)
    {
        ok(!CreateWithParent(pi.hProcess, hValue, &dwError, &dwExit), "a handle that is not inheritable was accepted\n");
        ok_long(dwError, ERROR_INVALID_PARAMETER);
    }

    CloseHandle(hEvent);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
