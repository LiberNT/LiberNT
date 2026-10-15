/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service Discovery Protocol client and server
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define SDP_ERROR_RESPONSE                      0x01
#define SDP_SERVICE_SEARCH_REQUEST              0x02
#define SDP_SERVICE_SEARCH_RESPONSE             0x03
#define SDP_SERVICE_ATTRIBUTE_REQUEST           0x04
#define SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST    0x06
#define SDP_SERVICE_SEARCH_ATTRIBUTE_RESPONSE   0x07
#define SDP_ERROR_INVALID_HANDLE                0x0002
#define SDP_ERROR_INVALID_SYNTAX                0x0003
#define SDP_TYPE_UINT                           1
#define SDP_TYPE_UUID                           3
#define SDP_TYPE_SEQUENCE                       6
#define SDP_TYPE_ALTERNATIVE                    7
#define SDP_ATTRIBUTE_PROTOCOLS                 0x0004
#define SDP_UUID_RFCOMM                         0x0003
#define SDP_MAX_RESPONSE                        0x4000
#define SDP_MAX_DEPTH                           8

typedef struct _BTHMINI_SDP_QUERY
{
    BTHMINI_SDP_CALLBACK *Callback;
    PVOID Context;
    PBTHMINI_CHANNEL Channel;
    USHORT Uuid;
    USHORT Transaction;
    BOOLEAN Done;
    ULONG Length;
    UCHAR Response[SDP_MAX_RESPONSE];
} BTHMINI_SDP_QUERY, *PBTHMINI_SDP_QUERY;

static const UCHAR BthSdpBaseUuid[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB };

static
ULONG
BthSdpElement(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PUCHAR Type,
    _Out_ PULONG ValueOffset,
    _Out_ PULONG ValueLength)
{
    static const UCHAR FixedSizes[] = { 1, 2, 4, 8, 16 };
    UCHAR Index;

    if (!Length)
        return 0;
    *Type = Data[0] >> 3;
    Index = Data[0] & 7;
    if (*Type == 0)
    {
        *ValueOffset = 1;
        *ValueLength = 0;
    }
    else if (Index < 5)
    {
        *ValueOffset = 1;
        *ValueLength = FixedSizes[Index];
    }
    else if (Index == 5 && Length >= 2)
    {
        *ValueOffset = 2;
        *ValueLength = Data[1];
    }
    else if (Index == 6 && Length >= 3)
    {
        *ValueOffset = 3;
        *ValueLength = (Data[1] << 8) | Data[2];
    }
    else if (Index == 7 && Length >= 5)
    {
        *ValueOffset = 5;
        *ValueLength = ((ULONG)Data[1] << 24) | (Data[2] << 16) | (Data[3] << 8) | Data[4];
    }
    else
    {
        return 0;
    }
    if (*ValueOffset + *ValueLength > Length)
        return 0;
    return *ValueOffset + *ValueLength;
}

static
BOOLEAN
BthSdpUuidIs(
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length,
    _In_ USHORT Uuid)
{
    if (Length == 2)
        return ((Value[0] << 8) | Value[1]) == Uuid;
    if (Length == 4)
        return !Value[0] && !Value[1] && ((Value[2] << 8) | Value[3]) == Uuid;
    if (Length == 16)
        return !Value[0] && !Value[1] && ((Value[2] << 8) | Value[3]) == Uuid &&
               RtlEqualMemory(Value + 4, BthSdpBaseUuid, sizeof(BthSdpBaseUuid));
    return FALSE;
}

static
UCHAR
BthSdpFindRfcommChannel(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG Depth)
{
    ULONG Offset = 0;

    if (Depth > SDP_MAX_DEPTH)
        return 0;

    while (Offset < Length)
    {
        ULONG ValueOffset, ValueLength, Size;
        UCHAR Type;

        Size = BthSdpElement(Data + Offset, Length - Offset, &Type, &ValueOffset, &ValueLength);
        if (!Size)
            return 0;
        if (Type == SDP_TYPE_SEQUENCE || Type == SDP_TYPE_ALTERNATIVE)
        {
            UCHAR Channel = BthSdpFindRfcommChannel(Data + Offset + ValueOffset, ValueLength, Depth + 1);

            if (Channel)
                return Channel;
        }
        else if (Type == SDP_TYPE_UUID && BthSdpUuidIs(Data + Offset + ValueOffset, ValueLength, SDP_UUID_RFCOMM))
        {
            ULONG NextOffset, NextLength;
            UCHAR NextType;
            const UCHAR *Next = Data + Offset + Size;

            if (BthSdpElement(Next, Length - Offset - Size, &NextType, &NextOffset, &NextLength) &&
                NextType == SDP_TYPE_UINT && NextLength == 1)
                return Next[NextOffset];
        }
        Offset += Size;
    }
    return 0;
}

static
UCHAR
BthSdpParseRecords(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG ValueOffset, ValueLength, Offset;
    UCHAR Type;

    if (!BthSdpElement(Data, Length, &Type, &ValueOffset, &ValueLength) || Type != SDP_TYPE_SEQUENCE)
        return 0;
    Data += ValueOffset;
    Length = ValueLength;
    for (Offset = 0; Offset < Length;)
    {
        ULONG RecordOffset, RecordLength, Size, Inner;
        const UCHAR *Record;

        Size = BthSdpElement(Data + Offset, Length - Offset, &Type, &RecordOffset, &RecordLength);
        if (!Size || Type != SDP_TYPE_SEQUENCE)
            return 0;
        Record = Data + Offset + RecordOffset;
        for (Inner = 0; Inner + 3 <= RecordLength;)
        {
            ULONG IdOffset, IdLength, IdSize, AttributeOffset, AttributeLength, AttributeSize;
            UCHAR IdType, AttributeType;

            IdSize = BthSdpElement(Record + Inner, RecordLength - Inner, &IdType, &IdOffset, &IdLength);
            if (!IdSize || IdType != SDP_TYPE_UINT || IdLength != 2)
                break;
            AttributeSize = BthSdpElement(Record + Inner + IdSize, RecordLength - Inner - IdSize,
                                          &AttributeType, &AttributeOffset, &AttributeLength);
            if (!AttributeSize)
                break;
            if (((Record[Inner + IdOffset] << 8) | Record[Inner + IdOffset + 1]) == SDP_ATTRIBUTE_PROTOCOLS)
            {
                UCHAR Channel = BthSdpFindRfcommChannel(Record + Inner + IdSize, AttributeSize, 0);

                if (Channel)
                    return Channel;
            }
            Inner += IdSize + AttributeSize;
        }
        Offset += Size;
    }
    return 0;
}

static
VOID
BthSdpSendQuery(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_SDP_QUERY Query,
    _In_reads_bytes_(ContinuationLength) const UCHAR *Continuation,
    _In_ UCHAR ContinuationLength)
{
    UCHAR Pdu[5 + 5 + 2 + 7 + 1 + 16];
    ULONG Length = 5;

    Query->Transaction++;
    Pdu[0] = SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
    Pdu[1] = (UCHAR)(Query->Transaction >> 8);
    Pdu[2] = (UCHAR)Query->Transaction;
    Pdu[Length++] = 0x35;
    Pdu[Length++] = 0x03;
    Pdu[Length++] = 0x19;
    Pdu[Length++] = (UCHAR)(Query->Uuid >> 8);
    Pdu[Length++] = (UCHAR)Query->Uuid;
    Pdu[Length++] = (UCHAR)((SDP_MAX_RESPONSE - 512) >> 8);
    Pdu[Length++] = (UCHAR)(SDP_MAX_RESPONSE - 512);
    Pdu[Length++] = 0x35;
    Pdu[Length++] = 0x05;
    Pdu[Length++] = 0x0A;
    Pdu[Length++] = 0x00;
    Pdu[Length++] = 0x00;
    Pdu[Length++] = 0xFF;
    Pdu[Length++] = 0xFF;
    Pdu[Length++] = ContinuationLength;
    RtlCopyMemory(Pdu + Length, Continuation, min(ContinuationLength, 16));
    Length += min(ContinuationLength, 16);
    Pdu[3] = (UCHAR)((Length - 5) >> 8);
    Pdu[4] = (UCHAR)(Length - 5);
    BthL2capSend(Dev, Query->Channel, Pdu, Length);
}

static
VOID
BthSdpFinish(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_SDP_QUERY Query,
    _In_ NTSTATUS Status,
    _In_ UCHAR Channel)
{
    if (Query->Done)
        return;
    Query->Done = TRUE;
    Query->Callback(Dev, Query->Channel->Remote, Query->Context, Status, Channel);
    BthL2capDisconnect(Dev, Query->Channel);
}

static
VOID
BthSdpClientOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SDP_QUERY Query = Channel->Context;

    if (NT_SUCCESS(Status))
    {
        BthSdpSendQuery(Dev, Query, NULL, 0);
        return;
    }
    Query->Callback(Dev, Channel->Remote, Query->Context, Status, 0);
    ExFreePoolWithTag(Query, BTHMINI_TAG);
}

static
VOID
BthSdpClientReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_SDP_QUERY Query = Channel->Context;
    ULONG ByteCount, ParameterLength;
    UCHAR ContinuationLength;

    if (Query->Done || Length < 7)
        return;
    ParameterLength = (Data[3] << 8) | Data[4];
    if (Data[0] != SDP_SERVICE_SEARCH_ATTRIBUTE_RESPONSE || 5 + ParameterLength > Length || ParameterLength < 3)
    {
        BthSdpFinish(Dev, Query, STATUS_NOT_FOUND, 0);
        return;
    }
    ByteCount = (Data[5] << 8) | Data[6];
    if (7 + ByteCount + 1 > 5 + ParameterLength || Query->Length + ByteCount > sizeof(Query->Response))
    {
        BthSdpFinish(Dev, Query, STATUS_INVALID_NETWORK_RESPONSE, 0);
        return;
    }
    RtlCopyMemory(Query->Response + Query->Length, Data + 7, ByteCount);
    Query->Length += ByteCount;
    ContinuationLength = Data[7 + ByteCount];
    if (ContinuationLength)
    {
        if (8 + ByteCount + ContinuationLength > 5 + ParameterLength)
        {
            BthSdpFinish(Dev, Query, STATUS_INVALID_NETWORK_RESPONSE, 0);
            return;
        }
        BthSdpSendQuery(Dev, Query, Data + 8 + ByteCount, ContinuationLength);
        return;
    }
    {
        UCHAR RfcommChannel = BthSdpParseRecords(Query->Response, Query->Length);

        BthSdpFinish(Dev, Query, RfcommChannel ? STATUS_SUCCESS : STATUS_NOT_FOUND, RfcommChannel);
    }
}

static
VOID
BthSdpClientClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SDP_QUERY Query = Channel->Context;

    if (!Query->Done)
        Query->Callback(Dev, Channel->Remote, Query->Context, NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status, 0);
    ExFreePoolWithTag(Query, BTHMINI_TAG);
}

static const BTHMINI_CHANNEL_CALLBACKS BthSdpClientCallbacks =
{
    BthSdpClientOpened,
    BthSdpClientReceive,
    BthSdpClientClosed
};

NTSTATUS
BthSdpQueryRfcommChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceUuid,
    _In_ BTHMINI_SDP_CALLBACK *Callback,
    _In_opt_ PVOID Context)
{
    PBTHMINI_SDP_QUERY Query;
    NTSTATUS Status;

    Query = ExAllocatePoolZero(NonPagedPool, sizeof(*Query), BTHMINI_TAG);
    if (!Query)
        return STATUS_INSUFFICIENT_RESOURCES;
    Query->Callback = Callback;
    Query->Context = Context;
    Query->Uuid = ServiceUuid;
    Status = BthL2capConnect(Dev, Remote, L2CAP_PSM_SDP, &BthSdpClientCallbacks, Query, &Query->Channel);
    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(Query, BTHMINI_TAG);
    return Status;
}

static
VOID
BthSdpServerReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Response[5 + 5];
    ULONG ResponseLength = 5;

    if (Length < 5)
        return;
    Response[1] = Data[1];
    Response[2] = Data[2];
    switch (Data[0])
    {
        case SDP_SERVICE_SEARCH_REQUEST:
            Response[0] = SDP_SERVICE_SEARCH_RESPONSE;
            RtlZeroMemory(Response + 5, 5);
            ResponseLength += 5;
            break;

        case SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST:
            Response[0] = SDP_SERVICE_SEARCH_ATTRIBUTE_RESPONSE;
            Response[5] = 0x00;
            Response[6] = 0x02;
            Response[7] = 0x35;
            Response[8] = 0x00;
            Response[9] = 0x00;
            ResponseLength += 5;
            break;

        case SDP_SERVICE_ATTRIBUTE_REQUEST:
            Response[0] = SDP_ERROR_RESPONSE;
            Response[5] = 0x00;
            Response[6] = (UCHAR)SDP_ERROR_INVALID_HANDLE;
            ResponseLength += 2;
            break;

        default:
            Response[0] = SDP_ERROR_RESPONSE;
            Response[5] = 0x00;
            Response[6] = (UCHAR)SDP_ERROR_INVALID_SYNTAX;
            ResponseLength += 2;
            break;
    }
    Response[3] = 0;
    Response[4] = (UCHAR)(ResponseLength - 5);
    BthL2capSend(Dev, Channel, Response, ResponseLength);
}

static const BTHMINI_CHANNEL_CALLBACKS BthSdpServer =
{
    NULL,
    BthSdpServerReceive,
    NULL
};

const BTHMINI_CHANNEL_CALLBACKS *
BthSdpServerCallbacks(VOID)
{
    return &BthSdpServer;
}
