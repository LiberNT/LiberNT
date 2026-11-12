/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for software restriction policy enforcement by CreateProcess
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <ndk/obfuncs.h>
#include <ndk/sefuncs.h>
#include <ndk/psfuncs.h>

#define SAFER_PARENT_PATH L"Software\\Policies\\Microsoft\\Windows"
#define SAFER_POLICY_PATH SAFER_PARENT_PATH L"\\Safer\\CodeIdentifiers"
#define TEST_MARKER L"LiberNTSaferApitest"
#define SAFER_LEVELID_DISALLOWED 0x00000
#define SAFER_LEVELID_NORMALUSER 0x20000
#ifndef CREATE_PRESERVE_CODE_AUTHZ_LEVEL
#define CREATE_PRESERVE_CODE_AUTHZ_LEVEL 0x02000000
#endif
#define CHILD_MARK 0x10000
#define CHILD_RESTRICTED 0x1000

static WCHAR TestDir[MAX_PATH];
static WCHAR BlockedPath[MAX_PATH];
static WCHAR BasicPath[MAX_PATH];
static WCHAR FreePath[MAX_PATH];

static PVOID
QueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class)
{
    ULONG Size = 0;
    PVOID Buffer = NULL;

    NtQueryInformationToken(Token, Class, NULL, 0, &Size);
    if (Size)
    {
        Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (Buffer && !NT_SUCCESS(NtQueryInformationToken(Token, Class, Buffer, Size, &Size)))
        {
            HeapFree(GetProcessHeap(), 0, Buffer);
            Buffer = NULL;
        }
    }
    return Buffer;
}

static DWORD
TokenState(HANDLE Token)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    BYTE AdminBuffer[SECURITY_MAX_SID_SIZE];
    PSID AdminSid = (PSID)AdminBuffer;
    PTOKEN_GROUPS Groups, Restricted;
    PTOKEN_PRIVILEGES Privileges;
    DWORD State = CHILD_MARK, i, Admin = 0;

    RtlInitializeSid(AdminSid, &NtAuthority, 2);
    *RtlSubAuthoritySid(AdminSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(AdminSid, 1) = DOMAIN_ALIAS_RID_ADMINS;

    Groups = QueryToken(Token, TokenGroups);
    if (Groups)
    {
        for (i = 0; i < Groups->GroupCount; i++)
        {
            if (!RtlEqualSid(Groups->Groups[i].Sid, AdminSid))
                continue;
            if (Groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
                Admin = 2;
            else if (Groups->Groups[i].Attributes & SE_GROUP_ENABLED)
                Admin = 1;
            else
                Admin = 3;
        }
        HeapFree(GetProcessHeap(), 0, Groups);
    }
    State |= Admin;

    Privileges = QueryToken(Token, TokenPrivileges);
    if (Privileges)
    {
        State |= min(Privileges->PrivilegeCount, 0xFF) << 4;
        HeapFree(GetProcessHeap(), 0, Privileges);
    }

    Restricted = QueryToken(Token, TokenRestrictedSids);
    if (Restricted)
    {
        if (Restricted->GroupCount)
            State |= CHILD_RESTRICTED;
        HeapFree(GetProcessHeap(), 0, Restricted);
    }

    return State;
}

static DWORD
ProcessTokenState(HANDLE Process)
{
    HANDLE Token;
    DWORD State;

    if (!NT_SUCCESS(NtOpenProcessToken(Process, TOKEN_QUERY, &Token)))
        return 0;
    State = TokenState(Token);
    NtClose(Token);
    return State;
}

static BOOL
PolicyKeyExists(HKEY Root, PBOOL HasMarker)
{
    HKEY Key;

    *HasMarker = FALSE;
    if (RegOpenKeyExW(Root, SAFER_POLICY_PATH, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    *HasMarker = RegQueryValueExW(Key, TEST_MARKER, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(Key);
    return TRUE;
}

static void
RemoveTestPolicy(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, SAFER_POLICY_PATH);
    RegDeleteKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, 0);
    RegDeleteKeyExW(HKEY_CURRENT_USER, SAFER_PARENT_PATH L"\\Safer", 0, 0);
}

static BOOL
AddPathRule(DWORD LevelId, PCWSTR Guid, PCWSTR ItemData)
{
    WCHAR Path[MAX_PATH];
    DWORD Flags = 0;
    LSTATUS Status;
    HKEY Key;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%lu\\Paths\\%s", SAFER_POLICY_PATH, LevelId, Guid);
    Status = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Status != ERROR_SUCCESS)
        return FALSE;
    Status = RegSetValueExW(Key, L"ItemData", 0, REG_SZ, (const BYTE *)ItemData,
                            (DWORD)((wcslen(ItemData) + 1) * sizeof(WCHAR)));
    if (Status == ERROR_SUCCESS)
        Status = RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD, (const BYTE *)&Flags, sizeof(Flags));
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static BOOL
SetupPolicy(void)
{
    DWORD Value = 1;
    LSTATUS Status;
    HKEY Key;

    Status = RegCreateKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Status != ERROR_SUCCESS)
        return FALSE;
    RegSetValueExW(Key, TEST_MARKER, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    Status = RegSetValueExW(Key, L"TransparentEnabled", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);

    return Status == ERROR_SUCCESS &&
           AddPathRule(SAFER_LEVELID_DISALLOWED, L"{5afe1800-0000-4000-8000-0000000000a1}", BlockedPath) &&
           AddPathRule(SAFER_LEVELID_NORMALUSER, L"{5afe1800-0000-4000-8000-0000000000a2}", BasicPath);
}

static BOOL
PrepareImages(void)
{
    WCHAR Temp[MAX_PATH], Self[MAX_PATH];

    if (!GetTempPathW(ARRAYSIZE(Temp), Temp) || !GetLongPathNameW(Temp, TestDir, ARRAYSIZE(TestDir)))
        return FALSE;
    StringCchCatW(TestDir, ARRAYSIZE(TestDir), L"WinSaferApitest");
    CreateDirectoryW(TestDir, NULL);

    StringCchPrintfW(BlockedPath, ARRAYSIZE(BlockedPath), L"%s\\blocked.exe", TestDir);
    StringCchPrintfW(BasicPath, ARRAYSIZE(BasicPath), L"%s\\basic.exe", TestDir);
    StringCchPrintfW(FreePath, ARRAYSIZE(FreePath), L"%s\\free.exe", TestDir);

    if (!GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self)))
        return FALSE;

    return CopyFileW(Self, BlockedPath, FALSE) && CopyFileW(Self, BasicPath, FALSE) &&
           CopyFileW(Self, FreePath, FALSE);
}

static void
RemoveImages(void)
{
    DeleteFileW(BlockedPath);
    DeleteFileW(BasicPath);
    DeleteFileW(FreePath);
    RemoveDirectoryW(TestDir);
}

static BOOL
StartChild(PCWSTR Path, DWORD Flags, PPROCESS_INFORMATION ProcessInfo, PDWORD Error)
{
    WCHAR CommandLine[MAX_PATH * 2];
    STARTUPINFOW StartupInfo;
    BOOL Ret;

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" WinSaferRestrictions child", Path);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    SetLastError(0xdeadbeef);
    Ret = CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, Flags, NULL, NULL, &StartupInfo, ProcessInfo);
    *Error = GetLastError();
    return Ret;
}

static BOOL
RunChild(PCWSTR Path, DWORD Flags, PDWORD ExitCode, PDWORD Error)
{
    PROCESS_INFORMATION ProcessInfo;

    *ExitCode = 0xdeadbeef;
    if (!StartChild(Path, Flags, &ProcessInfo, Error))
        return FALSE;

    ok(WaitForSingleObject(ProcessInfo.hProcess, 10000) == WAIT_OBJECT_0, "child %S did not exit\n", Path);
    GetExitCodeProcess(ProcessInfo.hProcess, ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return TRUE;
}

static BOOL
SuspendedChildState(PCWSTR Path, PDWORD State, PDWORD Error)
{
    PROCESS_INFORMATION ProcessInfo;

    *State = 0;
    if (!StartChild(Path, CREATE_SUSPENDED, &ProcessInfo, Error))
        return FALSE;

    *State = ProcessTokenState(ProcessInfo.hProcess);
    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 10000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return TRUE;
}

START_TEST(WinSaferRestrictions)
{
    DWORD ParentState, State, ExitCode, Error, ExpectedAdmin;
    char **Arguments;
    BOOL HasMarker, Ret;

    if (winetest_get_mainargs(&Arguments) >= 3 && strcmp(Arguments[2], "child") == 0)
        ExitProcess(ProcessTokenState(NtCurrentProcess()));

    if (PolicyKeyExists(HKEY_LOCAL_MACHINE, &HasMarker))
    {
        skip("a machine software restriction policy is configured\n");
        return;
    }
    if (PolicyKeyExists(HKEY_CURRENT_USER, &HasMarker))
    {
        if (!HasMarker)
        {
            skip("a user software restriction policy is configured\n");
            return;
        }
        RemoveTestPolicy();
    }

    if (!PrepareImages())
    {
        skip("test images not created: %lu\n", GetLastError());
        RemoveImages();
        return;
    }

    if (!SetupPolicy())
    {
        skip("user policy not writable\n");
        RemoveTestPolicy();
        RemoveImages();
        return;
    }

    ParentState = ProcessTokenState(NtCurrentProcess());
    trace("parent token state 0x%lx\n", ParentState);

    Ret = RunChild(BlockedPath, 0, &ExitCode, &Error);
    ok(!Ret, "a disallowed image started\n");
    ok(Error == ERROR_ACCESS_DISABLED_BY_POLICY, "disallowed image error %lu\n", Error);

    Ret = RunChild(BlockedPath, CREATE_PRESERVE_CODE_AUTHZ_LEVEL, &ExitCode, &Error);
    ok(Ret, "CREATE_PRESERVE_CODE_AUTHZ_LEVEL did not bypass the policy: %lu\n", Error);
    ok(!Ret || ExitCode == ParentState, "preserved child state 0x%lx, parent 0x%lx\n", ExitCode, ParentState);

    ExpectedAdmin = (ParentState & 0xF) ? 2 : 0;
    Ret = SuspendedChildState(BasicPath, &State, &Error);
    ok(Ret, "a Basic User image did not start: %lu\n", Error);
    if (Ret)
    {
        ok((State & ~0x1FFF) == CHILD_MARK, "Basic User child token state 0x%lx\n", State);
        ok((State & 0xF) == ExpectedAdmin, "Basic User child Administrators state %lu, expected %lu\n",
           State & 0xF, ExpectedAdmin);
        ok(((State >> 4) & 0xFF) == 1, "Basic User child has %lu privileges\n", (State >> 4) & 0xFF);
        ok(!(State & CHILD_RESTRICTED), "Basic User child token has restricting SIDs\n");
    }

    Ret = RunChild(BasicPath, 0, &ExitCode, &Error);
    ok(Ret, "a Basic User image did not start: %lu\n", Error);
    ok(!Ret || ExitCode == State, "running Basic User child state 0x%lx, expected 0x%lx\n", ExitCode, State);

    Ret = SuspendedChildState(FreePath, &State, &Error);
    ok(Ret, "an unrestricted image did not start: %lu\n", Error);
    ok(!Ret || State == ParentState, "unrestricted child token state 0x%lx, parent 0x%lx\n", State, ParentState);

    Ret = RunChild(FreePath, 0, &ExitCode, &Error);
    ok(Ret, "an unrestricted image did not start: %lu\n", Error);
    ok(!Ret || ExitCode == ParentState, "unrestricted child state 0x%lx, parent 0x%lx\n", ExitCode, ParentState);

    RemoveTestPolicy();

    Ret = RunChild(BlockedPath, 0, &ExitCode, &Error);
    ok(Ret, "the image stayed blocked after the policy was removed: %lu\n", Error);

    RemoveImages();
}
