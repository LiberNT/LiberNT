/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Named events a driver shares with user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
IopCreateEvent(
    _In_ PUNICODE_STRING EventName,
    _Out_ PHANDLE EventHandle,
    _In_ BOOLEAN ManualReset)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               EventName,
                               OBJ_OPENIF | OBJ_CASE_INSENSITIVE |
                                   OBJ_PERMANENT | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return NtCreateEvent(EventHandle,
                         EVENT_ALL_ACCESS,
                         &ObjectAttributes,
                         ManualReset ? NotificationEvent
                                     : SynchronizationEvent,
                         FALSE);
}

VOID
IopSetEvent(
    _In_ PVOID EventPointer)
{
    KeSetEvent((PKEVENT)EventPointer, IO_NO_INCREMENT, FALSE);
}

static
PKEVENT
IopResolveNamedEvent(
    _In_ PUNICODE_STRING EventName,
    _Out_ PHANDLE EventHandle,
    _In_ BOOLEAN ManualReset)
{
    PKEVENT Event;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    Status = IopCreateEvent(EventName, &Handle, ManualReset);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       ExEventObjectType,
                                       KernelMode,
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Handle);
        return NULL;
    }

    ObDereferenceObject(Event);

    *EventHandle = Handle;

    return Event;
}

/* PUBLIC FUNCTIONS ***********************************************************/

PKEVENT
NTAPI
IoCreateNotificationEvent(
    _In_ PUNICODE_STRING EventName,
    _Out_ PHANDLE EventHandle)
{
    return IopResolveNamedEvent(EventName, EventHandle, TRUE);
}

PKEVENT
NTAPI
IoCreateSynchronizationEvent(
    _In_ PUNICODE_STRING EventName,
    _Out_ PHANDLE EventHandle)
{
    return IopResolveNamedEvent(EventName, EventHandle, FALSE);
}
