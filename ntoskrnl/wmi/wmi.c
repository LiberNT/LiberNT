/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/wmi.c
 * PURPOSE:         I/O Windows Management Instrumentation (WMI) Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define INITGUID
#include <wmiguid.h>
#include <wmidata.h>
#include <wmistr.h>

#include "wmip.h"

#define NDEBUG
#include <debug.h>

typedef PVOID PWMI_LOGGER_INFORMATION; // FIXME

typedef struct _WMIP_EVENT_DELIVERY
{
    WORK_QUEUE_ITEM WorkItem;
    PWNODE_HEADER Wnode;
} WMIP_EVENT_DELIVERY, *PWMIP_EVENT_DELIVERY;

typedef struct _WMIP_EVENT_TARGET
{
    PWMIP_GUID_OBJECT GuidObject;
    WMI_NOTIFICATION_CALLBACK Callback;
    PVOID Context;
} WMIP_EVENT_TARGET, *PWMIP_EVENT_TARGET;

EX_PUSH_LOCK WmipNotificationLock;
LIST_ENTRY WmipNotificationList = { &WmipNotificationList, &WmipNotificationList };

typedef enum _WMI_CLOCK_TYPE
{
    WMICT_DEFAULT,
    WMICT_SYSTEMTIME,
    WMICT_PERFCOUNTER,
    WMICT_PROCESS,
    WMICT_THREAD,
    WMICT_CPUCYCLE
} WMI_CLOCK_TYPE;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
WmiInitialize(
    VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\WMIxWDM");
    NTSTATUS Status;

    /* Capture the firmware SMBIOS entry point while the loader block lives */
    WmipCaptureSMBiosFromLoader(KeLoaderBlock);

    WmipInitializeProviders();

    /* Initialize the GUID object type */
    Status = WmipInitializeGuidObjectType();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipInitializeGuidObjectType() failed: 0x%lx\n", Status);
        return FALSE;
    }

    /* Create the WMI driver */
    Status = IoCreateDriver(&DriverName, WmipDriverEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create WMI driver: 0x%lx\n", Status);
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
NTAPI
IoWMIAllocateInstanceIds(IN GUID *Guid,
                         IN ULONG InstanceCount,
                         OUT ULONG *FirstInstanceId)
{
    if (!Guid || !FirstInstanceId)
        return STATUS_INVALID_PARAMETER;

    return WmipAllocateInstanceIds(Guid, InstanceCount, FirstInstanceId);
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISuggestInstanceName(IN PDEVICE_OBJECT PhysicalDeviceObject OPTIONAL,
                         IN PUNICODE_STRING SymbolicLinkName OPTIONAL,
                         IN BOOLEAN CombineNames,
                         OUT PUNICODE_STRING SuggestedInstanceName)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

static
VOID
NTAPI
WmipDeliverEvent(
    _In_ PVOID Context)
{
    PWMIP_EVENT_DELIVERY Delivery = Context;
    PWMIP_GUID_OBJECT GuidObject;
    PWMIP_EVENT_TARGET Targets = NULL;
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index;

    Delivery->Wnode = WmipNameEventInstance(Delivery->Wnode);

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&WmipNotificationLock);
    for (Entry = WmipNotificationList.Flink; Entry != &WmipNotificationList; Entry = Entry->Flink)
    {
        GuidObject = CONTAINING_RECORD(Entry, WMIP_GUID_OBJECT, NotificationLink);
        if (IsEqualGUID(&GuidObject->Guid, &Delivery->Wnode->Guid))
            Count++;
    }
    if (Count)
        Targets = ExAllocatePoolWithTag(PagedPool, Count * sizeof(*Targets), 'eimW');
    Count = 0;
    if (Targets)
    {
        for (Entry = WmipNotificationList.Flink; Entry != &WmipNotificationList; Entry = Entry->Flink)
        {
            GuidObject = CONTAINING_RECORD(Entry, WMIP_GUID_OBJECT, NotificationLink);
            if (IsEqualGUID(&GuidObject->Guid, &Delivery->Wnode->Guid) &&
                ObReferenceObjectSafe(GuidObject))
            {
                Targets[Count].GuidObject = GuidObject;
                Targets[Count].Callback = GuidObject->NotificationCallback;
                Targets[Count].Context = GuidObject->NotificationContext;
                Count++;
            }
        }
    }
    ExReleasePushLockShared(&WmipNotificationLock);
    KeLeaveCriticalRegion();

    for (Index = 0; Index < Count; Index++)
    {
        if (Targets[Index].Callback)
            Targets[Index].Callback(Delivery->Wnode, Targets[Index].Context);
        ObDereferenceObject(Targets[Index].GuidObject);
    }

    if (Targets)
        ExFreePoolWithTag(Targets, 'eimW');
    ExFreePool(Delivery->Wnode);
    ExFreePoolWithTag(Delivery, 'eimW');
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIWriteEvent(_Inout_ PVOID WnodeEventItem)
{
    PWNODE_HEADER Header = WnodeEventItem;
    PWMIP_EVENT_DELIVERY Delivery;

    if(!Header)
    {
        DPRINT1("Got NULL Item!\n");
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT1("IoWMIWriteEvent() called for WnodeEventItem %p (Flags = 0x%08lx), returning success\n",
            WnodeEventItem, Header->Flags);

    if (Header->Flags & WNODE_FLAG_TRACED_GUID)
    {
        // Never free WnodeEventItem in this case.
        DPRINT("IoWMIWriteEvent(): Flags has WNODE_FLAG_TRACED_GUID\n");
        return STATUS_SUCCESS;
    }

    Delivery = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Delivery), 'eimW');
    if (!Delivery)
        return STATUS_INSUFFICIENT_RESOURCES;

    Delivery->Wnode = Header;
    ExInitializeWorkItem(&Delivery->WorkItem, WmipDeliverEvent, Delivery);
    ExQueueWorkItem(&Delivery->WorkItem, DelayedWorkQueue);
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIOpenBlock(
    _In_ LPCGUID DataBlockGuid,
    _In_ ULONG DesiredAccess,
    _Out_ PVOID *DataBlockObject)
{
    HANDLE GuidObjectHandle;
    NTSTATUS Status;

    /* Open the GIOD object */
    Status = WmipOpenGuidObject(DataBlockGuid,
                                DesiredAccess,
                                KernelMode,
                                &GuidObjectHandle,
                                DataBlockObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipOpenGuidObject failed: 0x%lx\n", Status);
        return Status;
    }

    ZwClose(GuidObjectHandle);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipQueryObjectAllData(
    _In_ PWMIP_GUID_OBJECT GuidObject,
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    if (IsEqualGUID(&GuidObject->Guid, &MSSmBios_RawSMBiosTables_GUID))
        return WmipQueryRawSMBiosTables(InOutBufferSize, OutBuffer);

    return WmipQueryAllData(&GuidObject->Guid, InOutBufferSize, OutBuffer);
}

static
NTSTATUS
WmipChainResults(
    _In_ ULONG Count,
    _In_reads_(Count) PVOID *Results,
    _In_reads_(Count) PULONG Sizes,
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    PWNODE_HEADER Header;
    ULONG Index, Total = 0, Offset = 0, Last = MAXULONG, Node;

    for (Index = 0; Index < Count; Index++)
    {
        if (Results[Index])
            Total = ALIGN_UP_BY(Total, sizeof(ULONGLONG)) + Sizes[Index];
    }

    if (!Total)
        return STATUS_WMI_GUID_NOT_FOUND;

    if (!OutBuffer || *InOutBufferSize < Total)
    {
        *InOutBufferSize = Total;
        return STATUS_BUFFER_TOO_SMALL;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (!Results[Index])
            continue;

        Offset = ALIGN_UP_BY(Offset, sizeof(ULONGLONG));
        if (Last != MAXULONG)
            ((PWNODE_HEADER)((PUCHAR)OutBuffer + Last))->Linkage = Offset - Last;
        RtlCopyMemory((PUCHAR)OutBuffer + Offset, Results[Index], Sizes[Index]);

        Node = Offset;
        for (;;)
        {
            Header = (PWNODE_HEADER)((PUCHAR)OutBuffer + Node);
            if (!Header->Linkage || Header->Linkage > Offset + Sizes[Index] - Node)
            {
                Header->Linkage = 0;
                break;
            }
            Node += Header->Linkage;
        }

        Last = Node;
        Offset += Sizes[Index];
    }

    *InOutBufferSize = Total;
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipQueryMultiple(
    _In_ PVOID *DataBlockObjectList,
    _In_opt_ PUNICODE_STRING InstanceNames,
    _In_ ULONG ObjectCount,
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    PWMIP_GUID_OBJECT GuidObject;
    PVOID *Results;
    PULONG Sizes;
    ULONG Index, Size;
    NTSTATUS Status, LastError = STATUS_WMI_GUID_NOT_FOUND;

    if (!DataBlockObjectList || !ObjectCount || !InOutBufferSize)
        return STATUS_INVALID_PARAMETER;

    Results = ExAllocatePoolZero(PagedPool, ObjectCount * (sizeof(PVOID) + sizeof(ULONG)), 'qimW');
    if (!Results)
        return STATUS_INSUFFICIENT_RESOURCES;
    Sizes = (PULONG)&Results[ObjectCount];

    for (Index = 0; Index < ObjectCount; Index++)
    {
        GuidObject = DataBlockObjectList[Index];
        Status = ObReferenceObjectByPointer(GuidObject, WMIGUID_QUERY, WmipGuidObjectType, KernelMode);
        if (!NT_SUCCESS(Status))
        {
            LastError = Status;
            continue;
        }

        Size = 0;
        Status = InstanceNames ?
                 WmipQuerySingleInstance(&GuidObject->Guid, &InstanceNames[Index], &Size, NULL) :
                 WmipQueryObjectAllData(GuidObject, &Size, NULL);
        if ((Status == STATUS_BUFFER_TOO_SMALL || NT_SUCCESS(Status)) && Size)
        {
            Results[Index] = ExAllocatePoolWithTag(NonPagedPoolNx, Size, 'qimW');
            if (!Results[Index])
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                Sizes[Index] = Size;
                Status = InstanceNames ?
                         WmipQuerySingleInstance(&GuidObject->Guid, &InstanceNames[Index], &Sizes[Index], Results[Index]) :
                         WmipQueryObjectAllData(GuidObject, &Sizes[Index], Results[Index]);
                if (!NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(Results[Index], 'qimW');
                    Results[Index] = NULL;
                }
            }
        }

        if (!NT_SUCCESS(Status))
            LastError = Status;

        ObDereferenceObject(GuidObject);
    }

    Status = WmipChainResults(ObjectCount, Results, Sizes, InOutBufferSize, OutBuffer);
    if (Status == STATUS_WMI_GUID_NOT_FOUND)
        Status = LastError;

    for (Index = 0; Index < ObjectCount; Index++)
    {
        if (Results[Index])
            ExFreePoolWithTag(Results[Index], 'qimW');
    }
    ExFreePoolWithTag(Results, 'qimW');
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQueryAllData(
    IN PVOID DataBlockObject,
    IN OUT ULONG *InOutBufferSize,
    OUT PVOID OutBuffer)
{
    PWMIP_GUID_OBJECT GuidObject;
    NTSTATUS Status;


    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_QUERY,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    GuidObject = DataBlockObject;
    Status = WmipQueryObjectAllData(GuidObject, InOutBufferSize, OutBuffer);

    ObDereferenceObject(DataBlockObject);

    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQueryAllDataMultiple(IN PVOID *DataBlockObjectList,
                          IN ULONG ObjectCount,
                          IN OUT ULONG *InOutBufferSize,
                          OUT PVOID OutBuffer)
{
    return WmipQueryMultiple(DataBlockObjectList, NULL, ObjectCount, InOutBufferSize, OutBuffer);
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQuerySingleInstance(IN PVOID DataBlockObject,
                         IN PUNICODE_STRING InstanceName,
                         IN OUT ULONG *InOutBufferSize,
                         OUT PVOID OutBuffer)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    NTSTATUS Status;

    if (!InstanceName || !InOutBufferSize)
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_QUERY,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WmipQuerySingleInstance(&GuidObject->Guid, InstanceName, InOutBufferSize, OutBuffer);

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQuerySingleInstanceMultiple(IN PVOID *DataBlockObjectList,
                                 IN PUNICODE_STRING InstanceNames,
                                 IN ULONG ObjectCount,
                                 IN OUT ULONG *InOutBufferSize,
                                 OUT PVOID OutBuffer)
{
    if (!InstanceNames)
        return STATUS_INVALID_PARAMETER;

    return WmipQueryMultiple(DataBlockObjectList, InstanceNames, ObjectCount, InOutBufferSize, OutBuffer);
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetSingleInstance(IN PVOID DataBlockObject,
                       IN PUNICODE_STRING InstanceName,
                       IN ULONG Version,
                       IN ULONG ValueBufferSize,
                       IN PVOID ValueBuffer)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    NTSTATUS Status;

    if (!InstanceName || (ValueBufferSize && !ValueBuffer))
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_SET,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WmipSetInstance(&GuidObject->Guid, InstanceName, FALSE, 0, Version, ValueBufferSize, ValueBuffer);

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetSingleItem(IN PVOID DataBlockObject,
                   IN PUNICODE_STRING InstanceName,
                   IN ULONG DataItemId,
                   IN ULONG Version,
                   IN ULONG ValueBufferSize,
                   IN PVOID ValueBuffer)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    NTSTATUS Status;

    if (!InstanceName || (ValueBufferSize && !ValueBuffer))
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_SET,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WmipSetInstance(&GuidObject->Guid, InstanceName, TRUE, DataItemId, Version, ValueBufferSize, ValueBuffer);

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIExecuteMethod(IN PVOID DataBlockObject,
                   IN PUNICODE_STRING InstanceName,
                   IN ULONG MethodId,
                   IN ULONG InBufferSize,
                   IN OUT PULONG OutBufferSize,
                   IN OUT PUCHAR InOutBuffer)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    NTSTATUS Status;

    if (!InstanceName || !OutBufferSize || ((InBufferSize || *OutBufferSize) && !InOutBuffer))
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_EXECUTE,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WmipExecuteMethod(&GuidObject->Guid, InstanceName, MethodId,
                               InBufferSize, OutBufferSize, InOutBuffer);

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetNotificationCallback(IN PVOID Object,
                             IN WMI_NOTIFICATION_CALLBACK Callback,
                             IN PVOID Context)
{
    PWMIP_GUID_OBJECT GuidObject = Object;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByPointer(Object,
                                        WMIGUID_NOTIFICATION,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&WmipNotificationLock);
    GuidObject->NotificationCallback = Callback;
    GuidObject->NotificationContext = Context;
    if (Callback && IsListEmpty(&GuidObject->NotificationLink))
    {
        InsertTailList(&WmipNotificationList, &GuidObject->NotificationLink);
    }
    else if (!Callback && !IsListEmpty(&GuidObject->NotificationLink))
    {
        RemoveEntryList(&GuidObject->NotificationLink);
        InitializeListHead(&GuidObject->NotificationLink);
    }
    ExReleasePushLockExclusive(&WmipNotificationLock);
    KeLeaveCriticalRegion();

    WmipSetEventConsumer(GuidObject, Callback != NULL);

    ObDereferenceObject(Object);
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIHandleToInstanceName(IN PVOID DataBlockObject,
                          IN HANDLE FileHandle,
                          OUT PUNICODE_STRING InstanceName)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    if (!InstanceName)
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject, 0, WmipGuidObjectType, KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(FileHandle, 0, IoFileObjectType, KernelMode,
                                       (PVOID *)&FileObject, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = WmipDeviceObjectToInstanceName(&GuidObject->Guid,
                                                IoGetRelatedDeviceObject(FileObject),
                                                InstanceName);
        ObDereferenceObject(FileObject);
    }

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIDeviceObjectToInstanceName(IN PVOID DataBlockObject,
                                IN PDEVICE_OBJECT DeviceObject,
                                OUT PUNICODE_STRING InstanceName)
{
    PWMIP_GUID_OBJECT GuidObject = DataBlockObject;
    NTSTATUS Status;

    if (!DeviceObject || !InstanceName)
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByPointer(DataBlockObject, 0, WmipGuidObjectType, KernelMode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WmipDeviceObjectToInstanceName(&GuidObject->Guid, DeviceObject, InstanceName);

    ObDereferenceObject(DataBlockObject);
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
WmiQueryTraceInformation(IN TRACE_INFORMATION_CLASS TraceInformationClass,
                         OUT PVOID TraceInformation,
                         IN ULONG TraceInformationLength,
                         OUT PULONG RequiredLength OPTIONAL,
                         IN PVOID Buffer OPTIONAL)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
__cdecl
WmiTraceMessage(IN TRACEHANDLE LoggerHandle,
                IN ULONG MessageFlags,
                IN LPGUID MessageGuid,
                IN USHORT MessageNumber,
                IN ...)
{
    UNREFERENCED_PARAMETER(LoggerHandle);
    UNREFERENCED_PARAMETER(MessageFlags);
    /*
     * Diagnostic: surface classic-WPP DoTraceMessage output (the trace-point
     * GUID + message number) so otherwise-silent WPP-instrumented drivers
     * (e.g. viogpudo.sys) reveal their init progress in the serial log.
     */
    DbgPrint("WPPTRACE: guid=%08lX msg=%u\n",
             MessageGuid ? MessageGuid->Data1 : 0, MessageNumber);
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
WmiTraceMessageVa(IN TRACEHANDLE LoggerHandle,
                  IN ULONG MessageFlags,
                  IN LPGUID MessageGuid,
                  IN USHORT MessageNumber,
                  IN va_list MessageArgList)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiFlushTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

LONG64
FASTCALL
WmiGetClock(IN WMI_CLOCK_TYPE ClockType,
            IN PVOID Context)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiQueryTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiStartTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiStopTrace(IN PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
FASTCALL
WmiTraceFastEvent(IN PWNODE_HEADER Wnode)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiUpdateTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

ULONG
NTAPI
EtwpDisableStackWalkApc(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    LONG OldFlags;
    LONG NewFlags;
    LONG ActualFlags;

    OldFlags = ReadAcquire(&Thread->ThreadFlags);
    for (;;)
    {
        NewFlags = OldFlags | (LONG)0xFF800000;
        ActualFlags = InterlockedCompareExchange(&Thread->ThreadFlags,
                                                 NewFlags,
                                                 OldFlags);
        if (ActualFlags == OldFlags)
            return ((ULONG)OldFlags >> 23);

        OldFlags = ActualFlags;
    }
}

VOID
NTAPI
EtwpReenableStackWalkApc(
    _In_ ULONG PreviousState)
{
    PKTHREAD Thread = KeGetCurrentThread();
    LONG OldFlags;
    LONG NewFlags;
    LONG ActualFlags;
    ULONG RestoreMask;

    RestoreMask = (PreviousState << 23) | 0x007FFFFF;
    OldFlags = ReadAcquire(&Thread->ThreadFlags);
    for (;;)
    {
        NewFlags = OldFlags & (LONG)RestoreMask;
        ActualFlags = InterlockedCompareExchange(&Thread->ThreadFlags,
                                                 NewFlags,
                                                 OldFlags);
        if (ActualFlags == OldFlags)
            return;

        OldFlags = ActualFlags;
    }
}

/*Eof*/
