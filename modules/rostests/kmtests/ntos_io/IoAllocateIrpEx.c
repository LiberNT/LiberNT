/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test IoAllocateIrpEx with no device and with DEVICE_WITH_IRP_EXTENSION
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef PIRP (NTAPI *PFN_IO_ALLOCATE_IRP_EX)(PDEVICE_OBJECT, CCHAR, BOOLEAN);

static PFN_IO_ALLOCATE_IRP_EX pIoAllocateIrpEx;

static
VOID
CheckIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ CCHAR StackSize,
    _In_ BOOLEAN ExpectExtension,
    _In_ PCSTR Label)
{
    static const GUID ActivityId = { 0x11223344, 0x5566, 0x7788, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    GUID ReadBack;
    PIRP Irp;
    NTSTATUS Status;

    Irp = pIoAllocateIrpEx(DeviceObject, StackSize, FALSE);
    ok(Irp != NULL, "%s: no IRP\n", Label);
    if (Irp == NULL)
        return;

    ok(Irp->Type == IO_TYPE_IRP, "%s: type %u\n", Label, Irp->Type);
    ok(Irp->StackCount == StackSize, "%s: StackCount %d\n", Label, Irp->StackCount);
    ok(Irp->CurrentLocation == StackSize + 1, "%s: CurrentLocation %d\n", Label, Irp->CurrentLocation);
    ok((Irp->Tail.Overlay.IrpExtension != NULL) == ExpectExtension, "%s: extension %p\n", Label,
       Irp->Tail.Overlay.IrpExtension);

    Status = IoSetActivityIdIrp(Irp, &ActivityId);
    ok(Status == STATUS_SUCCESS, "%s: IoSetActivityIdIrp 0x%08lx\n", Label, Status);
    RtlZeroMemory(&ReadBack, sizeof(ReadBack));
    Status = IoGetActivityIdIrp(Irp, &ReadBack);
    ok(Status == STATUS_SUCCESS, "%s: IoGetActivityIdIrp 0x%08lx\n", Label, Status);
    ok(RtlCompareMemory(&ReadBack, &ActivityId, sizeof(ActivityId)) == sizeof(ActivityId), "%s: activity id differs\n", Label);

    IoFreeIrp(Irp);
}

START_TEST(IoAllocateIrpEx)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"IoAllocateIrpEx");

    pIoAllocateIrpEx = (PFN_IO_ALLOCATE_IRP_EX)MmGetSystemRoutineAddress(&Name);
    ok(pIoAllocateIrpEx != NULL, "IoAllocateIrpEx is not exported\n");
    if (pIoAllocateIrpEx == NULL)
        return;

    CheckIrp(NULL, 3, FALSE, "no device");
    CheckIrp(DEVICE_WITH_IRP_EXTENSION, 3, TRUE, "with extension");
    CheckIrp(DEVICE_WITH_IRP_EXTENSION, 1, TRUE, "with extension, one location");
    CheckIrp(NULL, 0, FALSE, "no device, no location");
}
