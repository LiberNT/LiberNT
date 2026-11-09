/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling drivers and file systems that the machine is going down
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY ShutdownListHead;
LIST_ENTRY LastChanceShutdownListHead;
KSPIN_LOCK ShutdownListLock;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
IoInitShutdownNotification(VOID)
{
    InitializeListHead(&ShutdownListHead);
    InitializeListHead(&LastChanceShutdownListHead);
    KeInitializeSpinLock(&ShutdownListLock);
}

static
NTSTATUS
IopRegisterShutdownEntry(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PLIST_ENTRY ListHead)
{
    PSHUTDOWN_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    Entry = ExAllocatePoolZero(NonPagedPool,
                               sizeof(SHUTDOWN_ENTRY),
                               TAG_IO);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entry->DeviceObject = DeviceObject;

    ObReferenceObject(DeviceObject);
    DeviceObject->Flags |= DO_SHUTDOWN_REGISTERED;

    KeAcquireSpinLock(&ShutdownListLock, &OldIrql);
    InsertHeadList(ListHead, &Entry->ShutdownList);
    KeReleaseSpinLock(&ShutdownListLock, OldIrql);

    return STATUS_SUCCESS;
}

static
VOID
IopSendShutdownIrp(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT TopDevice;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    TopDevice = IoGetAttachedDeviceReference(DeviceObject);

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_SHUTDOWN,
                                       TopDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (Irp == NULL)
    {
        ObDereferenceObject(TopDevice);
        return;
    }

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = NULL;

    Status = IoCallDriver(TopDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    ObDereferenceObject(TopDevice);
}

static
VOID
IopRunShutdownList(
    _In_ PLIST_ENTRY ListHead)
{
    PSHUTDOWN_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;

    PAGED_CODE();

    while (TRUE)
    {
        KeAcquireSpinLock(&ShutdownListLock, &OldIrql);

        if (IsListEmpty(ListHead))
        {
            KeReleaseSpinLock(&ShutdownListLock, OldIrql);
            return;
        }

        ListEntry = RemoveHeadList(ListHead);

        KeReleaseSpinLock(&ShutdownListLock, OldIrql);

        Entry = CONTAINING_RECORD(ListEntry, SHUTDOWN_ENTRY, ShutdownList);

        IopSendShutdownIrp(Entry->DeviceObject);

        ObDereferenceObject(Entry->DeviceObject);
        ExFreePoolWithTag(Entry, TAG_IO);
    }
}

VOID
NTAPI
IopShutdownBaseFileSystems(
    IN PLIST_ENTRY ListHead)
{
    PDEVICE_OBJECT DeviceObject;
    PLIST_ENTRY ListEntry;

    PAGED_CODE();

    ListEntry = ListHead->Flink;
    while (ListEntry != ListHead)
    {
        DeviceObject = CONTAINING_RECORD(ListEntry,
                                         DEVICE_OBJECT,
                                         Queue.ListEntry);
        ListEntry = ListEntry->Flink;

        IopSendShutdownIrp(DeviceObject);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoRegisterShutdownNotification(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return IopRegisterShutdownEntry(DeviceObject, &ShutdownListHead);
}

NTSTATUS
NTAPI
IoRegisterLastChanceShutdownNotification(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return IopRegisterShutdownEntry(DeviceObject,
                                    &LastChanceShutdownListHead);
}

VOID
NTAPI
IoUnregisterShutdownNotification(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PSHUTDOWN_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;

    PAGED_CODE();

    if (!(DeviceObject->Flags & DO_SHUTDOWN_REGISTERED)) return;

    KeAcquireSpinLock(&ShutdownListLock, &OldIrql);

    ListEntry = ShutdownListHead.Flink;
    while (ListEntry != &ShutdownListHead)
    {
        Entry = CONTAINING_RECORD(ListEntry, SHUTDOWN_ENTRY, ShutdownList);
        ListEntry = ListEntry->Flink;

        if (Entry->DeviceObject != DeviceObject) continue;

        RemoveEntryList(&Entry->ShutdownList);

        KeReleaseSpinLock(&ShutdownListLock, OldIrql);

        ObDereferenceObject(DeviceObject);
        ExFreePoolWithTag(Entry, TAG_IO);

        KeAcquireSpinLock(&ShutdownListLock, &OldIrql);
        ListEntry = ShutdownListHead.Flink;
    }

    ListEntry = LastChanceShutdownListHead.Flink;
    while (ListEntry != &LastChanceShutdownListHead)
    {
        Entry = CONTAINING_RECORD(ListEntry, SHUTDOWN_ENTRY, ShutdownList);
        ListEntry = ListEntry->Flink;

        if (Entry->DeviceObject != DeviceObject) continue;

        RemoveEntryList(&Entry->ShutdownList);

        KeReleaseSpinLock(&ShutdownListLock, OldIrql);

        ObDereferenceObject(DeviceObject);
        ExFreePoolWithTag(Entry, TAG_IO);

        KeAcquireSpinLock(&ShutdownListLock, &OldIrql);
        ListEntry = LastChanceShutdownListHead.Flink;
    }

    DeviceObject->Flags &= ~DO_SHUTDOWN_REGISTERED;

    KeReleaseSpinLock(&ShutdownListLock, OldIrql);
}

VOID
IoShutdownPnpDevices(VOID)
{
    PAGED_CODE();

    if (IopRootDeviceNode == NULL) return;

    PiPerformSyncDeviceAction(IopRootDeviceNode->PhysicalDeviceObject,
                              PiActionQueryState);
}

VOID
NTAPI
IoShutdownSystem(
    IN ULONG Phase)
{
    PAGED_CODE();

    if (Phase == 0)
    {
        IopRunShutdownList(&ShutdownListHead);
        return;
    }

    if (Phase == 1)
    {
        IopShutdownBaseFileSystems(&IopDiskFileSystemQueueHead);
        IopShutdownBaseFileSystems(&IopCdRomFileSystemQueueHead);
        IopShutdownBaseFileSystems(&IopTapeFileSystemQueueHead);
        IopShutdownBaseFileSystems(&IopNetworkFileSystemQueueHead);
        return;
    }

    IopRunShutdownList(&LastChanceShutdownListHead);
}
