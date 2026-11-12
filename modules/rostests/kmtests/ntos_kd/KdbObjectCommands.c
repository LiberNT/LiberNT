/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a driver, a device, an IRP and a file object to look at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

PDRIVER_OBJECT KdbTestDriver;
PDEVICE_OBJECT KdbTestDevice;
PIRP KdbTestIrp;
PFILE_OBJECT KdbTestFile;

START_TEST(KdbObjectCommands)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\ntoskrnl.exe");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_DATA | SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&KdbTestFile, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KdbTestDriver = KmtDriverObject;
    KdbTestDevice = KmtDriverObject->DeviceObject;
    KdbTestIrp = IoAllocateIrp(2, FALSE);
    ok(KdbTestIrp != NULL, "IoAllocateIrp failed\n");

    if (NT_SUCCESS(Status) && (KdbTestIrp != NULL))
    {
        KdbTestIrp->IoStatus.Status = STATUS_PENDING;
        IoGetNextIrpStackLocation(KdbTestIrp)->MajorFunction = IRP_MJ_READ;
        IoGetNextIrpStackLocation(KdbTestIrp)->FileObject = KdbTestFile;

        /* The debugger's start-up file looks at these objects here */
        DbgBreakPoint();
    }

    if (KdbTestIrp != NULL)
        IoFreeIrp(KdbTestIrp);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(KdbTestFile);
    ZwClose(Handle);
}
