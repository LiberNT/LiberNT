/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     qWAVE flow driver: the QoS flows of each qwave.dll handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <winerror.h>
#include <libernt/qwavedrv.h>
#include <libernt/drivers/qosmark.h>

#define NDEBUG
#include <debug.h>

#define TAG_QWAVE 'evWQ'

typedef struct _QWAVE_SOCKET_ENTRY
{
    LIST_ENTRY Link;
    ULONG64 Socket;
    PFILE_OBJECT FileObject;
    ULONG MarkedDscp;
} QWAVE_SOCKET_ENTRY, *PQWAVE_SOCKET_ENTRY;

typedef struct _QWAVE_FLOW
{
    LIST_ENTRY Link;
    LIST_ENTRY Sockets;
    ULONG SocketCount;
    ULONG FlowId;
    ULONG TrafficType;
    ULONG Dscp;
    BOOLEAN DscpSet;
    BOOLEAN RateSet;
    QWAVE_FLOWRATE Rate;
} QWAVE_FLOW, *PQWAVE_FLOW;

typedef struct _QWAVE_HANDLE
{
    LIST_ENTRY Link;
    ERESOURCE Lock;
    LIST_ENTRY Flows;
} QWAVE_HANDLE, *PQWAVE_HANDLE;

/* DSCP marks of the QOS_TRAFFIC_TYPE values (qos2.h documentation); best effort traffic is not marked */
static const ULONG QwaveTrafficTypeDscp[QWAVE_TRAFFIC_TYPE_LAST + 1] = { QOS_MARK_UNMARKED, 0x08, 0x28, 0x28, 0x38, 0x38 };

static const UNICODE_STRING QwaveAfdDriverName = RTL_CONSTANT_STRING(L"\\Driver\\AFD");

static PDEVICE_OBJECT QwaveDeviceObject;
static PSECURITY_DESCRIPTOR QwaveMarkingSd;
static PSECURITY_DESCRIPTOR QwaveAdminSd;
static FAST_MUTEX QwaveHandlesLock;
static LIST_ENTRY QwaveHandles;
static LONG QwaveLastFlowId;

static
VOID
QwaveLock(
    _In_ PQWAVE_HANDLE Handle,
    _In_ BOOLEAN Exclusive)
{
    KeEnterCriticalRegion();
    if (Exclusive)
        ExAcquireResourceExclusiveLite(&Handle->Lock, TRUE);
    else
        ExAcquireResourceSharedLite(&Handle->Lock, TRUE);
}

static
VOID
QwaveUnlock(
    _In_ PQWAVE_HANDLE Handle)
{
    ExReleaseResourceLite(&Handle->Lock);
    KeLeaveCriticalRegion();
}

static
PQWAVE_FLOW
QwaveFindFlowLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ ULONG FlowId)
{
    PLIST_ENTRY Entry;

    ASSERT(ExIsResourceAcquiredSharedLite(&Handle->Lock));
    for (Entry = Handle->Flows.Flink; Entry != &Handle->Flows; Entry = Entry->Flink)
    {
        PQWAVE_FLOW Flow = CONTAINING_RECORD(Entry, QWAVE_FLOW, Link);

        ASSERT(Flow->FlowId != 0);
        ASSERT(Flow->SocketCount != 0);
        ASSERT(Flow->TrafficType <= QWAVE_TRAFFIC_TYPE_LAST);
        if (Flow->FlowId == FlowId)
            return Flow;
    }
    return NULL;
}

static
ULONG
QwaveFlowDscpLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow)
{
    ASSERT(ExIsResourceAcquiredSharedLite(&Handle->Lock));
    ASSERT(Flow->TrafficType <= QWAVE_TRAFFIC_TYPE_LAST);
    UNREFERENCED_PARAMETER(Handle);

    if (Flow->DscpSet)
        return Flow->Dscp;
    return QwaveTrafficTypeDscp[Flow->TrafficType];
}

static
NTSTATUS
QwaveMarkSocket(
    _In_ PQWAVE_SOCKET_ENTRY Socket,
    _In_ ULONG Dscp)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(Socket->FileObject);
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();
    ASSERT(Dscp == QOS_MARK_UNMARKED || Dscp <= QOS_MARK_DSCP_LAST);

    if (Socket->MarkedDscp == Dscp)
        return STATUS_SUCCESS;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_AFD_SET_QOS_DSCP, DeviceObject, &Dscp, sizeof(Dscp), NULL, 0, TRUE,
                                        &Event, &Iosb);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoGetNextIrpStackLocation(Irp)->FileObject = Socket->FileObject;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    if (NT_SUCCESS(Status))
        Socket->MarkedDscp = Dscp;
    return Status;
}

static
VOID
QwaveMarkFlowLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow)
{
    ULONG Dscp = QwaveFlowDscpLocked(Handle, Flow);
    PLIST_ENTRY Entry;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Handle->Lock));
    for (Entry = Flow->Sockets.Flink; Entry != &Flow->Sockets; Entry = Entry->Flink)
    {
        PQWAVE_SOCKET_ENTRY Socket = CONTAINING_RECORD(Entry, QWAVE_SOCKET_ENTRY, Link);
        NTSTATUS Status = QwaveMarkSocket(Socket, Dscp);

        if (!NT_SUCCESS(Status))
            DPRINT1("QWAVE: marking socket %I64x of flow %lu failed 0x%08lx\n", Socket->Socket, Flow->FlowId, Status);
    }
}

static
VOID
QwaveFreeSocket(
    _In_ PQWAVE_SOCKET_ENTRY Socket)
{
    PAGED_CODE();

    if (Socket->FileObject)
    {
        (VOID)QwaveMarkSocket(Socket, QOS_MARK_UNMARKED);
        ObDereferenceObject(Socket->FileObject);
    }
    ExFreePoolWithTag(Socket, TAG_QWAVE);
}

static
VOID
QwaveDestroyFlowLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Handle->Lock));
    UNREFERENCED_PARAMETER(Handle);

    RemoveEntryList(&Flow->Link);
    while (!IsListEmpty(&Flow->Sockets))
    {
        PQWAVE_SOCKET_ENTRY Socket = CONTAINING_RECORD(RemoveHeadList(&Flow->Sockets), QWAVE_SOCKET_ENTRY, Link);

        ASSERT(Flow->SocketCount != 0);
        Flow->SocketCount--;
        QwaveFreeSocket(Socket);
    }
    ASSERT(Flow->SocketCount == 0);
    ExFreePoolWithTag(Flow, TAG_QWAVE);
}

static
VOID
QwaveDestroyAllFlows(
    _In_ PQWAVE_HANDLE Handle)
{
    PAGED_CODE();

    QwaveLock(Handle, TRUE);
    while (!IsListEmpty(&Handle->Flows))
        QwaveDestroyFlowLocked(Handle, CONTAINING_RECORD(Handle->Flows.Flink, QWAVE_FLOW, Link));
    QwaveUnlock(Handle);
}

static
ULONG
QwaveNewFlowIdLocked(
    _In_ PQWAVE_HANDLE Handle)
{
    ULONG FlowId;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Handle->Lock));
    do
    {
        FlowId = (ULONG)InterlockedIncrement(&QwaveLastFlowId);
    } while (FlowId == 0 || QwaveFindFlowLocked(Handle, FlowId) != NULL);
    return FlowId;
}

static
BOOLEAN
QwaveAccessCheck(
    _In_ PIRP Irp,
    _In_ PSECURITY_DESCRIPTOR Sd)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    ACCESS_MASK Granted;
    NTSTATUS Status;
    BOOLEAN Allowed;

    PAGED_CODE();
    ASSERT(Sd != NULL);

    if (Irp->RequestorMode == KernelMode)
        return TRUE;

    SeCaptureSubjectContext(&Subject);
    SeLockSubjectContext(&Subject);
    Allowed = SeAccessCheck(Sd, &Subject, TRUE, FILE_WRITE_DATA, 0, NULL, IoGetFileObjectGenericMapping(), UserMode,
                            &Granted, &Status);
    SeUnlockSubjectContext(&Subject);
    SeReleaseSubjectContext(&Subject);
    return Allowed;
}

static
NTSTATUS
QwaveReferenceSocket(
    _In_ PIRP Irp,
    _In_ ULONG64 Socket,
    _Out_ PFILE_OBJECT *FileObject)
{
    PFILE_OBJECT Object;
    NTSTATUS Status;

    PAGED_CODE();

    *FileObject = NULL;
    if (Socket == 0 || (ULONG64)(ULONG_PTR)Socket != Socket)
        return STATUS_INVALID_PARAMETER;
    Status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)Socket, 0, *IoFileObjectType, Irp->RequestorMode,
                                       (PVOID *)&Object, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    if (!Object->DeviceObject ||
        !RtlEqualUnicodeString(&Object->DeviceObject->DriverObject->DriverName, &QwaveAfdDriverName, TRUE))
    {
        ObDereferenceObject(Object);
        return STATUS_INVALID_PARAMETER;
    }
    *FileObject = Object;
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveAddSocket(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    QWAVE_ADD_SOCKET_INPUT Input;
    PQWAVE_SOCKET_ENTRY Socket;
    PQWAVE_FLOW Flow, NewFlow = NULL;
    NTSTATUS Status;
    ULONG FlowId;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if ((Input.Flags & ~QWAVE_NON_ADAPTIVE_FLOW) != 0 || Input.TrafficType > QWAVE_TRAFFIC_TYPE_LAST)
        return STATUS_INVALID_PARAMETER;
    if (!(Input.Flags & QWAVE_NON_ADAPTIVE_FLOW))
    {
        /* TODO: adaptive flows need the network experiments (LLTD probing) of the qWAVE service */
        return STATUS_NOT_SUPPORTED;
    }

    Socket = ExAllocatePoolZero(PagedPool, sizeof(*Socket), TAG_QWAVE);
    if (!Socket)
        return STATUS_INSUFFICIENT_RESOURCES;
    Socket->Socket = Input.Socket;
    Socket->MarkedDscp = QOS_MARK_UNMARKED;
    Status = QwaveReferenceSocket(Irp, Input.Socket, &Socket->FileObject);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Socket, TAG_QWAVE);
        return Status;
    }
    if (Input.FlowId == 0)
    {
        NewFlow = ExAllocatePoolZero(PagedPool, sizeof(*NewFlow), TAG_QWAVE);
        if (!NewFlow)
        {
            QwaveFreeSocket(Socket);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        InitializeListHead(&NewFlow->Sockets);
        NewFlow->TrafficType = Input.TrafficType;
    }

    QwaveLock(Handle, TRUE);
    if (NewFlow)
    {
        NewFlow->FlowId = QwaveNewFlowIdLocked(Handle);
        Flow = NewFlow;
    }
    else
    {
        Flow = QwaveFindFlowLocked(Handle, Input.FlowId);
        if (!Flow)
        {
            QwaveUnlock(Handle);
            QwaveFreeSocket(Socket);
            return STATUS_INVALID_PARAMETER;
        }
    }
    Status = QwaveMarkSocket(Socket, QwaveFlowDscpLocked(Handle, Flow));
    if (!NT_SUCCESS(Status))
    {
        QwaveUnlock(Handle);
        QwaveFreeSocket(Socket);
        if (NewFlow)
            ExFreePoolWithTag(NewFlow, TAG_QWAVE);
        return Status;
    }
    if (NewFlow)
        InsertTailList(&Handle->Flows, &NewFlow->Link);
    InsertTailList(&Flow->Sockets, &Socket->Link);
    Flow->SocketCount++;
    ASSERT(Flow->SocketCount != 0);
    FlowId = Flow->FlowId;
    QwaveUnlock(Handle);

    ASSERT(FlowId != 0);
    *(PULONG)Irp->AssociatedIrp.SystemBuffer = FlowId;
    Irp->IoStatus.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveRemoveSocket(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    QWAVE_REMOVE_SOCKET_INPUT Input;
    PQWAVE_FLOW Flow;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_NOT_FOUND;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if (Input.Flags != 0)
        return STATUS_INVALID_PARAMETER;

    QwaveLock(Handle, TRUE);
    Flow = QwaveFindFlowLocked(Handle, Input.FlowId);
    if (Flow && Input.Socket == 0)
    {
        QwaveDestroyFlowLocked(Handle, Flow);
        Status = STATUS_SUCCESS;
    }
    else if (Flow)
    {
        for (Entry = Flow->Sockets.Flink; Entry != &Flow->Sockets; Entry = Entry->Flink)
        {
            PQWAVE_SOCKET_ENTRY Socket = CONTAINING_RECORD(Entry, QWAVE_SOCKET_ENTRY, Link);

            if (Socket->Socket != Input.Socket)
                continue;
            RemoveEntryList(&Socket->Link);
            QwaveFreeSocket(Socket);
            ASSERT(Flow->SocketCount != 0);
            Flow->SocketCount--;
            ASSERT((Flow->SocketCount == 0) == IsListEmpty(&Flow->Sockets));
            if (Flow->SocketCount == 0)
                QwaveDestroyFlowLocked(Handle, Flow);
            Status = STATUS_SUCCESS;
            break;
        }
    }
    QwaveUnlock(Handle);
    return Status;
}

static
NTSTATUS
QwaveSetFlow(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PQWAVE_SET_FLOW_INPUT Input = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.DeviceIoControl.InputBufferLength;
    QWAVE_FLOWRATE Rate = { 0 };
    ULONG Value = 0;
    PQWAVE_FLOW Flow;

    PAGED_CODE();

    if (Length < FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) ||
        Length - FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) != min(Input->Size, QWAVE_SET_FLOW_MAX_BUFFER))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Input->Flags != 0 || Input->FlowId == 0)
        return STATUS_INVALID_PARAMETER;

    switch (Input->Operation)
    {
        case QWAVE_SET_TRAFFIC_TYPE:
        case QWAVE_SET_OUTGOING_DSCP:
            if (Input->Size < sizeof(ULONG))
                return STATUS_INVALID_PARAMETER;
            RtlCopyMemory(&Value, Input->Buffer, sizeof(Value));
            if (Input->Operation == QWAVE_SET_TRAFFIC_TYPE && Value > QWAVE_TRAFFIC_TYPE_LAST)
                return STATUS_INVALID_PARAMETER;
            if (Input->Operation == QWAVE_SET_OUTGOING_DSCP && Value > QWAVE_DSCP_LAST)
                return STATUS_INVALID_PARAMETER;
            if (Input->Operation == QWAVE_SET_OUTGOING_DSCP && !QwaveAccessCheck(Irp, QwaveMarkingSd))
                return STATUS_ACCESS_DENIED;
            break;

        case QWAVE_SET_OUTGOING_RATE:
            if (Input->Size < sizeof(Rate))
                return STATUS_INVALID_PARAMETER;
            RtlCopyMemory(&Rate, Input->Buffer, sizeof(Rate));
            if (Rate.ShapingBehavior > QWAVE_SHAPING_LAST || Rate.Reason > QWAVE_FLOWRATE_REASON_LAST)
                return STATUS_INVALID_PARAMETER;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    QwaveLock(Handle, TRUE);
    Flow = QwaveFindFlowLocked(Handle, Input->FlowId);
    if (!Flow)
    {
        QwaveUnlock(Handle);
        return STATUS_NOT_FOUND;
    }
    if (Input->Operation == QWAVE_SET_TRAFFIC_TYPE)
    {
        Flow->TrafficType = Value;
    }
    else if (Input->Operation == QWAVE_SET_OUTGOING_DSCP)
    {
        Flow->Dscp = Value;
        Flow->DscpSet = TRUE;
    }
    else
    {
        /* FIXME: no packet scheduler shapes the flow to its rate; the rate is only recorded */
        Flow->Rate = Rate;
        Flow->RateSet = TRUE;
    }
    QwaveMarkFlowLocked(Handle, Flow);
    QwaveUnlock(Handle);
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveQueryFlow(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    QWAVE_QUERY_FLOW_INPUT Input;
    QWAVE_PACKET_PRIORITY Priority = { 0 };
    ULONG64 Rate = 0;
    PQWAVE_FLOW Flow;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Dscp;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if ((Input.Flags & ~QWAVE_QUERYFLOW_FRESH) != 0 || Input.FlowId == 0)
        return STATUS_INVALID_PARAMETER;
    if ((Input.Operation == QWAVE_QUERY_PACKET_PRIORITY && OutputLength < sizeof(Priority)) ||
        (Input.Operation == QWAVE_QUERY_OUTGOING_RATE && OutputLength < sizeof(Rate)) ||
        (Input.Operation == QWAVE_QUERY_FUNDAMENTALS && OutputLength < sizeof(QWAVE_FUNDAMENTALS)) ||
        Input.Operation > QWAVE_QUERY_OUTGOING_RATE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    QwaveLock(Handle, FALSE);
    Flow = QwaveFindFlowLocked(Handle, Input.FlowId);
    if (!Flow)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (Input.Operation == QWAVE_QUERY_PACKET_PRIORITY)
    {
        Dscp = QwaveFlowDscpLocked(Handle, Flow);
        Priority.ConformantDscp = (Dscp == QOS_MARK_UNMARKED) ? 0 : Dscp;
        /* TODO: INFO_NEEDED: the non-conformant DSCP value Windows uses with QOSUseNonConformantMarkings */
        Priority.NonConformantDscp = 0;
        Priority.ConformantL2 = 0;
        Priority.NonConformantL2 = 0;
    }
    else if (Input.Operation == QWAVE_QUERY_OUTGOING_RATE)
    {
        if (Flow->RateSet)
            Rate = Flow->Rate.Bandwidth;
        else
            Status = QWAVE_STATUS_FROM_WIN32(ERROR_NO_DATA);
    }
    else
    {
        /* Non-adaptive flows run no network experiments (QOSAddSocketToFlow documentation) */
        Status = STATUS_NOT_SUPPORTED;
    }
    QwaveUnlock(Handle);

    if (!NT_SUCCESS(Status))
        return Status;
    if (Input.Operation == QWAVE_QUERY_PACKET_PRIORITY)
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Priority, sizeof(Priority));
        Irp->IoStatus.Information = sizeof(Priority);
    }
    else
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Rate, sizeof(Rate));
        Irp->IoStatus.Information = sizeof(Rate);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveEnumerateFlows(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PQWAVE_FLOW_LIST List = Irp->AssociatedIrp.SystemBuffer;
    ULONG Capacity, Total = 0, Returned = 0;
    PLIST_ENTRY HandleEntry, FlowEntry;

    PAGED_CODE();

    if (OutputLength < FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds))
        return STATUS_INVALID_PARAMETER;
    if (!QwaveAccessCheck(Irp, QwaveAdminSd))
        return STATUS_ACCESS_DENIED;
    Capacity = (OutputLength - FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds)) / sizeof(ULONG);

    ExAcquireFastMutex(&QwaveHandlesLock);
    for (HandleEntry = QwaveHandles.Flink; HandleEntry != &QwaveHandles; HandleEntry = HandleEntry->Flink)
    {
        PQWAVE_HANDLE Handle = CONTAINING_RECORD(HandleEntry, QWAVE_HANDLE, Link);

        QwaveLock(Handle, FALSE);
        for (FlowEntry = Handle->Flows.Flink; FlowEntry != &Handle->Flows; FlowEntry = FlowEntry->Flink)
        {
            PQWAVE_FLOW Flow = CONTAINING_RECORD(FlowEntry, QWAVE_FLOW, Link);

            if (Returned < Capacity)
                List->FlowIds[Returned++] = Flow->FlowId;
            Total++;
        }
        QwaveUnlock(Handle);
    }
    ExReleaseFastMutex(&QwaveHandlesLock);

    ASSERT(Returned <= Total);
    ASSERT(Returned <= Capacity);
    List->Total = Total;
    List->Returned = Returned;
    Irp->IoStatus.Information = FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds) + Returned * sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLEANUP)
_Dispatch_type_(IRP_MJ_CLOSE)
static DRIVER_DISPATCH QwaveDispatchHandle;

static
NTSTATUS
NTAPI
QwaveDispatchHandle(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PQWAVE_HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(DeviceObject == QwaveDeviceObject);
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            if (FileObject->FileName.Length != 0)
                return QwaveCompleteIrp(Irp, STATUS_OBJECT_NAME_NOT_FOUND);
            Handle = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Handle), TAG_QWAVE);
            if (!Handle)
                return QwaveCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);
            Status = ExInitializeResourceLite(&Handle->Lock);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Handle, TAG_QWAVE);
                return QwaveCompleteIrp(Irp, Status);
            }
            InitializeListHead(&Handle->Flows);
            FileObject->FsContext = Handle;
            ExAcquireFastMutex(&QwaveHandlesLock);
            InsertTailList(&QwaveHandles, &Handle->Link);
            ExReleaseFastMutex(&QwaveHandlesLock);
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLEANUP:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            QwaveDestroyAllFlows(Handle);
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLOSE:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            ExAcquireFastMutex(&QwaveHandlesLock);
            RemoveEntryList(&Handle->Link);
            ExReleaseFastMutex(&QwaveHandlesLock);
            QwaveDestroyAllFlows(Handle);
            ASSERT(IsListEmpty(&Handle->Flows));
            ExDeleteResourceLite(&Handle->Lock);
            FileObject->FsContext = NULL;
            ExFreePoolWithTag(Handle, TAG_QWAVE);
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        default:
            ASSERT(FALSE);
            return QwaveCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST);
    }
}

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
static DRIVER_DISPATCH QwaveDispatchDeviceControl;

static
NTSTATUS
NTAPI
QwaveDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PQWAVE_HANDLE Handle = Stack->FileObject->FsContext;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(DeviceObject == QwaveDeviceObject);
    ASSERT(Handle != NULL);
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_QWAVE_ADD_SOCKET:
            Status = QwaveAddSocket(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_REMOVE_SOCKET:
            Status = QwaveRemoveSocket(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_SET_FLOW:
            Status = QwaveSetFlow(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_QUERY_FLOW:
            Status = QwaveQueryFlow(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_ENUMERATE_FLOWS:
            Status = QwaveEnumerateFlows(Irp, Stack);
            break;
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    return QwaveCompleteIrp(Irp, Status);
}

static
NTSTATUS
QwaveCreateSd(
    _In_ BOOLEAN WithOperators,
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    ULONG SidLength = RtlLengthRequiredSid(2);
    ULONG AclLength = sizeof(ACL) + 2 * FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
                      RtlLengthSid(SeExports->SeAliasAdminsSid) + SidLength;
    PSECURITY_DESCRIPTOR Sd;
    PACL Acl;
    PSID OperatorsSid;
    NTSTATUS Status;

    PAGED_CODE();

    *SecurityDescriptor = NULL;
    Sd = ExAllocatePoolZero(PagedPool, sizeof(SECURITY_DESCRIPTOR) + AclLength + SidLength, TAG_QWAVE);
    if (!Sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    Acl = (PACL)((PUCHAR)Sd + sizeof(SECURITY_DESCRIPTOR));
    OperatorsSid = (PUCHAR)Acl + AclLength;

    Status = RtlInitializeSid(OperatorsSid, &NtAuthority, 2);
    if (NT_SUCCESS(Status))
    {
        *RtlSubAuthoritySid(OperatorsSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
        *RtlSubAuthoritySid(OperatorsSid, 1) = DOMAIN_ALIAS_RID_NETWORK_CONFIGURATION_OPS;
        Status = RtlCreateAcl(Acl, AclLength, ACL_REVISION);
    }
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, SeExports->SeAliasAdminsSid);
    if (NT_SUCCESS(Status) && WithOperators)
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, OperatorsSid);
    if (NT_SUCCESS(Status))
        Status = RtlCreateSecurityDescriptor(Sd, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Acl, FALSE);
    if (NT_SUCCESS(Status))
        Status = RtlSetOwnerSecurityDescriptor(Sd, SeExports->SeAliasAdminsSid, FALSE);
    if (NT_SUCCESS(Status))
        Status = RtlSetGroupSecurityDescriptor(Sd, SeExports->SeLocalSystemSid, FALSE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_QWAVE);
        return Status;
    }
    ASSERT(RtlValidSecurityDescriptor(Sd));
    *SecurityDescriptor = Sd;
    return STATUS_SUCCESS;
}

static
VOID
QwaveFreeSds(VOID)
{
    if (QwaveMarkingSd)
        ExFreePoolWithTag(QwaveMarkingSd, TAG_QWAVE);
    if (QwaveAdminSd)
        ExFreePoolWithTag(QwaveAdminSd, TAG_QWAVE);
    QwaveMarkingSd = NULL;
    QwaveAdminSd = NULL;
}

static DRIVER_UNLOAD QwaveUnload;

static
VOID
NTAPI
QwaveUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    ASSERT(IsListEmpty(&QwaveHandles));
    IoDeleteDevice(QwaveDeviceObject);
    QwaveDeviceObject = NULL;
    QwaveFreeSds();
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(QWAVE_DEVICE_NAME);
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(RegistryPath);

    ExInitializeFastMutex(&QwaveHandlesLock);
    InitializeListHead(&QwaveHandles);
    Status = QwaveCreateSd(TRUE, &QwaveMarkingSd);
    if (NT_SUCCESS(Status))
        Status = QwaveCreateSd(FALSE, &QwaveAdminSd);
    if (NT_SUCCESS(Status))
    {
        Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
                                &QwaveDeviceObject);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("QWAVE: initialization failed 0x%08lx\n", Status);
        QwaveFreeSds();
        return Status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = QwaveDispatchHandle;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = QwaveDispatchHandle;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = QwaveDispatchHandle;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = QwaveDispatchDeviceControl;
    DriverObject->DriverUnload = QwaveUnload;
    QwaveDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
