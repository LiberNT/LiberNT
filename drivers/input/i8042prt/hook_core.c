/*
 * PROJECT:     LiberNT i8042 port driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rules for the callbacks a keyboard filter hooks into the port
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntdd8042.h>

#include "hook_core.h"

NTSTATUS
I8042CoreKeyboardInitialization(
    _In_ PI8042_KEYBOARD_INITIALIZATION_ROUTINE InitializationRoutine,
    _In_ PVOID InitializationContext,
    _In_ PVOID SynchFuncContext,
    _In_ PI8042_SYNCH_READ_PORT ReadPort,
    _In_ PI8042_SYNCH_WRITE_PORT WritePort,
    _Out_ PBOOLEAN TurnTranslationOn)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(InitializationRoutine != NULL);
    ASSERT(SynchFuncContext != NULL);
    ASSERT(ReadPort != NULL);
    ASSERT(WritePort != NULL);
    ASSERT(TurnTranslationOn != NULL);

    *TurnTranslationOn = TRUE;
    return InitializationRoutine(InitializationContext,
                                 SynchFuncContext,
                                 ReadPort,
                                 WritePort,
                                 TurnTranslationOn);
}

NTSTATUS
I8042CoreSendStartInformation(
    _In_ PDEVICE_OBJECT TopOfStack,
    _In_ ULONG IoControlCode,
    _In_ PKINTERRUPT InterruptObject)
{
    INTERNAL_I8042_START_INFORMATION StartInformation;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(TopOfStack != NULL);
    ASSERT(InterruptObject != NULL);
    ASSERT(IoControlCode == IOCTL_INTERNAL_I8042_KEYBOARD_START_INFORMATION ||
           IoControlCode == IOCTL_INTERNAL_I8042_MOUSE_START_INFORMATION);

    RtlZeroMemory(&StartInformation, sizeof(StartInformation));
    StartInformation.Size = sizeof(StartInformation);
    StartInformation.InterruptObject = InterruptObject;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        TopOfStack,
                                        &StartInformation,
                                        sizeof(StartInformation),
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(TopOfStack, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}
