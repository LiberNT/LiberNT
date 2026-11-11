/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Safe-mode driver policy by name, group and device class
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

extern NTSYSAPI ULONG InitSafeBootMode;

#define SERVICES_PATH L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\"
#define ENUM_PATH     L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum\\"
#define SERVICE_NAME  L"KmtSafeBootSvc"

/* Keyboard is in SafeBoot\Minimal, Printer is not */
#define LISTED_CLASS   L"{4D36E96B-E325-11CE-BFC1-08002BE10318}"
#define UNLISTED_CLASS L"{4D36E979-E325-11CE-BFC1-08002BE10318}"
#define MISSING_IMAGE  L"\\SystemRoot\\System32\\drivers\\KmtSafeBootMissing.sys"

static
NTSTATUS
CreateKey(
    _Out_ PHANDLE KeyHandle,
    _In_ PCWSTR Path)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwCreateKey(KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
}

static
VOID
SetString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_ PCWSTR Data)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);
    ZwSetValueKey(KeyHandle, &Name, 0, Type, (PVOID)Data, (ULONG)(wcslen(Data) + 1) * sizeof(WCHAR));
}

static
VOID
SetDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ ULONG Data)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);
    ZwSetValueKey(KeyHandle, &Name, 0, REG_DWORD, &Data, sizeof(Data));
}

static
VOID
DeleteKeyPath(
    _In_ PCWSTR Path)
{
    HANDLE KeyHandle;

    if (NT_SUCCESS(CreateKey(&KeyHandle, Path)))
    {
        ZwDeleteKey(KeyHandle);
        ZwClose(KeyHandle);
    }
}

static
NTSTATUS
LoadService(
    _In_ ULONG SafeBootMode)
{
    UNICODE_STRING ServicePath = RTL_CONSTANT_STRING(SERVICES_PATH SERVICE_NAME);
    ULONG SavedMode = InitSafeBootMode;
    NTSTATUS Status;

    InitSafeBootMode = SafeBootMode;
    Status = ZwLoadDriver(&ServicePath);
    InitSafeBootMode = SavedMode;

    return Status;
}

static
VOID
TestPolicy(
    _In_ HANDLE ServiceKey)
{
    HANDLE InstanceKey;
    NTSTATUS Status;

    /* Neither the name, the group nor any device of the service is listed */
    ok_eq_hex(LoadService(1), STATUS_NOT_SAFE_MODE_DRIVER);

    Status = CreateKey(&InstanceKey, ENUM_PATH L"KMTEST");
    if (NT_SUCCESS(Status)) ZwClose(InstanceKey);
    Status = CreateKey(&InstanceKey, ENUM_PATH L"KMTEST\\KmtSafeBoot");
    if (NT_SUCCESS(Status)) ZwClose(InstanceKey);
    Status = CreateKey(&InstanceKey, ENUM_PATH L"KMTEST\\KmtSafeBoot\\0000");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        SetString(InstanceKey, L"Service", REG_SZ, SERVICE_NAME);

        /* A device of a listed class lets its driver through; the missing image fails the load later */
        SetString(InstanceKey, L"ClassGUID", REG_SZ, LISTED_CLASS);
        ok_eq_hex(LoadService(1), STATUS_OBJECT_NAME_NOT_FOUND);

        SetString(InstanceKey, L"ClassGUID", REG_SZ, UNLISTED_CLASS);
        ok_eq_hex(LoadService(1), STATUS_NOT_SAFE_MODE_DRIVER);

        /* Safe mode with networking reads its own list, which also names the class */
        SetString(InstanceKey, L"ClassGUID", REG_SZ, LISTED_CLASS);
        ok_eq_hex(LoadService(2), STATUS_OBJECT_NAME_NOT_FOUND);

        ZwDeleteKey(InstanceKey);
        ZwClose(InstanceKey);
    }
    DeleteKeyPath(ENUM_PATH L"KMTEST\\KmtSafeBoot");
    DeleteKeyPath(ENUM_PATH L"KMTEST");

    SetString(ServiceKey, L"Group", REG_SZ, L"Boot Bus Extender");
    ok_eq_hex(LoadService(1), STATUS_OBJECT_NAME_NOT_FOUND);

    SetString(ServiceKey, L"Group", REG_SZ, L"Extended Base");
    ok_eq_hex(LoadService(1), STATUS_NOT_SAFE_MODE_DRIVER);

    /* The lists also name drivers by image file; this one is listed and not in the image */
    SetString(ServiceKey, L"ImagePath", REG_EXPAND_SZ, L"\\SystemRoot\\System32\\drivers\\HidSpiCx.sys");
    ok_eq_hex(LoadService(1), STATUS_OBJECT_NAME_NOT_FOUND);
    SetString(ServiceKey, L"ImagePath", REG_EXPAND_SZ, MISSING_IMAGE);

    /* Outside safe mode nothing is checked */
    ok_eq_hex(LoadService(0), STATUS_OBJECT_NAME_NOT_FOUND);
}

/* User mode reads the mode from a volatile key only a safe boot creates */
static
VOID
TestPublishedMode(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\SafeBoot\\Option");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"OptionValue");
    ULONG Buffer[(sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)) / sizeof(ULONG) + 1];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    ULONG Length;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (InitSafeBootMode == 0)
    {
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status)) ZwClose(KeyHandle);
        return;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Information, sizeof(Buffer), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(Information->Type, REG_DWORD);
        ok_eq_ulong(*(PULONG)Information->Data, InitSafeBootMode);
    }

    ZwClose(KeyHandle);
}

START_TEST(IoSafeBootPolicy)
{
    HANDLE ServiceKey;
    NTSTATUS Status;

    TestPublishedMode();

    Status = CreateKey(&ServiceKey, SERVICES_PATH SERVICE_NAME);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    SetDword(ServiceKey, L"Type", SERVICE_KERNEL_DRIVER);
    SetDword(ServiceKey, L"Start", SERVICE_DEMAND_START);
    SetDword(ServiceKey, L"ErrorControl", SERVICE_ERROR_IGNORE);
    SetString(ServiceKey, L"ImagePath", REG_EXPAND_SZ, MISSING_IMAGE);

    TestPolicy(ServiceKey);

    ZwDeleteKey(ServiceKey);
    ZwClose(ServiceKey);
}
