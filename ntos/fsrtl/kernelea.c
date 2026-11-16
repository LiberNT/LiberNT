/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Extended attribute requests a kernel component sends by file object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
NTAPI
FsRtlpKernelEaComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    ASSERT(Irp->UserIosb != NULL);
    *Irp->UserIosb = Irp->IoStatus;
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
PIRP
FsRtlpAllocateKernelEaIrp(
    _In_ PFILE_OBJECT FileObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MajorFunction,
    _In_opt_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    ASSERT(MajorFunction == IRP_MJ_QUERY_EA || MajorFunction == IRP_MJ_SET_EA);

    RtlZeroMemory(IoStatus, sizeof(*IoStatus));
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
        return NULL;

    Irp->UserEvent = NULL;
    Irp->UserIosb = IoStatus;
    Irp->UserBuffer = Buffer;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = MajorFunction;
    Stack->MinorFunction = IRP_MN_KERNEL_CALL;
    Stack->FileObject = FileObject;
    Stack->DeviceObject = DeviceObject;
    return Irp;
}

static
NTSTATUS
FsRtlpCallKernelEaIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _Inout_ PIO_STATUS_BLOCK IoStatus)
{
    KEVENT Event;
    NTSTATUS Status;

    ASSERT(Irp->UserIosb == IoStatus);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, FsRtlpKernelEaComplete, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    return IoStatus->Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
FsRtlQueryKernelEaFile(
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(Length, *LengthReturned) PVOID ReturnedEaData,
    _In_ ULONG Length,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_reads_bytes_opt_(EaListLength) PVOID EaList,
    _In_ ULONG EaListLength,
    _In_opt_ PULONG EaIndex,
    _In_ BOOLEAN RestartScan,
    _Out_opt_ PULONG LengthReturned)
{
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT DeviceObject;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();
    ASSERT(FileObject != NULL);
    ASSERT(ReturnedEaData != NULL || Length == 0);
    ASSERT(EaList != NULL || EaListLength == 0);

    if (FileObject->Flags & FO_DIRECT_DEVICE_OPEN)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (LengthReturned != NULL)
        *LengthReturned = 0;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    Irp = FsRtlpAllocateKernelEaIrp(FileObject, DeviceObject, IRP_MJ_QUERY_EA, ReturnedEaData, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Flags = 0;
    if (ReturnSingleEntry)
        Stack->Flags |= SL_RETURN_SINGLE_ENTRY;
    if (RestartScan)
        Stack->Flags |= SL_RESTART_SCAN;
    if (EaIndex != NULL)
    {
        Stack->Flags |= SL_INDEX_SPECIFIED;
        Stack->Parameters.QueryEa.EaIndex = *EaIndex;
    }
    Stack->Parameters.QueryEa.Length = Length;
    Stack->Parameters.QueryEa.EaList = EaList;
    Stack->Parameters.QueryEa.EaListLength = EaListLength;

    Status = FsRtlpCallKernelEaIrp(DeviceObject, Irp, &IoStatus);
    if (LengthReturned != NULL && (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW))
    {
        ASSERT(IoStatus.Information <= Length);
        *LengthReturned = (ULONG)IoStatus.Information;
    }
    return Status;
}

NTSTATUS
FsRtlSetKernelEaFile(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) PVOID EaBuffer,
    _In_ ULONG Length)
{
    PDEVICE_OBJECT DeviceObject;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    PAGED_CODE();
    ASSERT(FileObject != NULL);
    ASSERT(EaBuffer != NULL || Length == 0);

    if (FileObject->Flags & FO_DIRECT_DEVICE_OPEN)
        return STATUS_INVALID_DEVICE_REQUEST;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    Irp = FsRtlpAllocateKernelEaIrp(FileObject, DeviceObject, IRP_MJ_SET_EA, EaBuffer, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoGetNextIrpStackLocation(Irp)->Parameters.SetEa.Length = Length;
    return FsRtlpCallKernelEaIrp(DeviceObject, Irp, &IoStatus);
}

/* EOF */
