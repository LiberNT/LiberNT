/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     How a kernel-mode caller reads, writes and listens to a WMI data block
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY WmipNotificationList =
    {&WmipNotificationList, &WmipNotificationList};

EX_PUSH_LOCK WmipNotificationLock;

/* PRIVATE DATA ***************************************************************/

#define WMIP_TAG 'imWD'

/* The longest instance name WMI will suggest on its own */
#define WMIP_SUGGESTED_NAME_MAX 512

/* How many identifiers a multiple query will walk in one call */
#define WMIP_MULTIPLE_MAX 256

/* PRIVATE FUNCTIONS **********************************************************/

/* The identifier an opaque data block object stands for */
static
NTSTATUS
WmipGuidFromObject(
    _In_ PVOID DataBlockObject,
    _Out_ LPGUID Guid)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;

    RtlZeroMemory(Guid, sizeof(*Guid));

    if (DataBlockObject == NULL) return STATUS_INVALID_PARAMETER;

    if (ObGetObjectType(DataBlockObject) != WmipGuidObjectType)
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    *Guid = GuidObject->Guid;
    return STATUS_SUCCESS;
}

static
ULONG
WmipAlign(
    _In_ ULONG Size)
{
    return (Size + 7) & ~7u;
}

static
NTSTATUS
WmipQueryMultiple(
    _In_reads_(ObjectCount) PVOID *DataBlockObjectList,
    _In_reads_opt_(ObjectCount) PUNICODE_STRING InstanceNames,
    _In_ ULONG ObjectCount,
    _Inout_ PULONG InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    PWNODE_HEADER Previous = NULL;
    PWNODE_HEADER Wnode;
    NTSTATUS Status;
    ULONG Available;
    ULONG Needed = 0;
    ULONG Offset = 0;
    ULONG Size;
    ULONG i;
    GUID Guid;

    if (DataBlockObjectList == NULL || InOutBufferSize == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ObjectCount == 0 || ObjectCount > WMIP_MULTIPLE_MAX)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Available = *InOutBufferSize;

    if (Available != 0 && OutBuffer == NULL) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ObjectCount; i++)
    {
        Status = WmipGuidFromObject(DataBlockObjectList[i], &Guid);
        if (!NT_SUCCESS(Status)) return Status;

        Size = (Offset < Available) ? Available - Offset : 0;
        Wnode = (Size != 0) ? (PWNODE_HEADER)((PUCHAR)OutBuffer + Offset) : NULL;

        if (InstanceNames != NULL)
        {
            Status = WmipQuerySingleInstance(&Guid,
                                             &InstanceNames[i],
                                             &Size,
                                             Wnode);
        }
        else
        {
            Status = WmipQueryAllData(&Guid, &Size, Wnode);
        }

        if (Status == STATUS_BUFFER_TOO_SMALL || Wnode == NULL)
        {
            Needed += WmipAlign(Size);
            Offset = Available;
            continue;
        }

        if (!NT_SUCCESS(Status)) return Status;

        if (Previous != NULL) Previous->Linkage = Offset - (ULONG)((PUCHAR)Previous - (PUCHAR)OutBuffer);

        Previous = Wnode;
        Needed += WmipAlign(Size);
        Offset += WmipAlign(Size);
    }

    if (Previous != NULL) Previous->Linkage = 0;

    *InOutBufferSize = Needed;

    return (Needed > Available) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
WmipIsDataBlockRegistered(
    _In_ LPCGUID Guid)
{
    if (Guid == NULL) return FALSE;

    if (WmipIsKernelDataBlock(Guid)) return TRUE;

    return WmipIsGuidRegistered(Guid);
}

BOOLEAN
NTAPI
WmiInitialize(VOID)
{
    static UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\WMIxWDM");
    NTSTATUS Status;

    PAGED_CODE();

    ExInitializePushLock(&WmipNotificationLock);
    InitializeListHead(&WmipNotificationList);

    Status = WmipInitializeGuidObjectType();
    if (!NT_SUCCESS(Status)) return FALSE;

    WmipInitializeProviders();
    EtwpInitializeProviders();

    Status = EtwpInitializeRegistrations();
    if (!NT_SUCCESS(Status)) return FALSE;

    WmipCaptureSMBiosFromLoader(KeLoaderBlock);

    Status = IoCreateDriver(&DriverName, WmipDriverEntry);
    if (!NT_SUCCESS(Status)) return FALSE;

    return TRUE;
}

NTSTATUS
NTAPI
IoWMIOpenBlock(
    _In_ LPCGUID DataBlockGuid,
    _In_ ULONG DesiredAccess,
    _Out_ PVOID *DataBlockObject)
{
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (DataBlockGuid == NULL || DataBlockObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DataBlockObject = NULL;

    /* Listening for events means waiting for them as well */
    if ((DesiredAccess & WMIGUID_NOTIFICATION) != 0 &&
        (DesiredAccess & SYNCHRONIZE) == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipOpenGuidObject(DataBlockGuid,
                               DesiredAccess,
                               KernelMode,
                               &Handle,
                               DataBlockObject);
    if (!NT_SUCCESS(Status)) return Status;

    ObCloseHandle(Handle, KernelMode);

    return STATUS_SUCCESS;
}

/* Reads every instance of one data block */
NTSTATUS
NTAPI
IoWMIQueryAllData(
    _In_ PVOID DataBlockObject,
    _Inout_ ULONG *InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (InOutBufferSize == NULL) return STATUS_INVALID_PARAMETER;

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    if (WmipIsKernelDataBlock(&Guid))
    {
        return WmipQueryKernelDataBlock(&Guid, InOutBufferSize, OutBuffer);
    }

    return WmipQueryAllData(&Guid, InOutBufferSize, OutBuffer);
}

/* Reads every instance of several data blocks into one chained answer */
NTSTATUS
NTAPI
IoWMIQueryAllDataMultiple(
    _In_reads_(ObjectCount) PVOID *DataBlockObjectList,
    _In_ ULONG ObjectCount,
    _Inout_ ULONG *InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    PAGED_CODE();

    return WmipQueryMultiple(DataBlockObjectList,
                             NULL,
                             ObjectCount,
                             InOutBufferSize,
                             OutBuffer);
}

/* Reads one named instance of one data block */
NTSTATUS
NTAPI
IoWMIQuerySingleInstance(
    _In_ PVOID DataBlockObject,
    _In_ PUNICODE_STRING InstanceName,
    _Inout_ ULONG *InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (InstanceName == NULL || InOutBufferSize == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    if (WmipIsKernelDataBlock(&Guid))
    {
        return WmipQueryKernelDataBlock(&Guid, InOutBufferSize, OutBuffer);
    }

    return WmipQuerySingleInstance(&Guid,
                                   InstanceName,
                                   InOutBufferSize,
                                   OutBuffer);
}

/* Reads one named instance of each of several data blocks */
NTSTATUS
NTAPI
IoWMIQuerySingleInstanceMultiple(
    _In_reads_(ObjectCount) PVOID *DataBlockObjectList,
    _In_reads_(ObjectCount) PUNICODE_STRING InstanceNames,
    _In_ ULONG ObjectCount,
    _Inout_ ULONG *InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    PAGED_CODE();

    if (InstanceNames == NULL) return STATUS_INVALID_PARAMETER;

    return WmipQueryMultiple(DataBlockObjectList,
                             InstanceNames,
                             ObjectCount,
                             InOutBufferSize,
                             OutBuffer);
}

/* Writes a whole instance of a data block */
NTSTATUS
NTAPI
IoWMISetSingleInstance(
    _In_ PVOID DataBlockObject,
    _In_ PUNICODE_STRING InstanceName,
    _In_ ULONG Version,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (InstanceName == NULL || ValueBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    return WmipSetInstance(&Guid,
                           InstanceName,
                           FALSE,
                           0,
                           Version,
                           ValueBufferSize,
                           ValueBuffer);
}

/* Writes one item of one instance of a data block */
NTSTATUS
NTAPI
IoWMISetSingleItem(
    _In_ PVOID DataBlockObject,
    _In_ PUNICODE_STRING InstanceName,
    _In_ ULONG DataItemId,
    _In_ ULONG Version,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (InstanceName == NULL || ValueBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    return WmipSetInstance(&Guid,
                           InstanceName,
                           TRUE,
                           DataItemId,
                           Version,
                           ValueBufferSize,
                           ValueBuffer);
}

/* Calls a method of one instance of a data block */
NTSTATUS
NTAPI
IoWMIExecuteMethod(
    _In_ PVOID DataBlockObject,
    _In_ PUNICODE_STRING InstanceName,
    _In_ ULONG MethodId,
    _In_ ULONG InBufferSize,
    _Inout_ PULONG OutBufferSize,
    _Inout_updates_bytes_to_opt_(*OutBufferSize, InBufferSize) PUCHAR InOutBuffer)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (InstanceName == NULL || OutBufferSize == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    return WmipExecuteMethod(&Guid,
                             InstanceName,
                             MethodId,
                             InBufferSize,
                             OutBufferSize,
                             InOutBuffer);
}

NTSTATUS
NTAPI
IoWMIWriteEvent(
    _Inout_ _When_(return==0, __drv_aliasesMem) PVOID WnodeEventItem)
{
    PWNODE_HEADER Wnode = WnodeEventItem;
    PWNODE_HEADER Named;

    if (Wnode == NULL) return STATUS_INVALID_PARAMETER;

    if (Wnode->BufferSize < sizeof(WNODE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Wnode->Flags & WNODE_FLAG_EVENT_ITEM) == 0 &&
        (Wnode->Flags & WNODE_FLAG_SINGLE_INSTANCE) == 0 &&
        (Wnode->Flags & WNODE_FLAG_ALL_DATA) == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wnode->KernelHandle && !WmipIsDataBlockRegistered(&Wnode->Guid))
    {
        return STATUS_WMI_GUID_NOT_FOUND;
    }

    Named = WmipNameEventInstance(Wnode);
    if (Named == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeQuerySystemTime(&Named->TimeStamp);

    WmipDeliverNotification(Named);

    if (Named != Wnode) ExFreePool(Named);

    ExFreePool(Wnode);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoWMISetNotificationCallback(
    _Inout_ PVOID Object,
    _In_ WMI_NOTIFICATION_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PWMIP_GUID_OBJECT GuidObject = Object;

    PAGED_CODE();

    if (Object == NULL) return STATUS_INVALID_PARAMETER;

    if (ObGetObjectType(Object) != WmipGuidObjectType)
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    ExAcquirePushLockExclusive(&WmipNotificationLock);

    GuidObject->NotificationCallback = Callback;
    GuidObject->NotificationContext = Context;

    if (Callback != NULL)
    {
        if (IsListEmpty(&GuidObject->NotificationLink))
        {
            InsertTailList(&WmipNotificationList,
                           &GuidObject->NotificationLink);
        }
    }
    else if (!IsListEmpty(&GuidObject->NotificationLink))
    {
        RemoveEntryList(&GuidObject->NotificationLink);
        InitializeListHead(&GuidObject->NotificationLink);
    }

    ExReleasePushLockExclusive(&WmipNotificationLock);

    WmipSetEventConsumer(GuidObject, Callback != NULL);

    return STATUS_SUCCESS;
}

/* What the instance of this data block belonging to that device is called */
NTSTATUS
NTAPI
IoWMIDeviceObjectToInstanceName(
    _In_ PVOID DataBlockObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PUNICODE_STRING InstanceName)
{
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (DeviceObject == NULL || InstanceName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipGuidFromObject(DataBlockObject, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    return WmipDeviceObjectToInstanceName(&Guid, DeviceObject, InstanceName);
}

/* The same, for a device named by an open handle rather than by its object */
NTSTATUS
NTAPI
IoWMIHandleToInstanceName(
    _In_ PVOID DataBlockObject,
    _In_ HANDLE FileHandle,
    _Out_ PUNICODE_STRING InstanceName)
{
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    if (InstanceName == NULL) return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByHandle(FileHandle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);

    if (DeviceObject == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = IoWMIDeviceObjectToInstanceName(DataBlockObject,
                                             DeviceObject,
                                             InstanceName);

    ObDereferenceObject(FileObject);

    return Status;
}

NTSTATUS
NTAPI
IoWMIAllocateInstanceIds(
    _In_ GUID *Guid,
    _In_ ULONG InstanceCount,
    _Out_ ULONG *FirstInstanceId)
{
    PAGED_CODE();

    if (Guid == NULL || FirstInstanceId == NULL || InstanceCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return WmipAllocateInstanceIds(Guid, InstanceCount, FirstInstanceId);
}

NTSTATUS
NTAPI
IoWMISuggestInstanceName(
    _In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_opt_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN CombineNames,
    _Out_ PUNICODE_STRING SuggestedInstanceName)
{
    UNICODE_STRING Suggestion;
    PWSTR DeviceName = NULL;
    NTSTATUS Status;
    PWSTR Buffer;
    ULONG Length;

    PAGED_CODE();

    if (SuggestedInstanceName == NULL) return STATUS_INVALID_PARAMETER;

    RtlInitEmptyUnicodeString(SuggestedInstanceName, NULL, 0);

    if (CombineNames)
    {
        if (PhysicalDeviceObject == NULL || SymbolicLinkName == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if ((PhysicalDeviceObject == NULL) == (SymbolicLinkName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (WmipGuidObjectType == NULL) return STATUS_UNSUCCESSFUL;

    Buffer = ExAllocatePoolZero(PagedPool,
                                WMIP_SUGGESTED_NAME_MAX * sizeof(WCHAR),
                                WMIP_TAG);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlInitEmptyUnicodeString(&Suggestion,
                              Buffer,
                              WMIP_SUGGESTED_NAME_MAX * sizeof(WCHAR));

    if (PhysicalDeviceObject != NULL)
    {
        Status = IoGetDeviceProperty(PhysicalDeviceObject,
                                     DevicePropertyPhysicalDeviceObjectName,
                                     0,
                                     NULL,
                                     &Length);

        if (Status == STATUS_BUFFER_TOO_SMALL && Length != 0)
        {
            DeviceName = ExAllocatePoolZero(PagedPool, Length, WMIP_TAG);

            if (DeviceName == NULL)
            {
                ExFreePoolWithTag(Buffer, WMIP_TAG);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = IoGetDeviceProperty(PhysicalDeviceObject,
                                         DevicePropertyPhysicalDeviceObjectName,
                                         Length,
                                         DeviceName,
                                         &Length);
        }

        if (NT_SUCCESS(Status) && DeviceName != NULL)
        {
            RtlAppendUnicodeToString(&Suggestion, DeviceName);
        }
    }

    if (SymbolicLinkName != NULL)
    {
        if (Suggestion.Length != 0) RtlAppendUnicodeToString(&Suggestion, L"_");

        RtlAppendUnicodeStringToString(&Suggestion, SymbolicLinkName);
    }

    if (DeviceName != NULL) ExFreePoolWithTag(DeviceName, WMIP_TAG);

    if (Suggestion.Length == 0)
    {
        ExFreePoolWithTag(Buffer, WMIP_TAG);
        return STATUS_UNSUCCESSFUL;
    }

    *SuggestedInstanceName = Suggestion;

    return STATUS_SUCCESS;
}

/* EOF */
