/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling drivers when a device or an interface changes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <wdmguid.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PiNotifyDeviceInterfaceListHead;
KGUARDED_MUTEX PiNotifyDeviceInterfaceLock;

LIST_ENTRY PiNotifyHwProfileListHead;
KGUARDED_MUTEX PiNotifyHwProfileLock;

KGUARDED_MUTEX PiNotifyTargetDeviceLock;

/* PRIVATE DATA ***************************************************************/

static ULONGLONG PiNotifyTargetRegistrations;

typedef struct _PI_NOTIFY_CALL
{
    LIST_ENTRY ListEntry;
    PKTHREAD Thread;
} PI_NOTIFY_CALL, *PPI_NOTIFY_CALL;

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

/*
 * Called with the lock of the entry's list held. An unregistered entry stays
 * linked until its last reference goes, so a walk that let go of the lock to
 * call it can still step from it to the next one.
 */
static
VOID
PiDereferenceNotifyEntry(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry)
{
    if (InterlockedDecrement(&Entry->ReferenceCount) != 0) return;

    ASSERT(Entry->Unregistered);
    RemoveEntryList(&Entry->ListEntry);

    if (Entry->DeviceObject != NULL) ObDereferenceObject(Entry->DeviceObject);
    if (Entry->DriverObject != NULL) ObDereferenceObject(Entry->DriverObject);
    if (Entry->Session != NULL) MmQuitNextSession(Entry->Session);

    ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
}

static
BOOLEAN
PiNotifyCallsElsewhere(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry,
    _In_opt_ PKTHREAD Thread)
{
    PLIST_ENTRY ListEntry;

    for (ListEntry = Entry->Calls.Flink; ListEntry != &Entry->Calls; ListEntry = ListEntry->Flink)
    {
        if (CONTAINING_RECORD(ListEntry, PI_NOTIFY_CALL, ListEntry)->Thread != Thread) return TRUE;
    }

    return FALSE;
}

static
VOID
PiEnterNotifyCall(
    _Inout_ PIOP_PNP_NOTIFY_ENTRY Entry,
    _Out_ PPI_NOTIFY_CALL Call)
{
    ASSERT(!Entry->Unregistered);

    PiReferenceNotifyEntry(Entry);
    Call->Thread = KeGetCurrentThread();
    InsertTailList(&Entry->Calls, &Call->ListEntry);
}

static
VOID
PiLeaveNotifyCall(
    _Inout_ PIOP_PNP_NOTIFY_ENTRY Entry,
    _Inout_ PPI_NOTIFY_CALL Call)
{
    ASSERT(Call->Thread == KeGetCurrentThread());
    ASSERT(Entry->ReferenceCount > 0);

    RemoveEntryList(&Call->ListEntry);

    if (Entry->CallsDone != NULL && !PiNotifyCallsElsewhere(Entry, Entry->Unregistering))
    {
        ASSERT(Entry->Unregistered);
        KeSetEvent(Entry->CallsDone, IO_NO_INCREMENT, FALSE);
    }
}

/* A routine a session driver registered runs inside that driver's session */
static
NTSTATUS
PiInvokeNotifyEntry(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry,
    _In_ PVOID NotificationStructure)
{
    KAPC_STATE ApcState;
    NTSTATUS Status;

    PAGED_CODE();

    if (Entry->Session == NULL)
    {
        ASSERT(!MmIsSessionAddress((PVOID)Entry->CallbackRoutine));
        return Entry->CallbackRoutine(NotificationStructure, Entry->Context);
    }

    Status = MmAttachSession(Entry->Session, &ApcState);
    if (!NT_SUCCESS(Status))
    {
        /* The session is ending, and its drivers with it */
        DPRINT1("PnP: notification %p skipped, its session is going (%lx)\n", Entry, Status);
        return STATUS_SUCCESS;
    }

    Status = Entry->CallbackRoutine(NotificationStructure, Entry->Context);

    MmDetachSession(Entry->Session, &ApcState);

    return Status;
}

static
NTSTATUS
PiBindNotifySession(
    _Inout_ PIOP_PNP_NOTIFY_ENTRY Entry)
{
    if (!MmIsSessionAddress((PVOID)Entry->CallbackRoutine)) return STATUS_SUCCESS;

    Entry->Session = MmGetSessionById(MmGetSessionId(PsGetCurrentProcess()));
    ASSERT(Entry->Session != NULL);

    return (Entry->Session != NULL) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static
VOID
PiReportExistingInterfaces(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry)
{
    DEVICE_INTERFACE_CHANGE_NOTIFICATION Notification;
    UNICODE_STRING Name;
    PZZWSTR List;
    PWSTR Link;

    PAGED_CODE();

    if (!NT_SUCCESS(IoGetDeviceInterfaces(&Entry->ClassGuid, NULL, 0, &List))) return;

    for (Link = List; *Link != UNICODE_NULL; Link += wcslen(Link) + 1)
    {
        RtlInitUnicodeString(&Name, Link);

        RtlZeroMemory(&Notification, sizeof(Notification));
        Notification.Version = 1;
        Notification.Size = sizeof(Notification);
        Notification.Event = GUID_DEVICE_INTERFACE_ARRIVAL;
        Notification.InterfaceClassGuid = Entry->ClassGuid;
        Notification.SymbolicLinkName = &Name;

        PiInvokeNotifyEntry(Entry, &Notification);
    }

    ExFreePool(List);
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
    PI_NOTIFY_CALL Call;

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

        /* Asking for the existing interfaces too does not widen the class registered for */
        if (ClassGuid != NULL &&
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

        PiEnterNotifyCall(Entry, &Call);

        KeReleaseGuardedMutex(Lock);

        PiInvokeNotifyEntry(Entry, NotificationStructure);

        KeAcquireGuardedMutex(Lock);

        PiLeaveNotifyCall(Entry, &Call);
        ListEntry = Entry->ListEntry.Flink;
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

/* Calls the registrants of a device in turn; with StopOnFailure, the first one that fails ends the round and is named */
static
NTSTATUS
PiRunTargetNotify(
    _In_ LPCGUID Event,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PTARGET_DEVICE_CUSTOM_NOTIFICATION CustomNotification,
    _In_ BOOLEAN StopOnFailure,
    _Out_opt_ PDRIVER_OBJECT *VetoDriver)
{
    TARGET_DEVICE_REMOVAL_NOTIFICATION Notification;
    PIOP_PNP_NOTIFY_ENTRY Entry;
    PDEVICE_NODE DeviceNode;
    PLIST_ENTRY ListEntry;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG Last;
    PI_NOTIFY_CALL Call;
    PVOID Payload;

    PAGED_CODE();
    ASSERT(!StopOnFailure || VetoDriver != NULL);

    if (VetoDriver != NULL) *VetoDriver = NULL;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_SUCCESS;

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

    Last = PiNotifyTargetRegistrations;

    ListEntry = DeviceNode->TargetDeviceNotify.Flink;
    while (ListEntry != &DeviceNode->TargetDeviceNotify)
    {
        Entry = CONTAINING_RECORD(ListEntry,
                                  IOP_PNP_NOTIFY_ENTRY,
                                  ListEntry);
        ListEntry = ListEntry->Flink;

        /* A registration made during the round, as a cancelled removal's handler makes one, waits for the next round */
        if (Entry->Unregistered || Entry->Registration > Last) continue;

        if (CustomNotification != NULL)
        {
            CustomNotification->FileObject = Entry->FileObject;
        }
        else
        {
            Notification.FileObject = Entry->FileObject;
        }

        PiEnterNotifyCall(Entry, &Call);

        KeReleaseGuardedMutex(&PiNotifyTargetDeviceLock);

        Status = PiInvokeNotifyEntry(Entry, Payload);

        KeAcquireGuardedMutex(&PiNotifyTargetDeviceLock);

        PiLeaveNotifyCall(Entry, &Call);

        if (StopOnFailure && !NT_SUCCESS(Status))
        {
            ObReferenceObject(Entry->DriverObject);
            *VetoDriver = Entry->DriverObject;
            PiDereferenceNotifyEntry(Entry);
            break;
        }

        Status = STATUS_SUCCESS;
        ListEntry = Entry->ListEntry.Flink;
        PiDereferenceNotifyEntry(Entry);
    }

    KeReleaseGuardedMutex(&PiNotifyTargetDeviceLock);

    return Status;
}

CODE_SEG("PAGE")
VOID
PiNotifyTargetDeviceChange(
    _In_ LPCGUID Event,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PTARGET_DEVICE_CUSTOM_NOTIFICATION CustomNotification)
{
    PAGED_CODE();

    PiRunTargetNotify(Event, DeviceObject, CustomNotification, FALSE, NULL);
}

/* The registrants are asked before the drivers; the first one that fails keeps the device */
CODE_SEG("PAGE")
NTSTATUS
PiNotifyTargetDeviceQueryRemove(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDRIVER_OBJECT *VetoDriver)
{
    PAGED_CODE();

    return PiRunTargetNotify(&GUID_TARGET_DEVICE_QUERY_REMOVE, DeviceObject, NULL, TRUE, VetoDriver);
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

CODE_SEG("PAGE")
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
    InitializeListHead(&Entry->Calls);

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

            /* The PnP manager holds no reference on the file object (IoRegisterPlugPlayNotification documentation) */
            Entry->FileObject = EventCategoryData;

            if (!NT_SUCCESS(IopGetRelatedTargetDevice(Entry->FileObject, &DeviceNode)))
            {
                ObDereferenceObject(DriverObject);
                ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
                return STATUS_INVALID_DEVICE_REQUEST;
            }

            Entry->DeviceObject = DeviceNode->PhysicalDeviceObject;
            ObReferenceObject(Entry->DeviceObject);

            if (!NT_SUCCESS(PiBindNotifySession(Entry)))
            {
                ObDereferenceObject(Entry->DeviceObject);
                ObDereferenceObject(DriverObject);
                ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
                return STATUS_INVALID_PARAMETER;
            }

            KeAcquireGuardedMutex(&PiNotifyTargetDeviceLock);
            Entry->Registration = ++PiNotifyTargetRegistrations;
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

    if (!NT_SUCCESS(PiBindNotifySession(Entry)))
    {
        ObDereferenceObject(DriverObject);
        ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireGuardedMutex(Lock);
    InsertTailList(ListHead, &Entry->ListEntry);
    KeReleaseGuardedMutex(Lock);

    *NotificationEntry = Entry;

    if ((EventCategory == EventCategoryDeviceInterfaceChange) &&
        (EventCategoryFlags & PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES))
    {
        PiReportExistingInterfaces(Entry);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
PiUnregisterNotifyEntry(
    _In_ PIOP_PNP_NOTIFY_ENTRY Entry,
    _In_ BOOLEAN WaitForCalls)
{
    PKGUARDED_MUTEX Lock;
    KEVENT CallsDone;

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

    if (Entry->Unregistered)
    {
        KeReleaseGuardedMutex(Lock);
        return STATUS_SUCCESS;
    }

    Entry->Unregistered = TRUE;

    /* The callback the caller may be running in is the only one left once the routine returns */
    if (WaitForCalls && PiNotifyCallsElsewhere(Entry, KeGetCurrentThread()))
    {
        ASSERT(Entry->CallsDone == NULL);

        KeInitializeEvent(&CallsDone, NotificationEvent, FALSE);
        Entry->CallsDone = &CallsDone;
        Entry->Unregistering = KeGetCurrentThread();

        KeReleaseGuardedMutex(Lock);
        KeWaitForSingleObject(&CallsDone, Executive, KernelMode, FALSE, NULL);
        KeAcquireGuardedMutex(Lock);

        ASSERT(!PiNotifyCallsElsewhere(Entry, KeGetCurrentThread()));
        Entry->CallsDone = NULL;
        Entry->Unregistering = NULL;
    }

    /* The registration's own reference goes; the entry leaves its list with the last one */
    PiDereferenceNotifyEntry(Entry);

    KeReleaseGuardedMutex(Lock);

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
IoUnregisterPlugPlayNotification(
    _In_ PVOID NotificationEntry)
{
    PAGED_CODE();

    return PiUnregisterNotifyEntry(NotificationEntry, FALSE);
}

NTSTATUS
NTAPI
IoUnregisterPlugPlayNotificationEx(
    _In_ PVOID NotificationEntry)
{
    PAGED_CODE();

    return PiUnregisterNotifyEntry(NotificationEntry, TRUE);
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
