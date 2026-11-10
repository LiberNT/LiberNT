/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ZwOpenKey/ZwQueryValueKey API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'kCmK'

#define REUSE_VALUE_COUNT 48

static
NTSTATUS
CreateVolatileKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_opt_ PSECURITY_DESCRIPTOR Sd)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               Root,
                               Sd);
    return ZwCreateKey(KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL,
                       REG_OPTION_VOLATILE, NULL);
}

static
ULONG
ReuseValueLength(
    _In_ ULONG Index)
{
    return 0x30 + 8 * (Index % 24);
}

static
VOID
ReuseValueName(
    _Out_writes_(4) PWCHAR Name,
    _In_ ULONG Index)
{
    Name[0] = L'V';
    Name[1] = (WCHAR)(L'0' + Index / 10);
    Name[2] = (WCHAR)(L'0' + Index % 10);
    Name[3] = UNICODE_NULL;
}

/* A freed security cell must not go to the next key with the same descriptor */
static
VOID
TestFreedSecurityCell(VOID)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    ULONG SidBuffer[SID_MAX_SUB_AUTHORITIES + 2];
    PKEY_VALUE_PARTIAL_INFORMATION Partial;
    UCHAR Data[0x30 + 8 * 24];
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(Data)];
    UNICODE_STRING ValueName;
    SECURITY_DESCRIPTOR Sd;
    HANDLE Parent, Key;
    WCHAR Name[4];
    ULONG AclBuffer[32];
    PACL Acl = (PACL)AclBuffer;
    PSID Sid = (PSID)SidBuffer;
    ULONG ResultLength;
    ULONG Index, Byte;
    NTSTATUS Status;

    RtlInitializeSid(Sid, &NtAuthority, 4);
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_NT_NON_UNIQUE;
    *RtlSubAuthoritySid(Sid, 1) = TAG_TEST;
    *RtlSubAuthoritySid(Sid, 2) = (ULONG)KeQueryInterruptTime();
    *RtlSubAuthoritySid(Sid, 3) = 1;

    Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_ALL_ACCESS,
                                    SeExports->SeLocalSystemSid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, Sid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(&Sd, TRUE, Acl, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = CreateVolatileKey(&Parent, NULL,
                               L"\\Registry\\Machine\\SOFTWARE\\KmtFreedSecurityCell",
                               NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = CreateVolatileKey(&Key, Parent, L"First", &Sd);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwDeleteKey(Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Key);
    }

    RtlFillMemory(Data, sizeof(Data), 0xA5);
    for (Index = 0; Index < REUSE_VALUE_COUNT; Index++)
    {
        ReuseValueName(Name, Index);
        RtlInitUnicodeString(&ValueName, Name);
        Status = ZwSetValueKey(Parent, &ValueName, 0, REG_BINARY, Data,
                               ReuseValueLength(Index));
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Key = NULL;
    Status = CreateVolatileKey(&Key, Parent, L"Second", &Sd);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Partial = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    for (Index = 0; Index < REUSE_VALUE_COUNT; Index++)
    {
        ReuseValueName(Name, Index);
        RtlInitUnicodeString(&ValueName, Name);
        Status = ZwQueryValueKey(Parent, &ValueName, KeyValuePartialInformation,
                                 Partial, sizeof(Buffer), &ResultLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) continue;

        ok(Partial->Type == REG_BINARY, "value %lu changed type to %lu\n",
           Index, Partial->Type);
        ok(Partial->DataLength == ReuseValueLength(Index),
           "value %lu changed length to %lu\n", Index, Partial->DataLength);
        for (Byte = 0; Byte < Partial->DataLength && Byte < sizeof(Data); Byte++)
        {
            if (Partial->Data[Byte] != 0xA5) break;
        }
        ok(Byte == Partial->DataLength, "value %lu changed at byte %lu\n",
           Index, Byte);
    }

    if (Key != NULL)
    {
        Status = ZwDeleteKey(Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Key);
    }

    Status = ZwDeleteKey(Parent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Parent);
}

START_TEST(CmKeyKM)
{
    UNICODE_STRING Name, ValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION Partial;
    UCHAR Buffer[512];
    ULONG ResultLength = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&ValueName, L"SystemStartOptions");
    Partial = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Partial, sizeof(Buffer), &ResultLength);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW, "SystemStartOptions status %lx\n", Status);
    if (NT_SUCCESS(Status))
        ok_eq_ulong(Partial->Type, (ULONG)REG_SZ);

    RtlInitUnicodeString(&ValueName, L"KmtNoSuchValue");
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Partial, sizeof(Buffer), &ResultLength);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Status = ZwClose(KeyHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlInitUnicodeString(&Name, L"\\Registry\\Machine\\SYSTEM\\KmtNoSuchKey");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    TestFreedSecurityCell();
}
