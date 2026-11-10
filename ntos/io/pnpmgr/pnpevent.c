/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The queue the user-mode PnP manager reads its work from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <umpnpmgr/sysguid.h>
#include <wdmguid.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY IopPnpEventQueueHead;
KGUARDED_MUTEX IopPnpEventQueueLock;
KEVENT IopPnpEventQueueSemaphore;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
NTSTATUS
IopInitPlugPlayEvents(VOID)
{
    InitializeListHead(&IopPnpEventQueueHead);
    KeInitializeGuardedMutex(&IopPnpEventQueueLock);
    KeInitializeEvent(&IopPnpEventQueueSemaphore, NotificationEvent, FALSE);

    return STATUS_SUCCESS;
}

static
PIOP_PLUGPLAY_EVENT_BLOCK
IopAllocatePnpEvent(
    _In_ const GUID *EventGuid,
    _In_ PLUGPLAY_EVENT_CATEGORY Category,
    _In_ ULONG ExtraLength)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    ULONG Total;

    Total = (ULONG)FIELD_OFFSET(IOP_PLUGPLAY_EVENT_BLOCK, Event) +
            (ULONG)sizeof(PLUGPLAY_EVENT_BLOCK) + ExtraLength;

    Entry = ExAllocatePoolZero(PagedPool, Total, TAG_PNP_NOTIFY);
    if (Entry == NULL) return NULL;

    Entry->Size = Total;

    Entry->Event.EventGuid = *EventGuid;
    Entry->Event.EventCategory = Category;
    Entry->Event.TotalSize =
        (ULONG)sizeof(PLUGPLAY_EVENT_BLOCK) + ExtraLength;

    return Entry;
}

static
VOID
IopPostPnpEvent(
    _In_ PIOP_PLUGPLAY_EVENT_BLOCK Entry)
{
    KeAcquireGuardedMutex(&IopPnpEventQueueLock);

    InsertTailList(&IopPnpEventQueueHead, &Entry->ListEntry);

    KeReleaseGuardedMutex(&IopPnpEventQueueLock);

    KeSetEvent(&IopPnpEventQueueSemaphore, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
IopQueueDeviceChangeEvent(
    _In_ const GUID *EventGuid,
    _In_ const GUID *InterfaceClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    ULONG Extra;

    PAGED_CODE();

    Extra = SymbolicLinkName->Length + sizeof(WCHAR);

    Entry = IopAllocatePnpEvent(EventGuid,
                                DeviceClassChangeEvent,
                                Extra);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entry->Event.DeviceClass.ClassGuid = *InterfaceClassGuid;

    RtlCopyMemory(Entry->Event.DeviceClass.SymbolicLinkName,
                  SymbolicLinkName->Buffer,
                  SymbolicLinkName->Length);

    IopPostPnpEvent(Entry);

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueueTargetDeviceEvent(
    _In_ const GUID *Guid,
    _In_ PUNICODE_STRING DeviceIds)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    ULONG Extra;

    PAGED_CODE();

    Extra = DeviceIds->Length + 2 * sizeof(WCHAR);

    Entry = IopAllocatePnpEvent(Guid, TargetDeviceChangeEvent, Extra);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Entry->Event.TargetDevice.DeviceIds,
                  DeviceIds->Buffer,
                  DeviceIds->Length);

    IopPostPnpEvent(Entry);

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueueCustomTargetEvent(
    _In_ PUNICODE_STRING DeviceIds,
    _In_ PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    ULONG Extra;

    PAGED_CODE();

    Extra = DeviceIds->Length + 2 * sizeof(WCHAR);

    Entry = IopAllocatePnpEvent(&Notification->Event,
                                CustomDeviceEvent,
                                Extra);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entry->Event.CustomNotification.NotificationStructure = NULL;

    RtlCopyMemory(Entry->Event.CustomNotification.DeviceIds,
                  DeviceIds->Buffer,
                  DeviceIds->Length);

    IopPostPnpEvent(Entry);

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueueDeviceInstallEvent(
    _In_ const GUID *Guid,
    _In_opt_ PUNICODE_STRING DeviceId)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    ULONG Extra;

    PAGED_CODE();

    Extra = ((DeviceId != NULL) ? DeviceId->Length : 0) + 2 * sizeof(WCHAR);

    Entry = IopAllocatePnpEvent(Guid, DeviceInstallEvent, Extra);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (DeviceId != NULL)
    {
        RtlCopyMemory(Entry->Event.InstallDevice.DeviceId,
                      DeviceId->Buffer,
                      DeviceId->Length);
    }

    IopPostPnpEvent(Entry);

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueuePowerEvent(
    _In_ ULONG NotificationCode,
    _In_ ULONG NotificationData,
    _In_reads_bytes_opt_(PayloadSize) const VOID *Payload,
    _In_ ULONG PayloadSize,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Timeout);

    Entry = IopAllocatePnpEvent(&GUID_DEVICE_NOOP,
                                PowerEvent,
                                PayloadSize);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entry->Event.PowerNotification.NotificationCode = NotificationCode;
    Entry->Event.PowerNotification.NotificationData = NotificationData;

    if (Payload != NULL && PayloadSize != 0)
    {
        RtlCopyMemory(&Entry->Event + 1, Payload, PayloadSize);
    }

    IopPostPnpEvent(Entry);

    return STATUS_SUCCESS;
}

VOID
IopDisableDeviceInterfaces(
    _In_ PDEVICE_NODE DeviceNode)
{
    PIOP_DEVICE_INTERFACE Interface;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (DeviceNode == NULL) return;

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    Entry = IopDeviceInterfaceListHead.Flink;
    while (Entry != &IopDeviceInterfaceListHead)
    {
        Interface = CONTAINING_RECORD(Entry,
                                      IOP_DEVICE_INTERFACE,
                                      ListEntry);
        Entry = Entry->Flink;

        if (Interface->PhysicalDeviceObject !=
            DeviceNode->PhysicalDeviceObject)
        {
            continue;
        }

        if (!Interface->Enabled) continue;

        Interface->Enabled = FALSE;

        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

        PiNotifyDeviceInterfaceChange(&GUID_DEVICE_INTERFACE_REMOVAL,
                                      &Interface->ClassGuid,
                                      &Interface->SymbolicLinkName);

        KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

        Entry = IopDeviceInterfaceListHead.Flink;
    }

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
NtGetPlugPlayEvent(
    _In_ ULONG Reserved1,
    _In_ ULONG Reserved2,
    _Out_ PPLUGPLAY_EVENT_BLOCK Buffer,
    _In_ ULONG BufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIOP_PLUGPLAY_EVENT_BLOCK Entry;
    PLIST_ENTRY ListEntry;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Reserved1);
    UNREFERENCED_PARAMETER(Reserved2);

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (BufferSize < sizeof(PLUGPLAY_EVENT_BLOCK))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Buffer, BufferSize, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = KeWaitForSingleObject(&IopPnpEventQueueSemaphore,
                                   UserRequest,
                                   PreviousMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status)) return Status;

    KeAcquireGuardedMutex(&IopPnpEventQueueLock);

    if (IsListEmpty(&IopPnpEventQueueHead))
    {
        KeClearEvent(&IopPnpEventQueueSemaphore);
        KeReleaseGuardedMutex(&IopPnpEventQueueLock);
        return STATUS_NO_MORE_ENTRIES;
    }

    ListEntry = IopPnpEventQueueHead.Flink;
    Entry = CONTAINING_RECORD(ListEntry,
                              IOP_PLUGPLAY_EVENT_BLOCK,
                              ListEntry);

    if (Entry->Event.TotalSize > BufferSize)
    {
        KeReleaseGuardedMutex(&IopPnpEventQueueLock);
        return STATUS_BUFFER_TOO_SMALL;
    }

    RemoveEntryList(ListEntry);

    if (IsListEmpty(&IopPnpEventQueueHead))
    {
        KeClearEvent(&IopPnpEventQueueSemaphore);
    }

    KeReleaseGuardedMutex(&IopPnpEventQueueLock);

    Status = STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Buffer, &Entry->Event, Entry->Event.TotalSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Buffer, &Entry->Event, Entry->Event.TotalSize);
    }

    ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);

    return Status;
}

static
NTSTATUS
IopControlEnumerateDevice(
    _In_ PPLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA Data)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceObject = IopGetDeviceObjectFromDeviceInstance(&Data->DeviceInstance);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    Status = PiPerformSyncDeviceAction(DeviceObject, PiActionEnumDeviceTree);

    ObDereferenceObject(DeviceObject);

    return Status;
}

/*
 * Writes the name of the device that kept itself into the caller's buffer.
 * The buffer is the caller's own, so a user-mode caller's is written through
 * its own rules and a short one simply gets nothing.
 */
static
VOID
PiReportVetoName(
    _In_ PPLUGPLAY_CONTROL_QUERY_REMOVE_DATA Data,
    _In_ PCUNICODE_STRING VetoName,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Characters = VetoName->Length / sizeof(WCHAR);

    PAGED_CODE();

    if (Data->VetoName == NULL || Data->NameLength <= Characters) return;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Data->VetoName,
                          (Characters + 1) * sizeof(WCHAR),
                          sizeof(WCHAR));
            RtlCopyMemory(Data->VetoName, VetoName->Buffer, VetoName->Length);
            Data->VetoName[Characters] = UNICODE_NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;

        return;
    }

    RtlCopyMemory(Data->VetoName, VetoName->Buffer, VetoName->Length);
    Data->VetoName[Characters] = UNICODE_NULL;
}

NTSTATUS
NTAPI
NtPlugPlayControl(
    _In_ PLUGPLAY_CONTROL_CLASS PlugPlayControlClass,
    _Inout_ PVOID Buffer,
    _In_ ULONG BufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID Captured;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (PlugPlayControlClass >= MaxPlugPlayControl)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Buffer == NULL || BufferSize == 0) return STATUS_INVALID_PARAMETER;

    Captured = ExAllocatePoolZero(PagedPool, BufferSize, TAG_IO);
    if (Captured == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (PreviousMode != KernelMode)
    {
        Status = STATUS_SUCCESS;

        _SEH2_TRY
        {
            ProbeForWrite(Buffer, BufferSize, sizeof(ULONG));
            RtlCopyMemory(Captured, Buffer, BufferSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Captured, TAG_IO);
            return Status;
        }
    }
    else
    {
        RtlCopyMemory(Captured, Buffer, BufferSize);
    }

    switch (PlugPlayControlClass)
    {
        case PlugPlayControlEnumerateDevice:
            if (BufferSize < sizeof(PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = IopControlEnumerateDevice(Captured);
            break;

        case PlugPlayControlResetDevice:
        {
            PPLUGPLAY_CONTROL_DEVICE_CONTROL_DATA Data = Captured;
            PDEVICE_OBJECT DeviceObject;

            if (BufferSize < sizeof(*Data))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            DeviceObject = IopGetDeviceObjectFromDeviceInstance(
                &Data->DeviceInstance);
            if (DeviceObject == NULL)
            {
                Status = STATUS_NO_SUCH_DEVICE;
                break;
            }

            Status = PiPerformSyncDeviceAction(DeviceObject,
                                               PiActionResetDevice);

            ObDereferenceObject(DeviceObject);
            break;
        }

        case PlugPlayControlQueryAndRemoveDevice:
        {
            PPLUGPLAY_CONTROL_QUERY_REMOVE_DATA Data = Captured;
            PI_QUERY_REMOVE_DATA RemoveData;
            PDEVICE_OBJECT DeviceObject;

            if (BufferSize < sizeof(*Data))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            DeviceObject = IopGetDeviceObjectFromDeviceInstance(
                &Data->DeviceInstance);
            if (DeviceObject == NULL)
            {
                Status = STATUS_NO_SUCH_DEVICE;
                break;
            }

            RtlZeroMemory(&RemoveData, sizeof(RemoveData));

            Status = PiPerformSyncDeviceActionEx(DeviceObject,
                                                 PiActionQueryRemoveDevice,
                                                 &RemoveData);
            if (NT_SUCCESS(Status))
            {
                Status = PiPerformSyncDeviceAction(DeviceObject,
                                                   PiActionRemoveDevice);
            }
            else
            {
                /* The caller is told what kept the device and which one it was */
                Data->VetoType = RemoveData.VetoType;
                PiReportVetoName(Data, &RemoveData.VetoName, PreviousMode);
            }

            ObDereferenceObject(DeviceObject);
            break;
        }

        case PlugPlayControlUserResponse:
            if (BufferSize < sizeof(PLUGPLAY_CONTROL_USER_RESPONSE_DATA))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* The event was taken off the queue when it was read; the answer only acknowledges it */
            /* TODO: hand the answer to a vetoable event (query-remove) once such events are queued */
            Status = STATUS_SUCCESS;
            break;

        default:
            if (!PiPlugPlayControlDevice(PlugPlayControlClass, Captured, BufferSize, PreviousMode, &Status))
            {
                /* TODO: the legacy-device, blocked-driver and halt classes are not served */
                Status = STATUS_NOT_IMPLEMENTED;
            }
            break;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Buffer, Captured, BufferSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Buffer, Captured, BufferSize);
    }

    ExFreePoolWithTag(Captured, TAG_IO);

    return Status;
}

NTSTATUS
NTAPI
NtRequestDeviceWakeup(
    _In_ HANDLE DeviceHandle)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceHandle);

    /* TODO: the wake-up request path belongs to the power manager */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtCancelDeviceWakeupRequest(
    _In_ HANDLE DeviceHandle)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceHandle);

    /* TODO: the wake-up request path belongs to the power manager */
    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
IoPnPDeliverServicePowerNotification(
    _In_ POWER_ACTION PowerOperation,
    _In_ ULONG PowerNotificationCode,
    _In_ ULONG PowerNotificationData,
    _In_ BOOLEAN Synchronous)
{
    NTSTATUS Status;

    PAGED_CODE();

    /* TODO: the notification data does not reach the service event */
    UNREFERENCED_PARAMETER(PowerNotificationData);
    UNREFERENCED_PARAMETER(Synchronous);

    Status = IopQueuePowerEvent(PowerNotificationCode,
                                PowerOperation,
                                NULL,
                                0,
                                NULL);

    return NT_SUCCESS(Status);
}

BOOLEAN
NTAPI
IoPnPDeliverServicePowerSetting(
    _In_ ULONG PowerNotificationCode,
    _In_ ULONG ServiceStatusHandle,
    _In_reads_bytes_(SettingSize) PVOID Setting,
    _In_ ULONG SettingSize)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopQueuePowerEvent(PowerNotificationCode,
                                ServiceStatusHandle,
                                Setting,
                                SettingSize,
                                NULL);

    return NT_SUCCESS(Status);
}
