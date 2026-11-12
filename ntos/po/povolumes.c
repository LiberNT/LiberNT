/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Getting what is on a volume written before the power goes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PopVolumeDevices;
KGUARDED_MUTEX PopVolumeLock;

/* PRIVATE FUNCTIONS **********************************************************/

/* Writes out what one volume is holding, and waits for it to be written */
static
NTSTATUS
PopWriteOutVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN ShuttingDown)
{
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Done;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();

    KeInitializeEvent(&Done, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(ShuttingDown ? IRP_MJ_SHUTDOWN
                                                    : IRP_MJ_FLUSH_BUFFERS,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Done,
                                       &IoStatusBlock);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PopInitializeVolumes(VOID)
{
    PAGED_CODE();

    InitializeListHead(&PopVolumeDevices);
    KeInitializeGuardedMutex(&PopVolumeLock);
}

/*
 * A volume says it is holding something that has to reach the disk before the
 * machine stops. It stays on the list until it says otherwise.
 */
VOID
NTAPI
PoVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;

    PAGED_CODE();

    Dope = PopGetDope(DeviceObject);
    if (Dope == NULL) return;

    KeAcquireGuardedMutex(&PopVolumeLock);

    /* A volume that already said so is not counted twice */
    if (IsListEmpty(&Dope->Volume))
    {
        ObReferenceObject(DeviceObject);
        InsertTailList(&PopVolumeDevices, &Dope->Volume);
    }

    KeReleaseGuardedMutex(&PopVolumeLock);
}

VOID
NTAPI
PoRemoveVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;
    BOOLEAN Removed = FALSE;

    PAGED_CODE();

    Dope = DeviceObject->DeviceObjectExtension->Dope;
    if (Dope == NULL) return;

    KeAcquireGuardedMutex(&PopVolumeLock);

    if (!IsListEmpty(&Dope->Volume))
    {
        RemoveEntryList(&Dope->Volume);
        InitializeListHead(&Dope->Volume);
        Removed = TRUE;
    }

    KeReleaseGuardedMutex(&PopVolumeLock);

    if (Removed) ObDereferenceObject(DeviceObject);
}

/*
 * Writes out every volume that said it was holding something. The list is
 * read under its lock and walked outside it, because a volume may take its
 * own name off the list while it is being written.
 */
VOID
NTAPI
PopFlushVolumes(
    _In_ BOOLEAN ShuttingDown)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;
    PDEVICE_OBJECT *Volumes;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Held = 0;
    ULONG i;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopVolumeLock);

    for (Entry = PopVolumeDevices.Flink;
         Entry != &PopVolumeDevices;
         Entry = Entry->Flink)
    {
        Count++;
    }

    if (Count == 0)
    {
        KeReleaseGuardedMutex(&PopVolumeLock);
        return;
    }

    Volumes = ExAllocatePoolWithTag(NonPagedPool,
                                    Count * sizeof(PDEVICE_OBJECT),
                                    'vPoP');
    if (Volumes != NULL)
    {
        for (Entry = PopVolumeDevices.Flink;
             Entry != &PopVolumeDevices && Held < Count;
             Entry = Entry->Flink)
        {
            Dope = CONTAINING_RECORD(Entry,
                                     DEVICE_OBJECT_POWER_EXTENSION,
                                     Volume);

            ObReferenceObject(Dope->DeviceObject);
            Volumes[Held++] = Dope->DeviceObject;
        }
    }

    KeReleaseGuardedMutex(&PopVolumeLock);

    if (Volumes == NULL) return;

    for (i = 0; i < Held; i++)
    {
        PopWriteOutVolume(Volumes[i], ShuttingDown);
        ObDereferenceObject(Volumes[i]);
    }

    ExFreePoolWithTag(Volumes, 'vPoP');
}

/* The same, asked for from somewhere that cannot wait for disks itself */
VOID
NTAPI
PopFlushVolumeWorker(
    _In_ PVOID Context)
{
    PAGED_CODE();

    PopFlushVolumes((BOOLEAN)(Context != NULL));
}
