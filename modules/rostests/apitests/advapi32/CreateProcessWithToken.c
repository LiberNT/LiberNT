/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for CreateProcessWithTokenW
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <sddl.h>
#include <lm.h>
#include <ndk/psfuncs.h>
#include <ndk/obfuncs.h>

#define TEST_USER L"LntCpwtTest"
#define TEST_PASSWORD L"Lnt!Cpwt#2026q"

#define TOKEN_CPWT_ACCESS (TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT | \
                           TOKEN_ADJUST_SESSIONID)

typedef NET_API_STATUS (WINAPI *FN_NetUserAdd)(LPCWSTR, DWORD, LPBYTE, LPDWORD);
typedef NET_API_STATUS (WINAPI *FN_NetUserDel)(LPCWSTR, LPCWSTR);
typedef BOOL (WINAPI *FN_DeleteProfileW)(LPCWSTR, LPCWSTR, LPCWSTR);

static WCHAR s_szExe[MAX_PATH];
static WCHAR s_szOut[MAX_PATH];

typedef struct _CHILD_INFO
{
    ULONG ParentPid;
    ULONG SessionId;
    WCHAR szCwd[MAX_PATH];
    WCHAR szUserName[128];
} CHILD_INFO;

static void Child(const char *pszOut)
{
    PROCESS_BASIC_INFORMATION pbi;
    CHILD_INFO info;
    HANDLE hFile;
    DWORD cb;

    ZeroMemory(&pbi, sizeof(pbi));
    ZeroMemory(&info, sizeof(info));
    NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    info.ParentPid = (ULONG)pbi.InheritedFromUniqueProcessId;
    ProcessIdToSessionId(GetCurrentProcessId(), &info.SessionId);
    GetCurrentDirectoryW(_countof(info.szCwd), info.szCwd);
    GetEnvironmentVariableW(L"USERNAME", info.szUserName, _countof(info.szUserName));
    hFile = CreateFileA(pszOut, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, &info, sizeof(info), &cb, NULL);
        CloseHandle(hFile);
    }
}

static ACCESS_MASK GrantedAccess(HANDLE h)
{
    OBJECT_BASIC_INFORMATION obi;

    ZeroMemory(&obi, sizeof(obi));
    NtQueryObject(h, ObjectBasicInformation, &obi, sizeof(obi), NULL);
    return obi.GrantedAccess;
}

static BOOL RunWithToken(HANDLE hToken, DWORD dwFlags, PROCESS_INFORMATION *ppi, DWORD *pdwError)
{
    WCHAR szCmd[MAX_PATH * 3];
    STARTUPINFOW si = { sizeof(si) };
    BOOL fOk;

    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" CreateProcessWithToken child \"%s\"", s_szExe, s_szOut);
    DeleteFileW(s_szOut);
    ZeroMemory(ppi, sizeof(*ppi));
    SetLastError(0xdeadbeef);
    fOk = CreateProcessWithTokenW(hToken, 0, NULL, szCmd, dwFlags, NULL, NULL, &si, ppi);
    *pdwError = fOk ? ERROR_SUCCESS : GetLastError();
    return fOk;
}

static void CloseChild(PROCESS_INFORMATION *ppi, DWORD dwWait)
{
    if (!ppi->hProcess)
        return;
    if (WaitForSingleObject(ppi->hProcess, dwWait) != WAIT_OBJECT_0)
        TerminateProcess(ppi->hProcess, 0xdead);
    WaitForSingleObject(ppi->hProcess, 5000);
    CloseHandle(ppi->hThread);
    CloseHandle(ppi->hProcess);
    ZeroMemory(ppi, sizeof(*ppi));
}

static DWORD ErrorWithToken(HANDLE hToken)
{
    PROCESS_INFORMATION pi;
    DWORD dwError;

    RunWithToken(hToken, 0, &pi, &dwError);
    CloseChild(&pi, 30000);
    return dwError;
}

static DWORD ErrorWithAccess(HANDLE hToken, ACCESS_MASK Access)
{
    HANDLE hLimited;
    DWORD dwError;

    if (!DuplicateHandle(GetCurrentProcess(), hToken, GetCurrentProcess(), &hLimited, Access, FALSE, 0))
        return 0xbad;
    dwError = ErrorWithToken(hLimited);
    CloseHandle(hLimited);
    return dwError;
}

static BOOL SetPrivilege(LPCWSTR pszName, DWORD dwAttributes)
{
    TOKEN_PRIVILEGES tp;
    HANDLE hToken;
    BOOL fOk;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = dwAttributes;
    fOk = LookupPrivilegeValueW(NULL, pszName, &tp.Privileges[0].Luid) &&
          AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);
    return fOk;
}

static void TestChildEnvironment(HANDLE hToken)
{
    PROCESS_INFORMATION pi;
    CHILD_INFO info;
    WCHAR szSystem[MAX_PATH], szUserName[128];
    DWORD dwError, dwExit = 0xdead, dwSession = 0, cb = 0;
    HANDLE hFile;

    ok(RunWithToken(hToken, 0, &pi, &dwError), "CreateProcessWithTokenW failed %lu\n", dwError);
    if (!pi.hProcess)
        return;
    ok_long(GrantedAccess(pi.hProcess), PROCESS_ALL_ACCESS);
    ok_long(GrantedAccess(pi.hThread), THREAD_ALL_ACCESS);
    ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "the child did not exit\n");
    GetExitCodeProcess(pi.hProcess, &dwExit);
    ok_long(dwExit, 0);
    CloseChild(&pi, 0);

    ZeroMemory(&info, sizeof(info));
    hFile = CreateFileW(s_szOut, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ReadFile(hFile, &info, sizeof(info), &cb, NULL);
        CloseHandle(hFile);
    }
    ok(cb == sizeof(info), "no result from the child\n");
    ProcessIdToSessionId(GetCurrentProcessId(), &dwSession);
    GetSystemDirectoryW(szSystem, _countof(szSystem));
    szUserName[0] = UNICODE_NULL;
    GetEnvironmentVariableW(L"USERNAME", szUserName, _countof(szUserName));
    ok(info.ParentPid == GetCurrentProcessId(), "parent %lu, caller %lu\n", info.ParentPid, GetCurrentProcessId());
    ok(info.SessionId == dwSession, "session %lu, caller %lu\n", info.SessionId, dwSession);
    ok(!lstrcmpiW(info.szCwd, szSystem), "current directory %ls\n", info.szCwd);
    ok(!lstrcmpiW(info.szUserName, szUserName), "user name %ls, caller %ls\n", info.szUserName, szUserName);
}

static void TestFlagsAndHandles(HANDLE hToken)
{
    static const DWORD s_adwBadFlags[] = { DEBUG_PROCESS, DEBUG_ONLY_THIS_PROCESS, DETACHED_PROCESS };
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR szCmd[MAX_PATH * 3];
    HANDLE hEvent, hRead, hWrite;
    char szBuf[16] = "";
    DWORD dwError, dwExit = 0xdead, cb = 0;
    UINT i;

    for (i = 0; i < _countof(s_adwBadFlags); i++)
    {
        ok(!RunWithToken(hToken, s_adwBadFlags[i], &pi, &dwError), "flags 0x%lx were accepted\n", s_adwBadFlags[i]);
        ok(dwError == ERROR_INVALID_PARAMETER, "flags 0x%lx: error %lu\n", s_adwBadFlags[i], dwError);
        CloseChild(&pi, 5000);
    }

    hEvent = CreateEventW(&sa, TRUE, FALSE, NULL);
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    {
        skip("CreatePipe failed %lu\n", GetLastError());
        CloseHandle(hEvent);
        return;
    }
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" CreateProcessWithToken inherit %Ix", s_szExe,
                     (ULONG_PTR)hEvent);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    ok(CreateProcessWithTokenW(hToken, 0, NULL, szCmd, 0, NULL, NULL, &si, &pi),
       "CreateProcessWithTokenW failed %lu\n", GetLastError());
    CloseHandle(hWrite);
    if (pi.hProcess)
    {
        ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "the child did not exit\n");
        GetExitCodeProcess(pi.hProcess, &dwExit);
        ok(dwExit == 0, "the child received an inheritable handle that is not a standard handle\n");
        CloseChild(&pi, 0);
        ReadFile(hRead, szBuf, sizeof(szBuf) - 1, &cb, NULL);
        szBuf[cb] = 0;
        ok(!strcmp(szBuf, "hello"), "the child wrote \"%s\" to its standard output\n", szBuf);
    }
    CloseHandle(hRead);
    CloseHandle(hEvent);
}

static void TestOwnToken(void)
{
    PROCESS_INFORMATION pi, pi2;
    HANDLE hProcessToken, hToken, hImpersonation, hChildToken;
    DWORD dwError;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hProcessToken))
    {
        skip("OpenProcessToken failed %lu\n", GetLastError());
        return;
    }
    if (!DuplicateTokenEx(hProcessToken, TOKEN_CPWT_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hToken))
    {
        skip("DuplicateTokenEx failed %lu\n", GetLastError());
        CloseHandle(hProcessToken);
        return;
    }

    TestChildEnvironment(hToken);
    TestFlagsAndHandles(hToken);

    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS & ~TOKEN_QUERY), ERROR_ACCESS_DENIED);
    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS & ~TOKEN_DUPLICATE), ERROR_ACCESS_DENIED);
    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS & ~TOKEN_ASSIGN_PRIMARY), ERROR_ACCESS_DENIED);
    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS & ~TOKEN_ADJUST_DEFAULT), ERROR_ACCESS_DENIED);
    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS & ~TOKEN_ADJUST_SESSIONID), ERROR_ACCESS_DENIED);
    ok_long(ErrorWithAccess(hToken, TOKEN_CPWT_ACCESS), ERROR_SUCCESS);

    ok(RunWithToken(hToken, CREATE_SUSPENDED, &pi, &dwError), "CreateProcessWithTokenW failed %lu\n", dwError);
    if (pi.hProcess)
    {
        ok(RunWithToken(hToken, 0, &pi2, &dwError), "the token of a suspended child cannot be reused: %lu\n",
           dwError);
        CloseChild(&pi2, 30000);
        if (OpenProcessToken(pi.hProcess, TOKEN_CPWT_ACCESS, &hChildToken))
        {
            ok_long(ErrorWithToken(hChildToken), ERROR_TOKEN_ALREADY_IN_USE);
            CloseHandle(hChildToken);
        }
        else
        {
            ok(0, "OpenProcessToken failed %lu\n", GetLastError());
        }
        ok_long(ResumeThread(pi.hThread), 1);
        CloseChild(&pi, 30000);
    }

    ok_long(ErrorWithToken(hProcessToken), ERROR_TOKEN_ALREADY_IN_USE);

    if (DuplicateTokenEx(hProcessToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation,
                         &hImpersonation))
    {
        ok_long(ErrorWithToken(hImpersonation), ERROR_SUCCESS);
        ok_long(ErrorWithAccess(hImpersonation, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY),
                ERROR_ACCESS_DENIED);
        CloseHandle(hImpersonation);
    }

    ok_long(ErrorWithToken(NULL), ERROR_LOGON_FAILURE);

    if (SetPrivilege(L"SeImpersonatePrivilege", 0))
    {
        ok_long(ErrorWithToken(hToken), ERROR_PRIVILEGE_NOT_HELD);
        SetPrivilege(L"SeImpersonatePrivilege", SE_PRIVILEGE_ENABLED);
    }
    else
    {
        skip("Cannot disable SeImpersonatePrivilege\n");
    }

    CloseHandle(hToken);
    CloseHandle(hProcessToken);
}

static LPWSTR GetDaclString(HANDLE h)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    LPWSTR pszSddl = NULL;

    if (GetSecurityInfo(h, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD) == ERROR_SUCCESS)
        ConvertSecurityDescriptorToStringSecurityDescriptorW(pSD, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &pszSddl,
                                                             NULL);
    if (pSD)
        LocalFree(pSD);
    return pszSddl;
}

static void TestOtherUser(void)
{
    FN_NetUserAdd pNetUserAdd;
    FN_NetUserDel pNetUserDel;
    FN_DeleteProfileW pDeleteProfileW;
    HMODULE hNetapi;
    USER_INFO_1 ui;
    HANDLE hToken;
    PROCESS_INFORMATION pi;
    HWINSTA hWinSta = GetProcessWindowStation();
    HDESK hDesk = GetThreadDesktop(GetCurrentThreadId());
    LPWSTR pszWinSta, pszDesk, pszWinStaAfter, pszDeskAfter, pszSid = NULL;
    TOKEN_USER *pUser;
    DWORD dwError, cb = 0;

    hNetapi = LoadLibraryW(L"netapi32.dll");
    pNetUserAdd = (FN_NetUserAdd)GetProcAddress(hNetapi, "NetUserAdd");
    pNetUserDel = (FN_NetUserDel)GetProcAddress(hNetapi, "NetUserDel");
    if (!pNetUserAdd || !pNetUserDel)
    {
        skip("netapi32 is missing\n");
        return;
    }
    ZeroMemory(&ui, sizeof(ui));
    ui.usri1_name = (LPWSTR)TEST_USER;
    ui.usri1_password = (LPWSTR)TEST_PASSWORD;
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    pNetUserDel(NULL, TEST_USER);
    if (pNetUserAdd(NULL, 1, (LPBYTE)&ui, NULL) != NERR_Success)
    {
        skip("Cannot create the test user; run as administrator\n");
        return;
    }
    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        ok(0, "LogonUserW failed %lu\n", GetLastError());
        pNetUserDel(NULL, TEST_USER);
        return;
    }
    GetTokenInformation(hToken, TokenUser, NULL, 0, &cb);
    pUser = HeapAlloc(GetProcessHeap(), 0, cb);
    if (pUser && GetTokenInformation(hToken, TokenUser, pUser, cb, &cb))
        ConvertSidToStringSidW(pUser->User.Sid, &pszSid);

    pszWinSta = GetDaclString(hWinSta);
    pszDesk = GetDaclString(hDesk);
    ok(RunWithToken(hToken, CREATE_SUSPENDED, &pi, &dwError), "CreateProcessWithTokenW failed %lu\n", dwError);
    if (pi.hProcess)
        TerminateProcess(pi.hProcess, 0);
    CloseChild(&pi, 5000);
    pszWinStaAfter = GetDaclString(hWinSta);
    pszDeskAfter = GetDaclString(hDesk);
    ok(pszWinSta && pszWinStaAfter && !wcscmp(pszWinSta, pszWinStaAfter), "the window station DACL changed: %ls\n",
       pszWinStaAfter);
    ok(pszDesk && pszDeskAfter && !wcscmp(pszDesk, pszDeskAfter), "the desktop DACL changed: %ls\n", pszDeskAfter);
    LocalFree(pszWinSta);
    LocalFree(pszDesk);
    LocalFree(pszWinStaAfter);
    LocalFree(pszDeskAfter);

    CloseHandle(hToken);
    if (pszSid)
    {
        pDeleteProfileW = (FN_DeleteProfileW)GetProcAddress(LoadLibraryW(L"userenv.dll"), "DeleteProfileW");
        if (pDeleteProfileW)
            pDeleteProfileW(pszSid, NULL, NULL);
        LocalFree(pszSid);
    }
    HeapFree(GetProcessHeap(), 0, pUser);
    pNetUserDel(NULL, TEST_USER);
}

START_TEST(CreateProcessWithToken)
{
    char **argv;
    WCHAR *p;

    if (winetest_get_mainargs(&argv) >= 4)
    {
        if (!strcmp(argv[2], "inherit"))
        {
            DWORD cb;
            HANDLE hEvent = (HANDLE)(ULONG_PTR)_strtoui64(argv[3], NULL, 16);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "hello", 5, &cb, NULL);
            ExitProcess(WaitForSingleObject(hEvent, 0) == WAIT_TIMEOUT ? 1 : 0);
        }
        Child(argv[3]);
        return;
    }

    GetModuleFileNameW(NULL, s_szExe, _countof(s_szExe));
    StringCchCopyW(s_szOut, _countof(s_szOut), s_szExe);
    p = wcsrchr(s_szOut, L'\\');
    if (p)
        p[1] = UNICODE_NULL;
    StringCchCatW(s_szOut, _countof(s_szOut), L"cpwt.out");

    if (!SetPrivilege(L"SeImpersonatePrivilege", SE_PRIVILEGE_ENABLED))
    {
        skip("The caller has no SeImpersonatePrivilege; run as administrator\n");
        return;
    }

    TestOwnToken();
    TestOtherUser();
    DeleteFileW(s_szOut);
}
