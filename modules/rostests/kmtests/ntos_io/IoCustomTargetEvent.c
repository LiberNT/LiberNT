/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reports a custom target device event on the volume IoCustomTargetEvent watches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define RESULT_KEY L"\\Registry\\Machine\\SOFTWARE\\KmtIoCustomTargetEvent"
#define CUSTOM_DATA "KMTEST!!"

static const GUID TestEvent = {0x5a1d3e27, 0x9c4b, 0x4f6e, {0x8b, 0x02, 0x3d, 0x71, 0xc9, 0x48, 0xae, 0x15}};

static
BOOLEAN
ReadDeviceName(
    _Out_writes_(Chars) PWCHAR Name,
    _In_ ULONG Chars)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(RESULT_KEY);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Device");
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

START_TEST(IoCustomTargetEventReport)
{
    UCHAR Buffer[FIELD_OFFSET(TARGET_DEVICE_CUSTOM_NOTIFICATION, CustomDataBuffer) + sizeof(CUSTOM_DATA) - 1];
    PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification = (PTARGET_DEVICE_CUSTOM_NOTIFICATION)Buffer;
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT Pdo;
    WCHAR Name[128];
    NTSTATUS Status;

    if (!ReadDeviceName(Name, RTL_NUMBER_OF(Name))) return;

    RtlInitUnicodeString(&DeviceName, Name);
    Status = IoGetDeviceObjectPointer(&DeviceName, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
    ok(Status == STATUS_SUCCESS, "opening %wZ returned 0x%lx\n", &DeviceName, Status);
    if (!NT_SUCCESS(Status)) return;

    Pdo = IoGetDeviceAttachmentBaseRef(FileObject->DeviceObject);

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Notification->Version = 1;
    Notification->Size = sizeof(Buffer);
    Notification->Event = TestEvent;
    Notification->NameBufferOffset = -1;
    RtlCopyMemory(Notification->CustomDataBuffer, CUSTOM_DATA, sizeof(CUSTOM_DATA) - 1);

    Status = IoReportTargetDeviceChange(Pdo, Notification);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ObDereferenceObject(Pdo);
    ObDereferenceObject(FileObject);
}
