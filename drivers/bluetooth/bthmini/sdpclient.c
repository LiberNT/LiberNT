/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SDP client connections and searches requested through IOCTL_BTH_SDP_*
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define SDP_CLIENT_DEFAULT_TIMEOUT_S    30
#define SDP_CLIENT_MAX_RESPONSE         0x10000
#define SDP_CLIENT_MAX_ATTRIBUTE_BYTES  (L2CAP_LOCAL_MTU - 24)

typedef struct _BTHMINI_SDP_WAITER
{
    LIST_ENTRY Link;
    WDFREQUEST Request;
} BTHMINI_SDP_WAITER, *PBTHMINI_SDP_WAITER;

typedef struct _BTHMINI_SDP_CLOSED_FILE
{
    LIST_ENTRY Link;
    WDFFILEOBJECT FileObject;
} BTHMINI_SDP_CLOSED_FILE, *PBTHMINI_SDP_CLOSED_FILE;

typedef struct _BTHMINI_SDP_CONNECTION
{
    LIST_ENTRY Link;
    HANDLE_SDP Handle;
    WDFFILEOBJECT FileObject;
    PBTHMINI_CHANNEL Channel;
    BOOLEAN Open;
    BOOLEAN Detached;
    ULONG TimeoutMs;
    WDFREQUEST Connect;
    WDFREQUEST Request;
    ULONG IoControlCode;
    LIST_ENTRY Waiting;
    ULONGLONG Deadline;
    USHORT Transaction;
    UCHAR Pdu;
    PUCHAR Parameters;
    ULONG ParameterLength;
    PUCHAR Response;
    ULONG ResponseLength;
    ULONG ResponseCapacity;
    ULONG MaxHandles;
} BTHMINI_SDP_CONNECTION, *PBTHMINI_SDP_CONNECTION;

static BTHMINI_CHANNEL_EVENT BthSdpClientOpened;
static BTHMINI_CHANNEL_RECEIVE BthSdpClientReceive;
static BTHMINI_CHANNEL_EVENT BthSdpClientClosed;

static const BTHMINI_CHANNEL_CALLBACKS BthSdpClientConnectionCallbacks =
{
    BthSdpClientOpened,
    BthSdpClientReceive,
    BthSdpClientClosed
};

static
ULONGLONG
BthSdpClientNow(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
PBTHMINI_SDP_CONNECTION
BthSdpClientFind(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ HANDLE_SDP Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->SdpConnections.Flink; Entry != &Dev->SdpConnections; Entry = Entry->Flink)
    {
        PBTHMINI_SDP_CONNECTION Connection = CONTAINING_RECORD(Entry, BTHMINI_SDP_CONNECTION, Link);

        if (Connection->Handle == Handle)
            return Connection;
    }
    return NULL;
}

static
VOID
BthSdpClientResetRequest(
    _Inout_ PBTHMINI_SDP_CONNECTION Connection)
{
    if (Connection->Parameters)
        ExFreePoolWithTag(Connection->Parameters, BTHMINI_TAG);
    if (Connection->Response)
        ExFreePoolWithTag(Connection->Response, BTHMINI_TAG);
    Connection->Parameters = NULL;
    Connection->ParameterLength = 0;
    Connection->Response = NULL;
    Connection->ResponseLength = 0;
    Connection->ResponseCapacity = 0;
    Connection->Request = NULL;
}

static
VOID
BthSdpClientFailAll(
    _Inout_ PBTHMINI_SDP_CONNECTION Connection,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request;

    if (Connection->Connect)
    {
        Request = Connection->Connect;
        Connection->Connect = NULL;
        WdfRequestComplete(Request, Status);
    }
    if (Connection->Request)
    {
        Request = Connection->Request;
        BthSdpClientResetRequest(Connection);
        WdfRequestComplete(Request, Status);
    }
    while (!IsListEmpty(&Connection->Waiting))
    {
        PBTHMINI_SDP_WAITER Waiter = CONTAINING_RECORD(RemoveHeadList(&Connection->Waiting), BTHMINI_SDP_WAITER, Link);

        WdfRequestComplete(Waiter->Request, Status);
        ExFreePoolWithTag(Waiter, BTHMINI_TAG);
    }
}

static
VOID
BthSdpClientFree(
    _In_ PBTHMINI_SDP_CONNECTION Connection)
{
    BthSdpClientFailAll(Connection, STATUS_DEVICE_NOT_CONNECTED);
    ExFreePoolWithTag(Connection, BTHMINI_TAG);
}

static
VOID
BthSdpClientDetach(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_SDP_CONNECTION Connection)
{
    PBTHMINI_CHANNEL Channel = Connection->Channel;

    RemoveEntryList(&Connection->Link);
    Connection->Detached = TRUE;
    BthSdpClientFailAll(Connection, STATUS_DEVICE_NOT_CONNECTED);
    if (!Channel)
    {
        ExFreePoolWithTag(Connection, BTHMINI_TAG);
        return;
    }
    BthL2capDisconnect(Dev, Channel);
}

static
NTSTATUS
BthSdpClientEncodeUuids(
    _In_reads_(MAX_UUIDS_IN_QUERY) const SdpQueryUuid *Uuids,
    _Out_writes_bytes_to_(MAX_UUIDS_IN_QUERY * 17, *Length) PUCHAR Output,
    _Out_ PULONG Length)
{
    ULONG Index, Used = 0;

    for (Index = 0; Index < MAX_UUIDS_IN_QUERY && Uuids[Index].uuidType; Index++)
    {
        const SdpQueryUuid *Uuid = &Uuids[Index];

        switch (Uuid->uuidType)
        {
            case SDP_ST_UUID16:
                Output[Used++] = 0x19;
                Output[Used++] = (UCHAR)(Uuid->u.uuid16 >> 8);
                Output[Used++] = (UCHAR)Uuid->u.uuid16;
                break;

            case SDP_ST_UUID32:
                Output[Used++] = 0x1A;
                Output[Used++] = (UCHAR)(Uuid->u.uuid32 >> 24);
                Output[Used++] = (UCHAR)(Uuid->u.uuid32 >> 16);
                Output[Used++] = (UCHAR)(Uuid->u.uuid32 >> 8);
                Output[Used++] = (UCHAR)Uuid->u.uuid32;
                break;

            case SDP_ST_UUID128:
                Output[Used++] = 0x1C;
                Output[Used++] = (UCHAR)(Uuid->u.uuid128.Data1 >> 24);
                Output[Used++] = (UCHAR)(Uuid->u.uuid128.Data1 >> 16);
                Output[Used++] = (UCHAR)(Uuid->u.uuid128.Data1 >> 8);
                Output[Used++] = (UCHAR)Uuid->u.uuid128.Data1;
                Output[Used++] = (UCHAR)(Uuid->u.uuid128.Data2 >> 8);
                Output[Used++] = (UCHAR)Uuid->u.uuid128.Data2;
                Output[Used++] = (UCHAR)(Uuid->u.uuid128.Data3 >> 8);
                Output[Used++] = (UCHAR)Uuid->u.uuid128.Data3;
                RtlCopyMemory(Output + Used, Uuid->u.uuid128.Data4, sizeof(Uuid->u.uuid128.Data4));
                Used += sizeof(Uuid->u.uuid128.Data4);
                break;

            default:
                return STATUS_INVALID_PARAMETER;
        }
    }
    *Length = Used;
    return Used ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static
NTSTATUS
BthSdpClientEncodeRanges(
    _In_reads_(Count) const SdpAttributeRange *Ranges,
    _In_ ULONG Count,
    _Outptr_result_bytebuffer_(*Length) PUCHAR *Output,
    _Out_ PULONG Length)
{
    PUCHAR Buffer;
    ULONG Index, Used = 0;

    *Output = NULL;
    *Length = 0;
    if (!Count || Count > 0x10000 / 5)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Count; Index++)
    {
        if (Ranges[Index].minAttribute > Ranges[Index].maxAttribute ||
            (Index && Ranges[Index].minAttribute <= Ranges[Index - 1].maxAttribute))
            return STATUS_INVALID_PARAMETER;
    }
    Buffer = ExAllocatePoolWithTag(NonPagedPool, Count * 5, BTHMINI_TAG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < Count; Index++)
    {
        USHORT Minimum = Ranges[Index].minAttribute, Maximum = Ranges[Index].maxAttribute;

        if (Minimum == Maximum)
        {
            Buffer[Used++] = 0x09;
        }
        else
        {
            Buffer[Used++] = 0x0A;
            Buffer[Used++] = (UCHAR)(Minimum >> 8);
            Buffer[Used++] = (UCHAR)Minimum;
        }
        Buffer[Used++] = (UCHAR)(Maximum >> 8);
        Buffer[Used++] = (UCHAR)Maximum;
    }
    *Output = Buffer;
    *Length = Used;
    return STATUS_SUCCESS;
}

static
ULONG
BthSdpClientPutSequence(
    _Out_writes_bytes_opt_(3 + Length) PUCHAR Output,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Header = Length <= 0xFF ? 2 : 3;

    if (!Output)
        return Header + Length;
    if (Header == 2)
    {
        Output[0] = 0x35;
        Output[1] = (UCHAR)Length;
    }
    else
    {
        Output[0] = 0x36;
        Output[1] = (UCHAR)(Length >> 8);
        Output[2] = (UCHAR)Length;
    }
    RtlCopyMemory(Output + Header, Data, Length);
    return Header + Length;
}

static
NTSTATUS
BthSdpClientCompleteStream(
    _In_ WDFREQUEST Request,
    _In_reads_bytes_(Length) const UCHAR *Stream,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PBTH_SDP_STREAM_RESPONSE Output;
    size_t OutputLength;
    ULONG Copy;
    NTSTATUS Status;

    *Information = 0;
    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Output), (PVOID *)&Output, &OutputLength);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_BUFFER_SIZE;
    Copy = (ULONG)min((size_t)Length, OutputLength - FIELD_OFFSET(BTH_SDP_STREAM_RESPONSE, response));
    Output->requiredSize = Length;
    Output->responseSize = Copy;
    RtlCopyMemory(Output->response, Stream, Copy);
    *Information = FIELD_OFFSET(BTH_SDP_STREAM_RESPONSE, response) + Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthSdpClientCompleteHandles(
    _In_ WDFREQUEST Request,
    _In_reads_bytes_(Count * 4) const UCHAR *Handles,
    _In_ ULONG Count,
    _Out_ PULONG_PTR Information)
{
    PULONG Output;
    size_t OutputLength;
    ULONG Index;
    NTSTATUS Status;

    *Information = 0;
    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID *)&Output, &OutputLength);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_BUFFER_SIZE;
    Count = (ULONG)min((size_t)Count, OutputLength / sizeof(ULONG));
    for (Index = 0; Index < Count; Index++)
    {
        const UCHAR *Handle = Handles + Index * 4;

        Output[Index] = ((ULONG)Handle[0] << 24) | ((ULONG)Handle[1] << 16) | ((ULONG)Handle[2] << 8) | Handle[3];
    }
    *Information = Count * sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
VOID
BthSdpClientSend(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHMINI_SDP_CONNECTION Connection,
    _In_reads_bytes_(ContinuationLength) const UCHAR *Continuation,
    _In_ UCHAR ContinuationLength)
{
    ULONG Length = 5 + Connection->ParameterLength + 1 + ContinuationLength;
    PUCHAR Pdu;
    NTSTATUS Status;

    Pdu = ExAllocatePoolWithTag(NonPagedPool, Length, BTHMINI_TAG);
    if (!Pdu)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        Connection->Transaction++;
        Pdu[0] = Connection->Pdu;
        Pdu[1] = (UCHAR)(Connection->Transaction >> 8);
        Pdu[2] = (UCHAR)Connection->Transaction;
        Pdu[3] = (UCHAR)((Length - 5) >> 8);
        Pdu[4] = (UCHAR)(Length - 5);
        RtlCopyMemory(Pdu + 5, Connection->Parameters, Connection->ParameterLength);
        Pdu[5 + Connection->ParameterLength] = ContinuationLength;
        RtlCopyMemory(Pdu + 6 + Connection->ParameterLength, Continuation, ContinuationLength);
        Status = BthL2capSend(Dev, Connection->Channel, Pdu, Length);
        ExFreePoolWithTag(Pdu, BTHMINI_TAG);
    }
    if (!NT_SUCCESS(Status))
    {
        WDFREQUEST Request = Connection->Request;

        BthSdpClientResetRequest(Connection);
        WdfRequestComplete(Request, Status == STATUS_INVALID_BUFFER_SIZE ? STATUS_INVALID_PARAMETER : Status);
    }
}

static
NTSTATUS
BthSdpClientPrepare(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _In_ ULONG IoControlCode,
    _Out_ PHANDLE_SDP Handle,
    _Out_ PUCHAR Pdu,
    _Outptr_result_bytebuffer_(*ParameterLength) PUCHAR *Parameters,
    _Out_ PULONG ParameterLength,
    _Out_ PULONG MaxHandles,
    _Out_ PULONG_PTR Information)
{
    UCHAR Pattern[MAX_UUIDS_IN_QUERY * 17];
    ULONG PatternLength = 0, ListLength = 0, Used, Count;
    PUCHAR List = NULL, Buffer, Local = NULL;
    const ULONG *RecordHandle = NULL;
    size_t InputLength, OutputLength;
    PVOID Input, Output;
    NTSTATUS Status;

    *Parameters = NULL;
    *ParameterLength = 0;
    *MaxHandles = 0;
    *Information = 0;
    switch (IoControlCode)
    {
        case IOCTL_BTH_SDP_SERVICE_SEARCH:
        {
            PBTH_SDP_SERVICE_SEARCH_REQUEST Search;

            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Search), &Input, NULL);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_PARAMETER;
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Output, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_BUFFER_SIZE;
            Search = Input;
            *Handle = Search->hConnection;
            Status = BthSdpClientEncodeUuids(Search->uuids, Pattern, &PatternLength);
            if (!NT_SUCCESS(Status))
                return Status;
            *MaxHandles = (ULONG)min(OutputLength / sizeof(ULONG), 0xFFFF);
            if (*Handle == HANDLE_SDP_LOCAL)
            {
                Count = 0;
                BthSdpLocalSearch(Dev, Pattern, PatternLength, Output, *MaxHandles, &Count);
                *Information = Count * sizeof(ULONG);
                return STATUS_SUCCESS;
            }
            *Pdu = SDP_PDU_SERVICE_SEARCH_REQUEST;
            break;
        }

        case IOCTL_BTH_SDP_ATTRIBUTE_SEARCH:
        {
            PBTH_SDP_ATTRIBUTE_SEARCH_REQUEST Search;

            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Search), &Input, &InputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_PARAMETER;
            Search = Input;
            *Handle = Search->hConnection;
            RecordHandle = &Search->recordHandle;
            Status = BthSdpClientEncodeRanges(Search->range,
                                              (ULONG)((InputLength - FIELD_OFFSET(BTH_SDP_ATTRIBUTE_SEARCH_REQUEST, range)) /
                                                      sizeof(SdpAttributeRange)),
                                              &List, &ListLength);
            if (!NT_SUCCESS(Status))
                return Status;
            *Pdu = SDP_PDU_SERVICE_ATTRIBUTE_REQUEST;
            break;
        }

        case IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH:
        {
            PBTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST Search;

            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Search), &Input, &InputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_PARAMETER;
            Search = Input;
            *Handle = Search->hConnection;
            Status = BthSdpClientEncodeUuids(Search->uuids, Pattern, &PatternLength);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = BthSdpClientEncodeRanges(Search->range,
                                              (ULONG)((InputLength -
                                                       FIELD_OFFSET(BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST, range)) /
                                                      sizeof(SdpAttributeRange)),
                                              &List, &ListLength);
            if (!NT_SUCCESS(Status))
                return Status;
            *Pdu = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
            break;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }

    if (List)
    {
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(BTH_SDP_STREAM_RESPONSE), &Output, NULL);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(List, BTHMINI_TAG);
            return STATUS_INVALID_BUFFER_SIZE;
        }
        if (*Handle == HANDLE_SDP_LOCAL)
        {
            Status = BthSdpLocalAttributes(Dev, RecordHandle, RecordHandle ? NULL : Pattern, PatternLength, List,
                                           ListLength, &Local, &Count);
            ExFreePoolWithTag(List, BTHMINI_TAG);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = BthSdpClientCompleteStream(Request, Local, Count, Information);
            ExFreePoolWithTag(Local, BTHMINI_TAG);
            return Status;
        }
    }

    Used = (RecordHandle ? 4 : BthSdpClientPutSequence(NULL, Pattern, PatternLength)) + 2 +
           (List ? BthSdpClientPutSequence(NULL, List, ListLength) : 0);
    Buffer = ExAllocatePoolWithTag(NonPagedPool, Used, BTHMINI_TAG);
    if (!Buffer)
    {
        if (List)
            ExFreePoolWithTag(List, BTHMINI_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (RecordHandle)
    {
        Buffer[0] = (UCHAR)(*RecordHandle >> 24);
        Buffer[1] = (UCHAR)(*RecordHandle >> 16);
        Buffer[2] = (UCHAR)(*RecordHandle >> 8);
        Buffer[3] = (UCHAR)*RecordHandle;
        Used = 4;
    }
    else
    {
        Used = BthSdpClientPutSequence(Buffer, Pattern, PatternLength);
    }
    Count = List ? SDP_CLIENT_MAX_ATTRIBUTE_BYTES : *MaxHandles;
    Buffer[Used++] = (UCHAR)(Count >> 8);
    Buffer[Used++] = (UCHAR)Count;
    if (List)
    {
        Used += BthSdpClientPutSequence(Buffer + Used, List, ListLength);
        ExFreePoolWithTag(List, BTHMINI_TAG);
    }
    *Parameters = Buffer;
    *ParameterLength = Used;
    return STATUS_PENDING;
}

static
VOID
BthSdpClientStartNext(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHMINI_SDP_CONNECTION Connection);

static
VOID
BthSdpClientStart(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHMINI_SDP_CONNECTION Connection,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS RequestParameters;
    ULONG_PTR Information;
    HANDLE_SDP Handle;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&RequestParameters);
    WdfRequestGetParameters(Request, &RequestParameters);
    Connection->IoControlCode = RequestParameters.Parameters.DeviceIoControl.IoControlCode;
    Status = BthSdpClientPrepare(Dev, Request, Connection->IoControlCode, &Handle, &Connection->Pdu,
                                 &Connection->Parameters, &Connection->ParameterLength, &Connection->MaxHandles,
                                 &Information);
    if (Status != STATUS_PENDING)
    {
        WdfRequestCompleteWithInformation(Request, Status, NT_SUCCESS(Status) ? Information : 0);
        return;
    }
    if (!Connection->Open || !Connection->Channel)
    {
        BthSdpClientResetRequest(Connection);
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }
    if (5 + Connection->ParameterLength + 17 > Connection->Channel->RemoteMtu)
    {
        BthSdpClientResetRequest(Connection);
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    Connection->Request = Request;
    Connection->Deadline = BthSdpClientNow() + Connection->TimeoutMs;
    BthSdpClientSend(Dev, Connection, NULL, 0);
}

static
VOID
BthSdpClientStartNext(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHMINI_SDP_CONNECTION Connection)
{
    while (!Connection->Request && !IsListEmpty(&Connection->Waiting))
    {
        PBTHMINI_SDP_WAITER Waiter = CONTAINING_RECORD(RemoveHeadList(&Connection->Waiting), BTHMINI_SDP_WAITER, Link);
        WDFREQUEST Request = Waiter->Request;

        ExFreePoolWithTag(Waiter, BTHMINI_TAG);
        BthSdpClientStart(Dev, Connection, Request);
    }
}

static
VOID
BthSdpClientFinish(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHMINI_SDP_CONNECTION Connection,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request = Connection->Request;
    ULONG_PTR Information = 0;

    if (NT_SUCCESS(Status))
    {
        if (Connection->Pdu == SDP_PDU_SERVICE_SEARCH_REQUEST)
            Status = BthSdpClientCompleteHandles(Request, Connection->Response, Connection->ResponseLength / 4,
                                                 &Information);
        else
            Status = BthSdpClientCompleteStream(Request, Connection->Response, Connection->ResponseLength,
                                                &Information);
    }
    BthSdpClientResetRequest(Connection);
    WdfRequestCompleteWithInformation(Request, Status, NT_SUCCESS(Status) ? Information : 0);
    BthSdpClientStartNext(Dev, Connection);
}

static
BOOLEAN
BthSdpClientAppend(
    _Inout_ PBTHMINI_SDP_CONNECTION Connection,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    if (Connection->ResponseLength + Length > SDP_CLIENT_MAX_RESPONSE)
        return FALSE;
    if (Connection->ResponseLength + Length > Connection->ResponseCapacity)
    {
        ULONG Capacity = max(Connection->ResponseCapacity * 2, 1024);
        PUCHAR Buffer;

        while (Capacity < Connection->ResponseLength + Length)
            Capacity *= 2;
        Capacity = min(Capacity, SDP_CLIENT_MAX_RESPONSE);
        Buffer = ExAllocatePoolWithTag(NonPagedPool, Capacity, BTHMINI_TAG);
        if (!Buffer)
            return FALSE;
        if (Connection->Response)
        {
            RtlCopyMemory(Buffer, Connection->Response, Connection->ResponseLength);
            ExFreePoolWithTag(Connection->Response, BTHMINI_TAG);
        }
        Connection->Response = Buffer;
        Connection->ResponseCapacity = Capacity;
    }
    RtlCopyMemory(Connection->Response + Connection->ResponseLength, Data, Length);
    Connection->ResponseLength += Length;
    return TRUE;
}

static
VOID
BthSdpClientReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_SDP_CONNECTION Connection = Channel->Context;
    ULONG ParameterLength, Payload, Offset;
    const UCHAR *Continuation;

    if (!Connection->Request || Length < 5)
        return;
    if (((Data[1] << 8) | Data[2]) != Connection->Transaction)
        return;
    ParameterLength = (Data[3] << 8) | Data[4];
    if (5 + ParameterLength > Length || Data[0] != Connection->Pdu + 1)
    {
        BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
        return;
    }
    if (Connection->Pdu == SDP_PDU_SERVICE_SEARCH_REQUEST)
    {
        ULONG Current, Room;

        if (ParameterLength < 5)
        {
            BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
            return;
        }
        Current = (Data[7] << 8) | Data[8];
        Payload = Current * 4;
        Offset = 9;
        if (Offset + Payload + 1 > 5 + ParameterLength)
        {
            BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
            return;
        }
        Room = Connection->MaxHandles * 4 - min(Connection->ResponseLength, Connection->MaxHandles * 4);
        if (!BthSdpClientAppend(Connection, Data + Offset, min(Payload, Room)))
        {
            BthSdpClientFinish(Dev, Connection, STATUS_INSUFFICIENT_RESOURCES);
            return;
        }
        if (Connection->ResponseLength >= Connection->MaxHandles * 4)
        {
            BthSdpClientFinish(Dev, Connection, STATUS_SUCCESS);
            return;
        }
    }
    else
    {
        if (ParameterLength < 3)
        {
            BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
            return;
        }
        Payload = (Data[5] << 8) | Data[6];
        Offset = 7;
        if (Offset + Payload + 1 > 5 + ParameterLength)
        {
            BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
            return;
        }
        if (!BthSdpClientAppend(Connection, Data + Offset, Payload))
        {
            BthSdpClientFinish(Dev, Connection, STATUS_INSUFFICIENT_RESOURCES);
            return;
        }
    }
    Continuation = Data + Offset + Payload;
    if (!Continuation[0])
    {
        BthSdpClientFinish(Dev, Connection, STATUS_SUCCESS);
        return;
    }
    if (Continuation[0] > 16 || Offset + Payload + 1 + Continuation[0] > 5 + ParameterLength)
    {
        BthSdpClientFinish(Dev, Connection, STATUS_REQUEST_NOT_ACCEPTED);
        return;
    }
    Connection->Deadline = BthSdpClientNow() + Connection->TimeoutMs;
    BthSdpClientSend(Dev, Connection, Continuation + 1, Continuation[0]);
    BthSdpClientStartNext(Dev, Connection);
}

static
VOID
BthSdpClientOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SDP_CONNECTION Connection = Channel->Context;
    WDFREQUEST Request = Connection->Connect;
    PBTH_SDP_CONNECT Input, Output;

    Connection->Connect = NULL;
    if (!NT_SUCCESS(Status))
    {
        Connection->Channel = NULL;
        if (!Connection->Detached)
            RemoveEntryList(&Connection->Link);
        if (Request)
            WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        BthSdpClientFree(Connection);
        return;
    }
    if (Connection->Detached)
    {
        BthL2capDisconnect(Dev, Channel);
        return;
    }
    Connection->Open = TRUE;
    if (!Request)
        return;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*Input), (PVOID *)&Input, NULL)) ||
        !NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(*Output), (PVOID *)&Output, NULL)))
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Output != Input)
        RtlMoveMemory(Output, Input, sizeof(*Output));
    Output->hConnection = Connection->Handle;
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(*Output));
}

static
VOID
BthSdpClientClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SDP_CONNECTION Connection = Channel->Context;

    UNREFERENCED_PARAMETER(Dev);
    UNREFERENCED_PARAMETER(Status);
    Connection->Channel = NULL;
    Connection->Open = FALSE;
    if (Connection->Detached)
    {
        BthSdpClientFree(Connection);
        return;
    }
    BthSdpClientFailAll(Connection, STATUS_DEVICE_NOT_CONNECTED);
}

static
NTSTATUS
BthSdpClientConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_SDP_CONNECTION Connection;
    PBTH_SDP_CONNECT Input;
    PVOID Output;
    PBTHMINI_REMOTE Remote;
    NTSTATUS Status;

    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*Input), (PVOID *)&Input, NULL)) ||
        !NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(BTH_SDP_CONNECT), &Output, NULL)))
        return STATUS_INVALID_PARAMETER;
    if (!Input->bthAddress || Input->bthAddress == Dev->Address ||
        (Input->fSdpConnect & ~(SDP_CONNECT_CACHE | SDP_CONNECT_ALLOW_PIN)) ||
        (Input->requestTimeout != SDP_REQUEST_TO_DEFAULT &&
         (Input->requestTimeout < SDP_REQUEST_TO_MIN || Input->requestTimeout > SDP_REQUEST_TO_MAX)))
        return STATUS_INVALID_PARAMETER;
    if (Input->fSdpConnect & SDP_CONNECT_CACHE)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (!Dev->Ready)
        return STATUS_DEVICE_BUSY;
    Remote = BthRadioGetRemote(Dev, Input->bthAddress);
    if (!Remote)
        return STATUS_INSUFFICIENT_RESOURCES;
    Connection = ExAllocatePoolZero(NonPagedPool, sizeof(*Connection), BTHMINI_TAG);
    if (!Connection)
        return STATUS_INSUFFICIENT_RESOURCES;
    InitializeListHead(&Connection->Waiting);
    Connection->Handle = ++Dev->NextSdpConnection;
    Connection->FileObject = WdfRequestGetFileObject(Request);
    Connection->TimeoutMs = (Input->requestTimeout ? Input->requestTimeout : SDP_CLIENT_DEFAULT_TIMEOUT_S) * 1000;
    Connection->Connect = Request;
    InsertTailList(&Dev->SdpConnections, &Connection->Link);
    Status = BthL2capConnect(Dev, Remote, L2CAP_PSM_SDP, &BthSdpClientConnectionCallbacks, Connection,
                             &Connection->Channel);
    if (Status == STATUS_PENDING)
        return STATUS_PENDING;
    RemoveEntryList(&Connection->Link);
    ExFreePoolWithTag(Connection, BTHMINI_TAG);
    return Status == STATUS_INSUFFICIENT_RESOURCES ? Status : STATUS_DEVICE_NOT_CONNECTED;
}

NTSTATUS
BthSdpClientControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _In_ ULONG IoControlCode,
    _Out_ PULONG_PTR Information)
{
    PBTHMINI_SDP_CONNECTION Connection;
    PBTHMINI_SDP_WAITER Waiter;
    PHANDLE_SDP Handle;
    PUCHAR Parameters;
    ULONG ParameterLength, MaxHandles;
    HANDLE_SDP Target;
    UCHAR Pdu;
    NTSTATUS Status;

    *Information = 0;
    switch (IoControlCode)
    {
        case IOCTL_BTH_SDP_CONNECT:
            return BthSdpClientConnect(Dev, Request);

        case IOCTL_BTH_SDP_DISCONNECT:
            if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*Handle), (PVOID *)&Handle, NULL)))
                return STATUS_INVALID_PARAMETER;
            Connection = BthSdpClientFind(Dev, *Handle);
            if (!Connection)
                return STATUS_INVALID_PARAMETER;
            BthSdpClientDetach(Dev, Connection);
            return STATUS_SUCCESS;

        default:
            break;
    }

    Status = BthSdpClientPrepare(Dev, Request, IoControlCode, &Target, &Pdu, &Parameters, &ParameterLength,
                                 &MaxHandles, Information);
    if (Status != STATUS_PENDING)
        return Status;
    ExFreePoolWithTag(Parameters, BTHMINI_TAG);
    Connection = BthSdpClientFind(Dev, Target);
    if (!Connection)
        return STATUS_INVALID_PARAMETER;
    if (!Connection->Open)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (!Connection->Request)
    {
        BthSdpClientStart(Dev, Connection, Request);
        return STATUS_PENDING;
    }
    Waiter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Waiter), BTHMINI_TAG);
    if (!Waiter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Waiter->Request = Request;
    InsertTailList(&Connection->Waiting, &Waiter->Link);
    return STATUS_PENDING;
}

VOID
BthSdpClientFileClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFFILEOBJECT FileObject)
{
    PBTHMINI_SDP_CLOSED_FILE Closed;
    KIRQL OldIrql;

    Closed = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Closed), BTHMINI_TAG);
    if (!Closed)
        return;
    WdfObjectReference(FileObject);
    Closed->FileObject = FileObject;
    KeAcquireSpinLock(&Dev->SdpClosedFilesLock, &OldIrql);
    InsertTailList(&Dev->SdpClosedFiles, &Closed->Link);
    KeReleaseSpinLock(&Dev->SdpClosedFilesLock, OldIrql);
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
PBTHMINI_SDP_CLOSED_FILE
BthSdpClientNextClosedFile(
    _In_ PBTHMINI_DEVICE Dev)
{
    PBTHMINI_SDP_CLOSED_FILE Closed = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->SdpClosedFilesLock, &OldIrql);
    if (!IsListEmpty(&Dev->SdpClosedFiles))
        Closed = CONTAINING_RECORD(RemoveHeadList(&Dev->SdpClosedFiles), BTHMINI_SDP_CLOSED_FILE, Link);
    KeReleaseSpinLock(&Dev->SdpClosedFilesLock, OldIrql);
    return Closed;
}

VOID
BthSdpClientTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    PBTHMINI_SDP_CLOSED_FILE Closed;
    ULONGLONG Now = BthSdpClientNow();
    PLIST_ENTRY Entry;

    while ((Closed = BthSdpClientNextClosedFile(Dev)) != NULL)
    {
        Entry = Dev->SdpConnections.Flink;
        while (Entry != &Dev->SdpConnections)
        {
            PBTHMINI_SDP_CONNECTION Connection = CONTAINING_RECORD(Entry, BTHMINI_SDP_CONNECTION, Link);

            Entry = Entry->Flink;
            if (Connection->FileObject == Closed->FileObject)
                BthSdpClientDetach(Dev, Connection);
        }
        BthSdpRemoveOwnedRecords(Dev, Closed->FileObject);
        WdfObjectDereference(Closed->FileObject);
        ExFreePoolWithTag(Closed, BTHMINI_TAG);
    }

    Entry = Dev->SdpConnections.Flink;
    while (Entry != &Dev->SdpConnections)
    {
        PBTHMINI_SDP_CONNECTION Connection = CONTAINING_RECORD(Entry, BTHMINI_SDP_CONNECTION, Link);

        Entry = Entry->Flink;
        if (Connection->Request && Now >= Connection->Deadline)
        {
            WDFREQUEST Request = Connection->Request;

            DPRINT1("BTHMINI: SDP request on connection %I64u timed out\n", Connection->Handle);
            BthSdpClientResetRequest(Connection);
            WdfRequestComplete(Request, STATUS_IO_TIMEOUT);
            if (Connection->Channel)
            {
                Connection->Open = FALSE;
                BthL2capDisconnect(Dev, Connection->Channel);
            }
        }
    }
}

VOID
BthSdpClientFlush(
    _In_ PBTHMINI_DEVICE Dev)
{
    PBTHMINI_SDP_CLOSED_FILE Closed;

    while (!IsListEmpty(&Dev->SdpConnections))
        BthSdpClientFree(CONTAINING_RECORD(RemoveHeadList(&Dev->SdpConnections), BTHMINI_SDP_CONNECTION, Link));
    while ((Closed = BthSdpClientNextClosedFile(Dev)) != NULL)
    {
        WdfObjectDereference(Closed->FileObject);
        ExFreePoolWithTag(Closed, BTHMINI_TAG);
    }
}
