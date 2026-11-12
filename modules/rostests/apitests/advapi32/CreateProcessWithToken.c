/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CreateProcessWithTokenW needs only the privilege to impersonate and starts the process in the caller's session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <lm.h>
#include <ndk/psfuncs.h>

#define TEST_USER L"LntWithTokenTest"
#define TEST_PASSWORD L"Lnt!WithToken#2026q"

static
BOOL
SetImpersonatePrivilege(
    _In_ BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE hToken;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
        return FALSE;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Result = LookupPrivilegeValueW(NULL, L"SeImpersonatePrivilege", &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(hToken, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);

    return Result;
}

static
BOOL
SameUser(
    _In_ HANDLE hFirst,
    _In_ HANDLE hSecond)
{
    union
    {
        TOKEN_USER User;
        BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } First, Second;
    DWORD Length;

    return GetTokenInformation(hFirst, TokenUser, &First, sizeof(First), &Length) &&
           GetTokenInformation(hSecond, TokenUser, &Second, sizeof(Second), &Length) &&
           EqualSid(First.User.User.Sid, Second.User.User.Sid);
}

static
VOID
TestStart(
    _In_ HANDLE hToken)
{
    STARTUPINFOW StartupInfo = {sizeof(StartupInfo)};
    PROCESS_INFORMATION ProcessInfo = {0};
    PROCESS_SESSION_INFORMATION ChildSession = {MAXULONG};
    WCHAR CommandLine[MAX_PATH + 32];
    DWORD Session = MAXDWORD;
    HANDLE hChildToken;
    NTSTATUS Status;
    BOOL Result;

    GetModuleFileNameW(NULL, CommandLine, MAX_PATH);
    StringCchCatW(CommandLine, ARRAYSIZE(CommandLine), L" CreateProcessWithToken");

    /* Without the privilege the caller is refused */
    if (SetImpersonatePrivilege(FALSE))
    {
        Result = CreateProcessWithTokenW(hToken, 0, NULL, CommandLine, CREATE_SUSPENDED, NULL, NULL,
                                         &StartupInfo, &ProcessInfo);
        ok(!Result && GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "without the privilege: %d, error %lu\n",
           Result, GetLastError());
        if (Result)
        {
            TerminateProcess(ProcessInfo.hProcess, 0);
            CloseHandle(ProcessInfo.hThread);
            CloseHandle(ProcessInfo.hProcess);
        }
    }

    if (!SetImpersonatePrivilege(TRUE))
    {
        skip("SeImpersonatePrivilege is not held\n");
        return;
    }

    Result = CreateProcessWithTokenW(hToken, 0, NULL, CommandLine, CREATE_SUSPENDED, NULL, NULL,
                                     &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessWithTokenW failed %lu\n", GetLastError());
    if (!Result)
        return;

    ok(ProcessIdToSessionId(GetCurrentProcessId(), &Session), "ProcessIdToSessionId failed %lu\n", GetLastError());
    ok(GetProcessId(ProcessInfo.hProcess) == ProcessInfo.dwProcessId, "process handle and id disagree\n");

    /* The process runs in the caller's session, not in the token's (CreateProcessWithTokenW) */
    Status = NtQueryInformationProcess(ProcessInfo.hProcess, ProcessSessionInformation, &ChildSession,
                                       sizeof(ChildSession), NULL);
    ok(NT_SUCCESS(Status), "ProcessSessionInformation query failed 0x%lx\n", Status);
    ok(ChildSession.SessionId == Session, "the process runs in session %lu, the caller in %lu\n",
       ChildSession.SessionId, Session);

    /* The new process's token is secured for its own user, so only a caller it admits can look at it */
    if (OpenProcessToken(ProcessInfo.hProcess, TOKEN_QUERY, &hChildToken))
    {
        ok(SameUser(hChildToken, hToken), "the process does not run as the token's user\n");
        CloseHandle(hChildToken);
    }
    else
    {
        ok(GetLastError() == ERROR_ACCESS_DENIED, "OpenProcessToken failed %lu\n", GetLastError());
    }

    TerminateProcess(ProcessInfo.hProcess, 0);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}

START_TEST(CreateProcessWithToken)
{
    USER_INFO_1 User = {0};
    HANDLE hToken = NULL;
    NET_API_STATUS Status;
    char **argv;

    /* The child is only ever looked at, never run */
    if (winetest_get_mainargs(&argv) >= 3)
        return;

    NetUserDel(NULL, TEST_USER);
    User.usri1_name = TEST_USER;
    User.usri1_password = TEST_PASSWORD;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    if (Status == ERROR_ACCESS_DENIED)
    {
        skip("Creating a user needs administrator rights\n");
        return;
    }
    ok(Status == NERR_Success, "NetUserAdd failed %lu\n", Status);
    if (Status != NERR_Success)
        return;

    if (LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        TestStart(hToken);
        CloseHandle(hToken);
    }
    else
    {
        ok(FALSE, "LogonUserW failed %lu\n", GetLastError());
    }

    NetUserDel(NULL, TEST_USER);
}
