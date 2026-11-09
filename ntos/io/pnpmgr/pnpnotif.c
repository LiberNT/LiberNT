/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling drivers when a device or an interface changes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PiNotifyDeviceInterfaceListHead;
KGUARDED_MUTEX PiNotifyDeviceInterfaceLock;

LIST_ENTRY PiNotifyHwProfileListHead;
KGUARDED_MUTEX PiNotifyHwProfileLock;

KGUARDED_MUTEX PiNotifyTargetDeviceLock;

/* PRIVATE DATA ***************************************************************/

typedef struct _IOP_ASYNC_TARGET_CHANGE
{
    WORK_QUEUE_ITEM Item;
    PDEVICE_OBJECT DeviceObject;
    TARGET_DEVICE_CUSTOM_NOTIFICATION Notification;
} IOP_ASYNC_TARGET_CHANGE, *PIOP_ASYNC_TARGET_CHANGE;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
PiInitializeNotifications(VOID)
{
    InitializeListHead(&PiNotifyDeviceInterfaceListHead);
    KeInitializeGuardedMutex(&PiNotifyDeviceInterfaceLock);

    InitializeListHead(&PiNotifyHwProfileListHead);
    KeInitializeGuardedMutex(&PiNotifyHwProfileLock);

    KeInitializeGuardedMutex(&PiNotifyTargetDeviceLock);
}

static
VOID
PiReferenceNotifyEntry(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry)
{
    InterlockedIncrement(&Entry->ReferenceCount);
}

static
VOID
PiDereferenceNotifyEntry(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry)
{
    if (InterlockedDecrement(&Entry->ReferenceCount) != 0) return;

    if (Entry->FileObject != NULL) ObDereferenceObject(Entry->FileObject);
    if (Entry->DeviceObject != NULL) ObDereferenceObject(Entry->DeviceObject);
    if (Entry->DriverObject != NULL) ObDereferenceObject(Entry->DriverObject);

    ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
}

static
VOID
PiRunNotifyList(
    _In_ PLIST_ENTRY ListHead,
    _In_ PKGUARDED_MUTEX Lock,
    _In_ PVOID NotificationStructure,
    _In_opt_ LPCGUID ClassGuid,
    _In_opt_ PFILE_OBJECT TargetFileObject)
{
    PIOP_PNP_NOTIFY_ENTRY Entry;
    PLIST_ENTRY ListEntry;

    PAGED_CODE();

    KeAcquireGuardedMutex(Lock);

    ListEntry = ListHead->Flink;
    while (ListEntry != ListHead)
    {
        Entry = CONTAINING_RECORD(ListEntry,
                                  IOP_PNP_NOTIFY_ENTRY,
                                  ListEntry);
        ListEntry = ListEntry->Flink;

        if (Entry->Unregistered) continue;

        if (ClassGuid != NULL &&
            !(Entry->EventCategoryFlags &
              PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES) &&
            RtlCompareMemory(&Entry->ClassGuid,
                             ClassGuid,
                             sizeof(GUID)) != sizeof(GUID))
        {
            continue;
        }

        if (TargetFileObject != NULL && Entry->FileObject != TargetFileObject)
        {
            continue;
        }

        PiReferenceNotifyEntry(Entry);

        KeReleaseGuardedMutex(Lock);

        Entry->CallbackRoutine(NotificationStructure, Entry->Context);

        KeAcquireGuardedMutex(Lock);

        PiDereferenceNotifyEntry(Entry);
    }

    KeReleaseGuardedMutex(Lock);
}

CODE_SEG("PAGE")
VOID
PiNotifyDeviceInterfaceChange(
    _In_ LPCGUID Event,
    _In_ LPCGUID InterfaceClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    DEVICE_INTERFACE_CHANGE_NOTIFICATION Notification;

    PAGED_CODE();

    RtlZeroMemory(&Notification, sizeof(Notification));

    Notification.Version = 1;
    Notification.Size = sizeof(Notification);
    Notification.Event = *Event;
    Notification.InterfaceClassGuid = *InterfaceClassGuid;
    Notification.SymbolicLinkName = SymbolicLinkName;

    PiRunNotifyList(&PiNotifyDeviceInterfaceListHead,
                    &PiNotifyDeviceInterfaceLock,
                    &Notification,
                    InterfaceClassGuid,
                    NULL);

    IopQueueDeviceChangeEvent(Event, InterfaceClassGuid, SymbolicLinkName);
}

CODE_SEG("PAGE")
VOID
PiNotifyHardwareProfileChange(
    _In_ LPCGUID Event)
{
    HWPROFILE_CHANGE_NOTIFICATION Notification;

    PAGED_CODE();

    RtlZeroMemory(&Notification, sizeof(Notification));

    Notification.Version = 1;
    Notification.Size = sizeof(Notification);
    Notification.Event = *Event;

    PiRunNotifyList(&PiNotifyHwProfileListHead,
                    &PiNotifyHwProfileLock,
                    &Notification,
                    NULL,
                    NULL);
}

CODE_SEG("PAGE")
VOID
PiNotifyTargetDeviceChange(
    _In_ LPCGUID Event,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PTARGET_DEVICE_CUSTOM_NOTIFICATION CustomNotification)
{
    TARGET_DEVICE_REMOVAL_NOTIFICATION Notification;
    PIOP_PNP_NOTIFY_ENTRY Entry;
    PDEVICE_NODE DeviceNode;
    PLIST_ENTRY ListEntry;
    PVOID Payload;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return;

    if (CustomNotification != NULL)
    {
        Payload = CustomNotification;
    }
    else
    {
        RtlZeroMemory(&Notification, sizeof(Notification));

        Notification.Version = 1;
        Notification.Size = sizeof(Notification);
        Notification.Event = *Event;

        Payload = &Notification;
    }

    KeAcquireGuardedMutex(&PiNotifyTargetDeviceLock);

    ListEntry = DeviceNode->TargetDeviceNotify.Flink;
    while (ListEntry != &DeviceNode->TargetDeviceNotify)
    {
        Entry = CONTAINING_RECORD(ListEntry,
                                  IOP_PNP_NOTIFY_ENTRY,
                                  ListEntry);
        ListEntry = ListEntry->Flink;

        if (Entry->Unregistered) continue;

        if (CustomNotification != NULL)
        {
            CustomNotification->FileObject = Entry->FileObject;
        }
        else
        {
            Notification.FileObject = Entry->FileObject;
        }

        PiReferenceNotifyEntry(Entry);

        KeReleaseGuardedMutex(&PiNotifyTargetDeviceLock);

        Entry->CallbackRoutine(Payload, Entry->Context);

        KeAcquireGuardedMutex(&PiNotifyTargetDeviceLock);

        PiDereferenceNotifyEntry(Entry);
    }

    KeReleaseGuardedMutex(&PiNotifyTargetDeviceLock);
}

VOID
NTAPI
IopReportTargetDeviceChangeAsyncWorker(
    _In_ PVOID Context)
{
    PIOP_ASYNC_TARGET_CHANGE Async = Context;

    PAGED_CODE();

    PiNotifyTargetDeviceChange(&Async->Notification.Event,
                               Async->DeviceObject,
                               &Async->Notification);

    ExFreePoolWithTag(Async, TAG_PNP_NOTIFY);
}

NTSTATUS
PpSetCustomTargetEvent(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PKEVENT SyncEvent,
    _Out_opt_ PNTSTATUS SyncStatus,
    _In_opt_ PDEVICE_CHANGE_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _In_ PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification)
{
    PDEVICE_NODE DeviceNode;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    PiNotifyTargetDeviceChange(&Notification->Event,
                               DeviceObject,
                               Notification);

    IopQueueCustomTargetEvent(&DeviceNode->InstancePath, Notification);

    if (Callback != NULL) Callback(Context);
    if (SyncStatus != NULL) *SyncStatus = STATUS_SUCCESS;
    if (SyncEvent != NULL) KeSetEvent(SyncEvent, IO_NO_INCREMENT, FALSE);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoRegisterPlugPlayNotification(
    _In_ IO_NOTIFICATION_EVENT_CATEGORY EventCategory,
    _In_ ULONG EventCategoryFlags,
    _In_opt_ PVOID EventCategoryData,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_NOTIFICATION_CALLBACK_ROUTINE CallbackRoutine,
    _Inout_opt_ PVOID Context,
    _Outptr_ PVOID *NotificationEntry)
{
    PIOP_PNP_NOTIFY_ENTRY Entry;
    PKGUARDED_MUTEX Lock;
    PLIST_ENTRY ListHead;
    PDEVICE_NODE DeviceNode;

    PAGED_CODE();

    *NotificationEntry = NULL;

    Entry = ExAllocatePoolZero(NonPagedPool,
                               sizeof(IOP_PNP_NOTIFY_ENTRY),
                               TAG_PNP_NOTIFY);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entry->EventCategory = EventCategory;
    Entry->EventCategoryFlags = EventCategoryFlags;
    Entry->CallbackRoutine = CallbackRoutine;
    Entry->Context = Context;
    Entry->DriverObject = DriverObject;
    Entry->ReferenceCount = 1;

    ObReferenceObject(DriverObject);

    switch (EventCategory)
    {
        case EventCategoryDeviceInterfaceChange:
            if (EventCategoryData != NULL)
            {
                Entry->ClassGuid = *(LPGUID)EventCategoryData;
            }

            ListHead = &PiNotifyDeviceInterfaceListHead;
            Lock = &PiNotifyDeviceInterfaceLock;
            break;

        case EventCategoryHardwareProfileChange:
            ListHead = &PiNotifyHwProfileListHead;
            Lock = &PiNotifyHwProfileLock;
            break;

        case EventCategoryTargetDeviceChange:
            if (EventCategoryData == NULL)
            {
                ObDereferenceObject(DriverObject);
                ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
                return STATUS_INVALID_PARAMETER;
            }

            Entry->FileObject = EventCategoryData;
            ObReferenceObject(Entry->FileObject);

            DeviceNode = IopGetDeviceNode(
                IoGetRelatedDeviceObject(Entry->FileObject));
            if (DeviceNode == NULL)
            {
                ObDereferenceObject(Entry->FileObject);
                ObDereferenceObject(DriverObject);
                ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
                return STATUS_INVALID_DEVICE_REQUEST;
            }

            Entry->DeviceObject = DeviceNode->PhysicalDeviceObject;
            ObReferenceObject(Entry->DeviceObject);

            KeAcquireGuardedMutex(&PiNotifyTargetDeviceLock);
            InsertTailList(&DeviceNode->TargetDeviceNotify,
                           &Entry->ListEntry);
            KeReleaseGuardedMutex(&PiNotifyTargetDeviceLock);

            *NotificationEntry = Entry;

            return STATUS_SUCCESS;

        default:
            ObDereferenceObject(DriverObject);
            ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
            return STATUS_INVALID_PARAMETER;
    }

    KeAcquireGuardedMutex(Lock);
    InsertTailList(ListHead, &Entry->ListEntry);
    KeReleaseGuardedMutex(Lock);

    *NotificationEntry = Entry;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoUnregisterPlugPlayNotification(
    _In_ PVOID NotificationEntry)
{
    PIOP_PNP_NOTIFY_ENTRY Entry = NotificationEntry;
    PKGUARDED_MUTEX Lock;

    PAGED_CODE();

    switch (Entry->EventCategory)
    {
        case EventCategoryDeviceInterfaceChange:
            Lock = &PiNotifyDeviceInterfaceLock;
            break;

        case EventCategoryHardwareProfileChange:
            Lock = &PiNotifyHwProfileLock;
            break;

        case EventCategoryTargetDeviceChange:
            Lock = &PiNotifyTargetDeviceLock;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    KeAcquireGuardedMutex(Lock);

    if (!Entry->Unregistered)
    {
        Entry->Unregistered = TRUE;
        RemoveEntryList(&Entry->ListEntry);
    }

    KeReleaseGuardedMutex(Lock);

    PiDereferenceNotifyEntry(Entry);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoUnregisterPlugPlayNotificationEx(
    _In_ PVOID NotificationEntry)
{
    return IoUnregisterPlugPlayNotification(NotificationEntry);
}

NTSTATUS
NTAPI
IoReportTargetDeviceChange(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PVOID NotificationStructure)
{
    PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification = NotificationStructure;

    PAGED_CODE();

    if (Notification->Version != 1) return STATUS_INVALID_PARAMETER;
    if (Notification->Size < sizeof(TARGET_DEVICE_CUSTOM_NOTIFICATION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return PpSetCustomTargetEvent(PhysicalDeviceObject,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  Notification);
}

NTSTATUS
NTAPI
IoReportTargetDeviceChangeAsynchronous(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PVOID NotificationStructure,
    _In_opt_ PDEVICE_CHANGE_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PTARGET_DEVICE_CUSTOM_NOTIFICATION Source = NotificationStructure;
    PIOP_ASYNC_TARGET_CHANGE Async;
    ULONG Size;

    PAGED_CODE();

    if (Source->Version != 1) return STATUS_INVALID_PARAMETER;
    if (Source->Size < sizeof(TARGET_DEVICE_CUSTOM_NOTIFICATION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Size = (ULONG)FIELD_OFFSET(IOP_ASYNC_TARGET_CHANGE, Notification) +
           Source->Size;

    Async = ExAllocatePoolZero(NonPagedPool, Size, TAG_PNP_NOTIFY);
    if (Async == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(&Async->Notification, Source, Source->Size);
    Async->DeviceObject = PhysicalDeviceObject;

    ExInitializeWorkItem(&Async->Item,
                         IopReportTargetDeviceChangeAsyncWorker,
                         Async);
    ExQueueWorkItem(&Async->Item, DelayedWorkQueue);

    if (Callback != NULL) Callback(Context);

    return STATUS_SUCCESS;
}
