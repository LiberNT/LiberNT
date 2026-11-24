/*
 * PROJECT:     LiberNT LLTD
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LLTD frame I/O for QoS controllers ([MS-LLTD] 3.3)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ndis.h>
#include <wdmsec.h>
#include <libernt/lltd.h>
#include <libernt/drivers/lltdio.h>

#define NDEBUG
#include <debug.h>

#define TAG_LLTDIO 'oiTL'
#define LLTDIO_MAX_QUEUED 256
#define LLTDIO_ETHERNET_HEADER 14

typedef struct _LLTDIO_ADAPTER
{
    LIST_ENTRY Link;
    NDIS_HANDLE BindingHandle;
    NDIS_HANDLE NblPool;
    UCHAR Mac[6];
    ULONG64 LinkSpeed;
    KSPIN_LOCK Lock;
    LIST_ENTRY Handles;
    BOOLEAN Paused;
    BOOLEAN Unbinding;
    LONG PendingSends;
    KEVENT SendsDone;
    KEVENT AsyncEvent;
    NDIS_STATUS AsyncStatus;
    KEVENT RequestDone;
    NDIS_STATUS RequestStatus;
} LLTDIO_ADAPTER, *PLLTDIO_ADAPTER;

typedef struct _LLTDIO_FRAME
{
    LIST_ENTRY Link;
    ULONG64 ReceiveTimestamp;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} LLTDIO_FRAME, *PLLTDIO_FRAME;

typedef struct _LLTDIO_HANDLE
{
    LIST_ENTRY Link;
    PLLTDIO_ADAPTER Adapter;
    KSPIN_LOCK Lock;
    LIST_ENTRY Frames;
    ULONG FrameCount;
    IO_CSQ Csq;
    LIST_ENTRY PendingReads;
} LLTDIO_HANDLE, *PLLTDIO_HANDLE;

typedef struct _LLTDIO_SEND
{
    PLLTDIO_ADAPTER Adapter;
    PMDL Mdl;
    PKEVENT Done;
    PLONG Remaining;
    UCHAR Frame[ANYSIZE_ARRAY];
} LLTDIO_SEND, *PLLTDIO_SEND;

/* SYSTEM, Administrators and LocalService, the account of the qWAVE service */
static const UNICODE_STRING LltdioSddl =
    RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)");
/* {6E1E6D0B-3B5F-4B8E-9C45-2D6A1E8B7F31} */
static const GUID LltdioClassGuid = { 0x6e1e6d0b, 0x3b5f, 0x4b8e, { 0x9c, 0x45, 0x2d, 0x6a, 0x1e, 0x8b, 0x7f, 0x31 } };

static NDIS_HANDLE LltdioProtocolHandle;
static PDEVICE_OBJECT LltdioDeviceObject;
static LIST_ENTRY LltdioAdapters;
static KSPIN_LOCK LltdioAdaptersLock;

static
VOID
NTAPI
LltdioCsqInsert(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PLLTDIO_HANDLE Handle = CONTAINING_RECORD(Csq, LLTDIO_HANDLE, Csq);

    InsertTailList(&Handle->PendingReads, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
LltdioCsqRemove(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
LltdioCsqPeekNext(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PLLTDIO_HANDLE Handle = CONTAINING_RECORD(Csq, LLTDIO_HANDLE, Csq);
    PLIST_ENTRY Next = Irp ? Irp->Tail.Overlay.ListEntry.Flink : Handle->PendingReads.Flink;

    UNREFERENCED_PARAMETER(PeekContext);
    if (Next == &Handle->PendingReads)
        return NULL;
    return CONTAINING_RECORD(Next, IRP, Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
LltdioCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PLLTDIO_HANDLE Handle = CONTAINING_RECORD(Csq, LLTDIO_HANDLE, Csq);

    KeAcquireSpinLock(&Handle->Lock, Irql);
}

static
VOID
NTAPI
LltdioCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PLLTDIO_HANDLE Handle = CONTAINING_RECORD(Csq, LLTDIO_HANDLE, Csq);

    KeReleaseSpinLock(&Handle->Lock, Irql);
}

static
VOID
NTAPI
LltdioCsqCompleteCanceled(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/* Copies Frame into Irp's buffer; returns FALSE when the buffer is too small */
static
BOOLEAN
LltdioFillRead(
    _In_ PIRP Irp,
    _In_ const LLTDIO_FRAME *Frame)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Length = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PLLTDIO_RECEIVED Received = Irp->AssociatedIrp.SystemBuffer;

    if (Length < sizeof(*Received) + Frame->Length)
    {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
        return FALSE;
    }
    Received->ReceiveTimestamp = Frame->ReceiveTimestamp;
    Received->Length = Frame->Length;
    Received->Reserved = 0;
    RtlCopyMemory(Received + 1, Frame->Data, Frame->Length);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*Received) + Frame->Length;
    return TRUE;
}

static
VOID
LltdioDeliver(
    _In_ PLLTDIO_HANDLE Handle,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG64 Received)
{
    PLLTDIO_FRAME Frame;
    PIRP Irp;
    KIRQL OldIrql;

    Frame = ExAllocatePoolZero(NonPagedPoolNx, FIELD_OFFSET(LLTDIO_FRAME, Data) + Length, TAG_LLTDIO);
    if (!Frame)
        return;
    Frame->ReceiveTimestamp = Received;
    Frame->Length = Length;
    RtlCopyMemory(Frame->Data, Data, Length);

    Irp = IoCsqRemoveNextIrp(&Handle->Csq, NULL);
    if (Irp)
    {
        (VOID)LltdioFillRead(Irp, Frame);
        ExFreePoolWithTag(Frame, TAG_LLTDIO);
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
        return;
    }

    KeAcquireSpinLock(&Handle->Lock, &OldIrql);
    if (Handle->FrameCount < LLTDIO_MAX_QUEUED)
    {
        InsertTailList(&Handle->Frames, &Frame->Link);
        Handle->FrameCount++;
        Frame = NULL;
    }
    KeReleaseSpinLock(&Handle->Lock, OldIrql);
    if (Frame)
        ExFreePoolWithTag(Frame, TAG_LLTDIO);
}

static
VOID
NTAPI
LltdioReceive(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;
    ULONG64 Received = (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;
    UCHAR Storage[LLTD_MAX_FRAME];
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    for (Nbl = NetBufferLists; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb; Nb = NET_BUFFER_NEXT_NB(Nb))
        {
            ULONG Length = min(NET_BUFFER_DATA_LENGTH(Nb), LLTD_MAX_FRAME);
            const LLTD_QOS_HEADER *Header;
            PLIST_ENTRY Entry;

            if (Length < sizeof(LLTD_QOS_HEADER))
                continue;
            Header = NdisGetDataBuffer(Nb, Length, Storage, 1, 0);
            if (!Header || Header->EtherType != RtlUshortByteSwap(LLTD_ETHERTYPE) || Header->Version != LLTD_VERSION ||
                Header->TypeOfService != LLTD_TOS_QOS_DIAGNOSTICS || !RtlEqualMemory(Header->Destination, Adapter->Mac, 6))
            {
                continue;
            }

            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            for (Entry = Adapter->Handles.Flink; Entry != &Adapter->Handles; Entry = Entry->Flink)
                LltdioDeliver(CONTAINING_RECORD(Entry, LLTDIO_HANDLE, Link), (const UCHAR *)Header, Length, Received);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        }
    }

    if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
    {
        NdisReturnNetBufferLists(Adapter->BindingHandle, NetBufferLists,
                                 NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags) ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
}

static
VOID
NTAPI
LltdioSendComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;
    PNET_BUFFER_LIST Nbl, Next;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(SendCompleteFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        PLLTDIO_SEND Send = Nbl->ProtocolReserved[0];
        LONG Remaining;

        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        ASSERT(Send && Send->Adapter == Adapter);
        NdisFreeNetBufferList(Nbl);
        NdisFreeMdl(Send->Mdl);
        Remaining = InterlockedDecrement(Send->Remaining);
        ASSERT(Remaining >= 0);
        if (Remaining == 0)
            KeSetEvent(Send->Done, IO_NO_INCREMENT, FALSE);
        ExFreePoolWithTag(Send, TAG_LLTDIO);

        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        ASSERT(Adapter->PendingSends > 0);
        if (--Adapter->PendingSends == 0)
            KeSetEvent(&Adapter->SendsDone, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }
}

static
NTSTATUS
LltdioSend(
    _In_ PLLTDIO_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PLLTDIO_SEND_INPUT Input = Irp->AssociatedIrp.SystemBuffer;
    PNET_BUFFER_LIST Chain = NULL, *Tail = &Chain;
    PLLTDIO_ADAPTER Adapter;
    LONG Remaining;
    KEVENT Done;
    KIRQL OldIrql;
    ULONG Index, FrameLength;

    PAGED_CODE();

    if (InputLength < sizeof(*Input))
        return STATUS_INVALID_PARAMETER;
    FrameLength = Input->FrameLength;
    if (Input->Count == 0 || Input->Count > LLTDIO_MAX_TRAIN || FrameLength < 4 ||
        FrameLength > LLTD_MAX_FRAME - LLTDIO_ETHERNET_HEADER ||
        InputLength != sizeof(*Input) + Input->Count * FrameLength ||
        (Input->TimestampOffset != LLTDIO_NO_TIMESTAMP &&
         (Input->TimestampOffset > FrameLength || FrameLength - Input->TimestampOffset < sizeof(ULONG64))))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&LltdioAdaptersLock, &OldIrql);
    Adapter = Handle->Adapter;
    if (Adapter)
    {
        PLLTDIO_ADAPTER Bound = Adapter;

        KeAcquireSpinLockAtDpcLevel(&Bound->Lock);
        if (Bound->Paused || Bound->Unbinding)
        {
            Adapter = NULL;
        }
        else
        {
            Bound->PendingSends += Input->Count;
            KeClearEvent(&Bound->SendsDone);
        }
        KeReleaseSpinLockFromDpcLevel(&Bound->Lock);
    }
    KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
    if (!Adapter)
        return STATUS_DEVICE_NOT_READY;

    KeInitializeEvent(&Done, NotificationEvent, FALSE);
    Remaining = (LONG)Input->Count;
    for (Index = 0; Index < Input->Count; Index++)
    {
        ULONG Length = max(LLTDIO_ETHERNET_HEADER + FrameLength, LLTD_MIN_FRAME);
        PLLTDIO_SEND Send = ExAllocatePoolZero(NonPagedPoolNx, FIELD_OFFSET(LLTDIO_SEND, Frame) + Length, TAG_LLTDIO);
        PNET_BUFFER_LIST Nbl = NULL;

        if (Send)
        {
            Send->Adapter = Adapter;
            Send->Done = &Done;
            Send->Remaining = &Remaining;
            RtlCopyMemory(Send->Frame, Input->Destination, 6);
            RtlCopyMemory(Send->Frame + 6, Adapter->Mac, 6);
            *(PUSHORT)(Send->Frame + 12) = RtlUshortByteSwap(LLTD_ETHERTYPE);
            RtlCopyMemory(Send->Frame + LLTDIO_ETHERNET_HEADER, (PUCHAR)(Input + 1) + Index * FrameLength, FrameLength);
            Send->Mdl = NdisAllocateMdl(Adapter->BindingHandle, Send->Frame, Length);
            if (Send->Mdl)
                Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->NblPool, 0, 0, Send->Mdl, 0, Length);
        }
        if (!Nbl)
        {
            if (Send && Send->Mdl)
                NdisFreeMdl(Send->Mdl);
            if (Send)
                ExFreePoolWithTag(Send, TAG_LLTDIO);
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            Adapter->PendingSends -= (LONG)(Input->Count - Index);
            ASSERT(Adapter->PendingSends >= 0);
            if (Adapter->PendingSends == 0)
                KeSetEvent(&Adapter->SendsDone, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            Remaining -= (LONG)(Input->Count - Index);
            ASSERT(Remaining >= 0);
            break;
        }
        Nbl->SourceHandle = Adapter->BindingHandle;
        Nbl->ProtocolReserved[0] = Send;
        *Tail = Nbl;
        Tail = &NET_BUFFER_LIST_NEXT_NBL(Nbl);
    }
    if (!Chain)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Input->TimestampOffset != LLTDIO_NO_TIMESTAMP)
    {
        PNET_BUFFER_LIST Nbl;

        for (Nbl = Chain; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        {
            PLLTDIO_SEND Send = Nbl->ProtocolReserved[0];
            ULONG64 Stamp = RtlUlonglongByteSwap((ULONG64)KeQueryPerformanceCounter(NULL).QuadPart);

            RtlCopyMemory(Send->Frame + LLTDIO_ETHERNET_HEADER + Input->TimestampOffset, &Stamp, sizeof(Stamp));
        }
    }
    NdisSendNetBufferLists(Adapter->BindingHandle, Chain, NDIS_DEFAULT_PORT_NUMBER, 0);
    KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
    return (Remaining == 0 && Index == Input->Count) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
LltdioBind(
    _In_ PLLTDIO_HANDLE Handle,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLLTDIO_ADAPTER Adapter = NULL;
    LLTDIO_ADAPTER_INFO Info;
    LARGE_INTEGER Frequency;
    UCHAR Mac[6];
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Mac) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Info))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(Mac, Irp->AssociatedIrp.SystemBuffer, sizeof(Mac));

    KeAcquireSpinLock(&LltdioAdaptersLock, &OldIrql);
    if (Handle->Adapter)
    {
        KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
        return STATUS_ADDRESS_ALREADY_ASSOCIATED;
    }
    for (Entry = LltdioAdapters.Flink; Entry != &LltdioAdapters; Entry = Entry->Flink)
    {
        PLLTDIO_ADAPTER Candidate = CONTAINING_RECORD(Entry, LLTDIO_ADAPTER, Link);

        if (RtlEqualMemory(Candidate->Mac, Mac, sizeof(Mac)) && !Candidate->Unbinding)
        {
            Adapter = Candidate;
            break;
        }
    }
    if (Adapter)
    {
        KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
        InsertTailList(&Adapter->Handles, &Handle->Link);
        Handle->Adapter = Adapter;
        RtlCopyMemory(Info.MacAddress, Adapter->Mac, sizeof(Info.MacAddress));
        Info.LinkSpeed = Adapter->LinkSpeed;
        KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
    }
    KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
    if (!Adapter)
        return STATUS_NOT_FOUND;

    KeQueryPerformanceCounter(&Frequency);
    Info.Reserved = 0;
    Info.PerformanceCounterFrequency = (ULONG64)Frequency.QuadPart;
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Info, sizeof(Info));
    Irp->IoStatus.Information = sizeof(Info);
    return STATUS_SUCCESS;
}

static
NTSTATUS
LltdioReceiveRequest(
    _In_ PLLTDIO_HANDLE Handle,
    _In_ PIRP Irp)
{
    PLLTDIO_FRAME Frame = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Handle->Lock, &OldIrql);
    if (!IsListEmpty(&Handle->Frames))
    {
        Frame = CONTAINING_RECORD(RemoveHeadList(&Handle->Frames), LLTDIO_FRAME, Link);
        ASSERT(Handle->FrameCount != 0);
        Handle->FrameCount--;
    }
    KeReleaseSpinLock(&Handle->Lock, OldIrql);

    if (Frame)
    {
        NTSTATUS Status;

        (VOID)LltdioFillRead(Irp, Frame);
        ExFreePoolWithTag(Frame, TAG_LLTDIO);
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    if (IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.OutputBufferLength < sizeof(LLTDIO_RECEIVED))
    {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_BUFFER_TOO_SMALL;
    }
    IoCsqInsertIrp(&Handle->Csq, Irp, NULL);
    return STATUS_PENDING;
}

static
VOID
LltdioDetachHandle(
    _In_ PLLTDIO_HANDLE Handle)
{
    PLLTDIO_ADAPTER Adapter;
    KIRQL OldIrql;

    KeAcquireSpinLock(&LltdioAdaptersLock, &OldIrql);
    Adapter = Handle->Adapter;
    if (Adapter)
    {
        KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
        RemoveEntryList(&Handle->Link);
        Handle->Adapter = NULL;
        KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
    }
    KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
}

static
VOID
LltdioFlushHandle(
    _In_ PLLTDIO_HANDLE Handle,
    _In_ NTSTATUS Status)
{
    PIRP Irp;
    KIRQL OldIrql;

    while ((Irp = IoCsqRemoveNextIrp(&Handle->Csq, NULL)) != NULL)
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    KeAcquireSpinLock(&Handle->Lock, &OldIrql);
    while (!IsListEmpty(&Handle->Frames))
    {
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Handle->Frames), LLTDIO_FRAME, Link), TAG_LLTDIO);
        ASSERT(Handle->FrameCount != 0);
        Handle->FrameCount--;
    }
    ASSERT(Handle->FrameCount == 0);
    KeReleaseSpinLock(&Handle->Lock, OldIrql);
}

static DRIVER_DISPATCH LltdioDispatch;

static
NTSTATUS
NTAPI
LltdioDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PLLTDIO_HANDLE Handle = Stack->FileObject->FsContext;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            if (Stack->FileObject->FileName.Length != 0)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Handle = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Handle), TAG_LLTDIO);
            if (!Handle)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            KeInitializeSpinLock(&Handle->Lock);
            InitializeListHead(&Handle->Frames);
            InitializeListHead(&Handle->PendingReads);
            IoCsqInitialize(&Handle->Csq, LltdioCsqInsert, LltdioCsqRemove, LltdioCsqPeekNext, LltdioCsqAcquireLock,
                            LltdioCsqReleaseLock, LltdioCsqCompleteCanceled);
            Stack->FileObject->FsContext = Handle;
            break;

        case IRP_MJ_CLEANUP:
            ASSERT(Handle != NULL);
            LltdioDetachHandle(Handle);
            LltdioFlushHandle(Handle, STATUS_CANCELLED);
            break;

        case IRP_MJ_CLOSE:
            ASSERT(Handle != NULL);
            ASSERT(Handle->Adapter == NULL);
            ASSERT(IsListEmpty(&Handle->PendingReads));
            Stack->FileObject->FsContext = NULL;
            ExFreePoolWithTag(Handle, TAG_LLTDIO);
            break;

        case IRP_MJ_DEVICE_CONTROL:
            ASSERT(Handle != NULL);
            switch (Stack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_LLTDIO_BIND:
                    Status = LltdioBind(Handle, Irp, Stack);
                    break;
                case IOCTL_LLTDIO_SEND:
                    Status = LltdioSend(Handle, Irp, Stack);
                    break;
                case IOCTL_LLTDIO_RECEIVE:
                    return LltdioReceiveRequest(Handle, Irp);
                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    Irp->IoStatus.Status = Status;
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
LltdioFreeAdapter(
    _In_ PLLTDIO_ADAPTER Adapter)
{
    if (Adapter->NblPool)
        NdisFreeNetBufferListPool(Adapter->NblPool);
    ExFreePoolWithTag(Adapter, TAG_LLTDIO);
}

static
NDIS_STATUS
LltdioSetPacketFilter(
    _In_ PLLTDIO_ADAPTER Adapter)
{
    NDIS_OID_REQUEST Request;
    ULONG Filter = NDIS_PACKET_TYPE_DIRECTED;
    NDIS_STATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Request.RequestType = NdisRequestSetInformation;
    Request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    Request.DATA.SET_INFORMATION.InformationBuffer = &Filter;
    Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(Filter);
    KeClearEvent(&Adapter->RequestDone);
    Status = NdisOidRequest(Adapter->BindingHandle, &Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->RequestDone, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->RequestStatus;
    }
    return Status;
}

static
VOID
NTAPI
LltdioOidRequestComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;

    UNREFERENCED_PARAMETER(OidRequest);
    Adapter->RequestStatus = Status;
    KeSetEvent(&Adapter->RequestDone, IO_NO_INCREMENT, FALSE);
}

static
NDIS_STATUS
NTAPI
LltdioBindAdapter(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ NDIS_HANDLE BindContext,
    _In_ PNDIS_BIND_PARAMETERS BindParameters)
{
    NDIS_MEDIUM Media[] = { NdisMedium802_3 };
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    NDIS_OPEN_PARAMETERS OpenParameters;
    PLLTDIO_ADAPTER Adapter;
    NDIS_STATUS Status;
    UINT Medium = 0;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ProtocolDriverContext);
    PAGED_CODE();

    if (BindParameters->MacAddressLength != 6)
        return NDIS_STATUS_NOT_SUPPORTED;

    Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Adapter), TAG_LLTDIO);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;
    KeInitializeSpinLock(&Adapter->Lock);
    InitializeListHead(&Adapter->Handles);
    KeInitializeEvent(&Adapter->SendsDone, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->AsyncEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->RequestDone, NotificationEvent, FALSE);
    RtlCopyMemory(Adapter->Mac, BindParameters->CurrentMacAddress, 6);
    Adapter->LinkSpeed = BindParameters->XmitLinkSpeed;
    Adapter->Paused = TRUE;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = TAG_LLTDIO;
    Adapter->NblPool = NdisAllocateNetBufferListPool(LltdioProtocolHandle, &PoolParameters);
    if (!Adapter->NblPool)
    {
        LltdioFreeAdapter(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(&OpenParameters, sizeof(OpenParameters));
    OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.AdapterName = BindParameters->AdapterName;
    OpenParameters.MediumArray = Media;
    OpenParameters.MediumArraySize = ARRAYSIZE(Media);
    OpenParameters.SelectedMediumIndex = &Medium;
    Status = NdisOpenAdapterEx(LltdioProtocolHandle, Adapter, &OpenParameters, BindContext, &Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->AsyncEvent, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->AsyncStatus;
    }
    if (Status != NDIS_STATUS_SUCCESS)
    {
        LltdioFreeAdapter(Adapter);
        return Status;
    }
    Status = LltdioSetPacketFilter(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("LLTDIO: setting the packet filter failed 0x%08lx\n", Status);

    KeAcquireSpinLock(&LltdioAdaptersLock, &OldIrql);
    InsertTailList(&LltdioAdapters, &Adapter->Link);
    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Adapter->Paused = FALSE;
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
    KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
LltdioOpenAdapterComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ NDIS_STATUS Status)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;

    Adapter->AsyncStatus = Status;
    KeSetEvent(&Adapter->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
LltdioCloseAdapterComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;

    Adapter->AsyncStatus = NDIS_STATUS_SUCCESS;
    KeSetEvent(&Adapter->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

static
NDIS_STATUS
NTAPI
LltdioUnbindAdapter(
    _In_ NDIS_HANDLE UnbindContext,
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(UnbindContext);
    PAGED_CODE();

    /* Cleanup detaches a handle under the same locks, so a handle met here stays allocated while it is flushed */
    KeAcquireSpinLock(&LltdioAdaptersLock, &OldIrql);
    RemoveEntryList(&Adapter->Link);
    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Adapter->Unbinding = TRUE;
    while (!IsListEmpty(&Adapter->Handles))
    {
        PLLTDIO_HANDLE Handle = CONTAINING_RECORD(RemoveHeadList(&Adapter->Handles), LLTDIO_HANDLE, Link);

        ASSERT(Handle->Adapter == Adapter);
        Handle->Adapter = NULL;
        InitializeListHead(&Handle->Link);
        LltdioFlushHandle(Handle, STATUS_DEVICE_REMOVED);
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
    KeReleaseSpinLock(&LltdioAdaptersLock, OldIrql);
    KeWaitForSingleObject(&Adapter->SendsDone, Executive, KernelMode, FALSE, NULL);

    Status = NdisCloseAdapterEx(Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->AsyncEvent, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->AsyncStatus;
    }
    LltdioFreeAdapter(Adapter);
    return Status;
}

static
NDIS_STATUS
NTAPI
LltdioNetPnPEvent(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;
    KIRQL OldIrql;

    if (!Adapter)
        return NDIS_STATUS_SUCCESS;
    switch (NetPnPEventNotification->NetPnPEvent.NetEvent)
    {
        case NetEventPause:
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            Adapter->Paused = TRUE;
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            KeWaitForSingleObject(&Adapter->SendsDone, Executive, KernelMode, FALSE, NULL);
            return NDIS_STATUS_SUCCESS;
        case NetEventRestart:
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            Adapter->Paused = FALSE;
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_SUCCESS;
        default:
            return NDIS_STATUS_SUCCESS;
    }
}

static
VOID
NTAPI
LltdioStatus(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PLLTDIO_ADAPTER Adapter = ProtocolBindingContext;

    if (StatusIndication->StatusCode == NDIS_STATUS_LINK_STATE &&
        StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
    {
        Adapter->LinkSpeed = ((PNDIS_LINK_STATE)StatusIndication->StatusBuffer)->XmitLinkSpeed;
    }
}

static DRIVER_UNLOAD LltdioUnload;

static
VOID
NTAPI
LltdioUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PAGED_CODE();

    NdisDeregisterProtocolDriver(LltdioProtocolHandle);
    ASSERT(IsListEmpty(&LltdioAdapters));
    IoDeleteDevice(LltdioDeviceObject);
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS Characteristics;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(LLTDIO_DEVICE_NAME);
    NDIS_STRING Name = RTL_CONSTANT_STRING(L"LLTDIO");
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    PAGED_CODE();

    InitializeListHead(&LltdioAdapters);
    KeInitializeSpinLock(&LltdioAdaptersLock);

    Status = IoCreateDeviceSecure(DriverObject, 0, &DeviceName, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
                                  &LltdioSddl, &LltdioClassGuid, &LltdioDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = LltdioDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = LltdioDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = LltdioDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = LltdioDispatch;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 0;
    Characteristics.Name = Name;
    Characteristics.BindAdapterHandlerEx = LltdioBindAdapter;
    Characteristics.UnbindAdapterHandlerEx = LltdioUnbindAdapter;
    Characteristics.OpenAdapterCompleteHandlerEx = LltdioOpenAdapterComplete;
    Characteristics.CloseAdapterCompleteHandlerEx = LltdioCloseAdapterComplete;
    Characteristics.NetPnPEventHandler = LltdioNetPnPEvent;
    Characteristics.OidRequestCompleteHandler = LltdioOidRequestComplete;
    Characteristics.StatusHandlerEx = LltdioStatus;
    Characteristics.ReceiveNetBufferListsHandler = LltdioReceive;
    Characteristics.SendNetBufferListsCompleteHandler = LltdioSendComplete;

    Status = NdisRegisterProtocolDriver(NULL, &Characteristics, &LltdioProtocolHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("LLTDIO: NdisRegisterProtocolDriver failed 0x%08lx\n", Status);
        IoDeleteDevice(LltdioDeviceObject);
        return Status;
    }
    DriverObject->DriverUnload = LltdioUnload;
    return STATUS_SUCCESS;
}
