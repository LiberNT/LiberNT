/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User mode tests for the access checks and alarms a subsystem audits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/exfuncs.h>
#include <ndk/sefuncs.h>
#include <ndk/setypes.h>

NTSTATUS
NTAPI
NtAccessCheckByTypeAndAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ AUDIT_EVENT_TYPE AuditType,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose);

NTSTATUS
NTAPI
NtAccessCheckByTypeResultListAndAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ AUDIT_EVENT_TYPE AuditType,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose);

static UNICODE_STRING Subsystem = RTL_CONSTANT_STRING(L"LiberNT audit test");
static UNICODE_STRING ObjectType = RTL_CONSTANT_STRING(L"Test object");
static UNICODE_STRING ObjectName = RTL_CONSTANT_STRING(L"SeAuditAlarm");
static UNICODE_STRING ServiceName = RTL_CONSTANT_STRING(L"SeAuditAlarm service");
static GENERIC_MAPPING FileMapping =
{
    FILE_GENERIC_READ,
    FILE_GENERIC_WRITE,
    FILE_GENERIC_EXECUTE,
    FILE_ALL_ACCESS
};
static GUID TestObjectGuid = {0x6d2a4b7e, 0x31c2, 0x4a59, {0x9e, 0x04, 0x5b, 0x1c, 0x77, 0x0a, 0xd3, 0x41}};

/* World may read; the owner and the group are the guests, so nobody here owns it */
typedef struct _TEST_SD
{
    SECURITY_DESCRIPTOR Sd;
    ULONG Guests[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG World[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Acl[64];
} TEST_SD;

static
VOID
BuildTestSd(
    _Out_ TEST_SD *Test)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    PACL Acl = (PACL)Test->Acl;

    RtlInitializeSid(Test->Guests, &NtAuthority, 2);
    *RtlSubAuthoritySid(Test->Guests, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(Test->Guests, 1) = DOMAIN_ALIAS_RID_GUESTS;
    RtlInitializeSid(Test->World, &WorldAuthority, 1);
    *RtlSubAuthoritySid(Test->World, 0) = SECURITY_WORLD_RID;

    ok_eq_hex(RtlCreateAcl(Acl, sizeof(Test->Acl), ACL_REVISION), STATUS_SUCCESS);
    ok_eq_hex(RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_GENERIC_READ, Test->World), STATUS_SUCCESS);
    ok_eq_hex(RtlCreateSecurityDescriptor(&Test->Sd, SECURITY_DESCRIPTOR_REVISION), STATUS_SUCCESS);
    ok_eq_hex(RtlSetDaclSecurityDescriptor(&Test->Sd, TRUE, Acl, FALSE), STATUS_SUCCESS);
    ok_eq_hex(RtlSetOwnerSecurityDescriptor(&Test->Sd, Test->Guests, FALSE), STATUS_SUCCESS);
    ok_eq_hex(RtlSetGroupSecurityDescriptor(&Test->Sd, Test->Guests, FALSE), STATUS_SUCCESS);
}

static
BOOLEAN
StartImpersonatingSelf(VOID)
{
    NTSTATUS Status = RtlImpersonateSelf(SecurityImpersonation);

    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status);
}

static
VOID
StopImpersonating(VOID)
{
    HANDLE None = NULL;

    NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &None, sizeof(None));
}

/* One audited check of a single right, through the plain, the typed and the list forms */
static
VOID
CheckAudited(
    _In_ ACCESS_MASK Desired,
    _In_ NTSTATUS ExpectedStatus,
    _In_ NTSTATUS ExpectedAccessStatus,
    _In_ ACCESS_MASK ExpectedGranted)
{
    OBJECT_TYPE_LIST Types[1];
    TEST_SD Test;
    PVOID HandleId = &Test;
    ACCESS_MASK Granted;
    NTSTATUS AccessStatus;
    BOOLEAN OnClose;
    NTSTATUS Status;

    BuildTestSd(&Test);

    Granted = 0xdeadbeef;
    AccessStatus = 0xdeadbeef;
    OnClose = 0x55;
    Status = NtAccessCheckAndAuditAlarm(&Subsystem, HandleId, &ObjectType, &ObjectName, &Test.Sd,
                                        Desired, &FileMapping, FALSE, &Granted, &AccessStatus, &OnClose);
    ok_eq_hex(Status, ExpectedStatus);
    if (NT_SUCCESS(ExpectedStatus))
    {
        ok_eq_hex(AccessStatus, ExpectedAccessStatus);
        ok_eq_hex(Granted, ExpectedGranted);
        ok_eq_uint(OnClose, FALSE);
    }

    Granted = 0xdeadbeef;
    AccessStatus = 0xdeadbeef;
    Status = NtAccessCheckByTypeAndAuditAlarm(&Subsystem, HandleId, &ObjectType, &ObjectName, &Test.Sd,
                                              NULL, Desired, AuditEventObjectAccess, 0, NULL, 0,
                                              &FileMapping, FALSE, &Granted, &AccessStatus, &OnClose);
    ok_eq_hex(Status, ExpectedStatus);
    if (NT_SUCCESS(ExpectedStatus))
    {
        ok_eq_hex(AccessStatus, ExpectedAccessStatus);
        ok_eq_hex(Granted, ExpectedGranted);
    }

    Types[0].Level = ACCESS_OBJECT_GUID;
    Types[0].Sbz = 0;
    Types[0].ObjectType = &TestObjectGuid;
    Granted = 0xdeadbeef;
    AccessStatus = 0xdeadbeef;
    Status = NtAccessCheckByTypeResultListAndAuditAlarm(&Subsystem, HandleId, &ObjectType, &ObjectName,
                                                        &Test.Sd, NULL, Desired, AuditEventObjectAccess, 0,
                                                        Types, RTL_NUMBER_OF(Types), &FileMapping, FALSE,
                                                        &Granted, &AccessStatus, &OnClose);
    ok_eq_hex(Status, ExpectedStatus);
    if (NT_SUCCESS(ExpectedStatus))
    {
        ok_eq_hex(AccessStatus, ExpectedAccessStatus);
        ok_eq_hex(Granted, ExpectedGranted);
    }
}

/* K0: the audited checks work from user mode, and need no privilege set of the caller's */
static
VOID
TestAuditedChecks(VOID)
{
    if (!StartImpersonatingSelf()) return;

    CheckAudited(FILE_READ_DATA, STATUS_SUCCESS, STATUS_SUCCESS, FILE_READ_DATA);
    CheckAudited(FILE_WRITE_DATA, STATUS_SUCCESS, STATUS_ACCESS_DENIED, 0);

    /* Two privileges carry these rights, which the services have no set to report in */
    CheckAudited(ACCESS_SYSTEM_SECURITY | WRITE_OWNER, STATUS_SUCCESS, STATUS_SUCCESS,
                 ACCESS_SYSTEM_SECURITY | WRITE_OWNER);

    StopImpersonating();
}

/* K1: asking in a client's name needs a client, so a thread wearing none is refused */
static
VOID
TestNotImpersonating(VOID)
{
    CheckAudited(FILE_READ_DATA, STATUS_NO_IMPERSONATION_TOKEN, 0, 0);
}

static
BOOLEAN
SetAuditPrivilege(
    _In_ HANDLE Token,
    _In_ BOOLEAN Enable)
{
    TOKEN_PRIVILEGES Privileges;
    NTSTATUS Status;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Luid.LowPart = SE_AUDIT_PRIVILEGE;
    Privileges.Privileges[0].Luid.HighPart = 0;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;

    Status = NtAdjustPrivilegesToken(Token, FALSE, &Privileges, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return Status == STATUS_SUCCESS;
}

/* Every alarm a caller raises for the client it serves, with its result */
static
VOID
CheckAlarms(
    _In_ HANDLE ClientToken,
    _In_ NTSTATUS Expected)
{
    PRIVILEGE_SET Privileges;
    TEST_SD Test;
    PVOID HandleId = &Test;
    BOOLEAN OnClose;
    NTSTATUS Status;

    BuildTestSd(&Test);

    Privileges.PrivilegeCount = 1;
    Privileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
    Privileges.Privilege[0].Luid.LowPart = SE_BACKUP_PRIVILEGE;
    Privileges.Privilege[0].Luid.HighPart = 0;
    Privileges.Privilege[0].Attributes = SE_PRIVILEGE_USED_FOR_ACCESS;

    Status = NtOpenObjectAuditAlarm(&Subsystem, HandleId, &ObjectType, &ObjectName, &Test.Sd, ClientToken,
                                    FILE_READ_DATA, FILE_READ_DATA, &Privileges, FALSE, TRUE, &OnClose);
    ok(Status == Expected, "NtOpenObjectAuditAlarm: 0x%08lx, expected 0x%08lx\n", Status, Expected);

    Status = NtPrivilegeObjectAuditAlarm(&Subsystem, HandleId, ClientToken, FILE_READ_DATA, &Privileges, TRUE);
    ok(Status == Expected, "NtPrivilegeObjectAuditAlarm: 0x%08lx, expected 0x%08lx\n", Status, Expected);

    Status = NtPrivilegedServiceAuditAlarm(&Subsystem, &ServiceName, ClientToken, &Privileges, TRUE);
    ok(Status == Expected, "NtPrivilegedServiceAuditAlarm: 0x%08lx, expected 0x%08lx\n", Status, Expected);
}

/* K2: the audit privilege is asked of the caller's own process, never of the client it wears */
static
VOID
TestPrimaryTokenPrivilege(VOID)
{
    TEST_SD Test;
    PVOID HandleId = &Test;
    HANDLE Process = NULL;
    HANDLE Thread = NULL;
    ACCESS_MASK Granted;
    NTSTATUS AccessStatus;
    BOOLEAN OnClose;
    NTSTATUS Status;

    BuildTestSd(&Test);

    /* A client without the privilege is the ordinary case and is served */
    if (!StartImpersonatingSelf()) return;
    Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && SetAuditPrivilege(Thread, FALSE))
    {
        CheckAudited(FILE_READ_DATA, STATUS_SUCCESS, STATUS_SUCCESS, FILE_READ_DATA);
    }
    if (Thread != NULL) NtClose(Thread);
    StopImpersonating();

    /* A process without it is refused whatever its client holds */
    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Process);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    if (!StartImpersonatingSelf())
    {
        NtClose(Process);
        return;
    }

    Thread = NULL;
    Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status) && SetAuditPrivilege(Process, FALSE))
    {
        CheckAudited(FILE_READ_DATA, STATUS_PRIVILEGE_NOT_HELD, 0, 0);
        CheckAlarms(Thread, STATUS_PRIVILEGE_NOT_HELD);

        Status = NtCloseObjectAuditAlarm(&Subsystem, HandleId, FALSE);
        ok_eq_hex(Status, STATUS_PRIVILEGE_NOT_HELD);
        Status = NtDeleteObjectAuditAlarm(&Subsystem, HandleId, FALSE);
        ok_eq_hex(Status, STATUS_PRIVILEGE_NOT_HELD);

        /* A caller may ask for the check alone when it cannot have it audited */
        Granted = 0xdeadbeef;
        AccessStatus = 0xdeadbeef;
        Status = NtAccessCheckByTypeAndAuditAlarm(&Subsystem, HandleId, &ObjectType, &ObjectName, &Test.Sd,
                                                  NULL, FILE_READ_DATA, AuditEventObjectAccess,
                                                  AUDIT_ALLOW_NO_PRIVILEGE, NULL, 0, &FileMapping, FALSE,
                                                  &Granted, &AccessStatus, &OnClose);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(AccessStatus, STATUS_SUCCESS);
        ok_eq_hex(Granted, FILE_READ_DATA);

        SetAuditPrivilege(Process, TRUE);
    }

    if (Thread != NULL) NtClose(Thread);
    StopImpersonating();
    NtClose(Process);
}

/* K3: the client token an alarm names has to be one the caller may query */
static
VOID
TestAlarmClientToken(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Query = NULL;
    HANDLE NoQuery = NULL;
    HANDLE Event = NULL;
    NTSTATUS Status;

    if (!StartImpersonatingSelf()) return;

    Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &Query);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE, TRUE, &NoQuery);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (Query != NULL) CheckAlarms(Query, STATUS_SUCCESS);
    if (NoQuery != NULL) CheckAlarms(NoQuery, STATUS_ACCESS_DENIED);
    if (Event != NULL) CheckAlarms(Event, STATUS_OBJECT_TYPE_MISMATCH);

    if (Event != NULL) NtClose(Event);
    if (NoQuery != NULL) NtClose(NoQuery);
    if (Query != NULL) NtClose(Query);
    StopImpersonating();
}

START_TEST(SeAuditAlarm)
{
    TestAuditedChecks();
    TestNotImpersonating();
    TestPrimaryTokenPrivilege();
    TestAlarmClientToken();
}
