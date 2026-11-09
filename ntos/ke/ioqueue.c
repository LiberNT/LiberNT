/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Holding requests for a device that can only serve one at a time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Puts an entry in key order. Equal keys keep their arrival order, so a driver
 * that gives every request the same key gets a plain queue.
 */
static
VOID
KiInsertDeviceQueueByKey(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _Inout_ PKDEVICE_QUEUE_ENTRY DeviceQueueEntry,
    _In_ ULONG SortKey)
{
    PLIST_ENTRY ListHead, Entry;
    PKDEVICE_QUEUE_ENTRY Current;

    ListHead = &DeviceQueue->DeviceListHead;

    /*
     * Searching from the tail suits the usual case, where a driver hands out
     * keys that climb: the new entry belongs at or near the end.
     */
    Entry = ListHead->Blink;
    while (Entry != ListHead)
    {
        Current = CONTAINING_RECORD(Entry, KDEVICE_QUEUE_ENTRY, DeviceListEntry);
        if (Current->SortKey <= SortKey)
            break;

        Entry = Entry->Blink;
    }

    InsertHeadList(Entry, &DeviceQueueEntry->DeviceListEntry);
}

/*
 * Finds the entry a request for this key should be served by: the first one at
 * or past the key, and failing that the one at the head, so that a key beyond
 * everything queued wraps round rather than finding nothing.
 */
static
PKDEVICE_QUEUE_ENTRY
KiFindDeviceQueueEntryByKey(
    _In_ PKDEVICE_QUEUE DeviceQueue,
    _In_ ULONG SortKey)
{
    PLIST_ENTRY ListHead, Entry;
    PKDEVICE_QUEUE_ENTRY Current;

    ListHead = &DeviceQueue->DeviceListHead;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        Current = CONTAINING_RECORD(Entry, KDEVICE_QUEUE_ENTRY, DeviceListEntry);
        if (Current->SortKey >= SortKey)
            return Current;
    }

    return CONTAINING_RECORD(ListHead->Flink,
                             KDEVICE_QUEUE_ENTRY,
                             DeviceListEntry);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * A device queue is busy or it is not. While it is not, a request handed to it
 * is not queued at all: the caller is told to start it straight away, and the
 * queue only begins holding requests once one is in flight.
 */
VOID
NTAPI
KeInitializeDeviceQueue(
    _Out_ PKDEVICE_QUEUE DeviceQueue)
{
    DeviceQueue->Type = DeviceQueueObject;
    DeviceQueue->Size = sizeof(KDEVICE_QUEUE);
    InitializeListHead(&DeviceQueue->DeviceListHead);
    KeInitializeSpinLock(&DeviceQueue->Lock);
    DeviceQueue->Busy = FALSE;
}

/*
 * Offers a request to the queue. Answers FALSE when the device was idle, which
 * means the queue took nothing and the caller owns the device from here.
 */
BOOLEAN
NTAPI
KeInsertDeviceQueue(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _Inout_ PKDEVICE_QUEUE_ENTRY DeviceQueueEntry)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    BOOLEAN Inserted;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);

    DeviceQueueEntry->SortKey = 0;

    if (!DeviceQueue->Busy)
    {
        Inserted = FALSE;
        DeviceQueue->Busy = TRUE;
    }
    else
    {
        Inserted = TRUE;
        InsertTailList(&DeviceQueue->DeviceListHead,
                       &DeviceQueueEntry->DeviceListEntry);
    }

    DeviceQueueEntry->Inserted = Inserted;

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Inserted;
}

/*
 * As above, with a key the queue keeps its entries ordered by. A driver uses
 * it to serve requests in an order the device prefers rather than the order
 * they arrived in.
 */
BOOLEAN
NTAPI
KeInsertByKeyDeviceQueue(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _Inout_ PKDEVICE_QUEUE_ENTRY DeviceQueueEntry,
    _In_ ULONG SortKey)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    BOOLEAN Inserted;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);

    DeviceQueueEntry->SortKey = SortKey;

    if (!DeviceQueue->Busy)
    {
        Inserted = FALSE;
        DeviceQueue->Busy = TRUE;
    }
    else
    {
        Inserted = TRUE;
        KiInsertDeviceQueueByKey(DeviceQueue, DeviceQueueEntry, SortKey);
    }

    DeviceQueueEntry->Inserted = Inserted;

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Inserted;
}

/*
 * Asks for the next request. A NULL answer means there was nothing left, and
 * the queue has gone idle: the next insertion will again be told to start the
 * request itself.
 */
PKDEVICE_QUEUE_ENTRY
NTAPI
KeRemoveDeviceQueue(
    _Inout_ PKDEVICE_QUEUE DeviceQueue)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    PKDEVICE_QUEUE_ENTRY Entry = NULL;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);
    ASSERT(DeviceQueue->Busy);

    if (IsListEmpty(&DeviceQueue->DeviceListHead))
    {
        DeviceQueue->Busy = FALSE;
    }
    else
    {
        Entry = CONTAINING_RECORD(RemoveHeadList(&DeviceQueue->DeviceListHead),
                                  KDEVICE_QUEUE_ENTRY,
                                  DeviceListEntry);
        Entry->Inserted = FALSE;
    }

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Entry;
}

/*
 * Asks for the next request at or past a key, wrapping to the head when the
 * key is beyond everything queued. That wrap is what lets a driver sweep a
 * device in one direction and start again from the beginning.
 */
PKDEVICE_QUEUE_ENTRY
NTAPI
KeRemoveByKeyDeviceQueue(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _In_ ULONG SortKey)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    PKDEVICE_QUEUE_ENTRY Entry = NULL;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);
    ASSERT(DeviceQueue->Busy);

    if (IsListEmpty(&DeviceQueue->DeviceListHead))
    {
        DeviceQueue->Busy = FALSE;
    }
    else
    {
        Entry = KiFindDeviceQueueEntryByKey(DeviceQueue, SortKey);
        RemoveEntryList(&Entry->DeviceListEntry);
        Entry->Inserted = FALSE;
    }

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Entry;
}

/*
 * As above, except that a queue which was already idle is left idle and the
 * answer is NULL. A driver whose own state says the device may be free uses
 * this so that it does not claim the device by asking.
 */
PKDEVICE_QUEUE_ENTRY
NTAPI
KeRemoveByKeyDeviceQueueIfBusy(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _In_ ULONG SortKey)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    PKDEVICE_QUEUE_ENTRY Entry = NULL;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);

    if (!DeviceQueue->Busy)
    {
        KiReleaseDeviceQueueLock(&DeviceLock);
        return NULL;
    }

    if (IsListEmpty(&DeviceQueue->DeviceListHead))
    {
        DeviceQueue->Busy = FALSE;
    }
    else
    {
        Entry = KiFindDeviceQueueEntryByKey(DeviceQueue, SortKey);
        RemoveEntryList(&Entry->DeviceListEntry);
        Entry->Inserted = FALSE;
    }

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Entry;
}

/*
 * Takes one particular request back out. Answers FALSE when it was not in the
 * queue, which is how a cancellation learns that the request has already been
 * handed to the device.
 */
BOOLEAN
NTAPI
KeRemoveEntryDeviceQueue(
    _Inout_ PKDEVICE_QUEUE DeviceQueue,
    _Inout_ PKDEVICE_QUEUE_ENTRY DeviceQueueEntry)
{
    KLOCK_QUEUE_HANDLE DeviceLock;
    BOOLEAN Removed;

    ASSERT_DEVICE_QUEUE(DeviceQueue);

    KiAcquireDeviceQueueLock(DeviceQueue, &DeviceLock);

    Removed = DeviceQueueEntry->Inserted;
    if (Removed)
    {
        DeviceQueueEntry->Inserted = FALSE;
        RemoveEntryList(&DeviceQueueEntry->DeviceListEntry);
    }

    KiReleaseDeviceQueueLock(&DeviceLock);
    return Removed;
}

/* EOF */
