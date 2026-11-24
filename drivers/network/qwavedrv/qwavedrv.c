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

#define QWAVE_FLOW_READY        0
#define QWAVE_FLOW_STARTING     1

#define QWAVE_WAIT_ANY          0
#define QWAVE_WAIT_WORK         1
#define QWAVE_WAIT_NOTIFY       2

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
    BOOLEAN Adaptive;
    BOOLEAN MarkingActive;
    BOOLEAN Monitored;
    BOOLEAN FundamentalsValid;
    UCHAR State;
    QWAVE_FLOWRATE Rate;
    QWAVE_ADDRESS Destination;
    QWAVE_FUNDAMENTALS Fundamentals;
} QWAVE_FLOW, *PQWAVE_FLOW;

typedef struct _QWAVE_TRACKED
{
    LIST_ENTRY Link;
    QWAVE_ADDRESS Client;
} QWAVE_TRACKED, *PQWAVE_TRACKED;

typedef struct _QWAVE_HANDLE
{
    LIST_ENTRY Link;
    ERESOURCE Lock;
    LIST_ENTRY Flows;
    LIST_ENTRY Tracked;
    BOOLEAN IsService;
} QWAVE_HANDLE, *PQWAVE_HANDLE;

typedef struct _QWAVE_WORK_ITEM
{
    LIST_ENTRY Link;
    QWAVE_WORK Work;
} QWAVE_WORK_ITEM, *PQWAVE_WORK_ITEM;

typedef struct _QWAVE_WAIT_MATCH
{
    ULONG Kind;
    ULONG Key;
    ULONG Operation;
    PFILE_OBJECT FileObject;
} QWAVE_WAIT_MATCH, *PQWAVE_WAIT_MATCH;

/* DSCP marks of the QOS_TRAFFIC_TYPE values (qos2.h documentation); best effort traffic is not marked */
static const ULONG QwaveTrafficTypeDscp[QWAVE_TRAFFIC_TYPE_LAST + 1] = { QOS_MARK_UNMARKED, 0x08, 0x28, 0x28, 0x38, 0x38 };

static const UNICODE_STRING QwaveAfdDriverName = RTL_CONSTANT_STRING(L"\\Driver\\AFD");

static PDEVICE_OBJECT QwaveDeviceObject;
static PSECURITY_DESCRIPTOR QwaveMarkingSd;
static PSECURITY_DESCRIPTOR QwaveAdminSd;
static PSECURITY_DESCRIPTOR QwaveServiceSd;
/* An ERESOURCE: socket marking under it waits for I/O completion APCs */
static ERESOURCE QwaveHandlesLock;
static LIST_ENTRY QwaveHandles;
static LONG QwaveLastFlowId;

/* Work for the qWAVE service and its pending reads, both under QwaveWorkLock */
static KSPIN_LOCK QwaveWorkLock;
static LIST_ENTRY QwaveWorkQueue;
static LIST_ENTRY QwaveServiceReads;
static LONG QwaveLastWorkId;
static LONG QwaveServiceCount;

/* Client requests that wait for a service result or a flow notification */
static KSPIN_LOCK QwaveWaitLock;
static LIST_ENTRY QwaveWaiters;
static IO_CSQ QwaveWaitCsq;

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
NTSTATUS
QwaveWin32(
    _In_ ULONG Error)
{
    ASSERT(Error != ERROR_SUCCESS);
    return QWAVE_STATUS_FROM_WIN32(Error);
}

/* ---- Waiting client requests ---- */

static
BOOLEAN
QwaveWaitMatches(
    _In_ PIRP Irp,
    _In_opt_ PQWAVE_WAIT_MATCH Match)
{
    if (!Match)
        return TRUE;
    if (Match->FileObject && IoGetCurrentIrpStackLocation(Irp)->FileObject != Match->FileObject)
        return FALSE;
    if (Match->Kind == QWAVE_WAIT_ANY)
        return TRUE;
    if ((ULONG_PTR)Irp->Tail.Overlay.DriverContext[0] != Match->Kind ||
        (ULONG_PTR)Irp->Tail.Overlay.DriverContext[1] != Match->Key)
    {
        return FALSE;
    }
    return Match->Kind != QWAVE_WAIT_NOTIFY || (ULONG_PTR)Irp->Tail.Overlay.DriverContext[2] == Match->Operation;
}

static
VOID
NTAPI
QwaveWaitInsert(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    InsertTailList(&QwaveWaiters, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
QwaveWaitRemove(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
QwaveWaitPeekNext(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PLIST_ENTRY Entry = Irp ? Irp->Tail.Overlay.ListEntry.Flink : QwaveWaiters.Flink;

    UNREFERENCED_PARAMETER(Csq);
    for (; Entry != &QwaveWaiters; Entry = Entry->Flink)
    {
        PIRP Candidate = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        if (QwaveWaitMatches(Candidate, PeekContext))
            return Candidate;
    }
    return NULL;
}

static
VOID
NTAPI
QwaveWaitAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    UNREFERENCED_PARAMETER(Csq);
    KeAcquireSpinLock(&QwaveWaitLock, Irql);
}

static
VOID
NTAPI
QwaveWaitReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Csq);
    KeReleaseSpinLock(&QwaveWaitLock, Irql);
}

static
VOID
NTAPI
QwaveWaitCompleteCanceled(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
QwaveWait(
    _In_ PIRP Irp,
    _In_ ULONG Kind,
    _In_ ULONG Key,
    _In_ ULONG Operation)
{
    ASSERT(Kind != QWAVE_WAIT_ANY);
    Irp->Tail.Overlay.DriverContext[0] = (PVOID)(ULONG_PTR)Kind;
    Irp->Tail.Overlay.DriverContext[1] = (PVOID)(ULONG_PTR)Key;
    Irp->Tail.Overlay.DriverContext[2] = (PVOID)(ULONG_PTR)Operation;
    IoCsqInsertIrp(&QwaveWaitCsq, Irp, NULL);
    return STATUS_PENDING;
}

static
PIRP
QwaveTakeWaiter(
    _In_ ULONG Kind,
    _In_ ULONG Key,
    _In_ ULONG Operation,
    _In_opt_ PFILE_OBJECT FileObject)
{
    QWAVE_WAIT_MATCH Match = { Kind, Key, Operation, FileObject };

    return IoCsqRemoveNextIrp(&QwaveWaitCsq, &Match);
}

static
BOOLEAN
QwaveHasWaiter(
    _In_ ULONG Kind,
    _In_ ULONG Key,
    _In_ ULONG Operation)
{
    QWAVE_WAIT_MATCH Match = { Kind, Key, Operation, NULL };
    BOOLEAN Found;
    KIRQL OldIrql;

    KeAcquireSpinLock(&QwaveWaitLock, &OldIrql);
    Found = QwaveWaitPeekNext(&QwaveWaitCsq, NULL, &Match) != NULL;
    KeReleaseSpinLock(&QwaveWaitLock, OldIrql);
    return Found;
}

static
VOID
QwaveFinishIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/* ---- Work for the qWAVE service ---- */

static
VOID
NTAPI
QwaveCancelServiceRead(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&QwaveWorkLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&QwaveWorkLock, OldIrql);
    QwaveFinishIrp(Irp, STATUS_CANCELLED, 0);
}

/* Takes a pending service read whose cancel routine can still be cleared; QwaveWorkLock held */
static
PIRP
QwaveTakeServiceReadLocked(
    _In_opt_ PFILE_OBJECT FileObject)
{
    PLIST_ENTRY Entry;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    for (Entry = QwaveServiceReads.Flink; Entry != &QwaveServiceReads; Entry = Entry->Flink)
    {
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        if (FileObject && IoGetCurrentIrpStackLocation(Irp)->FileObject != FileObject)
            continue;
        if (IoSetCancelRoutine(Irp, NULL) != NULL)
        {
            RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
            return Irp;
        }
    }
    return NULL;
}

static
VOID
QwaveDeliverWork(
    _In_ PIRP Irp,
    _In_ PQWAVE_WORK_ITEM Item)
{
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Item->Work, sizeof(Item->Work));
    ExFreePoolWithTag(Item, TAG_QWAVE);
    QwaveFinishIrp(Irp, STATUS_SUCCESS, sizeof(QWAVE_WORK));
}

static
ULONG
QwaveNewWorkId(VOID)
{
    ULONG WorkId;

    do
    {
        WorkId = (ULONG)InterlockedIncrement(&QwaveLastWorkId);
    } while (WorkId == 0);
    return WorkId;
}

/* Queues work for the service; returns FALSE when it cannot be allocated */
static
BOOLEAN
QwaveQueueWorkId(
    _In_ ULONG WorkId,
    _In_ ULONG Type,
    _In_ ULONG FlowId,
    _In_opt_ const QWAVE_ADDRESS *Destination)
{
    PQWAVE_WORK_ITEM Item;
    PIRP Irp;
    KIRQL OldIrql;

    ASSERT(WorkId != 0);
    Item = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Item), TAG_QWAVE);
    if (!Item)
        return FALSE;
    Item->Work.WorkId = WorkId;
    Item->Work.Type = Type;
    Item->Work.FlowId = FlowId;
    if (Destination)
        Item->Work.Destination = *Destination;

    KeAcquireSpinLock(&QwaveWorkLock, &OldIrql);
    Irp = QwaveTakeServiceReadLocked(NULL);
    if (!Irp)
        InsertTailList(&QwaveWorkQueue, &Item->Link);
    KeReleaseSpinLock(&QwaveWorkLock, OldIrql);

    if (Irp)
        QwaveDeliverWork(Irp, Item);
    return TRUE;
}

static
VOID
QwaveQueueWork(
    _In_ ULONG Type,
    _In_ ULONG FlowId,
    _In_opt_ const QWAVE_ADDRESS *Destination)
{
    (VOID)QwaveQueueWorkId(QwaveNewWorkId(), Type, FlowId, Destination);
}

/* Makes Irp wait for the result of new work; the work id is set before the service can answer */
static
NTSTATUS
QwaveWaitForWork(
    _In_ PIRP Irp,
    _In_ ULONG Type,
    _In_ ULONG FlowId,
    _In_ const QWAVE_ADDRESS *Destination)
{
    ULONG WorkId = QwaveNewWorkId();

    (VOID)QwaveWait(Irp, QWAVE_WAIT_WORK, WorkId, 0);
    if (!QwaveQueueWorkId(WorkId, Type, FlowId, Destination))
    {
        PIRP Waiter = QwaveTakeWaiter(QWAVE_WAIT_WORK, WorkId, 0, NULL);

        if (Waiter)
            QwaveFinishIrp(Waiter, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return STATUS_PENDING;
}

static
VOID
QwaveDropWork(VOID)
{
    LIST_ENTRY Dropped;
    KIRQL OldIrql;

    InitializeListHead(&Dropped);
    KeAcquireSpinLock(&QwaveWorkLock, &OldIrql);
    while (!IsListEmpty(&QwaveWorkQueue))
        InsertTailList(&Dropped, RemoveHeadList(&QwaveWorkQueue));
    KeReleaseSpinLock(&QwaveWorkLock, OldIrql);
    while (!IsListEmpty(&Dropped))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dropped), QWAVE_WORK_ITEM, Link), TAG_QWAVE);
}

/* ---- Flows ---- */

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
        ASSERT(!Flow->Adaptive || Flow->SocketCount == 1);
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

    /* An adaptive flow is prioritized once QOSSetFlow has granted its rate */
    if (Flow->Adaptive && !Flow->MarkingActive)
        return QOS_MARK_UNMARKED;
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
    if (Flow->Adaptive)
        QwaveQueueWork(QWAVE_WORK_STOP_SESSION, Flow->FlowId, &Flow->Destination);
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
    while (!IsListEmpty(&Handle->Tracked))
    {
        PQWAVE_TRACKED Tracked = CONTAINING_RECORD(RemoveHeadList(&Handle->Tracked), QWAVE_TRACKED, Link);

        QwaveQueueWork(QWAVE_WORK_STOP_SESSION, 0, &Tracked->Client);
        ExFreePoolWithTag(Tracked, TAG_QWAVE);
    }
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
BOOLEAN
QwaveValidAddress(
    _In_ const QWAVE_ADDRESS *Address)
{
    return Address->Family == 2 /* AF_INET */ || Address->Family == 23 /* AF_INET6 */;
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
    BOOLEAN Adaptive;
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
    Adaptive = !(Input.Flags & QWAVE_NON_ADAPTIVE_FLOW);
    if (Adaptive)
    {
        /* Only non-adaptive flows can have several sockets (QOSAddSocketToFlow documentation) */
        if (Input.FlowId != 0 || !QwaveValidAddress(&Input.Destination))
            return STATUS_INVALID_PARAMETER;
        if (InterlockedCompareExchange(&QwaveServiceCount, 0, 0) == 0)
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
        NewFlow->Adaptive = Adaptive;
        NewFlow->State = Adaptive ? QWAVE_FLOW_STARTING : QWAVE_FLOW_READY;
        NewFlow->Destination = Input.Destination;
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
        if (!Flow || Flow->Adaptive)
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
    if (!Adaptive)
    {
        Irp->IoStatus.Information = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    /* The flow is adaptive once the service has a network test session with the destination's sink */
    return QwaveWaitForWork(Irp, QWAVE_WORK_START_SESSION, FlowId, &Input.Destination);
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
    QWAVE_ADDRESS Destination;
    ULONG Value = 0, FlowId;
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
    if (Flow->State != QWAVE_FLOW_READY)
    {
        QwaveUnlock(Handle);
        return STATUS_INVALID_PARAMETER;
    }
    if (Flow->Adaptive && Input->Operation == QWAVE_SET_OUTGOING_RATE)
    {
        /* The rate is granted against a fresh measurement of the path (QOSSetFlow documentation) */
        FlowId = Flow->FlowId;
        Destination = Flow->Destination;
        QwaveUnlock(Handle);
        return QwaveWaitForWork(Irp, QWAVE_WORK_MEASURE, FlowId, &Destination);
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
    QWAVE_FUNDAMENTALS Fundamentals;
    QWAVE_ADDRESS Destination;
    ULONG64 Rate = 0;
    PQWAVE_FLOW Flow;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Measure = FALSE;
    ULONG Dscp;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if ((Input.Flags & ~QWAVE_QUERYFLOW_FRESH) != 0 || Input.FlowId == 0)
        return STATUS_INVALID_PARAMETER;
    if ((Input.Operation == QWAVE_QUERY_PACKET_PRIORITY && OutputLength < sizeof(Priority)) ||
        (Input.Operation == QWAVE_QUERY_OUTGOING_RATE && OutputLength < sizeof(Rate)) ||
        (Input.Operation == QWAVE_QUERY_FUNDAMENTALS && OutputLength < sizeof(Fundamentals)) ||
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
        /* FIXME: no 802.1p tags are added, so the layer-2 values stay 0 */
        Priority.ConformantL2 = 0;
        Priority.NonConformantL2 = 0;
    }
    else if (Input.Operation == QWAVE_QUERY_OUTGOING_RATE)
    {
        if (Flow->RateSet)
            Rate = Flow->Rate.Bandwidth;
        else
            Status = QwaveWin32(ERROR_NO_DATA);
    }
    else if (!Flow->Adaptive)
    {
        /* Non-adaptive flows run no network experiments (QOSAddSocketToFlow documentation) */
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (Flow->State != QWAVE_FLOW_READY)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (Flow->FundamentalsValid && !(Input.Flags & QWAVE_QUERYFLOW_FRESH))
    {
        Fundamentals = Flow->Fundamentals;
    }
    else
    {
        Measure = TRUE;
        Destination = Flow->Destination;
    }
    QwaveUnlock(Handle);

    if (!NT_SUCCESS(Status))
        return Status;
    if (Measure)
        return QwaveWaitForWork(Irp, QWAVE_WORK_MEASURE, Input.FlowId, &Destination);
    if (Input.Operation == QWAVE_QUERY_PACKET_PRIORITY)
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Priority, sizeof(Priority));
        Irp->IoStatus.Information = sizeof(Priority);
    }
    else if (Input.Operation == QWAVE_QUERY_OUTGOING_RATE)
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Rate, sizeof(Rate));
        Irp->IoStatus.Information = sizeof(Rate);
    }
    else
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Fundamentals, sizeof(Fundamentals));
        Irp->IoStatus.Information = sizeof(Fundamentals);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
QwaveNotifyFlow(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    QWAVE_NOTIFY_INPUT Input;
    QWAVE_ADDRESS Destination;
    PQWAVE_FLOW Flow;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Monitor = FALSE;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if (Input.Flags != 0 || Input.FlowId == 0 || Input.Operation > QWAVE_NOTIFY_AVAILABLE)
        return STATUS_INVALID_PARAMETER;

    QwaveLock(Handle, TRUE);
    Flow = QwaveFindFlowLocked(Handle, Input.FlowId);
    if (!Flow)
        Status = STATUS_NOT_FOUND;
    else if (!Flow->Adaptive)
        Status = STATUS_NOT_SUPPORTED;
    else if (Flow->State != QWAVE_FLOW_READY)
        Status = STATUS_INVALID_PARAMETER;
    else if (QwaveHasWaiter(QWAVE_WAIT_NOTIFY, Input.FlowId, Input.Operation))
        Status = QwaveWin32(ERROR_ALREADY_EXISTS);
    if (NT_SUCCESS(Status))
    {
        (VOID)QwaveWait(Irp, QWAVE_WAIT_NOTIFY, Input.FlowId, Input.Operation);
        Monitor = !Flow->Monitored;
        Flow->Monitored = TRUE;
        Destination = Flow->Destination;
    }
    QwaveUnlock(Handle);

    if (!NT_SUCCESS(Status))
        return Status;
    if (Monitor)
        QwaveQueueWork(QWAVE_WORK_MONITOR, Input.FlowId, &Destination);
    return STATUS_PENDING;
}

static
NTSTATUS
QwaveTrackClient(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    QWAVE_TRACK_INPUT Input;
    PLIST_ENTRY Entry;
    PQWAVE_TRACKED Found = NULL;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if (Input.Flags != 0 || !QwaveValidAddress(&Input.Client))
        return STATUS_INVALID_PARAMETER;
    Input.Client.Port = 0;

    if (Input.Start)
    {
        if (InterlockedCompareExchange(&QwaveServiceCount, 0, 0) == 0)
            return STATUS_NOT_SUPPORTED;
        return QwaveWaitForWork(Irp, QWAVE_WORK_START_SESSION, 0, &Input.Client);
    }

    QwaveLock(Handle, TRUE);
    for (Entry = Handle->Tracked.Flink; Entry != &Handle->Tracked; Entry = Entry->Flink)
    {
        PQWAVE_TRACKED Tracked = CONTAINING_RECORD(Entry, QWAVE_TRACKED, Link);

        if (RtlEqualMemory(&Tracked->Client, &Input.Client, sizeof(Input.Client)))
        {
            RemoveEntryList(&Tracked->Link);
            Found = Tracked;
            break;
        }
    }
    QwaveUnlock(Handle);
    if (!Found)
        return STATUS_NOT_FOUND;
    QwaveQueueWork(QWAVE_WORK_STOP_SESSION, 0, &Found->Client);
    ExFreePoolWithTag(Found, TAG_QWAVE);
    return STATUS_SUCCESS;
}

/* Finds FlowId among every handle's flows and runs Callback on it with that handle locked exclusively */
static
BOOLEAN
QwaveWithFlow(
    _In_ ULONG FlowId,
    _In_ VOID (*Callback)(PQWAVE_HANDLE, PQWAVE_FLOW, PVOID),
    _In_opt_ PVOID Context)
{
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&QwaveHandlesLock, TRUE);
    for (Entry = QwaveHandles.Flink; Entry != &QwaveHandles && !Found; Entry = Entry->Flink)
    {
        PQWAVE_HANDLE Handle = CONTAINING_RECORD(Entry, QWAVE_HANDLE, Link);
        PQWAVE_FLOW Flow;

        QwaveLock(Handle, TRUE);
        Flow = QwaveFindFlowLocked(Handle, FlowId);
        if (Flow)
        {
            Callback(Handle, Flow, Context);
            Found = TRUE;
        }
        QwaveUnlock(Handle);
    }
    ExReleaseResourceLite(&QwaveHandlesLock);
    KeLeaveCriticalRegion();
    return Found;
}

static
VOID
QwaveDropStartingFlow(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    if (Flow->State == QWAVE_FLOW_STARTING)
        QwaveDestroyFlowLocked(Handle, Flow);
}

static
VOID
QwaveEvaluateNotifies(
    _In_ ULONG FlowId,
    _In_ BOOLEAN RateSet,
    _In_ ULONG64 Rate,
    _In_ const QWAVE_FUNDAMENTALS *Fundamentals)
{
    ULONG Operation;

    if (!Fundamentals->AvailableBandwidthSet)
        return;
    for (Operation = QWAVE_NOTIFY_CONGESTED; Operation <= QWAVE_NOTIFY_AVAILABLE; Operation++)
    {
        PIRP Irp = QwaveTakeWaiter(QWAVE_WAIT_NOTIFY, FlowId, Operation, NULL);
        PQWAVE_NOTIFY_INPUT Input;
        BOOLEAN Holds;

        if (!Irp)
            continue;
        Input = Irp->AssociatedIrp.SystemBuffer;
        if (Operation == QWAVE_NOTIFY_CONGESTED)
            Holds = RateSet && Fundamentals->AvailableBandwidth < Rate;
        else if (Operation == QWAVE_NOTIFY_UNCONGESTED)
            Holds = RateSet && Fundamentals->AvailableBandwidth >= Rate;
        else
            Holds = Fundamentals->AvailableBandwidth >= Input->Threshold;
        if (!Holds)
        {
            (VOID)QwaveWait(Irp, QWAVE_WAIT_NOTIFY, FlowId, Operation);
            continue;
        }
        if (IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(ULONG64))
        {
            *(PULONG64)Irp->AssociatedIrp.SystemBuffer = Fundamentals->AvailableBandwidth;
            QwaveFinishIrp(Irp, STATUS_SUCCESS, sizeof(ULONG64));
        }
        else
        {
            QwaveFinishIrp(Irp, STATUS_SUCCESS, 0);
        }
    }
}

typedef struct _QWAVE_RESULT_CONTEXT
{
    const QWAVE_WORK_RESULT *Result;
    BOOLEAN RateSet;
    ULONG64 Rate;
} QWAVE_RESULT_CONTEXT, *PQWAVE_RESULT_CONTEXT;

static
VOID
QwaveStoreFundamentals(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow,
    _In_opt_ PVOID Context)
{
    PQWAVE_RESULT_CONTEXT Result = Context;

    UNREFERENCED_PARAMETER(Handle);
    ASSERT(Result != NULL);
    Flow->Fundamentals = Result->Result->Fundamentals;
    Flow->FundamentalsValid = TRUE;
    Result->RateSet = Flow->RateSet;
    Result->Rate = Flow->Rate.Bandwidth;
}

/* Completes the client request that waited for Result */
static
VOID
QwaveFinishWork(
    _In_ PIRP Waiter,
    _In_ const QWAVE_WORK_RESULT *Result)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Waiter);
    PQWAVE_HANDLE Handle = Stack->FileObject->FsContext;
    NTSTATUS Status = Result->Error ? QwaveWin32(Result->Error) : STATUS_SUCCESS;
    ULONG_PTR Information = 0;
    PQWAVE_FLOW Flow;

    PAGED_CODE();
    ASSERT(Handle != NULL);

    if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QWAVE_TRACK_CLIENT)
    {
        PQWAVE_TRACK_INPUT Input = Waiter->AssociatedIrp.SystemBuffer;
        PQWAVE_TRACKED Tracked;

        if (NT_SUCCESS(Status))
        {
            Tracked = ExAllocatePoolZero(PagedPool, sizeof(*Tracked), TAG_QWAVE);
            if (!Tracked)
            {
                QwaveQueueWork(QWAVE_WORK_STOP_SESSION, 0, &Input->Client);
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                Tracked->Client = Input->Client;
                Tracked->Client.Port = 0;
                QwaveLock(Handle, TRUE);
                InsertTailList(&Handle->Tracked, &Tracked->Link);
                QwaveUnlock(Handle);
            }
        }
        QwaveFinishIrp(Waiter, Status, 0);
        return;
    }

    QwaveLock(Handle, TRUE);
    Flow = QwaveFindFlowLocked(Handle, Result->FlowId);
    if (!Flow)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QWAVE_ADD_SOCKET)
    {
        ASSERT(Flow->Adaptive && Flow->State == QWAVE_FLOW_STARTING);
        if (NT_SUCCESS(Status))
        {
            Flow->State = QWAVE_FLOW_READY;
            *(PULONG)Waiter->AssociatedIrp.SystemBuffer = Flow->FlowId;
            Information = sizeof(ULONG);
        }
        else
        {
            /* The session never started, so there is none to stop */
            Flow->Adaptive = FALSE;
            QwaveDestroyFlowLocked(Handle, Flow);
        }
    }
    else if (NT_SUCCESS(Status))
    {
        Flow->Fundamentals = Result->Fundamentals;
        Flow->FundamentalsValid = TRUE;
        if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QWAVE_SET_FLOW)
        {
            PQWAVE_SET_FLOW_INPUT Input = Waiter->AssociatedIrp.SystemBuffer;
            QWAVE_FLOWRATE Rate;

            RtlCopyMemory(&Rate, Input->Buffer, sizeof(Rate));
            if (Result->Fundamentals.AvailableBandwidthSet && Rate.Bandwidth > Result->Fundamentals.AvailableBandwidth)
            {
                Status = QwaveWin32(ERROR_NETWORK_BUSY);
            }
            else
            {
                /* FIXME: no packet scheduler shapes the flow to its rate; the rate is only recorded */
                Flow->Rate = Rate;
                Flow->RateSet = TRUE;
                Flow->MarkingActive = TRUE;
                QwaveMarkFlowLocked(Handle, Flow);
            }
        }
        else
        {
            ASSERT(Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QWAVE_QUERY_FLOW);
            RtlCopyMemory(Waiter->AssociatedIrp.SystemBuffer, &Result->Fundamentals, sizeof(Result->Fundamentals));
            Information = sizeof(Result->Fundamentals);
        }
    }
    QwaveUnlock(Handle);
    QwaveFinishIrp(Waiter, Status, Information);
}

static
NTSTATUS
QwaveServiceRequest(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    QWAVE_RESULT_CONTEXT Context;
    QWAVE_WORK_RESULT Result;
    PQWAVE_WORK_ITEM Item = NULL;
    PIRP Waiter;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Code == IOCTL_QWAVE_SERVICE_REGISTER)
    {
        if (!QwaveAccessCheck(Irp, QwaveServiceSd))
            return STATUS_ACCESS_DENIED;
        QwaveLock(Handle, TRUE);
        if (!Handle->IsService)
        {
            Handle->IsService = TRUE;
            InterlockedIncrement(&QwaveServiceCount);
        }
        QwaveUnlock(Handle);
        return STATUS_SUCCESS;
    }
    if (!Handle->IsService)
        return STATUS_ACCESS_DENIED;

    if (Code == IOCTL_QWAVE_SERVICE_GET_WORK)
    {
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(QWAVE_WORK))
            return STATUS_INVALID_PARAMETER;
        KeAcquireSpinLock(&QwaveWorkLock, &OldIrql);
        if (!IsListEmpty(&QwaveWorkQueue))
        {
            Item = CONTAINING_RECORD(RemoveHeadList(&QwaveWorkQueue), QWAVE_WORK_ITEM, Link);
        }
        else
        {
            IoSetCancelRoutine(Irp, QwaveCancelServiceRead);
            if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL) != NULL)
            {
                KeReleaseSpinLock(&QwaveWorkLock, OldIrql);
                return STATUS_CANCELLED;
            }
            IoMarkIrpPending(Irp);
            InsertTailList(&QwaveServiceReads, &Irp->Tail.Overlay.ListEntry);
        }
        KeReleaseSpinLock(&QwaveWorkLock, OldIrql);
        if (!Item)
            return STATUS_PENDING;
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Item->Work, sizeof(Item->Work));
        ExFreePoolWithTag(Item, TAG_QWAVE);
        Irp->IoStatus.Information = sizeof(QWAVE_WORK);
        return STATUS_SUCCESS;
    }

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Result))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Result, Irp->AssociatedIrp.SystemBuffer, sizeof(Result));
    if (Code == IOCTL_QWAVE_SERVICE_UPDATE)
    {
        Context.Result = &Result;
        Context.RateSet = FALSE;
        Context.Rate = 0;
        if (QwaveWithFlow(Result.FlowId, QwaveStoreFundamentals, &Context))
            QwaveEvaluateNotifies(Result.FlowId, Context.RateSet, Context.Rate, &Result.Fundamentals);
        return STATUS_SUCCESS;
    }

    ASSERT(Code == IOCTL_QWAVE_SERVICE_COMPLETE);
    Waiter = QwaveTakeWaiter(QWAVE_WAIT_WORK, Result.WorkId, 0, NULL);
    if (Waiter)
    {
        QwaveFinishWork(Waiter, &Result);
    }
    else if (Result.FlowId != 0)
    {
        /* The client gave up waiting: a flow still starting is not wanted any more */
        (VOID)QwaveWithFlow(Result.FlowId, QwaveDropStartingFlow, NULL);
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

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&QwaveHandlesLock, TRUE);
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
    ExReleaseResourceLite(&QwaveHandlesLock);
    KeLeaveCriticalRegion();

    ASSERT(Returned <= Total);
    ASSERT(Returned <= Capacity);
    List->Total = Total;
    List->Returned = Returned;
    Irp->IoStatus.Information = FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds) + Returned * sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
VOID
QwaveCleanupHandle(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PFILE_OBJECT FileObject)
{
    PIRP Irp;
    KIRQL OldIrql;

    PAGED_CODE();

    while ((Irp = QwaveTakeWaiter(QWAVE_WAIT_ANY, 0, 0, FileObject)) != NULL)
        QwaveFinishIrp(Irp, STATUS_CANCELLED, 0);
    QwaveDestroyAllFlows(Handle);

    if (!Handle->IsService)
        return;
    for (;;)
    {
        KeAcquireSpinLock(&QwaveWorkLock, &OldIrql);
        Irp = QwaveTakeServiceReadLocked(FileObject);
        KeReleaseSpinLock(&QwaveWorkLock, OldIrql);
        if (!Irp)
            break;
        QwaveFinishIrp(Irp, STATUS_CANCELLED, 0);
    }
    if (InterlockedDecrement(&QwaveServiceCount) == 0)
    {
        /* Without the service no session result will come: fail the requests that wait for one */
        QwaveDropWork();
        while ((Irp = QwaveTakeWaiter(QWAVE_WAIT_ANY, 0, 0, NULL)) != NULL)
            QwaveFinishIrp(Irp, STATUS_NOT_SUPPORTED, 0);
    }
    Handle->IsService = FALSE;
}

static
NTSTATUS
QwaveCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_PENDING)
        return Status;
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
            InitializeListHead(&Handle->Tracked);
            FileObject->FsContext = Handle;
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&QwaveHandlesLock, TRUE);
            InsertTailList(&QwaveHandles, &Handle->Link);
            ExReleaseResourceLite(&QwaveHandlesLock);
            KeLeaveCriticalRegion();
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLEANUP:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            QwaveCleanupHandle(Handle, FileObject);
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLOSE:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&QwaveHandlesLock, TRUE);
            RemoveEntryList(&Handle->Link);
            ExReleaseResourceLite(&QwaveHandlesLock);
            KeLeaveCriticalRegion();
            QwaveDestroyAllFlows(Handle);
            ASSERT(IsListEmpty(&Handle->Flows));
            ASSERT(IsListEmpty(&Handle->Tracked));
            ASSERT(!Handle->IsService);
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
        case IOCTL_QWAVE_NOTIFY_FLOW:
            Status = QwaveNotifyFlow(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_TRACK_CLIENT:
            Status = QwaveTrackClient(Handle, Irp, Stack);
            break;
        case IOCTL_QWAVE_SERVICE_REGISTER:
        case IOCTL_QWAVE_SERVICE_GET_WORK:
        case IOCTL_QWAVE_SERVICE_COMPLETE:
        case IOCTL_QWAVE_SERVICE_UPDATE:
            Status = QwaveServiceRequest(Handle, Irp, Stack);
            break;
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    return QwaveCompleteIrp(Irp, Status);
}

/* A DACL that grants FILE_ALL_ACCESS to each of Sids */
static
NTSTATUS
QwaveCreateSd(
    _In_reads_(Count) PSID *Sids,
    _In_ ULONG Count,
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    ULONG AclLength = sizeof(ACL), Index;
    PSECURITY_DESCRIPTOR Sd;
    PACL Acl;
    NTSTATUS Status;

    PAGED_CODE();

    *SecurityDescriptor = NULL;
    for (Index = 0; Index < Count; Index++)
        AclLength += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + RtlLengthSid(Sids[Index]);
    Sd = ExAllocatePoolZero(PagedPool, sizeof(SECURITY_DESCRIPTOR) + AclLength, TAG_QWAVE);
    if (!Sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    Acl = (PACL)((PUCHAR)Sd + sizeof(SECURITY_DESCRIPTOR));

    Status = RtlCreateAcl(Acl, AclLength, ACL_REVISION);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, Sids[Index]);
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
    if (QwaveServiceSd)
        ExFreePoolWithTag(QwaveServiceSd, TAG_QWAVE);
    QwaveMarkingSd = NULL;
    QwaveAdminSd = NULL;
    QwaveServiceSd = NULL;
}

static
NTSTATUS
QwaveCreateSds(VOID)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    UCHAR OperatorsBuffer[SECURITY_MAX_SID_SIZE];
    PSID OperatorsSid = (PSID)OperatorsBuffer;
    PSID Sids[2];
    NTSTATUS Status;

    PAGED_CODE();

    Status = RtlInitializeSid(OperatorsSid, &NtAuthority, 2);
    if (!NT_SUCCESS(Status))
        return Status;
    *RtlSubAuthoritySid(OperatorsSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(OperatorsSid, 1) = DOMAIN_ALIAS_RID_NETWORK_CONFIGURATION_OPS;

    Sids[0] = SeExports->SeAliasAdminsSid;
    Sids[1] = OperatorsSid;
    Status = QwaveCreateSd(Sids, 2, &QwaveMarkingSd);
    if (NT_SUCCESS(Status))
        Status = QwaveCreateSd(Sids, 1, &QwaveAdminSd);
    if (NT_SUCCESS(Status))
    {
        Sids[0] = SeExports->SeLocalServiceSid;
        Sids[1] = SeExports->SeLocalSystemSid;
        Status = QwaveCreateSd(Sids, 2, &QwaveServiceSd);
    }
    return Status;
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
    ASSERT(IsListEmpty(&QwaveWaiters));
    ASSERT(IsListEmpty(&QwaveServiceReads));
    QwaveDropWork();
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

    ExInitializeResourceLite(&QwaveHandlesLock);
    InitializeListHead(&QwaveHandles);
    KeInitializeSpinLock(&QwaveWorkLock);
    InitializeListHead(&QwaveWorkQueue);
    InitializeListHead(&QwaveServiceReads);
    KeInitializeSpinLock(&QwaveWaitLock);
    InitializeListHead(&QwaveWaiters);
    IoCsqInitialize(&QwaveWaitCsq, QwaveWaitInsert, QwaveWaitRemove, QwaveWaitPeekNext, QwaveWaitAcquireLock,
                    QwaveWaitReleaseLock, QwaveWaitCompleteCanceled);

    Status = QwaveCreateSds();
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
