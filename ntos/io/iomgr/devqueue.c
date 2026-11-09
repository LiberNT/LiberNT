/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Serialising requests through a device's own queue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopCallStartIo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    DeviceObject->CurrentIrp = Irp;
    DeviceObject->DriverObject->DriverStartIo(DeviceObject, Irp);
}

static
VOID
IopStartNextPacketWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Cancelable,
    _In_ BOOLEAN ByKey,
    _In_ ULONG Key)
{
    PKDEVICE_QUEUE_ENTRY Entry;
    KIRQL OldIrql;
    PIRP Irp;

    DeviceObject->CurrentIrp = NULL;

    if (ByKey)
    {
        Entry = KeRemoveByKeyDeviceQueue(&DeviceObject->DeviceQueue, Key);
    }
    else
    {
        Entry = KeRemoveDeviceQueue(&DeviceObject->DeviceQueue);
    }

    if (Entry == NULL) return;

    Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.DeviceQueueEntry);

    if (!Cancelable)
    {
        IopCallStartIo(DeviceObject, Irp);
        return;
    }

    IoAcquireCancelSpinLock(&OldIrql);
    IoSetCancelRoutine(Irp, NULL);
    IoReleaseCancelSpinLock(OldIrql);

    IopCallStartIo(DeviceObject, Irp);
}

VOID
IopStartNextPacket(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Cancelable)
{
    IopStartNextPacketWorker(DeviceObject, Cancelable, FALSE, 0);
}

VOID
IopStartNextPacketByKey(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Cancelable,
    _In_ ULONG Key)
{
    IopStartNextPacketWorker(DeviceObject, Cancelable, TRUE, Key);
}

VOID
IopStartNextPacketByKeyEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Key,
    _In_ ULONG Flags)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    BOOLEAN Cancelable;
    ULONG CurrentKey = Key;
    ULONG CurrentFlags = Flags;

    Cancelable = (Extension->StartIoFlags & DOE_SIO_CANCELABLE) != 0;

    while (TRUE)
    {
        if (CurrentFlags & DOE_SIO_WITH_KEY)
        {
            IopStartNextPacketByKey(DeviceObject, Cancelable, CurrentKey);
        }
        else if (CurrentFlags & DOE_SIO_NO_KEY)
        {
            IopStartNextPacket(DeviceObject, Cancelable);
        }

        CurrentFlags = InterlockedExchange((PLONG)&Extension->StartIoFlags,
                                           Extension->StartIoFlags &
                                               ~(DOE_SIO_WITH_KEY |
                                                 DOE_SIO_NO_KEY |
                                                 DOE_SIO_DEFERRED));
        if (!(CurrentFlags & (DOE_SIO_WITH_KEY | DOE_SIO_NO_KEY))) return;

        CurrentKey = Extension->StartIoKey;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoStartPacket(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PULONG Key,
    _In_opt_ PDRIVER_CANCEL CancelFunction)
{
    BOOLEAN Inserted;
    KIRQL OldIrql;
    KIRQL CancelIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    if (CancelFunction != NULL) Irp->CancelRoutine = CancelFunction;

    if (Key != NULL)
    {
        Inserted = KeInsertByKeyDeviceQueue(&DeviceObject->DeviceQueue,
                                            &Irp->Tail.Overlay.DeviceQueueEntry,
                                            *Key);
    }
    else
    {
        Inserted = KeInsertDeviceQueue(&DeviceObject->DeviceQueue,
                                       &Irp->Tail.Overlay.DeviceQueueEntry);
    }

    if (!Inserted)
    {
        if (CancelFunction != NULL && Irp->Cancel)
        {
            IoAcquireCancelSpinLock(&CancelIrql);

            if (IoSetCancelRoutine(Irp, NULL) != NULL)
            {
                Irp->CancelIrql = CancelIrql;
                DeviceObject->CurrentIrp = Irp;
                CancelFunction(DeviceObject, Irp);
                KeLowerIrql(OldIrql);
                return;
            }

            IoReleaseCancelSpinLock(CancelIrql);
        }

        IopCallStartIo(DeviceObject, Irp);
        KeLowerIrql(OldIrql);
        return;
    }

    if (CancelFunction == NULL)
    {
        KeLowerIrql(OldIrql);
        return;
    }

    if (!Irp->Cancel)
    {
        KeLowerIrql(OldIrql);
        return;
    }

    IoAcquireCancelSpinLock(&CancelIrql);

    if (IoSetCancelRoutine(Irp, NULL) != NULL &&
        KeRemoveEntryDeviceQueue(&DeviceObject->DeviceQueue,
                                 &Irp->Tail.Overlay.DeviceQueueEntry))
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoReleaseCancelSpinLock(CancelIrql);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        KeLowerIrql(OldIrql);
        return;
    }

    IoReleaseCancelSpinLock(CancelIrql);
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
IoStartNextPacket(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Cancelable)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    ULONG Flags;

    if (!(Extension->StartIoFlags & DOE_SIO_DEFERRED))
    {
        IopStartNextPacket(DeviceObject, Cancelable);
        return;
    }

    Flags = InterlockedOr((PLONG)&Extension->StartIoFlags, DOE_SIO_NO_KEY);

    if (Flags & (DOE_SIO_WITH_KEY | DOE_SIO_NO_KEY)) return;

    if (InterlockedCompareExchange(&Extension->StartIoCount, 1, 0) != 0) return;

    IopStartNextPacketByKeyEx(DeviceObject, 0, DOE_SIO_NO_KEY);

    InterlockedExchange(&Extension->StartIoCount, 0);
}

VOID
NTAPI
IoStartNextPacketByKey(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Cancelable,
    _In_ ULONG Key)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    ULONG Flags;

    if (!(Extension->StartIoFlags & DOE_SIO_DEFERRED))
    {
        IopStartNextPacketByKey(DeviceObject, Cancelable, Key);
        return;
    }

    Extension->StartIoKey = Key;

    Flags = InterlockedOr((PLONG)&Extension->StartIoFlags, DOE_SIO_WITH_KEY);

    if (Flags & (DOE_SIO_WITH_KEY | DOE_SIO_NO_KEY)) return;

    if (InterlockedCompareExchange(&Extension->StartIoCount, 1, 0) != 0) return;

    IopStartNextPacketByKeyEx(DeviceObject, Key, DOE_SIO_WITH_KEY);

    InterlockedExchange(&Extension->StartIoCount, 0);
}

VOID
NTAPI
IoSetStartIoAttributes(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN DeferredStartIo,
    _In_ BOOLEAN NonCancelable)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);

    if (DeferredStartIo)
    {
        Extension->StartIoFlags |= DOE_SIO_DEFERRED;
    }
    else
    {
        Extension->StartIoFlags &= ~DOE_SIO_DEFERRED;
    }

    if (NonCancelable)
    {
        Extension->StartIoFlags |= DOE_SIO_NO_CANCEL;
        Extension->StartIoFlags &= ~DOE_SIO_CANCELABLE;
    }
    else
    {
        Extension->StartIoFlags &= ~DOE_SIO_NO_CANCEL;
        Extension->StartIoFlags |= DOE_SIO_CANCELABLE;
    }
}
