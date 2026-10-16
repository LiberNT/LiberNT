/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service Discovery Protocol client and server
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define SDP_ERROR_INVALID_HANDLE                0x0002
#define SDP_ERROR_INVALID_SYNTAX                0x0003
#define SDP_ERROR_INVALID_PDU_SIZE              0x0004
#define SDP_ERROR_INVALID_CONTINUATION          0x0005
#define SDP_ERROR_INSUFFICIENT_RESOURCES        0x0006
#define SDP_TYPE_UINT                           1
#define SDP_TYPE_UUID                           3
#define SDP_TYPE_BOOLEAN                        5
#define SDP_TYPE_SEQUENCE                       6
#define SDP_TYPE_ALTERNATIVE                    7
#define SDP_ATTRIBUTE_PROTOCOLS                 0x0004
#define SDP_UUID_RFCOMM                         0x0003
#define SDP_MAX_RESPONSE                        0x4000
#define SDP_MAX_DEPTH                           8

typedef struct _BTHMINI_SDP_QUERY
{
    BTHMINI_SDP_CALLBACK *Callback;
    BTHMINI_SDP_RECORDS_CALLBACK *RecordsCallback;
    PVOID Context;
    PBTHMINI_CHANNEL Channel;
    UCHAR Uuid[16];
    UCHAR UuidLength;
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
    UCHAR Pdu[5 + 2 + 17 + 2 + 7 + 1 + 16];
    ULONG Length = 5;

    Query->Transaction++;
    Pdu[0] = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
    Pdu[1] = (UCHAR)(Query->Transaction >> 8);
    Pdu[2] = (UCHAR)Query->Transaction;
    Pdu[Length++] = 0x35;
    Pdu[Length++] = (UCHAR)(1 + Query->UuidLength);
    Pdu[Length++] = Query->UuidLength == 2 ? 0x19 : 0x1C;
    RtlCopyMemory(Pdu + Length, Query->Uuid, Query->UuidLength);
    Length += Query->UuidLength;
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
    if (Query->RecordsCallback)
        Query->RecordsCallback(Dev, Query->Channel->Remote, Query->Context, Status,
                               NT_SUCCESS(Status) ? Query->Response : NULL, NT_SUCCESS(Status) ? Query->Length : 0);
    else
        Query->Callback(Dev, Query->Channel->Remote, Query->Context, Status, Channel);
    BthL2capDisconnect(Dev, Query->Channel);
}

static
VOID
BthSdpFail(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_SDP_QUERY Query,
    _In_ NTSTATUS Status)
{
    if (Query->RecordsCallback)
        Query->RecordsCallback(Dev, Remote, Query->Context, Status, NULL, 0);
    else
        Query->Callback(Dev, Remote, Query->Context, Status, 0);
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
    BthSdpFail(Dev, Channel->Remote, Query, Status);
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
    if (Data[0] != SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_RESPONSE || 5 + ParameterLength > Length || ParameterLength < 3)
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
    if (Query->RecordsCallback)
    {
        BthSdpFinish(Dev, Query, STATUS_SUCCESS, 0);
    }
    else
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
        BthSdpFail(Dev, Channel->Remote, Query, NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status);
    ExFreePoolWithTag(Query, BTHMINI_TAG);
}

static const BTHMINI_CHANNEL_CALLBACKS BthSdpClientCallbacks =
{
    BthSdpClientOpened,
    BthSdpClientReceive,
    BthSdpClientClosed
};

static
NTSTATUS
BthSdpStartQuery(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(UuidLength) const UCHAR *Uuid,
    _In_ UCHAR UuidLength,
    _In_opt_ BTHMINI_SDP_CALLBACK *Callback,
    _In_opt_ BTHMINI_SDP_RECORDS_CALLBACK *RecordsCallback,
    _In_opt_ PVOID Context)
{
    PBTHMINI_SDP_QUERY Query;
    NTSTATUS Status;

    Query = ExAllocatePoolZero(NonPagedPool, sizeof(*Query), BTHMINI_TAG);
    if (!Query)
        return STATUS_INSUFFICIENT_RESOURCES;
    Query->Callback = Callback;
    Query->RecordsCallback = RecordsCallback;
    Query->Context = Context;
    RtlCopyMemory(Query->Uuid, Uuid, UuidLength);
    Query->UuidLength = UuidLength;
    Status = BthL2capConnect(Dev, Remote, L2CAP_PSM_SDP, &BthSdpClientCallbacks, Query, &Query->Channel);
    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(Query, BTHMINI_TAG);
    return Status;
}

NTSTATUS
BthSdpQueryRfcommChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceUuid,
    _In_ BTHMINI_SDP_CALLBACK *Callback,
    _In_opt_ PVOID Context)
{
    UCHAR Uuid[2] = { (UCHAR)(ServiceUuid >> 8), (UCHAR)ServiceUuid };

    return BthSdpStartQuery(Dev, Remote, Uuid, sizeof(Uuid), Callback, NULL, Context);
}

NTSTATUS
BthSdpQueryRfcommChannelUuid(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_(16) const UCHAR *ServiceUuid,
    _In_ BTHMINI_SDP_CALLBACK *Callback,
    _In_opt_ PVOID Context)
{
    if (RtlEqualMemory(ServiceUuid + 4, BthSdpBaseUuid, sizeof(BthSdpBaseUuid)) && !ServiceUuid[0] && !ServiceUuid[1])
        return BthSdpStartQuery(Dev, Remote, ServiceUuid + 2, 2, Callback, NULL, Context);
    return BthSdpStartQuery(Dev, Remote, ServiceUuid, 16, Callback, NULL, Context);
}

NTSTATUS
BthSdpQueryRecords(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceUuid,
    _In_ BTHMINI_SDP_RECORDS_CALLBACK *Callback,
    _In_opt_ PVOID Context)
{
    UCHAR Uuid[2] = { (UCHAR)(ServiceUuid >> 8), (UCHAR)ServiceUuid };

    return BthSdpStartQuery(Dev, Remote, Uuid, sizeof(Uuid), NULL, Callback, Context);
}

BOOLEAN
BthSdpNextRecord(
    _In_reads_bytes_(Length) const UCHAR *Records,
    _In_ ULONG Length,
    _Inout_ PULONG Offset,
    _Out_ const UCHAR **Record,
    _Out_ PULONG RecordLength)
{
    ULONG ValueOffset, ValueLength, Size;
    UCHAR Type;

    *Record = NULL;
    *RecordLength = 0;
    if (!BthSdpElement(Records, Length, &Type, &ValueOffset, &ValueLength) || Type != SDP_TYPE_SEQUENCE)
        return FALSE;
    if (*Offset < ValueOffset)
        *Offset = ValueOffset;
    if (*Offset >= ValueOffset + ValueLength)
        return FALSE;
    Size = BthSdpElement(Records + *Offset, ValueOffset + ValueLength - *Offset, &Type, &ValueOffset, &ValueLength);
    if (!Size || Type != SDP_TYPE_SEQUENCE)
        return FALSE;
    *Record = Records + *Offset;
    *RecordLength = Size;
    *Offset += Size;
    return TRUE;
}

BOOLEAN
BthSdpFindAttribute(
    _In_reads_bytes_(RecordLength) const UCHAR *Record,
    _In_ ULONG RecordLength,
    _In_ USHORT Attribute,
    _Out_ const UCHAR **Value,
    _Out_ PULONG ValueLength)
{
    ULONG ListOffset, ListLength, Offset;
    UCHAR Type;

    *Value = NULL;
    *ValueLength = 0;
    if (!BthSdpElement(Record, RecordLength, &Type, &ListOffset, &ListLength) || Type != SDP_TYPE_SEQUENCE)
        return FALSE;
    Record += ListOffset;
    for (Offset = 0; Offset < ListLength;)
    {
        ULONG IdOffset, IdLength, IdSize, AttributeOffset, AttributeLength, AttributeSize;
        UCHAR IdType, AttributeType;

        IdSize = BthSdpElement(Record + Offset, ListLength - Offset, &IdType, &IdOffset, &IdLength);
        if (!IdSize || IdType != SDP_TYPE_UINT || IdLength != 2)
            return FALSE;
        AttributeSize = BthSdpElement(Record + Offset + IdSize, ListLength - Offset - IdSize,
                                      &AttributeType, &AttributeOffset, &AttributeLength);
        if (!AttributeSize)
            return FALSE;
        if (((Record[Offset + IdOffset] << 8) | Record[Offset + IdOffset + 1]) == Attribute)
        {
            *Value = Record + Offset + IdSize;
            *ValueLength = AttributeSize;
            return TRUE;
        }
        Offset += IdSize + AttributeSize;
    }
    return FALSE;
}

BOOLEAN
BthSdpGetUint(
    _In_reads_bytes_(Length) const UCHAR *Element,
    _In_ ULONG Length,
    _Out_ PULONG Value)
{
    ULONG ValueOffset, ValueLength, Index;
    UCHAR Type;

    *Value = 0;
    if (!BthSdpElement(Element, Length, &Type, &ValueOffset, &ValueLength) ||
        (Type != SDP_TYPE_UINT && Type != SDP_TYPE_BOOLEAN) || ValueLength > 4)
    {
        return FALSE;
    }
    for (Index = 0; Index < ValueLength; Index++)
        *Value = (*Value << 8) | Element[ValueOffset + Index];
    return TRUE;
}

typedef struct _BTHMINI_SDP_ATTRIBUTE
{
    USHORT Id;
    ULONG Offset;
    ULONG Length;
} BTHMINI_SDP_ATTRIBUTE, *PBTHMINI_SDP_ATTRIBUTE;

typedef struct _BTHMINI_SDP_RECORD
{
    LIST_ENTRY Link;
    ULONG Handle;
    WDFFILEOBJECT Owner;
    ULONG Count;
    PBTHMINI_SDP_ATTRIBUTE Attributes;
    ULONG Length;
    PUCHAR Values;
} BTHMINI_SDP_RECORD, *PBTHMINI_SDP_RECORD;

typedef struct _BTHMINI_SDP_SESSION
{
    UCHAR Pdu;
    ULONG Length;
    PUCHAR Response;
} BTHMINI_SDP_SESSION, *PBTHMINI_SDP_SESSION;

static
VOID
BthSdpNormalizeUuid(
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length,
    _Out_writes_(16) PUCHAR Uuid)
{
    RtlZeroMemory(Uuid, 16);
    if (Length == 16)
    {
        RtlCopyMemory(Uuid, Value, 16);
        return;
    }
    RtlCopyMemory(Uuid + 4, BthSdpBaseUuid, sizeof(BthSdpBaseUuid));
    if (Length == 2)
    {
        Uuid[2] = Value[0];
        Uuid[3] = Value[1];
    }
    else if (Length == 4)
    {
        RtlCopyMemory(Uuid, Value, 4);
    }
}

static
BOOLEAN
BthSdpContainsUuid(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_reads_(16) const UCHAR *Uuid,
    _In_ ULONG Depth)
{
    ULONG Offset = 0;

    while (Offset < Length)
    {
        ULONG ValueOffset, ValueLength, Size;
        UCHAR Type, Candidate[16];

        Size = BthSdpElement(Data + Offset, Length - Offset, &Type, &ValueOffset, &ValueLength);
        if (!Size)
            return FALSE;
        if (Type == SDP_TYPE_UUID && (ValueLength == 2 || ValueLength == 4 || ValueLength == 16))
        {
            BthSdpNormalizeUuid(Data + Offset + ValueOffset, ValueLength, Candidate);
            if (RtlEqualMemory(Candidate, Uuid, 16))
                return TRUE;
        }
        else if ((Type == SDP_TYPE_SEQUENCE || Type == SDP_TYPE_ALTERNATIVE) && Depth < SDP_MAX_DEPTH &&
                 BthSdpContainsUuid(Data + Offset + ValueOffset, ValueLength, Uuid, Depth + 1))
        {
            return TRUE;
        }
        Offset += Size;
    }
    return FALSE;
}

static
BOOLEAN
BthSdpRecordMatches(
    _In_ PBTHMINI_SDP_RECORD Record,
    _In_reads_bytes_(Length) const UCHAR *Pattern,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    while (Offset < Length)
    {
        ULONG ValueOffset, ValueLength, Size;
        UCHAR Type, Uuid[16];

        Size = BthSdpElement(Pattern + Offset, Length - Offset, &Type, &ValueOffset, &ValueLength);
        if (!Size || Type != SDP_TYPE_UUID || (ValueLength != 2 && ValueLength != 4 && ValueLength != 16))
            return FALSE;
        BthSdpNormalizeUuid(Pattern + Offset + ValueOffset, ValueLength, Uuid);
        if (!BthSdpContainsUuid(Record->Values, Record->Length, Uuid, 0))
            return FALSE;
        Offset += Size;
    }
    return TRUE;
}

static
ULONG
BthSdpSequenceHeader(
    _Out_writes_(5) PUCHAR Header,
    _In_ ULONG Length)
{
    if (Length <= 0xFF)
    {
        Header[0] = 0x35;
        Header[1] = (UCHAR)Length;
        return 2;
    }
    if (Length <= 0xFFFF)
    {
        Header[0] = 0x36;
        Header[1] = (UCHAR)(Length >> 8);
        Header[2] = (UCHAR)Length;
        return 3;
    }
    Header[0] = 0x37;
    Header[1] = (UCHAR)(Length >> 24);
    Header[2] = (UCHAR)(Length >> 16);
    Header[3] = (UCHAR)(Length >> 8);
    Header[4] = (UCHAR)Length;
    return 5;
}

static
BOOLEAN
BthSdpAttributeWanted(
    _In_ USHORT Id,
    _In_reads_bytes_(Length) const UCHAR *List,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    while (Offset < Length)
    {
        ULONG ValueOffset, ValueLength, Size;
        UCHAR Type;

        Size = BthSdpElement(List + Offset, Length - Offset, &Type, &ValueOffset, &ValueLength);
        if (!Size || Type != SDP_TYPE_UINT)
            return FALSE;
        if (ValueLength == 2 && ((List[Offset + ValueOffset] << 8) | List[Offset + ValueOffset + 1]) == Id)
            return TRUE;
        if (ValueLength == 4)
        {
            USHORT Low = (USHORT)((List[Offset + ValueOffset] << 8) | List[Offset + ValueOffset + 1]);
            USHORT High = (USHORT)((List[Offset + ValueOffset + 2] << 8) | List[Offset + ValueOffset + 3]);

            if (Id >= Low && Id <= High)
                return TRUE;
        }
        Offset += Size;
    }
    return FALSE;
}

static
BOOLEAN
BthSdpValidIdList(
    _In_reads_bytes_(Length) const UCHAR *List,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    if (!Length)
        return FALSE;
    while (Offset < Length)
    {
        ULONG ValueOffset, ValueLength, Size;
        UCHAR Type;

        Size = BthSdpElement(List + Offset, Length - Offset, &Type, &ValueOffset, &ValueLength);
        if (!Size || Type != SDP_TYPE_UINT || (ValueLength != 2 && ValueLength != 4))
            return FALSE;
        Offset += Size;
    }
    return TRUE;
}

static
ULONG
BthSdpBuildAttributeList(
    _In_ PBTHMINI_SDP_RECORD Record,
    _In_reads_bytes_(ListLength) const UCHAR *List,
    _In_ ULONG ListLength,
    _Out_writes_bytes_opt_(Capacity) PUCHAR Output,
    _In_ ULONG Capacity)
{
    UCHAR Header[5];
    ULONG Index, Body = 0, HeaderLength, Used;

    for (Index = 0; Index < Record->Count; Index++)
    {
        if (BthSdpAttributeWanted(Record->Attributes[Index].Id, List, ListLength))
            Body += 3 + Record->Attributes[Index].Length;
    }
    HeaderLength = BthSdpSequenceHeader(Header, Body);
    if (!Output)
        return HeaderLength + Body;
    if (Capacity < HeaderLength + Body)
        return 0;
    RtlCopyMemory(Output, Header, HeaderLength);
    Used = HeaderLength;
    for (Index = 0; Index < Record->Count; Index++)
    {
        PBTHMINI_SDP_ATTRIBUTE Attribute = &Record->Attributes[Index];

        if (!BthSdpAttributeWanted(Attribute->Id, List, ListLength))
            continue;
        Output[Used] = 0x09;
        Output[Used + 1] = (UCHAR)(Attribute->Id >> 8);
        Output[Used + 2] = (UCHAR)Attribute->Id;
        RtlCopyMemory(Output + Used + 3, Record->Values + Attribute->Offset, Attribute->Length);
        Used += 3 + Attribute->Length;
    }
    return Used;
}

static
PBTHMINI_SDP_RECORD
BthSdpFindRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->SdpRecords.Flink; Entry != &Dev->SdpRecords; Entry = Entry->Flink)
    {
        PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

        if (Record->Handle == Handle)
            return Record;
    }
    return NULL;
}

static
VOID
BthSdpFreeRecord(
    _In_ PBTHMINI_SDP_RECORD Record)
{
    if (Record->Attributes)
        ExFreePoolWithTag(Record->Attributes, BTHMINI_TAG);
    if (Record->Values)
        ExFreePoolWithTag(Record->Values, BTHMINI_TAG);
    ExFreePoolWithTag(Record, BTHMINI_TAG);
}

static
NTSTATUS
BthSdpInsertRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG Handle)
{
    ULONG ListOffset, ListLength, Offset, Count = 0, Total = 5, Index;
    PBTHMINI_SDP_RECORD Record;
    UCHAR Type;

    if (!BthSdpElement(Data, Length, &Type, &ListOffset, &ListLength) || Type != SDP_TYPE_SEQUENCE)
        return STATUS_INVALID_PARAMETER;
    for (Offset = ListOffset; Offset < ListOffset + ListLength;)
    {
        ULONG IdOffset, IdLength, IdSize, ValueOffset, ValueLength, ValueSize;
        UCHAR IdType, ValueType;

        IdSize = BthSdpElement(Data + Offset, ListOffset + ListLength - Offset, &IdType, &IdOffset, &IdLength);
        if (!IdSize || IdType != SDP_TYPE_UINT || IdLength != 2)
            return STATUS_INVALID_PARAMETER;
        ValueSize = BthSdpElement(Data + Offset + IdSize, ListOffset + ListLength - Offset - IdSize, &ValueType,
                                  &ValueOffset, &ValueLength);
        if (!ValueSize)
            return STATUS_INVALID_PARAMETER;
        if (Data[Offset + 1] || Data[Offset + 2])
        {
            Count++;
            Total += ValueSize;
        }
        Offset += IdSize + ValueSize;
    }

    Record = ExAllocatePoolZero(NonPagedPool, sizeof(*Record), BTHMINI_TAG);
    if (!Record)
        return STATUS_INSUFFICIENT_RESOURCES;
    Record->Attributes = ExAllocatePoolZero(NonPagedPool, (Count + 1) * sizeof(BTHMINI_SDP_ATTRIBUTE), BTHMINI_TAG);
    Record->Values = ExAllocatePoolZero(NonPagedPool, Total, BTHMINI_TAG);
    if (!Record->Attributes || !Record->Values)
    {
        BthSdpFreeRecord(Record);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Record->Handle = Handle;
    Record->Values[0] = 0x0A;
    Record->Values[1] = (UCHAR)(Handle >> 24);
    Record->Values[2] = (UCHAR)(Handle >> 16);
    Record->Values[3] = (UCHAR)(Handle >> 8);
    Record->Values[4] = (UCHAR)Handle;
    Record->Attributes[0].Id = 0x0000;
    Record->Attributes[0].Offset = 0;
    Record->Attributes[0].Length = 5;
    Record->Count = 1;
    Record->Length = 5;
    for (Offset = ListOffset; Offset < ListOffset + ListLength;)
    {
        ULONG IdOffset, IdLength, IdSize, ValueOffset, ValueLength, ValueSize;
        UCHAR IdType, ValueType;
        USHORT Id;

        IdSize = BthSdpElement(Data + Offset, ListOffset + ListLength - Offset, &IdType, &IdOffset, &IdLength);
        ValueSize = BthSdpElement(Data + Offset + IdSize, ListOffset + ListLength - Offset - IdSize, &ValueType,
                                  &ValueOffset, &ValueLength);
        Id = (USHORT)((Data[Offset + 1] << 8) | Data[Offset + 2]);
        if (Id)
        {
            for (Index = Record->Count; Index > 0 && Record->Attributes[Index - 1].Id > Id; Index--)
                Record->Attributes[Index] = Record->Attributes[Index - 1];
            if (Index > 0 && Record->Attributes[Index - 1].Id == Id)
            {
                BthSdpFreeRecord(Record);
                return STATUS_INVALID_PARAMETER;
            }
            Record->Attributes[Index].Id = Id;
            Record->Attributes[Index].Offset = Record->Length;
            Record->Attributes[Index].Length = ValueSize;
            RtlCopyMemory(Record->Values + Record->Length, Data + Offset + IdSize, ValueSize);
            Record->Length += ValueSize;
            Record->Count++;
        }
        Offset += IdSize + ValueSize;
    }
    InsertTailList(&Dev->SdpRecords, &Record->Link);
    return STATUS_SUCCESS;
}

NTSTATUS
BthSdpAddRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_opt_ WDFFILEOBJECT Owner,
    _Out_ PULONG Handle)
{
    NTSTATUS Status;

    *Handle = 0;
    if (Dev->NextSdpHandle == 0xFFFFFFFF)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = BthSdpInsertRecord(Dev, Data, Length, Dev->NextSdpHandle);
    if (NT_SUCCESS(Status))
    {
        BthSdpFindRecord(Dev, Dev->NextSdpHandle)->Owner = Owner;
        *Handle = Dev->NextSdpHandle++;
    }
    return Status;
}

VOID
BthSdpRemoveOwnedRecords(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFFILEOBJECT Owner)
{
    PLIST_ENTRY Entry = Dev->SdpRecords.Flink;

    while (Entry != &Dev->SdpRecords)
    {
        PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

        Entry = Entry->Flink;
        if (Record->Owner != Owner)
            continue;
        RemoveEntryList(&Record->Link);
        BthSdpFreeRecord(Record);
    }
}

NTSTATUS
BthSdpRemoveRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Handle)
{
    PBTHMINI_SDP_RECORD Record = Handle ? BthSdpFindRecord(Dev, Handle) : NULL;

    if (!Record)
        return STATUS_NOT_FOUND;
    RemoveEntryList(&Record->Link);
    BthSdpFreeRecord(Record);
    return STATUS_SUCCESS;
}

VOID
BthSdpInitialize(
    _In_ PBTHMINI_DEVICE Dev)
{
    static const UCHAR ServerRecord[] =
    {
        0x35, 0x18,
        0x09, 0x00, 0x01, 0x35, 0x03, 0x19, 0x10, 0x00,
        0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02,
        0x09, 0x02, 0x00, 0x35, 0x03, 0x09, 0x01, 0x00
    };

    InitializeListHead(&Dev->SdpRecords);
    Dev->NextSdpHandle = 0x00010000;
    if (!NT_SUCCESS(BthSdpInsertRecord(Dev, ServerRecord, sizeof(ServerRecord), 0)))
        DPRINT1("BTHMINI: SDP server record not created\n");
}

VOID
BthSdpCleanup(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (!IsListEmpty(&Dev->SdpRecords))
        BthSdpFreeRecord(CONTAINING_RECORD(RemoveHeadList(&Dev->SdpRecords), BTHMINI_SDP_RECORD, Link));
}

static
NTSTATUS
BthSdpBuildResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_opt_ PBTHMINI_SDP_RECORD Single,
    _In_reads_bytes_opt_(PatternLength) const UCHAR *Pattern,
    _In_ ULONG PatternLength,
    _In_reads_bytes_(ListLength) const UCHAR *List,
    _In_ ULONG ListLength,
    _Outptr_result_bytebuffer_(*Length) PUCHAR *Response,
    _Out_ PULONG Length)
{
    ULONG Body = 0, HeaderLength = 0, Used;
    PLIST_ENTRY Entry;
    UCHAR Header[5];
    PUCHAR Output;

    *Response = NULL;
    *Length = 0;
    if (Single)
    {
        Body = BthSdpBuildAttributeList(Single, List, ListLength, NULL, 0);
    }
    else
    {
        for (Entry = Dev->SdpRecords.Flink; Entry != &Dev->SdpRecords; Entry = Entry->Flink)
        {
            PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

            if (BthSdpRecordMatches(Record, Pattern, PatternLength))
                Body += BthSdpBuildAttributeList(Record, List, ListLength, NULL, 0);
        }
        HeaderLength = BthSdpSequenceHeader(Header, Body);
    }
    Output = ExAllocatePoolWithTag(NonPagedPool, max(HeaderLength + Body, 1), BTHMINI_TAG);
    if (!Output)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Output, Header, HeaderLength);
    Used = HeaderLength;
    if (Single)
    {
        Used += BthSdpBuildAttributeList(Single, List, ListLength, Output + Used, Body);
    }
    else
    {
        for (Entry = Dev->SdpRecords.Flink; Entry != &Dev->SdpRecords; Entry = Entry->Flink)
        {
            PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

            if (BthSdpRecordMatches(Record, Pattern, PatternLength))
                Used += BthSdpBuildAttributeList(Record, List, ListLength, Output + Used, HeaderLength + Body - Used);
        }
    }
    *Response = Output;
    *Length = Used;
    return STATUS_SUCCESS;
}

NTSTATUS
BthSdpLocalSearch(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(PatternLength) const UCHAR *Pattern,
    _In_ ULONG PatternLength,
    _Out_writes_to_(MaxCount, *Count) PULONG Handles,
    _In_ ULONG MaxCount,
    _Out_ PULONG Count)
{
    PLIST_ENTRY Entry;

    *Count = 0;
    for (Entry = Dev->SdpRecords.Flink; Entry != &Dev->SdpRecords && *Count < MaxCount; Entry = Entry->Flink)
    {
        PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

        if (BthSdpRecordMatches(Record, Pattern, PatternLength))
            Handles[(*Count)++] = Record->Handle;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
BthSdpLocalAttributes(
    _In_ PBTHMINI_DEVICE Dev,
    _In_opt_ const ULONG *Handle,
    _In_reads_bytes_opt_(PatternLength) const UCHAR *Pattern,
    _In_ ULONG PatternLength,
    _In_reads_bytes_(ListLength) const UCHAR *List,
    _In_ ULONG ListLength,
    _Outptr_result_bytebuffer_(*Length) PUCHAR *Response,
    _Out_ PULONG Length)
{
    PBTHMINI_SDP_RECORD Single = NULL;

    *Response = NULL;
    *Length = 0;
    if (Handle)
    {
        Single = BthSdpFindRecord(Dev, *Handle);
        if (!Single)
            return STATUS_INVALID_PARAMETER;
    }
    return BthSdpBuildResponse(Dev, Single, Pattern, PatternLength, List, ListLength, Response, Length);
}

static
VOID
BthSdpServerSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ UCHAR Pdu,
    _In_reads_(2) const UCHAR *Transaction,
    _In_reads_bytes_(Length) const UCHAR *Parameters,
    _In_ ULONG Length)
{
    PUCHAR Response;

    if (Length > SDP_MAX_RESPONSE)
        return;
    Response = ExAllocatePoolWithTag(NonPagedPool, 5 + Length, BTHMINI_TAG);
    if (!Response)
        return;
    Response[0] = Pdu;
    Response[1] = Transaction[0];
    Response[2] = Transaction[1];
    Response[3] = (UCHAR)(Length >> 8);
    Response[4] = (UCHAR)Length;
    RtlCopyMemory(Response + 5, Parameters, Length);
    BthL2capSend(Dev, Channel, Response, 5 + Length);
    ExFreePoolWithTag(Response, BTHMINI_TAG);
}

static
VOID
BthSdpServerError(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_(2) const UCHAR *Transaction,
    _In_ USHORT Error)
{
    UCHAR Parameters[2];

    Parameters[0] = (UCHAR)(Error >> 8);
    Parameters[1] = (UCHAR)Error;
    BthSdpServerSend(Dev, Channel, SDP_PDU_ERROR_RESPONSE, Transaction, Parameters, sizeof(Parameters));
}

static
PBTHMINI_SDP_SESSION
BthSdpSession(
    _In_ PBTHMINI_CHANNEL Channel)
{
    PBTHMINI_SDP_SESSION Session = Channel->Context;

    if (!Session)
    {
        Session = ExAllocatePoolZero(NonPagedPool, sizeof(*Session), BTHMINI_TAG);
        Channel->Context = Session;
    }
    return Session;
}

static
VOID
BthSdpSessionReset(
    _Inout_ PBTHMINI_SDP_SESSION Session)
{
    if (Session->Response)
        ExFreePoolWithTag(Session->Response, BTHMINI_TAG);
    Session->Response = NULL;
    Session->Length = 0;
    Session->Pdu = 0;
}

static
VOID
BthSdpServerAttributes(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_SDP_SESSION Session = BthSdpSession(Channel);
    const UCHAR *Parameters = Data + 5;
    ULONG ParameterLength = Length - 5, Offset = 0, PatternOffset = 0, PatternLength = 0, ListOffset, ListLength;
    ULONG Size, Start = 0, Chunk, Used;
    UCHAR Type, Pdu = Data[0];
    PUCHAR Output;
    USHORT MaxBytes;
    PBTHMINI_SDP_RECORD Single = NULL;
    const UCHAR *Continuation;

    if (!Session)
    {
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INSUFFICIENT_RESOURCES);
        return;
    }
    if (Pdu == SDP_PDU_SERVICE_ATTRIBUTE_REQUEST)
    {
        if (ParameterLength < 4)
            goto Syntax;
        Single = BthSdpFindRecord(Dev, ((ULONG)Parameters[0] << 24) | (Parameters[1] << 16) | (Parameters[2] << 8) |
                                       Parameters[3]);
        if (!Single)
        {
            BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_HANDLE);
            return;
        }
        Offset = 4;
    }
    else
    {
        Size = BthSdpElement(Parameters, ParameterLength, &Type, &PatternOffset, &PatternLength);
        if (!Size || Type != SDP_TYPE_SEQUENCE || !PatternLength)
            goto Syntax;
        Offset = Size;
    }
    if (Offset + 2 > ParameterLength)
        goto Syntax;
    MaxBytes = (USHORT)((Parameters[Offset] << 8) | Parameters[Offset + 1]);
    Offset += 2;
    Size = BthSdpElement(Parameters + Offset, ParameterLength - Offset, &Type, &ListOffset, &ListLength);
    if (!Size || Type != SDP_TYPE_SEQUENCE || MaxBytes < 7 ||
        !BthSdpValidIdList(Parameters + Offset + ListOffset, ListLength))
        goto Syntax;
    Continuation = Parameters + Offset + Size;
    if (Offset + Size >= ParameterLength || Continuation[0] > 16 ||
        Offset + Size + 1 + Continuation[0] > ParameterLength)
        goto Syntax;

    if (Continuation[0])
    {
        if (Continuation[0] != 4 || !Session->Response || Session->Pdu != Pdu)
        {
            BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_CONTINUATION);
            return;
        }
        Start = ((ULONG)Continuation[1] << 24) | (Continuation[2] << 16) | (Continuation[3] << 8) | Continuation[4];
        if (Start >= Session->Length)
        {
            BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_CONTINUATION);
            return;
        }
    }
    else
    {
        BthSdpSessionReset(Session);
        if (!NT_SUCCESS(BthSdpBuildResponse(Dev, Single, Single ? NULL : Parameters + PatternOffset, PatternLength,
                                            Parameters + Offset + ListOffset, ListLength, &Session->Response,
                                            &Session->Length)))
        {
            BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INSUFFICIENT_RESOURCES);
            return;
        }
        Session->Pdu = Pdu;
    }

    Chunk = Session->Length - Start;
    Chunk = min(Chunk, MaxBytes);
    Chunk = min(Chunk, (ULONG)min(max(Channel->RemoteMtu, 48), SDP_MAX_RESPONSE) - 5 - 2 - 5);
    Output = ExAllocatePoolWithTag(NonPagedPool, 2 + Chunk + 5, BTHMINI_TAG);
    if (!Output)
    {
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INSUFFICIENT_RESOURCES);
        return;
    }
    Output[0] = (UCHAR)(Chunk >> 8);
    Output[1] = (UCHAR)Chunk;
    RtlCopyMemory(Output + 2, Session->Response + Start, Chunk);
    Used = 2 + Chunk;
    if (Start + Chunk < Session->Length)
    {
        ULONG Next = Start + Chunk;

        Output[Used++] = 4;
        Output[Used++] = (UCHAR)(Next >> 24);
        Output[Used++] = (UCHAR)(Next >> 16);
        Output[Used++] = (UCHAR)(Next >> 8);
        Output[Used++] = (UCHAR)Next;
    }
    else
    {
        Output[Used++] = 0;
        BthSdpSessionReset(Session);
    }
    BthSdpServerSend(Dev, Channel, (UCHAR)(Pdu + 1), Data + 1, Output, Used);
    ExFreePoolWithTag(Output, BTHMINI_TAG);
    return;

Syntax:
    BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_SYNTAX);
}

static
VOID
BthSdpServerSearch(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Data + 5;
    ULONG ParameterLength = Length - 5, Size, PatternOffset, PatternLength, Total = 0, Start = 0, Current = 0, Capacity;
    UCHAR Type;
    PUCHAR Output;
    const UCHAR *Continuation;
    USHORT MaxCount;
    PLIST_ENTRY Entry;
    ULONG Used = 4;

    Size = BthSdpElement(Parameters, ParameterLength, &Type, &PatternOffset, &PatternLength);
    if (!Size || Type != SDP_TYPE_SEQUENCE || !PatternLength || Size + 3 > ParameterLength)
    {
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_SYNTAX);
        return;
    }
    MaxCount = (USHORT)((Parameters[Size] << 8) | Parameters[Size + 1]);
    Continuation = Parameters + Size + 2;
    if (!MaxCount || Size + 3 + Continuation[0] > ParameterLength ||
        (Continuation[0] && Continuation[0] != 4))
    {
        BthSdpServerError(Dev, Channel, Data + 1, Continuation[0] ? SDP_ERROR_INVALID_CONTINUATION :
                                                                    SDP_ERROR_INVALID_SYNTAX);
        return;
    }
    if (Continuation[0])
        Start = ((ULONG)Continuation[1] << 24) | (Continuation[2] << 16) | (Continuation[3] << 8) | Continuation[4];
    Capacity = ((ULONG)min(max(Channel->RemoteMtu, 48), SDP_MAX_RESPONSE) - 5 - 4 - 5) / 4;
    Output = ExAllocatePoolWithTag(NonPagedPool, 4 + Capacity * 4 + 5, BTHMINI_TAG);
    if (!Output)
    {
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INSUFFICIENT_RESOURCES);
        return;
    }
    for (Entry = Dev->SdpRecords.Flink; Entry != &Dev->SdpRecords; Entry = Entry->Flink)
    {
        PBTHMINI_SDP_RECORD Record = CONTAINING_RECORD(Entry, BTHMINI_SDP_RECORD, Link);

        if (!BthSdpRecordMatches(Record, Parameters + PatternOffset, PatternLength))
            continue;
        if (Total < MaxCount && Total >= Start && Current < Capacity)
        {
            Output[Used] = (UCHAR)(Record->Handle >> 24);
            Output[Used + 1] = (UCHAR)(Record->Handle >> 16);
            Output[Used + 2] = (UCHAR)(Record->Handle >> 8);
            Output[Used + 3] = (UCHAR)Record->Handle;
            Used += 4;
            Current++;
        }
        Total++;
    }
    Total = min(Total, MaxCount);
    if (Start > Total)
    {
        ExFreePoolWithTag(Output, BTHMINI_TAG);
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_CONTINUATION);
        return;
    }
    Output[0] = (UCHAR)(Total >> 8);
    Output[1] = (UCHAR)Total;
    Output[2] = (UCHAR)(Current >> 8);
    Output[3] = (UCHAR)Current;
    if (Start + Current < Total)
    {
        ULONG Next = Start + Current;

        Output[Used++] = 4;
        Output[Used++] = (UCHAR)(Next >> 24);
        Output[Used++] = (UCHAR)(Next >> 16);
        Output[Used++] = (UCHAR)(Next >> 8);
        Output[Used++] = (UCHAR)Next;
    }
    else
    {
        Output[Used++] = 0;
    }
    BthSdpServerSend(Dev, Channel, SDP_PDU_SERVICE_SEARCH_RESPONSE, Data + 1, Output, Used);
    ExFreePoolWithTag(Output, BTHMINI_TAG);
}

static
VOID
BthSdpServerReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    if (Length < 5)
        return;
    if (5 + (ULONG)((Data[3] << 8) | Data[4]) != Length)
    {
        BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_PDU_SIZE);
        return;
    }
    switch (Data[0])
    {
        case SDP_PDU_SERVICE_SEARCH_REQUEST:
            BthSdpServerSearch(Dev, Channel, Data, Length);
            break;

        case SDP_PDU_SERVICE_ATTRIBUTE_REQUEST:
        case SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST:
            BthSdpServerAttributes(Dev, Channel, Data, Length);
            break;

        default:
            BthSdpServerError(Dev, Channel, Data + 1, SDP_ERROR_INVALID_SYNTAX);
            break;
    }
}

static
VOID
BthSdpServerClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SDP_SESSION Session = Channel->Context;

    UNREFERENCED_PARAMETER(Dev);
    UNREFERENCED_PARAMETER(Status);
    if (!Session)
        return;
    Channel->Context = NULL;
    BthSdpSessionReset(Session);
    ExFreePoolWithTag(Session, BTHMINI_TAG);
}

static const BTHMINI_CHANNEL_CALLBACKS BthSdpServer =
{
    NULL,
    BthSdpServerReceive,
    BthSdpServerClosed
};

const BTHMINI_CHANNEL_CALLBACKS *
BthSdpServerCallbacks(VOID)
{
    return &BthSdpServer;
}
