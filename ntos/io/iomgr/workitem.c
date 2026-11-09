/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Work items a driver hands to a system worker thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
IopWorkItemCallback(
    _In_ PVOID Parameter)
{
    PIO_WORKITEM WorkItem = Parameter;
    PDEVICE_OBJECT DeviceObject = WorkItem->DeviceObject;

    WorkItem->WorkerRoutine(DeviceObject, WorkItem->Context);

    if (DeviceObject != NULL) ObDereferenceObject(DeviceObject);
}

/* PUBLIC FUNCTIONS ***********************************************************/

ULONG
NTAPI
IoSizeofWorkItem(VOID)
{
    return sizeof(IO_WORKITEM);
}

PIO_WORKITEM
NTAPI
IoAllocateWorkItem(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_WORKITEM WorkItem;

    WorkItem = ExAllocatePoolZero(NonPagedPool, sizeof(IO_WORKITEM), TAG_IOWI);
    if (WorkItem == NULL) return NULL;

    WorkItem->DeviceObject = DeviceObject;

    return WorkItem;
}

VOID
NTAPI
IoInitializeWorkItem(
    _In_ PVOID IoObject,
    _In_ PIO_WORKITEM IoWorkItem)
{
    RtlZeroMemory(IoWorkItem, sizeof(IO_WORKITEM));
    IoWorkItem->DeviceObject = IoObject;
}

VOID
NTAPI
IoUninitializeWorkItem(
    _In_ PIO_WORKITEM IoWorkItem)
{
    RtlZeroMemory(IoWorkItem, sizeof(IO_WORKITEM));
}

VOID
NTAPI
IoFreeWorkItem(
    _In_ PIO_WORKITEM IoWorkItem)
{
    ExFreePoolWithTag(IoWorkItem, TAG_IOWI);
}

static
VOID
NTAPI
IopWorkItemCallbackEx(
    _In_ PVOID Parameter)
{
    PIO_WORKITEM WorkItem = Parameter;
    PDEVICE_OBJECT DeviceObject = WorkItem->DeviceObject;
    PIO_WORKITEM_ROUTINE_EX Routine;

    Routine = (PIO_WORKITEM_ROUTINE_EX)WorkItem->WorkerRoutine;

    Routine(DeviceObject, WorkItem->Context, WorkItem);

    if (DeviceObject != NULL) ObDereferenceObject(DeviceObject);
}

VOID
NTAPI
IoQueueWorkItem(
    _Inout_ PIO_WORKITEM IoWorkItem,
    _In_ PIO_WORKITEM_ROUTINE WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ PVOID Context)
{
    ASSERT(IoWorkItem->DeviceObject != NULL);

    IoWorkItem->WorkerRoutine = WorkerRoutine;
    IoWorkItem->Context = Context;

    ObReferenceObject(IoWorkItem->DeviceObject);

    ExInitializeWorkItem(&IoWorkItem->Item, IopWorkItemCallback, IoWorkItem);

    ExQueueWorkItem(&IoWorkItem->Item, QueueType);
}

VOID
NTAPI
IoQueueWorkItemEx(
    _Inout_ PIO_WORKITEM IoWorkItem,
    _In_ PIO_WORKITEM_ROUTINE_EX WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ PVOID Context)
{
    ASSERT(IoWorkItem->DeviceObject != NULL);

    IoWorkItem->WorkerRoutine = (PIO_WORKITEM_ROUTINE)WorkerRoutine;
    IoWorkItem->Context = Context;

    ObReferenceObject(IoWorkItem->DeviceObject);

    ExInitializeWorkItem(&IoWorkItem->Item, IopWorkItemCallbackEx, IoWorkItem);

    ExQueueWorkItem(&IoWorkItem->Item, QueueType);
}
