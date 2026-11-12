/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for NtCreateKey
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include "precomp.h"
#include <winreg.h>

static
VOID
VerifyAccess_(
    _In_ HANDLE Handle,
    _In_ ACCESS_MASK ExpectedAccess,
    _In_ PCSTR File,
    _In_ INT Line)
{
    NTSTATUS Status;
    OBJECT_BASIC_INFORMATION BasicInfo;
    ULONG Length;

    Status = NtQueryObject(Handle,
                           ObjectBasicInformation,
                           &BasicInfo,
                           sizeof(BasicInfo),
                           &Length);
    ok_(File, Line)(Status == STATUS_SUCCESS, "NtQueryObject returned 0x%lx\n", Status);
    ok_(File, Line)(BasicInfo.GrantedAccess == ExpectedAccess,
                    "GrantedAccess is 0x%lx, expected 0x%lx\n",
                    BasicInfo.GrantedAccess, ExpectedAccess);
}
#define VerifyAccess(h, e) VerifyAccess_(h, e, __FILE__, __LINE__)

static
VOID
TestCreateOpen_(
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK ExpectedAccess,
    _In_ NTSTATUS ExpectedStatus,
    _In_ PCSTR File,
    _In_ INT Line)
{
    NTSTATUS Status;
    HANDLE KeyHandle;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software");
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtCreateKey(&KeyHandle,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    ok_(File, Line)(Status == ExpectedStatus,
                    "NtCreateKey returned 0x%lx, expected 0x%lx\n",
                    Status, ExpectedStatus);
    if (NT_SUCCESS(Status))
    {
        VerifyAccess_(KeyHandle, ExpectedAccess, File, Line);
        Status = NtClose(KeyHandle);
        ok_(File, Line)(Status == STATUS_SUCCESS,
                        "NtClose from NtCreateKey returned 0x%lx\n",
                        Status);
    }
    else if (NT_SUCCESS(ExpectedStatus))
    {
        skip_(File, Line)("NtCreateKey failed, skipping\n");
    }

    Status = NtOpenKey(&KeyHandle,
                       DesiredAccess,
                       &ObjectAttributes);
    ok_(File, Line)(Status == ExpectedStatus,
                    "NtOpenKey returned 0x%lx, expected 0x%lx\n",
                    Status, ExpectedStatus);
    if (NT_SUCCESS(Status))
    {
        VerifyAccess_(KeyHandle, ExpectedAccess, File, Line);
        Status = NtClose(KeyHandle);
        ok_(File, Line)(Status == STATUS_SUCCESS,
                        "NtClose from NtOpenKey returned 0x%lx\n",
                        Status);
    }
    else if (NT_SUCCESS(ExpectedStatus))
    {
        skip_(File, Line)("NtOpenKey failed, skipping\n");
    }
}
#define TestCreateOpen(d, ea, es) TestCreateOpen_(d, ea, es, __FILE__, __LINE__)

static
VOID
TestBackupOpen(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software");
    OBJECT_ATTRIBUTES ObjectAttributes;
    BOOLEAN OldBackup, OldRestore, Ignored;
    HANDLE KeyHandle;
    NTSTATUS Status;

    Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, FALSE, FALSE, &OldRestore);
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot adjust SeRestorePrivilege (0x%lx)\n", Status);
        return;
    }

    Status = RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &OldBackup);
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot enable SeBackupPrivilege (0x%lx)\n", Status);
        RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, OldRestore, FALSE, &Ignored);
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKeyEx(&KeyHandle, 0, &ObjectAttributes, REG_OPTION_BACKUP_RESTORE);
    ok(Status == STATUS_SUCCESS, "NtOpenKeyEx returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        VerifyAccess(KeyHandle, ACCESS_SYSTEM_SECURITY | KEY_READ);
        NtClose(KeyHandle);
    }

    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, OldBackup, FALSE, &Ignored);
    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, OldRestore, FALSE, &Ignored);
}

static
VOID
TestKeyDacl(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\RosTestsKeyDacl");
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    OBJECT_ATTRIBUTES ObjectAttributes;
    SECURITY_DESCRIPTOR Descriptor;
    UCHAR AclBuffer[64];
    PACL Acl = (PACL)AclBuffer;
    PSID World = NULL;
    HANDLE Created, KeyHandle;
    NTSTATUS Status;

    Status = RtlAllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World);
    ok(Status == STATUS_SUCCESS, "RtlAllocateAndInitializeSid returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status)) return;

    RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_QUERY_VALUE, World);
    RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, &Descriptor);
    Status = NtCreateKey(&Created, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok(Status == STATUS_SUCCESS, "NtCreateKey returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeSid(World);
        return;
    }
    VerifyAccess(Created, KEY_ALL_ACCESS);

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_SET_VALUE, &ObjectAttributes);
    ok(Status == STATUS_ACCESS_DENIED, "NtOpenKey(KEY_SET_VALUE) returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status)) NtClose(KeyHandle);

    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    ok(Status == STATUS_SUCCESS, "NtOpenKey(KEY_QUERY_VALUE) returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        VerifyAccess(KeyHandle, KEY_QUERY_VALUE);
        NtClose(KeyHandle);
    }

    Status = NtDeleteKey(Created);
    ok(Status == STATUS_SUCCESS, "NtDeleteKey returned 0x%lx\n", Status);
    NtClose(Created);
    RtlFreeSid(World);
}

static
VOID
TestLongLinkTarget(VOID)
{
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\RosTestsLongLink");
    UNICODE_STRING Through = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\RosTestsLongLink\\Beyond\\TheEnd");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SymbolicLinkValue");
    static const WCHAR Prefix[] = L"\\Registry\\Machine\\Software\\";
    OBJECT_ATTRIBUTES ObjectAttributes;
    const ULONG TargetLength = 0xFFF0;
    HANDLE Link, KeyHandle;
    PWCHAR Target;
    NTSTATUS Status;
    ULONG i;

    Target = RtlAllocateHeap(RtlGetProcessHeap(), 0, TargetLength);
    if (Target == NULL)
    {
        skip("Out of memory\n");
        return;
    }
    RtlCopyMemory(Target, Prefix, sizeof(Prefix) - sizeof(WCHAR));
    for (i = (sizeof(Prefix) - sizeof(WCHAR)) / sizeof(WCHAR); i < TargetLength / sizeof(WCHAR); i++)
        Target[i] = L'a';

    InitializeObjectAttributes(&ObjectAttributes, &LinkName, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, NULL, NULL);
    Status = NtCreateKey(&Link, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL,
                         REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK, NULL);
    ok(Status == STATUS_SUCCESS, "NtCreateKey(link) returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Target);
        return;
    }

    Status = NtSetValueKey(Link, &ValueName, 0, REG_LINK, Target, TargetLength);
    ok(Status == STATUS_SUCCESS, "NtSetValueKey returned 0x%lx\n", Status);

    InitializeObjectAttributes(&ObjectAttributes, &Through, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    ok(Status == STATUS_INVALID_PARAMETER || Status == STATUS_NAME_TOO_LONG,
       "NtOpenKey through the link returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status)) NtClose(KeyHandle);

    Status = NtDeleteKey(Link);
    ok(Status == STATUS_SUCCESS, "NtDeleteKey returned 0x%lx\n", Status);
    NtClose(Link);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Target);
}

START_TEST(NtCreateKey)
{
    TestCreateOpen(0, 0, STATUS_ACCESS_DENIED);
    TestCreateOpen(KEY_WOW64_32KEY, 0, STATUS_ACCESS_DENIED);
    TestCreateOpen(KEY_WOW64_64KEY, 0, STATUS_ACCESS_DENIED);
    TestCreateOpen(KEY_WOW64_32KEY | KEY_WOW64_64KEY, 0, STATUS_ACCESS_DENIED); // STATUS_INVALID_PARAMETER on Win7
    TestCreateOpen(READ_CONTROL, READ_CONTROL, STATUS_SUCCESS);
    TestCreateOpen(READ_CONTROL | KEY_WOW64_32KEY, READ_CONTROL, STATUS_SUCCESS);
    TestCreateOpen(READ_CONTROL | KEY_WOW64_64KEY, READ_CONTROL, STATUS_SUCCESS);
    TestCreateOpen(READ_CONTROL | KEY_WOW64_32KEY | KEY_WOW64_64KEY, READ_CONTROL, STATUS_SUCCESS); // STATUS_INVALID_PARAMETER on Win7
    TestBackupOpen();
    TestKeyDacl();
    TestLongLinkTarget();
}
