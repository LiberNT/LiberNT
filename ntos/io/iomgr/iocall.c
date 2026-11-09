/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Handing a request down a device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
IopSynchronousCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned) KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
IopUnloadSafeCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PIO_UNLOAD_SAFE_COMPLETION_CONTEXT Safe = Context;
    NTSTATUS Status;

    Status = Safe->CompletionRoutine(DeviceObject, Irp, Safe->Context);

    ObDereferenceObject(Safe->DeviceObject);
    ExFreePoolWithTag(Safe, TAG_IO);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
FASTCALL
IofCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr;
    PDRIVER_OBJECT DriverObject;

    ASSERT(Irp->Type == IO_TYPE_IRP);

    if (Irp->CurrentLocation <= 0)
    {
        KeBugCheckEx(NO_MORE_IRP_STACK_LOCATIONS, (ULONG_PTR)Irp, 0, 0, 0);
    }

    StackPtr = IoGetNextIrpStackLocation(Irp);
    Irp->CurrentLocation--;
    Irp->Tail.Overlay.CurrentStackLocation = StackPtr;

    StackPtr->DeviceObject = DeviceObject;

    DriverObject = DeviceObject->DriverObject;

    return DriverObject->MajorFunction[StackPtr->MajorFunction](DeviceObject,
                                                                Irp);
}

#undef IoCallDriver
NTSTATUS
NTAPI
IoCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    return IofCallDriver(DeviceObject, Irp);
}

BOOLEAN
NTAPI
IoForwardIrpSynchronously(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) return FALSE;

    if (Irp->CurrentLocation == 1) return FALSE;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           IopSynchronousCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    return TRUE;
}

NTSTATUS
NTAPI
IoSynchronousCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoSetCompletionRoutine(Irp,
                           IopSynchronousCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

NTSTATUS
NTAPI
IoSetCompletionRoutineEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN InvokeOnSuccess,
    _In_ BOOLEAN InvokeOnError,
    _In_ BOOLEAN InvokeOnCancel)
{
    PIO_UNLOAD_SAFE_COMPLETION_CONTEXT Safe;

    Safe = ExAllocatePoolZero(NonPagedPool,
                              sizeof(IO_UNLOAD_SAFE_COMPLETION_CONTEXT),
                              TAG_IO);
    if (Safe == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Safe->DeviceObject = DeviceObject;
    Safe->Context = Context;
    Safe->CompletionRoutine = CompletionRoutine;

    ObReferenceObject(DeviceObject);

    IoSetCompletionRoutine(Irp,
                           IopUnloadSafeCompletion,
                           Safe,
                           InvokeOnSuccess,
                           InvokeOnError,
                           InvokeOnCancel);

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoEnqueueIrp(
    _In_ PIRP Irp)
{
    IopQueueIrpToThread(Irp);
}

VOID
NTAPI
IoQueueThreadIrp(
    _In_ PIRP Irp)
{
    IopQueueIrpToThread(Irp);
}
