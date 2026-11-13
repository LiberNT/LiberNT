/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Requests to the storage device under a volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
NTSTATUS
NTAPI
BtrfsDeviceRequestDone(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    ASSERT(Context != NULL);
    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* Paging I/O arrives at APC_LEVEL, where a threaded IRP's completion APC could not run: the IRP is completed here */
static
NTSTATUS
BtrfsTransferChunk(
    _In_ PDEVICE_OBJECT Device,
    _In_ UCHAR Major,
    _In_ ULONGLONG Offset,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN OverrideVerify)
{
    PIO_STACK_LOCATION Stack;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;
    KEVENT Event;
    PMDL Mdl;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(Length != 0 && Length <= BTRFS_IO_CHUNK);

    Irp = IoAllocateIrp(Device->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, Irp);
    if (Mdl == NULL)
    {
        IoFreeIrp(Irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, KernelMode, (Major == IRP_MJ_READ) ? IoWriteAccess : IoReadAccess);
        Locked = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        Irp->MdlAddress = NULL;
        IoFreeIrp(Irp);
        return Status;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp->UserBuffer = Buffer;
    Irp->Flags = IRP_NOCACHE;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->RequestorMode = KernelMode;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = Major;
    if (Major == IRP_MJ_READ)
    {
        Stack->Parameters.Read.Length = Length;
        Stack->Parameters.Read.ByteOffset.QuadPart = (LONGLONG)Offset;
    }
    else
    {
        Stack->Parameters.Write.Length = Length;
        Stack->Parameters.Write.ByteOffset.QuadPart = (LONGLONG)Offset;
    }
    if (OverrideVerify) Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

    IoSetCompletionRoutine(Irp, BtrfsDeviceRequestDone, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;

    if (NT_SUCCESS(Status) && Irp->IoStatus.Information != Length) Status = STATUS_END_OF_FILE;

    ASSERT(Locked);
    MmUnlockPages(Mdl);
    IoFreeMdl(Mdl);
    Irp->MdlAddress = NULL;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
BtrfsReadDevice(
    _In_ PDEVICE_OBJECT Device,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN OverrideVerify)
{
    PUCHAR Cursor = Buffer;
    NTSTATUS Status;
    ULONG Part;

    ASSERT(Device != NULL);

    while (Length != 0)
    {
        Part = min(Length, BTRFS_IO_CHUNK);
        Status = BtrfsTransferChunk(Device, IRP_MJ_READ, Offset, Cursor, Part, OverrideVerify);
        if (!NT_SUCCESS(Status)) return Status;

        Offset += Part;
        Cursor += Part;
        Length -= Part;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsWriteDevice(
    _In_ PDEVICE_OBJECT Device,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    PUCHAR Cursor = (PUCHAR)Buffer;
    NTSTATUS Status;
    ULONG Part;

    while (Length != 0)
    {
        Part = min(Length, BTRFS_IO_CHUNK);
        Status = BtrfsTransferChunk(Device, IRP_MJ_WRITE, Offset, Cursor, Part, FALSE);
        if (!NT_SUCCESS(Status)) return Status;

        Offset += Part;
        Cursor += Part;
        Length -= Part;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsFlushDevice(
    _In_ PDEVICE_OBJECT Device)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset = { 0 };
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_FLUSH_BUFFERS, Device, NULL, 0, &Offset, &Event, &IoStatus);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return (Status == STATUS_INVALID_DEVICE_REQUEST || Status == STATUS_NOT_SUPPORTED) ? STATUS_SUCCESS : Status;
}

NTSTATUS
BtrfsDeviceControl(
    _In_ PDEVICE_OBJECT Device,
    _In_ ULONG Code,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength,
    _In_ BOOLEAN OverrideVerify)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(Code, Device, NULL, 0, Output, OutputLength, FALSE, &Event, &IoStatus);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (OverrideVerify) IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}
