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
CreateKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_opt_ PSECURITY_DESCRIPTOR Sd,
    _In_ ULONG Options)
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
                       Options, NULL);
}

static
NTSTATUS
CreateVolatileKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_opt_ PSECURITY_DESCRIPTOR Sd)
{
    return CreateKey(KeyHandle, Root, Name, Sd, REG_OPTION_VOLATILE);
}

/* A descriptor no other key holds: SYSTEM may do anything, Sid may read */
static
VOID
BuildUniqueDescriptor(
    _Out_ PSECURITY_DESCRIPTOR Sd,
    _Out_writes_bytes_(AclSize) PACL Acl,
    _In_ ULONG AclSize,
    _Out_writes_bytes_(RtlLengthRequiredSid(4)) PSID Sid,
    _In_ ULONG Which)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    NTSTATUS Status;

    RtlInitializeSid(Sid, &NtAuthority, 4);
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_NT_NON_UNIQUE;
    *RtlSubAuthoritySid(Sid, 1) = TAG_TEST;
    *RtlSubAuthoritySid(Sid, 2) = (ULONG)KeQueryInterruptTime();
    *RtlSubAuthoritySid(Sid, 3) = Which;

    Status = RtlCreateAcl(Acl, AclSize, ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_ALL_ACCESS,
                                    SeExports->SeLocalSystemSid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, Sid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlCreateSecurityDescriptor(Sd, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Acl, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
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
    ULONG SidBuffer[SID_MAX_SUB_AUTHORITIES + 2];
    PKEY_VALUE_PARTIAL_INFORMATION Partial;
    UCHAR Data[0x30 + 8 * 24];
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(Data)];
    UNICODE_STRING ValueName;
    SECURITY_DESCRIPTOR Sd;
    HANDLE Parent, Key;
    WCHAR Name[4];
    ULONG AclBuffer[32];
    ULONG ResultLength;
    ULONG Index, Byte;
    NTSTATUS Status;

    BuildUniqueDescriptor(&Sd, (PACL)AclBuffer, sizeof(AclBuffer),
                          (PSID)SidBuffer, 1);

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

static
NTSTATUS
OpenKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               Root,
                               NULL);
    return ZwOpenKey(KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
}

static
NTSTATUS
WriteHiveFile(
    _In_ PUNICODE_STRING FileName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Source, File;
    NTSTATUS Status;

    Status = CreateVolatileKey(&Source, NULL,
                               L"\\Registry\\Machine\\SOFTWARE\\KmtHiveSource",
                               NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               FileName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = ZwCreateFile(&File,
                          GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSaveKey(Source, File);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(File);
    }

    ZwDeleteKey(Source);
    ZwClose(Source);
    return Status;
}

/* A hive on disk names no volatile cell, so a reload finds every stable key's
 * security where it left it */
static
VOID
TestVolatileSecurityAcrossReload(VOID)
{
    static UNICODE_STRING FileName =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtVolatileSecurity.hiv");
    static UNICODE_STRING MountName =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\KmtVolatileSecurity");
    ULONG SidStable[SID_MAX_SUB_AUTHORITIES + 2];
    ULONG SidShared[SID_MAX_SUB_AUTHORITIES + 2];
    ULONG AclStable[32], AclShared[32];
    SECURITY_DESCRIPTOR SdStable, SdShared;
    OBJECT_ATTRIBUTES MountAttributes, FileAttributes;
    ULONG Descriptor[64];
    PSECURITY_DESCRIPTOR Read = (PSECURITY_DESCRIPTOR)Descriptor;
    HANDLE Root, Stable, Volatile, Shared;
    BOOLEAN Present, Defaulted;
    PACCESS_ALLOWED_ACE Ace;
    ULONG ResultLength;
    NTSTATUS Status;
    PACL Dacl;

    BuildUniqueDescriptor(&SdStable, (PACL)AclStable, sizeof(AclStable),
                          (PSID)SidStable, 2);
    BuildUniqueDescriptor(&SdShared, (PACL)AclShared, sizeof(AclShared),
                          (PSID)SidShared, 3);

    if (!NT_SUCCESS(WriteHiveFile(&FileName))) return;

    InitializeObjectAttributes(&MountAttributes, &MountName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &FileName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);

    Status = ZwLoadKey(&MountAttributes, &FileAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = OpenKey(&Root, NULL, MountName.Buffer);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = CreateKey(&Stable, Root, L"Stable", &SdStable,
                           REG_OPTION_NON_VOLATILE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ZwClose(Stable);

        Status = CreateVolatileKey(&Volatile, Root, L"Volatile", &SdShared);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ZwClose(Volatile);

        Status = CreateKey(&Shared, Root, L"Shared", &SdShared,
                           REG_OPTION_NON_VOLATILE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ZwClose(Shared);

        Status = ZwFlushKey(Root);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Root);
    }

    Status = ZwUnloadKey(&MountAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwLoadKey(&MountAttributes, &FileAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = OpenKey(&Volatile, NULL,
                     L"\\Registry\\Machine\\KmtVolatileSecurity\\Volatile");
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status)) ZwClose(Volatile);

    Status = OpenKey(&Shared, NULL,
                     L"\\Registry\\Machine\\KmtVolatileSecurity\\Shared");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQuerySecurityObject(Shared, DACL_SECURITY_INFORMATION, Read,
                                       sizeof(Descriptor), &ResultLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Dacl = NULL;
        if (NT_SUCCESS(Status))
        {
            Status = RtlGetDaclSecurityDescriptor(Read, &Present, &Dacl,
                                                  &Defaulted);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
        Ace = NULL;
        if (Dacl != NULL && Dacl->AceCount == 2)
        {
            RtlGetAce(Dacl, 1, (PVOID *)&Ace);
        }
        ok(Ace != NULL && RtlEqualSid((PSID)&Ace->SidStart, (PSID)SidShared),
           "the shared key lost its descriptor\n");

        Status = ZwDeleteKey(Shared);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Shared);
    }

    Status = OpenKey(&Stable, NULL,
                     L"\\Registry\\Machine\\KmtVolatileSecurity\\Stable");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwDeleteKey(Stable);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Stable);
    }

    Status = ZwUnloadKey(&MountAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwDeleteFile(&FileAttributes);
}

static
VOID
DeleteNewHiveFiles(
    _In_ POBJECT_ATTRIBUTES FileAttributes,
    _In_ POBJECT_ATTRIBUTES LogAttributes)
{
    ZwDeleteFile(FileAttributes);
    ZwDeleteFile(LogAttributes);
}

/* A hive made by loading a missing file has a root that inherits a descriptor */
static
VOID
TestNewHiveRootSecurity(VOID)
{
    static UNICODE_STRING FileName =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtNewHive.hiv");
    static UNICODE_STRING LogName =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtNewHive.hiv.LOG1");
    static UNICODE_STRING MountName =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\KmtNewHive");
    OBJECT_ATTRIBUTES MountAttributes, FileAttributes, LogAttributes;
    ULONG Descriptor[64];
    PSECURITY_DESCRIPTOR Read = (PSECURITY_DESCRIPTOR)Descriptor;
    BOOLEAN Present = FALSE, Defaulted;
    ULONG ResultLength;
    PACL Dacl = NULL;
    HANDLE Root;
    NTSTATUS Status;

    InitializeObjectAttributes(&MountAttributes, &MountName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &FileName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    InitializeObjectAttributes(&LogAttributes, &LogName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    DeleteNewHiveFiles(&FileAttributes, &LogAttributes);

    Status = ZwLoadKey(&MountAttributes, &FileAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = OpenKey(&Root, NULL, MountName.Buffer);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQuerySecurityObject(Root, DACL_SECURITY_INFORMATION, Read,
                                       sizeof(Descriptor), &ResultLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = RtlGetDaclSecurityDescriptor(Read, &Present, &Dacl,
                                                  &Defaulted);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok(Present && Dacl != NULL && Dacl->AceCount != 0,
               "the new hive's root inherited no DACL\n");

            Status = ZwSetSecurityObject(Root, DACL_SECURITY_INFORMATION, Read);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
        ZwClose(Root);
    }

    Status = ZwUnloadKey(&MountAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DeleteNewHiveFiles(&FileAttributes, &LogAttributes);
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
    TestVolatileSecurityAcrossReload();
    TestNewHiveRootSecurity();
}
