/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     qWAVE flow driver: the QoS flows of each qwave.dll handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <libernt/qwavedrv.h>

#define NDEBUG
#include <debug.h>

#define TAG_QWAVE 'evWQ'

typedef struct _QWAVE_SOCKET_ENTRY
{
    LIST_ENTRY Link;
    ULONG64 Socket;
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
    FAST_MUTEX Lock;
    LIST_ENTRY Flows;
} QWAVE_HANDLE, *PQWAVE_HANDLE;

static PDEVICE_OBJECT QwaveDeviceObject;
static PSECURITY_DESCRIPTOR QwaveMarkingSd;
static LONG QwaveLastFlowId;

static
VOID
QwaveAssertLocked(
    _In_ PQWAVE_HANDLE Handle)
{
    ASSERT(Handle->Lock.Owner == KeGetCurrentThread());
    UNREFERENCED_PARAMETER(Handle);
}

static
PQWAVE_FLOW
QwaveFindFlowLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ ULONG FlowId)
{
    PLIST_ENTRY Entry;

    QwaveAssertLocked(Handle);
    for (Entry = Handle->Flows.Flink; Entry != &Handle->Flows; Entry = Entry->Flink)
    {
        PQWAVE_FLOW Flow = CONTAINING_RECORD(Entry, QWAVE_FLOW, Link);

        ASSERT(Flow->FlowId != 0);
        ASSERT(Flow->SocketCount != 0);
        if (Flow->FlowId == FlowId)
            return Flow;
    }
    return NULL;
}

static
VOID
QwaveDestroyFlowLocked(
    _In_ PQWAVE_HANDLE Handle,
    _In_ PQWAVE_FLOW Flow)
{
    QwaveAssertLocked(Handle);
    RemoveEntryList(&Flow->Link);
    while (!IsListEmpty(&Flow->Sockets))
    {
        PQWAVE_SOCKET_ENTRY Socket = CONTAINING_RECORD(RemoveHeadList(&Flow->Sockets), QWAVE_SOCKET_ENTRY, Link);

        ASSERT(Flow->SocketCount != 0);
        Flow->SocketCount--;
        ExFreePoolWithTag(Socket, TAG_QWAVE);
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

    ExAcquireFastMutex(&Handle->Lock);
    while (!IsListEmpty(&Handle->Flows))
        QwaveDestroyFlowLocked(Handle, CONTAINING_RECORD(Handle->Flows.Flink, QWAVE_FLOW, Link));
    ExReleaseFastMutex(&Handle->Lock);
}

static
ULONG
QwaveNewFlowIdLocked(
    _In_ PQWAVE_HANDLE Handle)
{
    ULONG FlowId;

    QwaveAssertLocked(Handle);
    do
    {
        FlowId = (ULONG)InterlockedIncrement(&QwaveLastFlowId);
    } while (FlowId == 0 || QwaveFindFlowLocked(Handle, FlowId) != NULL);
    return FlowId;
}

static
BOOLEAN
QwaveCallerMayMark(
    _In_ PIRP Irp)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    ACCESS_MASK Granted;
    NTSTATUS Status;
    BOOLEAN Allowed;

    PAGED_CODE();
    ASSERT(QwaveMarkingSd != NULL);

    if (Irp->RequestorMode == KernelMode)
        return TRUE;

    SeCaptureSubjectContext(&Subject);
    SeLockSubjectContext(&Subject);
    Allowed = SeAccessCheck(QwaveMarkingSd, &Subject, TRUE, FILE_WRITE_DATA, 0, NULL, IoGetFileObjectGenericMapping(),
                            UserMode, &Granted, &Status);
    SeUnlockSubjectContext(&Subject);
    SeReleaseSubjectContext(&Subject);
    return Allowed;
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
    ULONG FlowId;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Input) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Input, Irp->AssociatedIrp.SystemBuffer, sizeof(Input));
    if ((Input.Flags & ~QWAVE_NON_ADAPTIVE_FLOW) != 0 || Input.TrafficType > QWAVE_TRAFFIC_TYPE_LAST ||
        Input.Socket == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(Input.Flags & QWAVE_NON_ADAPTIVE_FLOW))
    {
        /* TODO: adaptive flows need the network experiments (LLTD probing) of the qWAVE service */
        return STATUS_NOT_SUPPORTED;
    }

    Socket = ExAllocatePoolZero(PagedPool, sizeof(*Socket), TAG_QWAVE);
    if (!Socket)
        return STATUS_INSUFFICIENT_RESOURCES;
    Socket->Socket = Input.Socket;
    if (Input.FlowId == 0)
    {
        NewFlow = ExAllocatePoolZero(PagedPool, sizeof(*NewFlow), TAG_QWAVE);
        if (!NewFlow)
        {
            ExFreePoolWithTag(Socket, TAG_QWAVE);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        InitializeListHead(&NewFlow->Sockets);
        NewFlow->TrafficType = Input.TrafficType;
    }

    ExAcquireFastMutex(&Handle->Lock);
    if (NewFlow)
    {
        NewFlow->FlowId = QwaveNewFlowIdLocked(Handle);
        InsertTailList(&Handle->Flows, &NewFlow->Link);
        Flow = NewFlow;
    }
    else
    {
        Flow = QwaveFindFlowLocked(Handle, Input.FlowId);
        if (!Flow)
        {
            ExReleaseFastMutex(&Handle->Lock);
            ExFreePoolWithTag(Socket, TAG_QWAVE);
            return STATUS_INVALID_PARAMETER;
        }
    }
    InsertTailList(&Flow->Sockets, &Socket->Link);
    Flow->SocketCount++;
    ASSERT(Flow->SocketCount != 0);
    FlowId = Flow->FlowId;
    ExReleaseFastMutex(&Handle->Lock);

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

    ExAcquireFastMutex(&Handle->Lock);
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
            ExFreePoolWithTag(Socket, TAG_QWAVE);
            ASSERT(Flow->SocketCount != 0);
            Flow->SocketCount--;
            ASSERT((Flow->SocketCount == 0) == IsListEmpty(&Flow->Sockets));
            if (Flow->SocketCount == 0)
                QwaveDestroyFlowLocked(Handle, Flow);
            Status = STATUS_SUCCESS;
            break;
        }
    }
    ExReleaseFastMutex(&Handle->Lock);
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
            if (Input->Operation == QWAVE_SET_OUTGOING_DSCP && !QwaveCallerMayMark(Irp))
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

    ExAcquireFastMutex(&Handle->Lock);
    Flow = QwaveFindFlowLocked(Handle, Input->FlowId);
    if (!Flow)
    {
        ExReleaseFastMutex(&Handle->Lock);
        return STATUS_NOT_FOUND;
    }
    /* FIXME: no packet scheduler marks or shapes the flow's traffic; the settings are only recorded */
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
        Flow->Rate = Rate;
        Flow->RateSet = TRUE;
    }
    ExReleaseFastMutex(&Handle->Lock);
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
            ExInitializeFastMutex(&Handle->Lock);
            InitializeListHead(&Handle->Flows);
            FileObject->FsContext = Handle;
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLEANUP:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            QwaveDestroyAllFlows(Handle);
            return QwaveCompleteIrp(Irp, STATUS_SUCCESS);

        case IRP_MJ_CLOSE:
            Handle = FileObject->FsContext;
            ASSERT(Handle != NULL);
            QwaveDestroyAllFlows(Handle);
            ASSERT(IsListEmpty(&Handle->Flows));
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
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    return QwaveCompleteIrp(Irp, Status);
}

static
NTSTATUS
QwaveCreateMarkingSd(VOID)
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
    if (NT_SUCCESS(Status))
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
    QwaveMarkingSd = Sd;
    return STATUS_SUCCESS;
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

    IoDeleteDevice(QwaveDeviceObject);
    QwaveDeviceObject = NULL;
    ExFreePoolWithTag(QwaveMarkingSd, TAG_QWAVE);
    QwaveMarkingSd = NULL;
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

    Status = QwaveCreateMarkingSd();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &QwaveDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("QWAVE: IoCreateDevice failed 0x%08lx\n", Status);
        ExFreePoolWithTag(QwaveMarkingSd, TAG_QWAVE);
        QwaveMarkingSd = NULL;
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
