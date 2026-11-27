/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which tokens CreateProcessAsUser accepts without SeAssignPrimaryTokenPrivilege
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <tlhelp32.h>

static WCHAR CommandPath[MAX_PATH];

static
HANDLE
DuplicatePrimary(
    _In_ HANDLE Token)
{
    HANDLE Duplicate = NULL;

    ok(DuplicateTokenEx(Token, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &Duplicate),
       "DuplicateTokenEx failed with %lu\n", GetLastError());
    return Duplicate;
}

static
VOID
CheckAssignable(
    _In_ const char *Tag,
    _In_opt_ HANDLE Source)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    WCHAR CommandLine[] = L"cmd /c exit 7";
    HANDLE Token;
    BOOL Created;

    ok(Source != NULL, "%s: no source token\n", Tag);
    if (Source == NULL) return;

    Token = DuplicatePrimary(Source);
    if (Token == NULL) return;

    SetLastError(0xdeadbeef);
    Created = CreateProcessAsUserW(Token, CommandPath, CommandLine, NULL, NULL, FALSE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &Startup, &Info);
    ok(Created, "%s: CreateProcessAsUserW failed with %lu\n", Tag, GetLastError());
    if (Created)
    {
        TerminateProcess(Info.hProcess, 0);
        CloseHandle(Info.hThread);
        CloseHandle(Info.hProcess);
    }
    CloseHandle(Token);
}

static
HANDLE
OpenTokenOfProcess(
    _In_ DWORD ProcessId)
{
    HANDLE Process, Token = NULL;

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
    if (Process == NULL) return NULL;
    OpenProcessToken(Process, TOKEN_DUPLICATE | TOKEN_QUERY, &Token);
    CloseHandle(Process);
    return Token;
}

static
DWORD
ParentProcessId(VOID)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD Parent = 0;

    if (Snapshot == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (Entry.th32ProcessID == GetCurrentProcessId()) Parent = Entry.th32ParentProcessID;
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Parent;
}

START_TEST(CreateProcessAsUserToken)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Child;
    WCHAR CommandLine[] = L"cmd /c exit 0";
    TOKEN_PRIVILEGES Privileges;
    HANDLE Own = NULL, Duplicate, Filtered = NULL, ChildToken;

    GetSystemDirectoryW(CommandPath, ARRAYSIZE(CommandPath));
    StringCchCatW(CommandPath, ARRAYSIZE(CommandPath), L"\\cmd.exe");

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Own), "OpenProcessToken failed with %lu\n", GetLastError());
    if (Own == NULL) return;

    Privileges.PrivilegeCount = 1;
    ok(LookupPrivilegeValueW(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &Privileges.Privileges[0].Luid), "LookupPrivilegeValueW failed\n");
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
    ok(AdjustTokenPrivileges(Own, FALSE, &Privileges, 0, NULL, NULL), "AdjustTokenPrivileges failed with %lu\n", GetLastError());

    CheckAssignable("own", Own);

    Duplicate = DuplicatePrimary(Own);
    CheckAssignable("duplicate of a duplicate", Duplicate);
    if (Duplicate) CloseHandle(Duplicate);

    ok(CreateRestrictedToken(Own, 0, 0, NULL, 0, NULL, 0, NULL, &Filtered), "CreateRestrictedToken failed with %lu\n", GetLastError());
    CheckAssignable("filtered", Filtered);
    if (Filtered) CloseHandle(Filtered);

    if (CreateProcessW(CommandPath, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &Startup, &Child))
    {
        ChildToken = OpenTokenOfProcess(Child.dwProcessId);
        CheckAssignable("child", ChildToken);
        if (ChildToken) CloseHandle(ChildToken);
        TerminateProcess(Child.hProcess, 0);
        CloseHandle(Child.hThread);
        CloseHandle(Child.hProcess);
    }
    else
    {
        ok(0, "CreateProcessW failed with %lu\n", GetLastError());
    }

    ChildToken = OpenTokenOfProcess(ParentProcessId());
    CheckAssignable("parent", ChildToken);
    if (ChildToken) CloseHandle(ChildToken);

    CloseHandle(Own);
}
