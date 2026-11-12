/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the audit alarm functions of kernelbase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/setypes.h>

typedef BOOL (WINAPI *FN_AccessCheckAndAuditAlarmW)(LPCWSTR, LPVOID, LPWSTR, LPWSTR, PSECURITY_DESCRIPTOR,
                                                   DWORD, PGENERIC_MAPPING, BOOL, LPDWORD, LPBOOL, LPBOOL);
typedef BOOL (WINAPI *FN_ObjectCloseAuditAlarmW)(LPCWSTR, LPVOID, BOOL);
typedef BOOL (WINAPI *FN_ObjectOpenAuditAlarmW)(LPCWSTR, LPVOID, LPWSTR, LPWSTR, PSECURITY_DESCRIPTOR, HANDLE,
                                               DWORD, DWORD, PPRIVILEGE_SET, BOOL, BOOL, LPBOOL);
typedef BOOL (WINAPI *FN_ObjectPrivilegeAuditAlarmW)(LPCWSTR, LPVOID, HANDLE, DWORD, PPRIVILEGE_SET, BOOL);
typedef BOOL (WINAPI *FN_PrivilegedServiceAuditAlarmW)(LPCWSTR, LPCWSTR, HANDLE, PPRIVILEGE_SET, BOOL);

static FN_AccessCheckAndAuditAlarmW pAccessCheckAndAuditAlarmW;
static FN_ObjectCloseAuditAlarmW pObjectCloseAuditAlarmW;
static FN_ObjectCloseAuditAlarmW pObjectDeleteAuditAlarmW;
static FN_ObjectOpenAuditAlarmW pObjectOpenAuditAlarmW;
static FN_ObjectPrivilegeAuditAlarmW pObjectPrivilegeAuditAlarmW;
static FN_PrivilegedServiceAuditAlarmW pPrivilegedServiceAuditAlarmW;

static GENERIC_MAPPING FileMapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
static WCHAR Subsystem[] = L"LiberNT audit test";
static WCHAR ObjectType[] = L"Test object";
static WCHAR ObjectName[] = L"AuditAlarmApis";
static WCHAR ServiceName[] = L"AuditAlarmApis service";

typedef struct _TEST_SD
{
    SECURITY_DESCRIPTOR Sd;
    ULONG Guests[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Acl[64];
} TEST_SD;

static
VOID
BuildTestSd(
    _Out_ TEST_SD *Test)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    ULONG World[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    PACL Acl = (PACL)Test->Acl;

    ok(InitializeSid(Test->Guests, &NtAuthority, 2), "InitializeSid failed: %lu\n", GetLastError());
    *GetSidSubAuthority(Test->Guests, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *GetSidSubAuthority(Test->Guests, 1) = DOMAIN_ALIAS_RID_GUESTS;
    ok(InitializeSid(World, &WorldAuthority, 1), "InitializeSid failed: %lu\n", GetLastError());
    *GetSidSubAuthority(World, 0) = SECURITY_WORLD_RID;

    ok(InitializeAcl(Acl, sizeof(Test->Acl), ACL_REVISION), "InitializeAcl failed: %lu\n", GetLastError());
    ok(AddAccessAllowedAce(Acl, ACL_REVISION, FILE_GENERIC_READ, World),
       "AddAccessAllowedAce failed: %lu\n", GetLastError());
    ok(InitializeSecurityDescriptor(&Test->Sd, SECURITY_DESCRIPTOR_REVISION),
       "InitializeSecurityDescriptor failed: %lu\n", GetLastError());
    ok(SetSecurityDescriptorDacl(&Test->Sd, TRUE, Acl, FALSE), "SetSecurityDescriptorDacl failed: %lu\n", GetLastError());
    ok(SetSecurityDescriptorOwner(&Test->Sd, Test->Guests, FALSE),
       "SetSecurityDescriptorOwner failed: %lu\n", GetLastError());
    ok(SetSecurityDescriptorGroup(&Test->Sd, Test->Guests, FALSE),
       "SetSecurityDescriptorGroup failed: %lu\n", GetLastError());
}

static
BOOL
SetAuditPrivilege(
    _In_ HANDLE Token,
    _In_ BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Luid.LowPart = SE_AUDIT_PRIVILEGE;
    Privileges.Privileges[0].Luid.HighPart = 0;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;

    SetLastError(0xdeadbeef);
    if (!AdjustTokenPrivileges(Token, FALSE, &Privileges, sizeof(Privileges), NULL, NULL))
        return FALSE;
    return GetLastError() == ERROR_SUCCESS;
}

static
VOID
InitPrivilegeSet(
    _Out_ PPRIVILEGE_SET Privileges)
{
    Privileges->PrivilegeCount = 1;
    Privileges->Control = PRIVILEGE_SET_ALL_NECESSARY;
    Privileges->Privilege[0].Luid.LowPart = SE_BACKUP_PRIVILEGE;
    Privileges->Privilege[0].Luid.HighPart = 0;
    Privileges->Privilege[0].Attributes = SE_PRIVILEGE_USED_FOR_ACCESS;
}

static
BOOL
CheckAndAudit(
    _In_ PSECURITY_DESCRIPTOR Sd,
    _In_opt_ LPWSTR Name,
    _In_ DWORD Desired,
    _Out_ DWORD *Granted,
    _Out_ BOOL *Status,
    _Out_ BOOL *OnClose)
{
    PVOID HandleId = Sd;

    *Granted = 0xdeadbeef;
    *Status = *OnClose = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    return pAccessCheckAndAuditAlarmW(Subsystem, &HandleId, ObjectType, Name, Sd, Desired, &FileMapping, FALSE,
                                      Granted, Status, OnClose);
}

static
VOID
TestAccessCheckAndAuditAlarm(VOID)
{
    TEST_SD Test, NoOwner;
    HANDLE ThreadToken;
    BOOL Ret, Status, OnClose;
    DWORD Granted;

    BuildTestSd(&Test);
    BuildTestSd(&NoOwner);
    ok(SetSecurityDescriptorOwner(&NoOwner.Sd, NULL, FALSE), "SetSecurityDescriptorOwner failed: %lu\n", GetLastError());
    ok(SetSecurityDescriptorGroup(&NoOwner.Sd, NULL, FALSE), "SetSecurityDescriptorGroup failed: %lu\n", GetLastError());

    /* The check is made for a client, so a thread that is not impersonating one is refused */
    Ret = CheckAndAudit(&Test.Sd, ObjectName, FILE_READ_DATA, &Granted, &Status, &OnClose);
    ok(!Ret, "AccessCheckAndAuditAlarmW succeeded without impersonation\n");
    ok(GetLastError() == ERROR_NO_IMPERSONATION_TOKEN, "Unexpected error %lu\n", GetLastError());
    ok(Granted == 0xdeadbeef, "GrantedAccess 0x%lx\n", Granted);

    ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());

    Ret = CheckAndAudit(&Test.Sd, ObjectName, FILE_READ_DATA, &Granted, &Status, &OnClose);
    ok(Ret, "AccessCheckAndAuditAlarmW failed: %lu\n", GetLastError());
    ok(Status == TRUE, "AccessStatus %d\n", Status);
    ok(Granted == FILE_READ_DATA, "GrantedAccess 0x%lx\n", Granted);
    ok(OnClose == FALSE, "GenerateOnClose %d\n", OnClose);

    Ret = CheckAndAudit(&Test.Sd, NULL, FILE_READ_DATA, &Granted, &Status, &OnClose);
    ok(Ret, "AccessCheckAndAuditAlarmW without object name failed: %lu\n", GetLastError());
    ok(Status == TRUE, "AccessStatus %d\n", Status);
    ok(Granted == FILE_READ_DATA, "GrantedAccess 0x%lx\n", Granted);
    ok(OnClose == FALSE, "GenerateOnClose %d\n", OnClose);

    Ret = CheckAndAudit(&Test.Sd, ObjectName, FILE_WRITE_DATA, &Granted, &Status, &OnClose);
    ok(Ret, "AccessCheckAndAuditAlarmW failed: %lu\n", GetLastError());
    ok(Status == FALSE, "AccessStatus %d\n", Status);
    ok(Granted == 0, "GrantedAccess 0x%lx\n", Granted);
    ok(OnClose == FALSE, "GenerateOnClose %d\n", OnClose);

    /* A descriptor without owner and group is refused and the access mask is left alone */
    Ret = CheckAndAudit(&NoOwner.Sd, ObjectName, FILE_READ_DATA, &Granted, &Status, &OnClose);
    ok(!Ret, "AccessCheckAndAuditAlarmW succeeded without owner\n");
    ok(GetLastError() == ERROR_INVALID_SECURITY_DESCR, "Unexpected error %lu\n", GetLastError());
    ok(Granted == 0xdeadbeef, "GrantedAccess 0x%lx\n", Granted);

    /* The audit privilege is looked for in the primary token, not in the client's */
    ok(OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &ThreadToken),
       "OpenThreadToken failed: %lu\n", GetLastError());
    ok(SetAuditPrivilege(ThreadToken, FALSE), "Disabling the client's audit privilege failed\n");
    Ret = CheckAndAudit(&Test.Sd, ObjectName, FILE_READ_DATA, &Granted, &Status, &OnClose);
    ok(Ret, "AccessCheckAndAuditAlarmW failed for a client without the privilege: %lu\n", GetLastError());
    ok(Status == TRUE, "AccessStatus %d\n", Status);
    ok(Granted == FILE_READ_DATA, "GrantedAccess 0x%lx\n", Granted);
    CloseHandle(ThreadToken);

    ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());
}

static
VOID
TestClientTokenAccess(VOID)
{
    TEST_SD Test;
    PVOID HandleId = &Test;
    PRIVILEGE_SET Privileges;
    HANDLE NoQueryToken;
    BOOL Ret, OnClose;

    BuildTestSd(&Test);
    InitPrivilegeSet(&Privileges);

    ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());
    ok(OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &NoQueryToken),
       "OpenThreadToken failed: %lu\n", GetLastError());
    ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());

    /* The client token has to be open for TOKEN_QUERY */
    OnClose = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = pObjectOpenAuditAlarmW(Subsystem, &HandleId, ObjectType, ObjectName, &Test.Sd, NoQueryToken,
                                 FILE_READ_DATA, FILE_READ_DATA, &Privileges, FALSE, TRUE, &OnClose);
    ok(!Ret, "ObjectOpenAuditAlarmW succeeded without TOKEN_QUERY\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectPrivilegeAuditAlarmW(Subsystem, &HandleId, NoQueryToken, FILE_READ_DATA, &Privileges, TRUE);
    ok(!Ret, "ObjectPrivilegeAuditAlarmW succeeded without TOKEN_QUERY\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pPrivilegedServiceAuditAlarmW(Subsystem, ServiceName, NoQueryToken, &Privileges, TRUE);
    ok(!Ret, "PrivilegedServiceAuditAlarmW succeeded without TOKEN_QUERY\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Unexpected error %lu\n", GetLastError());

    CloseHandle(NoQueryToken);
}

static
VOID
TestAlarms(
    _In_ HANDLE ClientToken)
{
    TEST_SD Test;
    PVOID HandleId = &Test;
    PRIVILEGE_SET Privileges;
    BOOL Ret, OnClose;

    BuildTestSd(&Test);
    InitPrivilegeSet(&Privileges);

    OnClose = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = pObjectOpenAuditAlarmW(Subsystem, &HandleId, ObjectType, ObjectName, &Test.Sd, ClientToken,
                                 FILE_READ_DATA, FILE_READ_DATA, &Privileges, FALSE, TRUE, &OnClose);
    ok(Ret, "ObjectOpenAuditAlarmW failed: %lu\n", GetLastError());
    ok(OnClose == FALSE, "GenerateOnClose %d\n", OnClose);

    OnClose = 0xdeadbeef;
    Ret = pObjectOpenAuditAlarmW(Subsystem, &HandleId, ObjectType, NULL, &Test.Sd, ClientToken,
                                 FILE_WRITE_DATA, 0, NULL, TRUE, FALSE, &OnClose);
    ok(Ret, "ObjectOpenAuditAlarmW of a denied access failed: %lu\n", GetLastError());
    ok(OnClose == FALSE, "GenerateOnClose %d\n", OnClose);

    SetLastError(0xdeadbeef);
    Ret = pObjectPrivilegeAuditAlarmW(Subsystem, &HandleId, ClientToken, FILE_READ_DATA, &Privileges, TRUE);
    ok(Ret, "ObjectPrivilegeAuditAlarmW failed: %lu\n", GetLastError());
    Ret = pObjectPrivilegeAuditAlarmW(Subsystem, &HandleId, ClientToken, FILE_READ_DATA, &Privileges, FALSE);
    ok(Ret, "ObjectPrivilegeAuditAlarmW of a failed use failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pPrivilegedServiceAuditAlarmW(Subsystem, ServiceName, ClientToken, &Privileges, TRUE);
    ok(Ret, "PrivilegedServiceAuditAlarmW failed: %lu\n", GetLastError());
    Ret = pPrivilegedServiceAuditAlarmW(Subsystem, ServiceName, ClientToken, &Privileges, FALSE);
    ok(Ret, "PrivilegedServiceAuditAlarmW of a failed use failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectCloseAuditAlarmW(Subsystem, &HandleId, FALSE);
    ok(Ret, "ObjectCloseAuditAlarmW failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectDeleteAuditAlarmW(Subsystem, &HandleId, FALSE);
    ok(Ret, "ObjectDeleteAuditAlarmW failed: %lu\n", GetLastError());
}

static
VOID
TestAlarmsRefused(
    _In_ HANDLE ClientToken,
    _In_ BOOL Impersonating)
{
    TEST_SD Test;
    PVOID HandleId = &Test;
    PRIVILEGE_SET Privileges;
    BOOL Ret, Status, OnClose;
    DWORD Granted;

    BuildTestSd(&Test);
    InitPrivilegeSet(&Privileges);

    if (Impersonating)
    {
        Ret = CheckAndAudit(&Test.Sd, ObjectName, FILE_READ_DATA, &Granted, &Status, &OnClose);
        ok(!Ret, "AccessCheckAndAuditAlarmW succeeded\n");
        ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());
        ok(Granted == 0xdeadbeef, "GrantedAccess 0x%lx\n", Granted);
    }

    OnClose = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = pObjectOpenAuditAlarmW(Subsystem, &HandleId, ObjectType, ObjectName, &Test.Sd, ClientToken,
                                 FILE_READ_DATA, FILE_READ_DATA, &Privileges, FALSE, TRUE, &OnClose);
    ok(!Ret, "ObjectOpenAuditAlarmW succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectPrivilegeAuditAlarmW(Subsystem, &HandleId, ClientToken, FILE_READ_DATA, &Privileges, TRUE);
    ok(!Ret, "ObjectPrivilegeAuditAlarmW succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pPrivilegedServiceAuditAlarmW(Subsystem, ServiceName, ClientToken, &Privileges, TRUE);
    ok(!Ret, "PrivilegedServiceAuditAlarmW succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectCloseAuditAlarmW(Subsystem, &HandleId, FALSE);
    ok(!Ret, "ObjectCloseAuditAlarmW succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pObjectDeleteAuditAlarmW(Subsystem, &HandleId, FALSE);
    ok(!Ret, "ObjectDeleteAuditAlarmW succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected error %lu\n", GetLastError());
}

static
VOID
TraceCaller(
    _In_ HANDLE Token)
{
    ULONG Buffer[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE) / sizeof(ULONG)];
    PTOKEN_USER User = (PTOKEN_USER)Buffer;
    DWORD Length, Error = GetLastError();

    if (GetTokenInformation(Token, TokenUser, User, sizeof(Buffer), &Length))
    {
        trace("Audit privilege not enabled (error %lu), caller is LocalSystem: %d\n",
              Error, IsWellKnownSid(User->User.Sid, WinLocalSystemSid));
    }
}

START_TEST(AuditAlarmApis)
{
    HMODULE KernelBase = LoadLibraryW(L"kernelbase.dll");
    HANDLE ProcessToken, ClientToken;

    ok(KernelBase != NULL, "LoadLibraryW(kernelbase) failed: %lu\n", GetLastError());
    if (!KernelBase)
        return;

    pAccessCheckAndAuditAlarmW = (FN_AccessCheckAndAuditAlarmW)GetProcAddress(KernelBase, "AccessCheckAndAuditAlarmW");
    pObjectCloseAuditAlarmW = (FN_ObjectCloseAuditAlarmW)GetProcAddress(KernelBase, "ObjectCloseAuditAlarmW");
    pObjectDeleteAuditAlarmW = (FN_ObjectCloseAuditAlarmW)GetProcAddress(KernelBase, "ObjectDeleteAuditAlarmW");
    pObjectOpenAuditAlarmW = (FN_ObjectOpenAuditAlarmW)GetProcAddress(KernelBase, "ObjectOpenAuditAlarmW");
    pObjectPrivilegeAuditAlarmW = (FN_ObjectPrivilegeAuditAlarmW)GetProcAddress(KernelBase, "ObjectPrivilegeAuditAlarmW");
    pPrivilegedServiceAuditAlarmW = (FN_PrivilegedServiceAuditAlarmW)GetProcAddress(KernelBase, "PrivilegedServiceAuditAlarmW");
    if (!pAccessCheckAndAuditAlarmW || !pObjectCloseAuditAlarmW || !pObjectDeleteAuditAlarmW ||
        !pObjectOpenAuditAlarmW || !pObjectPrivilegeAuditAlarmW || !pPrivilegedServiceAuditAlarmW)
    {
        skip("Audit alarm functions not exported by kernelbase\n");
        return;
    }

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &ProcessToken),
       "OpenProcessToken failed: %lu\n", GetLastError());

    ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());
    ok(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &ClientToken),
       "OpenThreadToken failed: %lu\n", GetLastError());
    ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());

    if (!SetAuditPrivilege(ProcessToken, TRUE))
    {
        TraceCaller(ProcessToken);
        TestAlarmsRefused(ClientToken, FALSE);
        ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());
        TestAlarmsRefused(ClientToken, TRUE);
        ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());
        skip("The caller does not hold the audit privilege\n");
        CloseHandle(ClientToken);
        CloseHandle(ProcessToken);
        return;
    }

    TestAccessCheckAndAuditAlarm();
    TestAlarms(ClientToken);
    TestClientTokenAccess();

    /* The audit privilege has to be enabled in the primary token */
    ok(SetAuditPrivilege(ProcessToken, FALSE), "Disabling the audit privilege failed\n");
    TestAlarmsRefused(ClientToken, FALSE);
    ok(SetAuditPrivilege(ProcessToken, TRUE), "Enabling the audit privilege failed\n");

    /* An impersonation token that holds it does not stand in for the primary token */
    ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());
    ok(SetAuditPrivilege(ProcessToken, FALSE), "Disabling the audit privilege failed\n");
    TestAlarmsRefused(ClientToken, TRUE);
    ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());
    ok(SetAuditPrivilege(ProcessToken, TRUE), "Enabling the audit privilege failed\n");

    CloseHandle(ClientToken);
    CloseHandle(ProcessToken);
}
