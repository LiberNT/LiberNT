/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoDeleteDevice takes a device's name away at once, even while a file on the device stays open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KmtIoDeleteDeviceName");

static NTSTATUS CreateNamedDevice(PDEVICE_OBJECT *Device)
{
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, Device);
    if (NT_SUCCESS(Status))
        (*Device)->Flags &= ~DO_DEVICE_INITIALIZING;
    return Status;
}

START_TEST(IoDeleteDeviceName)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    PDEVICE_OBJECT Device, Again;
    PFILE_OBJECT Stream;
    HANDLE Handle;
    NTSTATUS Status;

    Status = CreateNamedDevice(&Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    /* A file on the device keeps the device object alive, not its name */
    Stream = IoCreateStreamFileObjectLite(NULL, Device);
    ok(Stream != NULL, "No stream file on the device\n");

    IoDeleteDevice(Device);

    InitializeObjectAttributes(&Attributes, &DeviceName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_ATTRIBUTES, &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);

    Status = CreateNamedDevice(&Again);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        IoDeleteDevice(Again);

    if (Stream != NULL)
        ObDereferenceObject(Stream);
}
