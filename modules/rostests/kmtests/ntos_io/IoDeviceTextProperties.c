/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the bus reported description and friendly name of a disk's device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <initguid.h>
#include <devpkey.h>

static
NTSTATUS
QueryString(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ const DEVPROPKEY *Key,
    _Out_writes_(Count) PWSTR Text,
    _In_ ULONG Count)
{
    DEVPROPTYPE Type = DEVPROP_TYPE_EMPTY;
    ULONG Required = 0;
    NTSTATUS Status;

    RtlZeroMemory(Text, Count * sizeof(WCHAR));
    Status = IoGetDevicePropertyData(Pdo, Key, 0, 0, (Count - 1) * sizeof(WCHAR), Text, &Required, &Type);
    if (NT_SUCCESS(Status))
        ok_eq_ulong(Type, (ULONG)DEVPROP_TYPE_STRING);
    return Status;
}

START_TEST(IoDeviceTextProperties)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Harddisk0\\DR0");
    PDEVICE_OBJECT Device, Pdo;
    PFILE_OBJECT File;
    WCHAR BusText[256];
    WCHAR FriendlyName[256];
    DEVPROPTYPE Type = DEVPROP_TYPE_EMPTY;
    ULONG Required = 0;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &File, &Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Pdo = IoGetDeviceAttachmentBaseRef(Device);
    ok(Pdo != NULL, "The disk has no PDO\n");
    if (Pdo != NULL)
    {
        Status = IoGetDevicePropertyData(Pdo, &DEVPKEY_Device_FriendlyName, 0, 0, 0, NULL, &Required, &Type);
        ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok(Required > sizeof(WCHAR), "Friendly name size %lu\n", Required);

        Status = QueryString(Pdo, &DEVPKEY_Device_BusReportedDeviceDesc, BusText, RTL_NUMBER_OF(BusText));
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(BusText[0] != UNICODE_NULL, "Empty bus reported description\n");

        Status = QueryString(Pdo, &DEVPKEY_Device_FriendlyName, FriendlyName, RTL_NUMBER_OF(FriendlyName));
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(!wcscmp(FriendlyName, BusText), "Friendly name '%ls', bus reported '%ls'\n", FriendlyName, BusText);

        ObDereferenceObject(Pdo);
    }
    ObDereferenceObject(File);
}
