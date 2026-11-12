/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A bug check while the system shuts down still reaches the boot log
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static DRIVER_DISPATCH KdShutdownCrashDispatch;

static
NTSTATUS
NTAPI
KdShutdownCrashDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    DbgPrint("KDSHUTDOWNCRASH: stopping the machine from its shutdown notification\n");
    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0x4B44534D, 0, 0, 0);
}

START_TEST(KdShutdownCrash)
{
    PDEVICE_OBJECT Device;
    NTSTATUS Status;

    /* Hidden test: the boot then ends in a bug check once the boot log has been closed for the shutdown */
    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    KmtDriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = KdShutdownCrashDispatch;
    Status = IoRegisterShutdownNotification(Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* EOF */
