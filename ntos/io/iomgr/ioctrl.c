/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Controller objects, the queue a driver shares between devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

PCONTROLLER_OBJECT
NTAPI
IoCreateController(
    _In_ ULONG Size)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCONTROLLER_OBJECT Controller;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE | OBJ_PERMANENT,
                               NULL,
                               NULL);

    Status = ObCreateObject(KernelMode,
                            IoControllerObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(CONTROLLER_OBJECT) + Size,
                            0,
                            0,
                            (PVOID *)&Controller);
    if (!NT_SUCCESS(Status)) return NULL;

    RtlZeroMemory(Controller, sizeof(CONTROLLER_OBJECT) + Size);

    Controller->Type = IO_TYPE_CONTROLLER;
    Controller->Size = (CSHORT)(sizeof(CONTROLLER_OBJECT) + Size);
    Controller->ControllerExtension = (Size != 0) ? (Controller + 1) : NULL;

    KeInitializeDeviceQueue(&Controller->DeviceWaitQueue);

    Status = ObInsertObject(Controller,
                            NULL,
                            FILE_READ_DATA | FILE_WRITE_DATA,
                            1,
                            (PVOID *)&Controller,
                            NULL);
    if (!NT_SUCCESS(Status)) return NULL;

    return Controller;
}

VOID
NTAPI
IoDeleteController(
    _In_ PCONTROLLER_OBJECT ControllerObject)
{
    PAGED_CODE();

    ObDereferenceObject(ControllerObject);
}

VOID
NTAPI
IoAllocateController(
    _In_ PCONTROLLER_OBJECT ControllerObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_opt_ PVOID Context)
{
    IO_ALLOCATION_ACTION Action;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    DeviceObject->Queue.Wcb.DeviceContext = Context;
    DeviceObject->Queue.Wcb.DeviceRoutine = ExecutionRoutine;
    DeviceObject->Queue.Wcb.CurrentIrp = DeviceObject->CurrentIrp;

    if (!KeInsertDeviceQueue(&ControllerObject->DeviceWaitQueue,
                             &DeviceObject->Queue.Wcb.WaitQueueEntry))
    {
        Action = ExecutionRoutine(DeviceObject,
                                  DeviceObject->CurrentIrp,
                                  NULL,
                                  Context);
        if (Action == DeallocateObject)
        {
            IoFreeController(ControllerObject);
        }
    }

    KeLowerIrql(OldIrql);
}

VOID
NTAPI
IoFreeController(
    _In_ PCONTROLLER_OBJECT ControllerObject)
{
    PKDEVICE_QUEUE_ENTRY Entry;
    IO_ALLOCATION_ACTION Action;
    PDEVICE_OBJECT DeviceObject;
    PWAIT_CONTEXT_BLOCK Wcb;

    Entry = KeRemoveDeviceQueue(&ControllerObject->DeviceWaitQueue);
    if (Entry == NULL) return;

    Wcb = CONTAINING_RECORD(Entry, WAIT_CONTEXT_BLOCK, WaitQueueEntry);
    DeviceObject = CONTAINING_RECORD(Wcb, DEVICE_OBJECT, Queue.Wcb);

    Action = Wcb->DeviceRoutine(DeviceObject,
                                Wcb->CurrentIrp,
                                NULL,
                                Wcb->DeviceContext);
    if (Action == DeallocateObject)
    {
        IoFreeController(ControllerObject);
    }
}
