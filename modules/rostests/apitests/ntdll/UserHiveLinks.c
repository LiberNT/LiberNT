/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The LocalSystem user key is a link to the .DEFAULT hive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

static
NTSTATUS
QueryLink(
    _In_ PCWSTR Path,
    _In_ ULONG Attributes,
    _Out_ PULONG Type,
    _Out_writes_bytes_(BufferSize) PWSTR Target,
    _In_ ULONG BufferSize,
    _Out_ PNTSTATUS OpenStatus)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 512];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name, ValueName = RTL_CONSTANT_STRING(L"SymbolicLinkValue");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    ULONG Length = 0;
    NTSTATUS Status;

    *Type = REG_NONE;
    Target[0] = UNICODE_NULL;
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | Attributes, NULL, NULL);
    *OpenStatus = NtOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(*OpenStatus))
        return *OpenStatus;

    Status = NtQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length);
    if (NT_SUCCESS(Status))
    {
        *Type = Information->Type;
        if (Information->DataLength + sizeof(WCHAR) <= BufferSize)
        {
            RtlCopyMemory(Target, Information->Data, Information->DataLength);
            Target[Information->DataLength / sizeof(WCHAR)] = UNICODE_NULL;
        }
    }

    NtClose(KeyHandle);
    return Status;
}

START_TEST(UserHiveLinks)
{
    WCHAR Target[256];
    NTSTATUS OpenStatus;
    NTSTATUS Status;
    ULONG Type;

    Status = QueryLink(L"\\Registry\\User\\S-1-5-18", OBJ_OPENLINK, &Type, Target, sizeof(Target), &OpenStatus);
    ok_ntstatus(OpenStatus, STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_long(Type, REG_LINK);
    ok(wcscmp(Target, L"\\Registry\\User\\.Default") == 0, "S-1-5-18 links to '%ls'\n", Target);

    Status = QueryLink(L"\\Registry\\User\\S-1-5-18", 0, &Type, Target, sizeof(Target), &OpenStatus);
    ok_ntstatus(OpenStatus, STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Status = QueryLink(L"\\Registry\\User\\.DEFAULT", OBJ_OPENLINK, &Type, Target, sizeof(Target), &OpenStatus);
    ok_ntstatus(OpenStatus, STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}
