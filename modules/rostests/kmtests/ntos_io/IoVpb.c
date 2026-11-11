/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the VPB of a disk device across attachment and deletion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoVpb)
{
    PDEVICE_OBJECT Lower, Disk, AttachedTo;
    NTSTATUS Status;
    PVPB Vpb;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Lower);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_pointer(Lower->Vpb, NULL);

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_DISK, 0, FALSE, &Disk);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Lower);
        return;
    }

    Vpb = Disk->Vpb;
    ok(Vpb != NULL, "A disk device has no VPB\n");
    if (Vpb != NULL)
    {
        ok_eq_pointer(Vpb->RealDevice, Disk);
        ok_eq_ulong(Vpb->ReferenceCount, 0UL);
        ok_eq_hex(Vpb->Flags & VPB_MOUNTED, 0);
    }

    AttachedTo = IoAttachDeviceToDeviceStack(Disk, Lower);
    ok_eq_pointer(AttachedTo, Lower);
    ok_eq_pointer(Disk->Vpb, Vpb);
    if (Disk->Vpb != NULL)
        ok_eq_pointer(Disk->Vpb->RealDevice, Disk);
    ok_eq_pointer(Lower->Vpb, NULL);
    ok_eq_int(Disk->StackSize, Lower->StackSize + 1);

    if (AttachedTo != NULL)
        IoDetachDevice(Lower);

    IoDeleteDevice(Disk);
    IoDeleteDevice(Lower);
}
