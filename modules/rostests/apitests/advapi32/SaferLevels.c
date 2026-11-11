/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SAFER levels, restricted tokens and policy queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <winsafer.h>

#define SAFER_POLICY_PATH L"Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers"
#define SAFER_EVENT_SOURCE L"Microsoft-Windows-SoftwareRestrictionPolicies"

static BOOL (WINAPI *pSaferCreateLevel)(DWORD, DWORD, DWORD, SAFER_LEVEL_HANDLE *, PVOID);
static BOOL (WINAPI *pSaferCloseLevel)(SAFER_LEVEL_HANDLE);
static BOOL (WINAPI *pSaferGetLevelInformation)(SAFER_LEVEL_HANDLE, SAFER_OBJECT_INFO_CLASS, PVOID, DWORD, PDWORD);
static BOOL (WINAPI *pSaferSetLevelInformation)(SAFER_LEVEL_HANDLE, SAFER_OBJECT_INFO_CLASS, PVOID, DWORD);
static BOOL (WINAPI *pSaferComputeTokenFromLevel)(SAFER_LEVEL_HANDLE, HANDLE, PHANDLE, DWORD, PVOID);
static BOOL (WINAPI *pSaferIdentifyLevel)(DWORD, PSAFER_CODE_PROPERTIES, SAFER_LEVEL_HANDLE *, PVOID);
static BOOL (WINAPI *pSaferGetPolicyInformation)(DWORD, SAFER_POLICY_INFO_CLASS, DWORD, PVOID, PDWORD, PVOID);
static BOOL (WINAPI *pSaferRecordEventLogEntry)(SAFER_LEVEL_HANDLE, PCWSTR, PVOID);
static BOOL (WINAPI *pSaferiIsExecutableFileType)(PCWSTR, BOOLEAN);
static BOOL (WINAPI *pSaferiSearchMatchingHashRules)(ALG_ID, PBYTE, DWORD, DWORD, PDWORD, PDWORD);

static const DWORD AllLevels[] =
{
    SAFER_LEVELID_DISALLOWED,
    SAFER_LEVELID_UNTRUSTED,
    SAFER_LEVELID_CONSTRAINED,
    SAFER_LEVELID_NORMALUSER,
    SAFER_LEVELID_FULLYTRUSTED,
};

static BOOL
ResolveSafer(void)
{
    HMODULE Advapi = GetModuleHandleW(L"advapi32.dll");

    pSaferCreateLevel = (void *)GetProcAddress(Advapi, "SaferCreateLevel");
    pSaferCloseLevel = (void *)GetProcAddress(Advapi, "SaferCloseLevel");
    pSaferGetLevelInformation = (void *)GetProcAddress(Advapi, "SaferGetLevelInformation");
    pSaferSetLevelInformation = (void *)GetProcAddress(Advapi, "SaferSetLevelInformation");
    pSaferComputeTokenFromLevel = (void *)GetProcAddress(Advapi, "SaferComputeTokenFromLevel");
    pSaferIdentifyLevel = (void *)GetProcAddress(Advapi, "SaferIdentifyLevel");
    pSaferGetPolicyInformation = (void *)GetProcAddress(Advapi, "SaferGetPolicyInformation");
    pSaferRecordEventLogEntry = (void *)GetProcAddress(Advapi, "SaferRecordEventLogEntry");
    pSaferiIsExecutableFileType = (void *)GetProcAddress(Advapi, "SaferiIsExecutableFileType");
    pSaferiSearchMatchingHashRules = (void *)GetProcAddress(Advapi, "SaferiSearchMatchingHashRules");

    return pSaferCreateLevel && pSaferCloseLevel && pSaferGetLevelInformation && pSaferSetLevelInformation &&
           pSaferComputeTokenFromLevel && pSaferIdentifyLevel && pSaferGetPolicyInformation &&
           pSaferRecordEventLogEntry && pSaferiIsExecutableFileType && pSaferiSearchMatchingHashRules;
}

static BOOL
PolicyKeyExists(HKEY Root)
{
    HKEY Key;

    if (RegOpenKeyExW(Root, SAFER_POLICY_PATH, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    RegCloseKey(Key);
    return TRUE;
}

static BOOL
PolicyValueExists(HKEY Root, PCWSTR Name)
{
    HKEY Key;
    LSTATUS Status;

    if (RegOpenKeyExW(Root, SAFER_POLICY_PATH, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    Status = RegQueryValueExW(Key, Name, NULL, NULL, NULL, NULL);
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static PVOID
QueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class)
{
    DWORD Size = 0;
    PVOID Buffer;

    GetTokenInformation(Token, Class, NULL, 0, &Size);
    if (!Size)
        return NULL;
    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Buffer && !GetTokenInformation(Token, Class, Buffer, Size, &Size))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
    }
    return Buffer;
}

static PSID
MakeSid(WELL_KNOWN_SID_TYPE Type, PBYTE Buffer)
{
    DWORD Size = SECURITY_MAX_SID_SIZE;

    ok(CreateWellKnownSid(Type, NULL, Buffer, &Size), "CreateWellKnownSid(%d) failed: %lu\n", Type, GetLastError());
    return Buffer;
}

static DWORD
GroupState(HANDLE Token, PSID Sid)
{
    PTOKEN_GROUPS Groups = QueryToken(Token, TokenGroups);
    DWORD i, State = 0;

    if (!Groups)
        return 0xFFFFFFFF;

    for (i = 0; i < Groups->GroupCount; i++)
    {
        if (EqualSid(Groups->Groups[i].Sid, Sid))
        {
            if (Groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
                State = 2;
            else if (Groups->Groups[i].Attributes & SE_GROUP_ENABLED)
                State = 1;
            else
                State = 3;
        }
    }

    HeapFree(GetProcessHeap(), 0, Groups);
    return State;
}

static DWORD
PrivilegeCount(HANDLE Token, PBOOL OnlyChangeNotify)
{
    PTOKEN_PRIVILEGES Privileges = QueryToken(Token, TokenPrivileges);
    DWORD i, Count;
    LUID ChangeNotify;

    if (!Privileges || !LookupPrivilegeValueW(NULL, L"SeChangeNotifyPrivilege", &ChangeNotify))
    {
        HeapFree(GetProcessHeap(), 0, Privileges);
        return 0xFFFFFFFF;
    }

    *OnlyChangeNotify = TRUE;
    for (i = 0; i < Privileges->PrivilegeCount; i++)
    {
        if (Privileges->Privileges[i].Luid.LowPart != ChangeNotify.LowPart ||
            Privileges->Privileges[i].Luid.HighPart != ChangeNotify.HighPart)
        {
            *OnlyChangeNotify = FALSE;
        }
    }
    Count = Privileges->PrivilegeCount;
    HeapFree(GetProcessHeap(), 0, Privileges);
    return Count;
}

static BOOL
RestrictedSidsContain(HANDLE Token, PSID Sid, PDWORD Count)
{
    PTOKEN_GROUPS Groups = QueryToken(Token, TokenRestrictedSids);
    BOOL Found = FALSE;
    DWORD i;

    *Count = 0;
    if (!Groups)
        return FALSE;

    *Count = Groups->GroupCount;
    for (i = 0; i < Groups->GroupCount; i++)
    {
        if (EqualSid(Groups->Groups[i].Sid, Sid))
            Found = TRUE;
    }
    HeapFree(GetProcessHeap(), 0, Groups);
    return Found;
}

static BOOL
OwnerIsUser(HANDLE Token, PSID UserSid)
{
    PTOKEN_OWNER Owner = QueryToken(Token, TokenOwner);
    BOOL Equal;

    if (!Owner)
        return FALSE;
    Equal = EqualSid(Owner->Owner, UserSid);
    HeapFree(GetProcessHeap(), 0, Owner);
    return Equal;
}

static BOOL
DefaultDaclGrants(HANDLE Token, PSID Sid)
{
    PTOKEN_DEFAULT_DACL Dacl = QueryToken(Token, TokenDefaultDacl);
    ACL_SIZE_INFORMATION SizeInfo;
    PACCESS_ALLOWED_ACE Ace;
    BOOL Found = FALSE;
    DWORD i;

    if (!Dacl)
        return FALSE;
    if (Dacl->DefaultDacl && GetAclInformation(Dacl->DefaultDacl, &SizeInfo, sizeof(SizeInfo), AclSizeInformation))
    {
        for (i = 0; i < SizeInfo.AceCount; i++)
        {
            if (GetAce(Dacl->DefaultDacl, i, (PVOID *)&Ace) && Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                EqualSid(&Ace->SidStart, Sid))
            {
                Found = TRUE;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Dacl);
    return Found;
}

static SAFER_LEVEL_HANDLE
CreateLevel(DWORD Scope, DWORD LevelId)
{
    SAFER_LEVEL_HANDLE Level = NULL;
    BOOL Ret;

    Ret = pSaferCreateLevel(Scope, LevelId, SAFER_LEVEL_OPEN, &Level, NULL);
    ok(Ret, "SaferCreateLevel(%lu, 0x%lx) failed: %lu\n", Scope, LevelId, GetLastError());
    return Ret ? Level : NULL;
}

static void
TestCreateAndQueryLevels(void)
{
    SAFER_LEVEL_HANDLE Level;
    DWORD Scope, i, Value, Size;
    WCHAR Name[SAFER_MAX_FRIENDLYNAME_SIZE * 2];
    BOOL Ret;

    for (Scope = SAFER_SCOPEID_MACHINE; Scope <= SAFER_SCOPEID_USER; Scope++)
    {
        for (i = 0; i < ARRAYSIZE(AllLevels); i++)
        {
            Level = CreateLevel(Scope, AllLevels[i]);
            if (!Level)
                continue;

            Value = 0xdeadbeef;
            Size = 0xdeadbeef;
            Ret = pSaferGetLevelInformation(Level, SaferObjectLevelId, &Value, sizeof(Value), &Size);
            ok(Ret, "LevelId query failed: %lu\n", GetLastError());
            ok(Value == AllLevels[i], "LevelId = 0x%lx, expected 0x%lx\n", Value, AllLevels[i]);
            ok(Size == sizeof(DWORD), "LevelId size = %lu\n", Size);

            Value = 0xdeadbeef;
            Ret = pSaferGetLevelInformation(Level, SaferObjectScopeId, &Value, sizeof(Value), &Size);
            ok(Ret && Value == Scope, "ScopeId = %lu (ret %d), expected %lu\n", Value, Ret, Scope);

            Value = 0xdeadbeef;
            Ret = pSaferGetLevelInformation(Level, SaferObjectBuiltin, &Value, sizeof(Value), &Size);
            ok(Ret && Value == TRUE, "Builtin = %lu (ret %d)\n", Value, Ret);

            Value = 0xdeadbeef;
            Ret = pSaferGetLevelInformation(Level, SaferObjectDisallowed, &Value, sizeof(Value), &Size);
            ok(Ret && Value == (AllLevels[i] == SAFER_LEVELID_DISALLOWED),
               "Disallowed = %lu (ret %d) for 0x%lx\n", Value, Ret, AllLevels[i]);

            Size = 0;
            SetLastError(0xdeadbeef);
            Ret = pSaferGetLevelInformation(Level, SaferObjectFriendlyName, NULL, 0, &Size);
            ok(!Ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
               "FriendlyName size query: ret %d, error %lu\n", Ret, GetLastError());
            ok(Size > sizeof(WCHAR) && Size <= sizeof(Name), "FriendlyName size = %lu\n", Size);
            if (Size > sizeof(WCHAR) && Size <= sizeof(Name))
            {
                Ret = pSaferGetLevelInformation(Level, SaferObjectFriendlyName, Name, sizeof(Name), &Size);
                ok(Ret, "FriendlyName query failed: %lu\n", GetLastError());
                ok(Ret && (wcslen(Name) + 1) * sizeof(WCHAR) == Size, "FriendlyName size %lu for %S\n", Size, Name);
            }

            Value = 0;
            SetLastError(0xdeadbeef);
            Ret = pSaferSetLevelInformation(Level, SaferObjectLevelId, &Value, sizeof(Value));
            ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER,
               "Setting LevelId: ret %d, error %lu\n", Ret, GetLastError());

            SetLastError(0xdeadbeef);
            Ret = pSaferGetLevelInformation(Level, (SAFER_OBJECT_INFO_CLASS)0, &Value, sizeof(Value), &Size);
            ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER,
               "Info class 0: ret %d, error %lu\n", Ret, GetLastError());

            Ret = pSaferCloseLevel(Level);
            ok(Ret, "SaferCloseLevel failed: %lu\n", GetLastError());

            SetLastError(0xdeadbeef);
            Ret = pSaferGetLevelInformation(Level, SaferObjectLevelId, &Value, sizeof(Value), &Size);
            ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE,
               "Closed handle query: ret %d, error %lu\n", Ret, GetLastError());

            SetLastError(0xdeadbeef);
            Ret = pSaferCloseLevel(Level);
            ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE,
               "Second close: ret %d, error %lu\n", Ret, GetLastError());
        }
    }

    Level = NULL;
    Ret = pSaferCreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_NORMALUSER, 0, &Level, NULL);
    ok(Ret && Level != NULL, "SaferCreateLevel without SAFER_LEVEL_OPEN: ret %d, error %lu\n", Ret, GetLastError());
    if (Ret)
        pSaferCloseLevel(Level);

    Level = (SAFER_LEVEL_HANDLE)(ULONG_PTR)0x1234;
    SetLastError(0xdeadbeef);
    Ret = pSaferCreateLevel(3, SAFER_LEVELID_NORMALUSER, SAFER_LEVEL_OPEN, &Level, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "Scope 3: ret %d, error %lu\n", Ret, GetLastError());
    ok(Level == (SAFER_LEVEL_HANDLE)(ULONG_PTR)0x1234, "Level changed to %p\n", Level);

    SetLastError(0xdeadbeef);
    Ret = pSaferCreateLevel(SAFER_SCOPEID_USER, 0x30000, SAFER_LEVEL_OPEN, &Level, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "Level 0x30000: ret %d, error %lu\n", Ret, GetLastError());
}

static void
TestTokenClasses(void)
{
    SAFER_LEVEL_HANDLE Level;
    BYTE Buffer[512] = { 0 };
    BYTE AdminSid[SECURITY_MAX_SID_SIZE], PowerSid[SECURITY_MAX_SID_SIZE];
    BYTE RestrictedSid[SECURITY_MAX_SID_SIZE];
    PTOKEN_GROUPS Groups = (PTOKEN_GROUPS)Buffer;
    DWORD Value, Size;
    BOOL Ret;

    MakeSid(WinBuiltinAdministratorsSid, AdminSid);
    MakeSid(WinBuiltinPowerUsersSid, PowerSid);
    MakeSid(WinRestrictedCodeSid, RestrictedSid);

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_NORMALUSER);
    if (Level)
    {
        Value = 0xdeadbeef;
        Ret = pSaferGetLevelInformation(Level, SaferObjectDisableMaxPrivilege, &Value, sizeof(Value), &Size);
        ok(Ret && Value == TRUE, "NORMALUSER DisableMaxPrivilege = %lu (ret %d)\n", Value, Ret);

        Ret = pSaferGetLevelInformation(Level, SaferObjectSidsToDisable, Buffer, sizeof(Buffer), &Size);
        ok(Ret, "NORMALUSER SidsToDisable failed: %lu\n", GetLastError());
        if (Ret)
        {
            ok(Groups->GroupCount == 2, "NORMALUSER SidsToDisable count = %lu\n", Groups->GroupCount);
            if (Groups->GroupCount == 2)
            {
                PSID_AND_ATTRIBUTES Entries = Groups->Groups;

                ok(EqualSid(Entries[0].Sid, AdminSid), "first SID to disable is not Administrators\n");
                ok(EqualSid(Entries[1].Sid, PowerSid), "second SID to disable is not Power Users\n");
            }
        }

        Ret = pSaferGetLevelInformation(Level, SaferObjectRestrictedSidsAdded, Buffer, sizeof(Buffer), &Size);
        ok(Ret && Groups->GroupCount == 0, "NORMALUSER RestrictedSidsAdded: ret %d, count %lu\n",
           Ret, Groups->GroupCount);
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_CONSTRAINED);
    if (Level)
    {
        Ret = pSaferGetLevelInformation(Level, SaferObjectRestrictedSidsAdded, Buffer, sizeof(Buffer), &Size);
        ok(Ret, "CONSTRAINED RestrictedSidsAdded failed: %lu\n", GetLastError());
        if (Ret)
        {
            ok(Groups->GroupCount == 1, "CONSTRAINED RestrictedSidsAdded count = %lu\n", Groups->GroupCount);
            if (Groups->GroupCount == 1)
                ok(EqualSid(Groups->Groups[0].Sid, RestrictedSid), "added SID is not RESTRICTED\n");
        }
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_UNTRUSTED);
    if (Level)
    {
        Ret = pSaferGetLevelInformation(Level, SaferObjectRestrictedSidsAdded, Buffer, sizeof(Buffer), &Size);
        ok(Ret && Groups->GroupCount == 5, "UNTRUSTED RestrictedSidsAdded: ret %d, count %lu\n",
           Ret, Groups->GroupCount);
        Size = 0;
        SetLastError(0xdeadbeef);
        Ret = pSaferGetLevelInformation(Level, SaferObjectRestrictedSidsAdded, Buffer, 4, &Size);
        ok(!Ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER && Size > 4,
           "short buffer: ret %d, error %lu, size %lu\n", Ret, GetLastError(), Size);
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_FULLYTRUSTED);
    if (Level)
    {
        Value = 0xdeadbeef;
        Ret = pSaferGetLevelInformation(Level, SaferObjectDisableMaxPrivilege, &Value, sizeof(Value), &Size);
        ok(Ret && Value == FALSE, "FULLYTRUSTED DisableMaxPrivilege = %lu (ret %d)\n", Value, Ret);
        pSaferCloseLevel(Level);
    }
}

static void
TestComputeToken(void)
{
    BYTE AdminSid[SECURITY_MAX_SID_SIZE], RestrictedSid[SECURITY_MAX_SID_SIZE], WorldSid[SECURITY_MAX_SID_SIZE];
    BYTE UsersSid[SECURITY_MAX_SID_SIZE], InteractiveSid[SECURITY_MAX_SID_SIZE], AuthSid[SECURITY_MAX_SID_SIZE];
    SAFER_LEVEL_HANDLE Level;
    HANDLE ProcessToken, Token;
    PTOKEN_USER User;
    DWORD AdminState, Count, Result, Inert, Size;
    BOOL Ret, OnlyChangeNotify;

    MakeSid(WinBuiltinAdministratorsSid, AdminSid);
    MakeSid(WinRestrictedCodeSid, RestrictedSid);
    MakeSid(WinWorldSid, WorldSid);
    MakeSid(WinBuiltinUsersSid, UsersSid);
    MakeSid(WinInteractiveSid, InteractiveSid);
    MakeSid(WinAuthenticatedUserSid, AuthSid);

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &ProcessToken))
    {
        skip("OpenProcessToken failed: %lu\n", GetLastError());
        return;
    }
    AdminState = GroupState(ProcessToken, AdminSid);
    trace("Administrators state in the test token: %lu\n", AdminState);
    User = QueryToken(ProcessToken, TokenUser);
    ok(User != NULL, "TokenUser query failed\n");

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_NORMALUSER);
    if (Level)
    {
        Token = NULL;
        Ret = pSaferComputeTokenFromLevel(Level, NULL, &Token, 0, NULL);
        ok(Ret && Token != NULL, "NORMALUSER token: ret %d, error %lu\n", Ret, GetLastError());
        if (Ret && Token)
        {
            ok(GroupState(Token, AdminSid) == (AdminState ? 2u : 0u), "Administrators state %lu, process %lu\n",
               GroupState(Token, AdminSid), AdminState);
            Count = PrivilegeCount(Token, &OnlyChangeNotify);
            ok(Count == 1 && OnlyChangeNotify, "NORMALUSER privileges: count %lu, only change notify %d\n",
               Count, OnlyChangeNotify);
            ok(!IsTokenRestricted(Token), "NORMALUSER token is restricted\n");
            if (User)
            {
                ok(OwnerIsUser(Token, User->User.Sid), "NORMALUSER default owner is not the user\n");
                if (AdminState)
                    ok(!DefaultDaclGrants(Token, AdminSid), "NORMALUSER default DACL grants Administrators\n");
            }
            CloseHandle(Token);
        }

        Token = NULL;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, 0, NULL);
        ok(Ret && Token != NULL, "NORMALUSER from explicit token: ret %d, error %lu\n", Ret, GetLastError());
        if (Ret && Token)
            CloseHandle(Token);

        Result = 0xdeadbeef;
        Token = (HANDLE)(ULONG_PTR)0x1234;
        Count = PrivilegeCount(ProcessToken, &OnlyChangeNotify);
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, SAFER_TOKEN_COMPARE_ONLY, &Result);
        ok(Ret, "COMPARE_ONLY failed: %lu\n", GetLastError());
        if (!OnlyChangeNotify || AdminState == 1)
            ok(Result == (DWORD)-1, "COMPARE_ONLY result = 0x%lx\n", Result);
        ok(Token == (HANDLE)(ULONG_PTR)0x1234, "COMPARE_ONLY changed the output token to %p\n", Token);

        SetLastError(0xdeadbeef);
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, SAFER_TOKEN_COMPARE_ONLY, NULL);
        ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER,
           "COMPARE_ONLY without result: ret %d, error %lu\n", Ret, GetLastError());

        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_CONSTRAINED);
    if (Level)
    {
        Token = NULL;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, 0, NULL);
        ok(Ret && Token != NULL, "CONSTRAINED token: ret %d, error %lu\n", Ret, GetLastError());
        if (Ret && Token)
        {
            ok(IsTokenRestricted(Token), "CONSTRAINED token is not restricted\n");
            ok(RestrictedSidsContain(Token, RestrictedSid, &Count), "RESTRICTED is not a restricting SID\n");
            ok(!RestrictedSidsContain(Token, AdminSid, &Count), "Administrators is a restricting SID\n");
            if (User)
                ok(!RestrictedSidsContain(Token, User->User.Sid, &Count), "the user is a restricting SID\n");
            if (GroupState(ProcessToken, WorldSid) == 1)
                ok(RestrictedSidsContain(Token, WorldSid, &Count), "Everyone is not a restricting SID\n");
            ok(GroupState(Token, AdminSid) == (AdminState ? 2u : 0u), "CONSTRAINED Administrators state %lu\n",
               GroupState(Token, AdminSid));
            Count = PrivilegeCount(Token, &OnlyChangeNotify);
            ok(Count == 1 && OnlyChangeNotify, "CONSTRAINED privileges: count %lu\n", Count);
            ok(DefaultDaclGrants(Token, RestrictedSid), "CONSTRAINED default DACL does not grant RESTRICTED\n");
            if (User)
                ok(OwnerIsUser(Token, User->User.Sid), "CONSTRAINED default owner is not the user\n");
            CloseHandle(Token);
        }
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_UNTRUSTED);
    if (Level)
    {
        Token = NULL;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, 0, NULL);
        ok(Ret && Token != NULL, "UNTRUSTED token: ret %d, error %lu\n", Ret, GetLastError());
        if (Ret && Token)
        {
            ok(IsTokenRestricted(Token), "UNTRUSTED token is not restricted\n");
            ok(RestrictedSidsContain(Token, RestrictedSid, &Count), "RESTRICTED is not a restricting SID\n");
            ok(Count == 5, "UNTRUSTED restricting SID count = %lu\n", Count);
            ok(RestrictedSidsContain(Token, WorldSid, &Count), "Everyone is not a restricting SID\n");
            ok(RestrictedSidsContain(Token, UsersSid, &Count), "Users is not a restricting SID\n");
            ok(RestrictedSidsContain(Token, InteractiveSid, &Count), "INTERACTIVE is not a restricting SID\n");
            ok(RestrictedSidsContain(Token, AuthSid, &Count), "Authenticated Users is not a restricting SID\n");
            ok(GroupState(Token, AdminSid) == (AdminState ? 2u : 0u), "UNTRUSTED Administrators state %lu\n",
               GroupState(Token, AdminSid));
            Count = PrivilegeCount(Token, &OnlyChangeNotify);
            ok(Count == 1 && OnlyChangeNotify, "UNTRUSTED privileges: count %lu\n", Count);
            CloseHandle(Token);
        }
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_FULLYTRUSTED);
    if (Level)
    {
        Token = (HANDLE)(ULONG_PTR)0x1234;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, SAFER_TOKEN_NULL_IF_EQUAL, NULL);
        ok(Ret && Token == NULL, "FULLYTRUSTED NULL_IF_EQUAL: ret %d, token %p\n", Ret, Token);

        Result = 0xdeadbeef;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, SAFER_TOKEN_COMPARE_ONLY, &Result);
        ok(Ret && Result == 0, "FULLYTRUSTED COMPARE_ONLY: ret %d, result 0x%lx\n", Ret, Result);

        Token = NULL;
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, SAFER_TOKEN_MAKE_INERT, NULL);
        ok(Ret && Token != NULL, "FULLYTRUSTED MAKE_INERT: ret %d, error %lu\n", Ret, GetLastError());
        if (Ret && Token)
        {
            Inert = FALSE;
            ok(GetTokenInformation(Token, TokenSandBoxInert, &Inert, sizeof(Inert), &Size),
               "TokenSandBoxInert query failed: %lu\n", GetLastError());
            ok(Inert, "MAKE_INERT token is not sandbox inert\n");
            ok(!IsTokenRestricted(Token), "FULLYTRUSTED token is restricted\n");
            ok(GroupState(Token, AdminSid) == AdminState, "FULLYTRUSTED changed Administrators to %lu\n",
               GroupState(Token, AdminSid));
            CloseHandle(Token);
        }
        pSaferCloseLevel(Level);
    }

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_DISALLOWED);
    if (Level)
    {
        Token = (HANDLE)(ULONG_PTR)0x1234;
        SetLastError(0xdeadbeef);
        Ret = pSaferComputeTokenFromLevel(Level, ProcessToken, &Token, 0, NULL);
        ok(!Ret && GetLastError() == ERROR_ACCESS_DISABLED_BY_POLICY,
           "DISALLOWED token: ret %d, error %lu\n", Ret, GetLastError());
        pSaferCloseLevel(Level);
    }

    SetLastError(0xdeadbeef);
    Ret = pSaferComputeTokenFromLevel((SAFER_LEVEL_HANDLE)(ULONG_PTR)0x1234, NULL, &Token, 0, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "invalid level: ret %d, error %lu\n", Ret, GetLastError());

    HeapFree(GetProcessHeap(), 0, User);
    CloseHandle(ProcessToken);
}

static void
TestPolicyInformation(void)
{
    DWORD Levels[8], Size, Value, i, j;
    BOOL Ret, Found;

    SetLastError(0xdeadbeef);
    Ret = pSaferGetPolicyInformation(0, SaferPolicyLevelList, sizeof(Levels), Levels, &Size, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "scope 0: ret %d, error %lu\n", Ret, GetLastError());

    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_MACHINE, SaferPolicyLevelList, 0, NULL, &Size, NULL);
    ok(!Ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "level list size query: ret %d, error %lu\n", Ret, GetLastError());
    ok(Size == 5 * sizeof(DWORD), "level list size = %lu\n", Size);

    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_MACHINE, SaferPolicyLevelList, sizeof(Levels), Levels, &Size, NULL);
    ok(Ret && Size == 5 * sizeof(DWORD), "level list: ret %d, size %lu\n", Ret, Size);
    if (Ret)
    {
        for (i = 0; i < ARRAYSIZE(AllLevels); i++)
        {
            Found = FALSE;
            for (j = 0; j < Size / sizeof(DWORD); j++)
                Found = Found || Levels[j] == AllLevels[i];
            ok(Found, "level 0x%lx missing from the level list\n", AllLevels[i]);
        }
    }

    if (!PolicyValueExists(HKEY_LOCAL_MACHINE, L"DefaultLevel"))
    {
        SetLastError(0xdeadbeef);
        Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_MACHINE, SaferPolicyDefaultLevel, sizeof(Value), &Value,
                                         &Size, NULL);
        ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "default level without policy: ret %d, error %lu\n",
           Ret, GetLastError());
    }

    if (!PolicyValueExists(HKEY_LOCAL_MACHINE, L"TransparentEnabled"))
    {
        Value = 0xdeadbeef;
        Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_MACHINE, SaferPolicyEnableTransparentEnforcement,
                                         sizeof(Value), &Value, &Size, NULL);
        ok(Ret && Value == 0 && Size == sizeof(DWORD), "transparent enforcement: ret %d, value %lu, size %lu\n",
           Ret, Value, Size);
    }

    SetLastError(0xdeadbeef);
    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_MACHINE, (SAFER_POLICY_INFO_CLASS)0, sizeof(Value), &Value,
                                     &Size, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "class 0: ret %d, error %lu\n", Ret, GetLastError());
}

static void
TestExecutableFileType(void)
{
    static const struct
    {
        PCWSTR Path;
        BOOLEAN FromShellExecute;
        BOOL Expected;
    } Tests[] =
    {
        { L"C:\\Folder\\Program.EXE", FALSE, TRUE },
        { L"C:\\Folder\\Program.exe", TRUE, FALSE },
        { L"script.bat", FALSE, TRUE },
        { L"script.CMD", TRUE, TRUE },
        { L"a.com", FALSE, TRUE },
        { L"a.js", FALSE, TRUE },
        { L"a.lnk", FALSE, TRUE },
        { L"a.pif", FALSE, TRUE },
        { L"a.pl", FALSE, TRUE },
        { L"a.shs", FALSE, TRUE },
        { L"a.url", FALSE, TRUE },
        { L"a.Vbs", FALSE, TRUE },
        { L"readme.txt", FALSE, FALSE },
        { L"C:\\dir.bat\\noextension", FALSE, FALSE },
        { L"trailing.", FALSE, FALSE },
        { L"", FALSE, FALSE },
    };
    ULONG i;

    if (PolicyValueExists(HKEY_LOCAL_MACHINE, L"ExecutableTypes") ||
        PolicyValueExists(HKEY_CURRENT_USER, L"ExecutableTypes"))
    {
        skip("a designated file type policy is configured\n");
        return;
    }

    for (i = 0; i < ARRAYSIZE(Tests); i++)
    {
        ok(pSaferiIsExecutableFileType(Tests[i].Path, Tests[i].FromShellExecute) == Tests[i].Expected,
           "SaferiIsExecutableFileType(%S, %u) != %d\n", Tests[i].Path, Tests[i].FromShellExecute,
           Tests[i].Expected);
    }
    ok(!pSaferiIsExecutableFileType(NULL, FALSE), "NULL path is executable\n");
}

static void
TestIdentifyWithoutPolicy(void)
{
    SAFER_CODE_PROPERTIES_V2 Properties;
    SAFER_LEVEL_HANDLE Level;
    WCHAR Path[MAX_PATH];
    DWORD Value, Size;
    BOOL Ret;

    if (PolicyKeyExists(HKEY_LOCAL_MACHINE) || PolicyKeyExists(HKEY_CURRENT_USER))
    {
        skip("a software restriction policy is configured\n");
        return;
    }

    GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\cmd.exe");

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.cbSize = sizeof(SAFER_CODE_PROPERTIES_V1);
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEPATH | SAFER_CRITERIA_IMAGEHASH;
    Properties.ImagePath = Path;
    Ret = pSaferIdentifyLevel(1, (PSAFER_CODE_PROPERTIES)&Properties, &Level, NULL);
    ok(Ret, "SaferIdentifyLevel failed: %lu\n", GetLastError());
    if (Ret)
    {
        Value = 0xdeadbeef;
        Ret = pSaferGetLevelInformation(Level, SaferObjectLevelId, &Value, sizeof(Value), &Size);
        ok(Ret && Value == SAFER_LEVELID_FULLYTRUSTED, "level without policy = 0x%lx (ret %d)\n", Value, Ret);
        ok(pSaferRecordEventLogEntry(Level, Path, NULL), "SaferRecordEventLogEntry(FULLYTRUSTED) failed: %lu\n",
           GetLastError());
        pSaferCloseLevel(Level);
    }
}

static void
TestHashSearchWithoutRule(void)
{
    BYTE Hash[16] = { 0x50, 0x18, 0x51, 0x18, 0x52, 0x18, 0x53, 0x18, 0x54, 0x18, 0x55, 0x18, 0x56, 0x18, 0x57, 0x18 };
    DWORD Level = 0xdeadbeef;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSaferiSearchMatchingHashRules(CALG_MD5, Hash, sizeof(Hash), 1234, &Level, NULL);
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "unknown hash: ret %d, error %lu\n", Ret, GetLastError());
    ok(Level == 0xdeadbeef, "found level changed to 0x%lx\n", Level);

    SetLastError(0xdeadbeef);
    Ret = pSaferiSearchMatchingHashRules(CALG_MD5, NULL, sizeof(Hash), 1234, &Level, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "NULL hash: ret %d, error %lu\n", Ret, GetLastError());
}

static PEVENTLOGRECORD
NewestSaferEvent(PBYTE Buffer, DWORD BufferSize)
{
    PEVENTLOGRECORD Record;
    HANDLE Log;
    DWORD Read = 0, Needed = 0, Offset = 0;

    Log = OpenEventLogW(NULL, L"Application");
    if (!Log)
        return NULL;

    if (!ReadEventLogW(Log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, Buffer, BufferSize, &Read, &Needed))
        Read = 0;
    CloseEventLog(Log);

    while (Offset + sizeof(EVENTLOGRECORD) <= Read)
    {
        Record = (PEVENTLOGRECORD)(Buffer + Offset);
        if (!_wcsicmp((PCWSTR)(Record + 1), SAFER_EVENT_SOURCE))
            return Record;
        if (Record->Length == 0)
            break;
        Offset += Record->Length;
    }
    return NULL;
}

static void
TestRecordEventLogEntry(void)
{
    static const WCHAR Target[] = L"C:\\SaferApitest\\blocked-by-default.exe";
    SAFER_LEVEL_HANDLE Level;
    PEVENTLOGRECORD Record;
    PBYTE Buffer;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSaferRecordEventLogEntry((SAFER_LEVEL_HANDLE)(ULONG_PTR)0x1234, Target, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "invalid level: ret %d, error %lu\n", Ret, GetLastError());

    Level = CreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_DISALLOWED);
    if (!Level)
        return;

    Ret = pSaferRecordEventLogEntry(Level, Target, NULL);
    ok(Ret, "SaferRecordEventLogEntry failed: %lu\n", GetLastError());
    pSaferCloseLevel(Level);
    if (!Ret)
        return;

    Buffer = HeapAlloc(GetProcessHeap(), 0, 0x10000);
    if (!Buffer)
        return;

    Record = NewestSaferEvent(Buffer, 0x10000);
    ok(Record != NULL, "no software restriction event in the Application log\n");
    if (Record)
    {
        ok((Record->EventID & 0xFFFF) == 865, "event ID = %lu\n", Record->EventID & 0xFFFF);
        ok(Record->NumStrings >= 1, "event has %u strings\n", Record->NumStrings);
        if (Record->NumStrings >= 1)
            ok(!wcscmp((PCWSTR)((PBYTE)Record + Record->StringOffset), Target), "event target = %S\n",
               (PCWSTR)((PBYTE)Record + Record->StringOffset));
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
}

START_TEST(SaferLevels)
{
    if (!ResolveSafer())
    {
        skip("SAFER functions are not available\n");
        return;
    }

    TestCreateAndQueryLevels();
    TestTokenClasses();
    TestComputeToken();
    TestPolicyInformation();
    TestExecutableFileType();
    TestIdentifyWithoutPolicy();
    TestHashSearchWithoutRule();
    TestRecordEventLogEntry();
}
