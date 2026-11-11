/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoGetDeviceInterfaces lists the present interfaces of a class in the order they were enabled, and an unchanged state is reported
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define RESULT_KEY L"\\Registry\\Machine\\SOFTWARE\\KmtIoInterfaceOrder"

static const GUID TestInterface = {0x8f6d2a43, 0x61c4, 0x4bd8, {0x9a, 0x35, 0x2b, 0x9e, 0x0f, 0x5c, 0x7d, 0x11}};

static
BOOLEAN
ReadDeviceName(
    _Out_writes_(Chars) PWCHAR Name,
    _In_ ULONG Chars)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(RESULT_KEY);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Pdo");
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Length;
    NTSTATUS Status;
    HANDLE Key;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer) - sizeof(WCHAR), &Length);
    ZwClose(Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Value->Type != REG_SZ || Value->DataLength >= Chars * sizeof(WCHAR)) return FALSE;

    RtlCopyMemory(Name, Value->Data, Value->DataLength);
    Name[Value->DataLength / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

/* A device object cannot be referenced by name without an open, so the root bus's own list is searched */
static
PDEVICE_OBJECT
ReferenceRootDevice(
    _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(L"\\Driver\\PnpManager");
    UCHAR Buffer[sizeof(OBJECT_NAME_INFORMATION) + 128 * sizeof(WCHAR)];
    POBJECT_NAME_INFORMATION NameInfo = (POBJECT_NAME_INFORMATION)Buffer;
    PDEVICE_OBJECT Found = NULL;
    PDEVICE_OBJECT *List = NULL;
    PDRIVER_OBJECT RootDriver;
    ULONG Count = 0, Index, Length;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&RootName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&RootDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = IoEnumerateDeviceObjectList(RootDriver, NULL, 0, &Count);
    if (Status == STATUS_BUFFER_TOO_SMALL && Count != 0)
    {
        List = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(PDEVICE_OBJECT), 'kDtK');
        if (List != NULL) Status = IoEnumerateDeviceObjectList(RootDriver, List, Count * sizeof(PDEVICE_OBJECT), &Count);
    }

    for (Index = 0; List != NULL && NT_SUCCESS(Status) && Index < Count; Index++)
    {
        if (Found == NULL &&
            NT_SUCCESS(ObQueryNameString(List[Index], NameInfo, sizeof(Buffer), &Length)) &&
            RtlEqualUnicodeString(&NameInfo->Name, Name, TRUE))
        {
            Found = List[Index];
            ObReferenceObject(Found);
        }

        ObDereferenceObject(List[Index]);
    }

    if (List != NULL) ExFreePoolWithTag(List, 'kDtK');
    ObDereferenceObject(RootDriver);

    ok(Found != NULL, "no root device object is named %wZ\n", Name);
    return Found;
}

START_TEST(IoInterfaceOrderCheck)
{
    UNICODE_STRING ReferenceA = RTL_CONSTANT_STRING(L"A");
    UNICODE_STRING ReferenceB = RTL_CONSTANT_STRING(L"B");
    UNICODE_STRING First = { 0 };
    UNICODE_STRING Second = { 0 };
    UNICODE_STRING Entry;
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT Pdo;
    WCHAR Name[128];
    PZZWSTR List;
    NTSTATUS Status;

    if (!ReadDeviceName(Name, RTL_NUMBER_OF(Name))) return;

    RtlInitUnicodeString(&DeviceName, Name);
    Pdo = ReferenceRootDevice(&DeviceName);
    if (Pdo == NULL) return;

    Status = IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceA, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) Status = IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceB, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(IoSetDeviceInterfaceState(&Second, TRUE), STATUS_SUCCESS);
        ok_eq_hex(IoSetDeviceInterfaceState(&First, TRUE), STATUS_SUCCESS);
        ok_eq_hex(IoSetDeviceInterfaceState(&First, TRUE), STATUS_OBJECT_NAME_EXISTS);

        Status = IoGetDeviceInterfaces(&TestInterface, NULL, 0, &List);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&Entry, List);
            ok(RtlEqualUnicodeString(&Entry, &Second, TRUE), "first listed %wZ, expected %wZ\n", &Entry, &Second);
            if (Entry.Length != 0)
            {
                RtlInitUnicodeString(&Entry, List + Entry.Length / sizeof(WCHAR) + 1);
                ok(RtlEqualUnicodeString(&Entry, &First, TRUE), "second listed %wZ, expected %wZ\n", &Entry, &First);
            }
            ExFreePool(List);
        }

        ok_eq_hex(IoSetDeviceInterfaceState(&First, FALSE), STATUS_SUCCESS);
        ok_eq_hex(IoSetDeviceInterfaceState(&First, FALSE), STATUS_OBJECT_NAME_NOT_FOUND);
        ok_eq_hex(IoSetDeviceInterfaceState(&Second, FALSE), STATUS_SUCCESS);
    }

    RtlFreeUnicodeString(&First);
    RtlFreeUnicodeString(&Second);
    ObDereferenceObject(Pdo);
}
