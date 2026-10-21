/*
 * PROJECT:     LiberNT Hyper-V Network Adapter Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NVSP and RNDIS protocol over the VMBus channel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "netvsc.h"

#define NDEBUG
#include <debug.h>

static
VOID
NvscPut32(
    _Out_writes_bytes_(Offset + sizeof(ULONG)) PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory(Buffer + Offset, &Value, sizeof(Value));
}

static
ULONG
NvscGet32(
    _In_reads_bytes_(Offset + sizeof(ULONG)) const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static
PVOID
NvscAllocatePages(
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    PVOID Buffer;

    Lowest.QuadPart = 0;
    Highest.QuadPart = -1;
    Boundary.QuadPart = 0;
    Buffer = MmAllocateContiguousMemorySpecifyCache(Size, Lowest, Highest, Boundary, MmCached);
    if (Buffer != NULL)
        RtlZeroMemory(Buffer, Size);

    return Buffer;
}

static
NDIS_STATUS
NvscSendNvsp(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(NVSP_MESSAGE_SIZE) const UCHAR *Message,
    _In_ ULONG64 TransactionId,
    _In_ BOOLEAN CompletionRequested)
{
    NTSTATUS Status;

    Status = VmbChannelSendPacket(Adapter->Channel,
                                  VMBUS_PACKET_TYPE_INBAND,
                                  CompletionRequested ? VMBUS_PACKET_FLAG_COMPLETION_REQUESTED : 0,
                                  TransactionId,
                                  Message,
                                  NVSP_MESSAGE_SIZE);
    return NT_SUCCESS(Status) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static
NDIS_STATUS
NvscNvspRequest(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(NVSP_MESSAGE_SIZE) const UCHAR *Message,
    _In_ ULONG ResponseType)
{
    ULONG64 TransactionId = NETVSC_TID_CONTROL | NvscGet32(Message, 0);
    NDIS_STATUS Status;

    NdisResetEvent(&Adapter->NvspEvent);
    Adapter->NvspExpectedTid = TransactionId;
    KeMemoryBarrier();

    Status = NvscSendNvsp(Adapter, Message, TransactionId, TRUE);
    if (Status == NDIS_STATUS_SUCCESS && !NdisWaitEvent(&Adapter->NvspEvent, NETVSC_WAIT_MS))
    {
        DPRINT1("NETVSC: no completion for NVSP message %lu\n", NvscGet32(Message, 0));
        Status = NDIS_STATUS_FAILURE;
    }

    Adapter->NvspExpectedTid = 0;

    if (Status == NDIS_STATUS_SUCCESS && NvscGet32(Adapter->NvspResponse, 0) != ResponseType)
        Status = NDIS_STATUS_FAILURE;

    return Status;
}

static
ULONG
NvscAllocateSection(
    _Inout_ PNETVSC_ADAPTER Adapter)
{
    ULONG Index;

    for (Index = 0; Index < Adapter->SendSectionCount; Index++)
    {
        if ((Adapter->SendSectionBitmap[Index / 32] & (1UL << (Index % 32))) == 0)
        {
            Adapter->SendSectionBitmap[Index / 32] |= 1UL << (Index % 32);
            Adapter->SendSectionsUsed++;
            return Index;
        }
    }

    return MAXULONG;
}

static
VOID
NvscFreeSection(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG Index)
{
    if (Index < Adapter->SendSectionCount &&
        (Adapter->SendSectionBitmap[Index / 32] & (1UL << (Index % 32))) != 0)
    {
        Adapter->SendSectionBitmap[Index / 32] &= ~(1UL << (Index % 32));
        Adapter->SendSectionsUsed--;
    }
}

static
NDIS_STATUS
NvscSendSection(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG Index,
    _In_ ULONG Length,
    _In_ ULONG ChannelType)
{
    UCHAR Message[NVSP_MESSAGE_SIZE];

    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, NVSP_MSG1_SEND_RNDIS_PACKET);
    NvscPut32(Message, 4, ChannelType);
    NvscPut32(Message, 8, Index);
    NvscPut32(Message, 12, Length);

    return NvscSendNvsp(Adapter, Message, NETVSC_TID_SECTION | Index, TRUE);
}

static
NDIS_STATUS
NvscSendRndisMessage(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(Length) const VOID *Message,
    _In_ ULONG Length)
{
    NDIS_STATUS Status;
    ULONG Index;

    if (Length > Adapter->SendSectionSize)
        return NDIS_STATUS_INVALID_LENGTH;

    NdisAcquireSpinLock(&Adapter->Lock);
    Index = NvscAllocateSection(Adapter);
    NdisReleaseSpinLock(&Adapter->Lock);
    if (Index == MAXULONG)
        return NDIS_STATUS_RESOURCES;

    RtlCopyMemory(Adapter->SendBuffer + Index * Adapter->SendSectionSize, Message, Length);
    Status = NvscSendSection(Adapter, Index, Length, NVSP_CHANNEL_CONTROL);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        NvscFreeSection(Adapter, Index);
        NdisReleaseSpinLock(&Adapter->Lock);
    }

    return Status;
}

static
NDIS_STATUS
NvscRndisRequest(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _Inout_updates_bytes_(Length) PUCHAR Message,
    _In_ ULONG Length,
    _In_ ULONG CompleteType)
{
    ULONG RequestId = (ULONG)InterlockedIncrement((PLONG)&Adapter->RndisRequestId);
    NDIS_STATUS Status;

    NvscPut32(Message, 8, RequestId);
    NdisResetEvent(&Adapter->RndisEvent);
    Adapter->RndisResponseLength = 0;
    Adapter->RndisPendingId = RequestId;
    KeMemoryBarrier();

    Status = NvscSendRndisMessage(Adapter, Message, Length);
    if (Status == NDIS_STATUS_SUCCESS && !NdisWaitEvent(&Adapter->RndisEvent, NETVSC_WAIT_MS))
    {
        DPRINT1("NETVSC: no RNDIS response to message 0x%lx\n", NvscGet32(Message, 0));
        Status = NDIS_STATUS_FAILURE;
    }

    Adapter->RndisPendingId = 0;

    if (Status == NDIS_STATUS_SUCCESS &&
        (Adapter->RndisResponseLength < 16 ||
         NvscGet32(Adapter->RndisResponse, 0) != CompleteType ||
         NvscGet32(Adapter->RndisResponse, 12) != RNDIS_STATUS_SUCCESS))
    {
        Status = NDIS_STATUS_FAILURE;
    }

    return Status;
}

static
NDIS_STATUS
NvscRndisQuery(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG Oid,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    UCHAR Message[28];
    ULONG InfoLength;
    ULONG InfoOffset;
    NDIS_STATUS Status;

    *Returned = 0;
    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, RNDIS_MSG_QUERY);
    NvscPut32(Message, 4, sizeof(Message));
    NvscPut32(Message, 12, Oid);
    NvscPut32(Message, 20, 20);

    Status = NvscRndisRequest(Adapter, Message, sizeof(Message), RNDIS_MSG_QUERY_CMPLT);
    if (Status != NDIS_STATUS_SUCCESS || Adapter->RndisResponseLength < 24)
        return NDIS_STATUS_FAILURE;

    InfoLength = NvscGet32(Adapter->RndisResponse, 16);
    InfoOffset = NvscGet32(Adapter->RndisResponse, 20);
    if (InfoOffset > Adapter->RndisResponseLength || InfoLength > Adapter->RndisResponseLength - 8 - InfoOffset)
        return NDIS_STATUS_FAILURE;

    *Returned = min(InfoLength, Length);
    RtlCopyMemory(Data, Adapter->RndisResponse + 8 + InfoOffset, *Returned);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NvscRndisSetPacketFilter(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG Filter,
    _In_opt_ PNDIS_OID_REQUEST OidRequest)
{
    UCHAR Message[32];
    ULONG RequestId;
    NDIS_STATUS Status;

    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, RNDIS_MSG_SET);
    NvscPut32(Message, 4, sizeof(Message));
    NvscPut32(Message, 12, OID_GEN_CURRENT_PACKET_FILTER);
    NvscPut32(Message, 16, sizeof(ULONG));
    NvscPut32(Message, 20, 20);
    NvscPut32(Message, 28, Filter);

    if (OidRequest == NULL)
        return NvscRndisRequest(Adapter, Message, sizeof(Message), RNDIS_MSG_SET_CMPLT);

    RequestId = (ULONG)InterlockedIncrement((PLONG)&Adapter->RndisRequestId);
    NvscPut32(Message, 8, RequestId);
    Adapter->PendingOidRequest = OidRequest;
    Adapter->RndisPendingId = RequestId;
    KeMemoryBarrier();

    Status = NvscSendRndisMessage(Adapter, Message, sizeof(Message));
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Adapter->RndisPendingId = 0;
        Adapter->PendingOidRequest = NULL;
        return Status;
    }

    return NDIS_STATUS_PENDING;
}

static
VOID
NvscRndisComplete(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    PNDIS_OID_REQUEST OidRequest;
    ULONG RequestId;

    if (Length < 16)
        return;

    RequestId = NvscGet32(Message, 8);
    if (RequestId == 0 || RequestId != Adapter->RndisPendingId)
        return;

    Adapter->RndisPendingId = 0;
    OidRequest = Adapter->PendingOidRequest;
    if (OidRequest != NULL)
    {
        Adapter->PendingOidRequest = NULL;
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle,
                                OidRequest,
                                NvscGet32(Message, 12) == RNDIS_STATUS_SUCCESS ? NDIS_STATUS_SUCCESS
                                                                               : NDIS_STATUS_FAILURE);
        return;
    }

    Adapter->RndisResponseLength = min(Length, (ULONG)RNDIS_RESPONSE_SIZE);
    RtlCopyMemory(Adapter->RndisResponse, Message, Adapter->RndisResponseLength);
    NdisSetEvent(&Adapter->RndisEvent);
}

static
VOID
NvscIndicateFrame(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _Inout_ PULONG Count)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;

    NdisDprAcquireSpinLock(&Adapter->Lock);

    if (Length < NETVSC_ETH_HEADER_SIZE || Length > NETVSC_FRAME_BUFFER_SIZE)
    {
        Adapter->ReceiveError++;
        NdisDprReleaseSpinLock(&Adapter->Lock);
        return;
    }

    if (Adapter->Paused || Adapter->FreeReceiveNbls == NULL)
    {
        Adapter->ReceiveNoBuffer++;
        NdisDprReleaseSpinLock(&Adapter->Lock);
        return;
    }

    Nbl = Adapter->FreeReceiveNbls;
    Adapter->FreeReceiveNbls = NET_BUFFER_LIST_NEXT_NBL(Nbl);
    Adapter->ReceiveNblsOutstanding++;
    Adapter->ReceiveOk++;
    NdisDprReleaseSpinLock(&Adapter->Lock);

    NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    RtlCopyMemory(Nbl->MiniportReserved[0], Frame, Length);
    NdisAdjustMdlLength(NET_BUFFER_FIRST_MDL(NetBuffer), Length);
    NET_BUFFER_DATA_OFFSET(NetBuffer) = 0;
    NET_BUFFER_CURRENT_MDL(NetBuffer) = NET_BUFFER_FIRST_MDL(NetBuffer);
    NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) = 0;
    NET_BUFFER_DATA_LENGTH(NetBuffer) = Length;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

    if (*Tail != NULL)
        NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
    else
        *Head = Nbl;
    *Tail = Nbl;
    (*Count)++;
}

static
VOID
NvscProcessRndis(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length,
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _Inout_ PULONG Count)
{
    UCHAR Reply[16];
    ULONG Offset = 0;
    ULONG Type;
    ULONG MessageLength;
    ULONG DataOffset;
    ULONG DataLength;
    ULONG Status;

    while (Length - Offset >= 8)
    {
        Type = NvscGet32(Message, Offset);
        MessageLength = NvscGet32(Message, Offset + 4);
        if (MessageLength < 8 || MessageLength > Length - Offset)
            break;

        switch (Type)
        {
            case RNDIS_MSG_PACKET:
                if (MessageLength < 16)
                    break;
                DataOffset = NvscGet32(Message, Offset + 8);
                DataLength = NvscGet32(Message, Offset + 12);
                if (DataOffset <= MessageLength - 8 && DataLength <= MessageLength - 8 - DataOffset)
                    NvscIndicateFrame(Adapter, Message + Offset + 8 + DataOffset, DataLength, Head, Tail, Count);
                break;

            case RNDIS_MSG_INITIALIZE_CMPLT:
            case RNDIS_MSG_QUERY_CMPLT:
            case RNDIS_MSG_SET_CMPLT:
            case RNDIS_MSG_KEEPALIVE_CMPLT:
                NvscRndisComplete(Adapter, Message + Offset, MessageLength);
                break;

            case RNDIS_MSG_INDICATE_STATUS:
                if (MessageLength < 12)
                    break;
                Status = NvscGet32(Message, Offset + 8);
                if (Status == RNDIS_STATUS_MEDIA_CONNECT || Status == RNDIS_STATUS_MEDIA_DISCONNECT)
                {
                    NdisDprAcquireSpinLock(&Adapter->Lock);
                    Adapter->MediaState = (Status == RNDIS_STATUS_MEDIA_CONNECT) ? NdisMediaStateConnected
                                                                                 : NdisMediaStateDisconnected;
                    Adapter->LinkChange = TRUE;
                    NdisDprReleaseSpinLock(&Adapter->Lock);
                }
                break;

            case RNDIS_MSG_KEEPALIVE:
                if (MessageLength < 12)
                    break;
                NvscPut32(Reply, 0, RNDIS_MSG_KEEPALIVE_CMPLT);
                NvscPut32(Reply, 4, sizeof(Reply));
                NvscPut32(Reply, 8, NvscGet32(Message, Offset + 8));
                NvscPut32(Reply, 12, RNDIS_STATUS_SUCCESS);
                NvscSendRndisMessage(Adapter, Reply, sizeof(Reply));
                break;

            default:
                break;
        }

        Offset += MessageLength;
    }
}

static
VOID
NvscHandleTransferPages(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG PacketLength,
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _Inout_ PULONG Count)
{
    PVMBUS_PACKET_DESCRIPTOR Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Adapter->PacketBuffer;
    PVMBUS_TRANSFER_PAGE_HEADER Header = (PVMBUS_TRANSFER_PAGE_HEADER)(Descriptor + 1);
    PVMBUS_TRANSFER_PAGE_RANGE Ranges = (PVMBUS_TRANSFER_PAGE_RANGE)(Header + 1);
    ULONG DataOffset = (ULONG)Descriptor->DataOffset8 * 8;
    ULONG64 TransactionId = Descriptor->TransactionId;
    UCHAR Done[8];
    ULONG Index;
    ULONG Attempt;

    if (DataOffset + sizeof(ULONG) <= PacketLength &&
        sizeof(VMBUS_PACKET_DESCRIPTOR) + sizeof(VMBUS_TRANSFER_PAGE_HEADER) +
            (ULONG64)Header->RangeCount * sizeof(VMBUS_TRANSFER_PAGE_RANGE) <= DataOffset &&
        NvscGet32(Adapter->PacketBuffer, DataOffset) == NVSP_MSG1_SEND_RNDIS_PACKET)
    {
        for (Index = 0; Index < Header->RangeCount; Index++)
        {
            if (Ranges[Index].ByteOffset < NETVSC_RECEIVE_BUFFER_SIZE &&
                Ranges[Index].ByteCount <= NETVSC_RECEIVE_BUFFER_SIZE - Ranges[Index].ByteOffset)
            {
                NvscProcessRndis(Adapter,
                                 Adapter->ReceiveBuffer + Ranges[Index].ByteOffset,
                                 Ranges[Index].ByteCount,
                                 Head, Tail, Count);
            }
        }
    }

    NvscPut32(Done, 0, NVSP_MSG1_SEND_RNDIS_PACKET_DONE);
    NvscPut32(Done, 4, NVSP_STATUS_SUCCESS);
    for (Attempt = 0; Attempt < 1000; Attempt++)
    {
        if (VmbChannelSendPacket(Adapter->Channel, VMBUS_PACKET_TYPE_COMPLETION, 0,
                                 TransactionId, Done, sizeof(Done)) != STATUS_INSUFFICIENT_RESOURCES)
        {
            break;
        }
        KeStallExecutionProcessor(10);
    }
}

static
VOID
NvscHandleCompletion(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG64 TransactionId,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    if (TransactionId & NETVSC_TID_SECTION)
    {
        NdisDprAcquireSpinLock(&Adapter->Lock);
        NvscFreeSection(Adapter, (ULONG)TransactionId);
        NdisDprReleaseSpinLock(&Adapter->Lock);
        return;
    }

    if (TransactionId != 0 && TransactionId == Adapter->NvspExpectedTid)
    {
        RtlZeroMemory(Adapter->NvspResponse, sizeof(Adapter->NvspResponse));
        RtlCopyMemory(Adapter->NvspResponse, Payload, min(Length, (ULONG)sizeof(Adapter->NvspResponse)));
        Adapter->NvspExpectedTid = 0;
        NdisSetEvent(&Adapter->NvspEvent);
    }
}

static
VOID
NTAPI
NvscChannelCallback(
    _In_opt_ PVOID Context)
{
    PNETVSC_ADAPTER Adapter = Context;
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    PNET_BUFFER_LIST ReceiveHead = NULL, ReceiveTail = NULL;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    ULONG ReceiveCount = 0;
    ULONG Length;
    ULONG Offset;
    ULONG Zero = 0;
    BOOLEAN LinkChange;
    NTSTATUS Status;

    if (Adapter == NULL)
        return;

    for (;;)
    {
        Status = VmbChannelReceivePacket(Adapter->Channel, Adapter->PacketBuffer,
                                         NETVSC_PACKET_BUFFER_SIZE, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NETVSC: receive failed with 0x%lx (length %lu)\n", Status, Length);
            break;
        }

        Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Adapter->PacketBuffer;
        Offset = (ULONG)Descriptor->DataOffset8 * 8;
        if (Offset < sizeof(VMBUS_PACKET_DESCRIPTOR) || Offset > Length)
            continue;

        switch (Descriptor->Type)
        {
            case VMBUS_PACKET_TYPE_COMPLETION:
                NvscHandleCompletion(Adapter, Descriptor->TransactionId,
                                     Adapter->PacketBuffer + Offset, Length - Offset);
                break;

            case VMBUS_PACKET_TYPE_TRANSFER_PAGES:
                NvscHandleTransferPages(Adapter, Length, &ReceiveHead, &ReceiveTail, &ReceiveCount);
                break;

            case VMBUS_PACKET_TYPE_INBAND:
                if (Descriptor->Flags & VMBUS_PACKET_FLAG_COMPLETION_REQUESTED)
                {
                    VmbChannelSendPacket(Adapter->Channel, VMBUS_PACKET_TYPE_COMPLETION, 0,
                                         Descriptor->TransactionId, &Zero, sizeof(Zero));
                }
                break;

            default:
                break;
        }
    }

    NdisDprAcquireSpinLock(&Adapter->Lock);
    NvscProcessSendQueue(Adapter, &CompleteHead, &CompleteTail);
    LinkChange = Adapter->LinkChange && Adapter->Ready;
    Adapter->LinkChange = FALSE;
    NdisDprReleaseSpinLock(&Adapter->Lock);

    NvscCompleteSends(Adapter, CompleteHead, TRUE);

    if (ReceiveHead != NULL)
    {
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                           ReceiveHead,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           ReceiveCount,
                                           NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    }

    if (LinkChange)
        NvscIndicateLinkState(Adapter);
}

static
BOOLEAN
NvscCopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(Capacity) PUCHAR Destination,
    _In_ ULONG Capacity,
    _Out_ PULONG Copied)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Total = 0;
    PUCHAR Source;
    ULONG Length;

    *Copied = 0;
    if (Remaining == 0 || Remaining > Capacity)
        return FALSE;

    while (Mdl != NULL && Remaining != 0)
    {
        NdisQueryMdl(Mdl, (PVOID *)&Source, &Length, NormalPagePriority);
        if (Source == NULL)
            return FALSE;

        if (Length <= Offset)
        {
            Offset -= Length;
            NdisGetNextMdl(Mdl, &Mdl);
            continue;
        }

        Length = min(Length - Offset, Remaining);
        RtlCopyMemory(Destination + Total, Source + Offset, Length);
        Total += Length;
        Remaining -= Length;
        Offset = 0;
        NdisGetNextMdl(Mdl, &Mdl);
    }

    if (Remaining != 0)
        return FALSE;

    *Copied = Total;
    return TRUE;
}

VOID
NvscProcessSendQueue(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST *CompleteHead,
    _Inout_ PNET_BUFFER_LIST *CompleteTail)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    PUCHAR Section;
    ULONG Index;
    ULONG Length;

    if (!Adapter->Ready)
        return;

    while ((Nbl = Adapter->SendQueueHead) != NULL)
    {
        NetBuffer = (PNET_BUFFER)Nbl->MiniportReserved[0];

        while (NetBuffer != NULL)
        {
            Index = NvscAllocateSection(Adapter);
            if (Index == MAXULONG)
                return;

            Section = Adapter->SendBuffer + Index * Adapter->SendSectionSize;
            if (!NvscCopyNetBuffer(NetBuffer, Section + RNDIS_PACKET_HEADER_SIZE,
                                   Adapter->SendSectionSize - RNDIS_PACKET_HEADER_SIZE, &Length))
            {
                NvscFreeSection(Adapter, Index);
                NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_INVALID_LENGTH;
                Adapter->TransmitError++;
            }
            else
            {
                RtlZeroMemory(Section, RNDIS_PACKET_HEADER_SIZE);
                NvscPut32(Section, 0, RNDIS_MSG_PACKET);
                NvscPut32(Section, 4, RNDIS_PACKET_HEADER_SIZE + Length);
                NvscPut32(Section, 8, RNDIS_PACKET_HEADER_SIZE - 8);
                NvscPut32(Section, 12, Length);

                if (NvscSendSection(Adapter, Index, RNDIS_PACKET_HEADER_SIZE + Length, NVSP_CHANNEL_DATA) !=
                    NDIS_STATUS_SUCCESS)
                {
                    NvscFreeSection(Adapter, Index);
                    return;
                }

                Adapter->TransmitOk++;
            }

            NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
            Nbl->MiniportReserved[0] = NetBuffer;
        }

        Adapter->SendQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (Adapter->SendQueueHead == NULL)
            Adapter->SendQueueTail = NULL;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        if (*CompleteTail != NULL)
            NET_BUFFER_LIST_NEXT_NBL(*CompleteTail) = Nbl;
        else
            *CompleteHead = Nbl;
        *CompleteTail = Nbl;
    }
}

NDIS_STATUS
NvscStartDevice(
    _Inout_ PNETVSC_ADAPTER Adapter)
{
    static const ULONG Versions[] = { NVSP_VERSION_5, NVSP_VERSION_4, NVSP_VERSION_2, NVSP_VERSION_1 };
    UCHAR Message[NVSP_MESSAGE_SIZE];
    UCHAR Rndis[24];
    ULONG Value;
    ULONG Returned;
    ULONG Index;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;

    Adapter->PacketBuffer = NvscAllocatePages(NETVSC_PACKET_BUFFER_SIZE);
    Adapter->ReceiveBuffer = NvscAllocatePages(NETVSC_RECEIVE_BUFFER_SIZE);
    Adapter->SendBuffer = NvscAllocatePages(NETVSC_SEND_BUFFER_SIZE);
    if (Adapter->PacketBuffer == NULL || Adapter->ReceiveBuffer == NULL || Adapter->SendBuffer == NULL)
        return NDIS_STATUS_RESOURCES;

    NtStatus = VmbChannelOpen(Adapter->Channel, NETVSC_RING_SIZE, NvscChannelCallback, Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("NETVSC: VmbChannelOpen failed with 0x%lx\n", NtStatus);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    Adapter->ChannelOpen = TRUE;

    for (Index = 0; Index < RTL_NUMBER_OF(Versions); Index++)
    {
        RtlZeroMemory(Message, sizeof(Message));
        NvscPut32(Message, 0, NVSP_MSG_INIT);
        NvscPut32(Message, 4, Versions[Index]);
        NvscPut32(Message, 8, Versions[Index]);
        if (NvscNvspRequest(Adapter, Message, NVSP_MSG_INIT_COMPLETE) == NDIS_STATUS_SUCCESS &&
            NvscGet32(Adapter->NvspResponse, 12) == NVSP_STATUS_SUCCESS)
        {
            Adapter->NvspVersion = Versions[Index];
            break;
        }
    }

    if (Adapter->NvspVersion == 0)
    {
        DPRINT1("NETVSC: host accepted no NVSP version\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    if (Adapter->NvspVersion >= NVSP_VERSION_2)
    {
        RtlZeroMemory(Message, sizeof(Message));
        NvscPut32(Message, 0, NVSP_MSG2_SEND_NDIS_CONFIG);
        NvscPut32(Message, 4, NETVSC_MAX_FRAME_SIZE);
        NvscSendNvsp(Adapter, Message, NETVSC_TID_CONTROL | NVSP_MSG2_SEND_NDIS_CONFIG, FALSE);
    }

    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, NVSP_MSG1_SEND_NDIS_VERSION);
    NvscPut32(Message, 4, 6);
    NvscPut32(Message, 8, Adapter->NvspVersion >= NVSP_VERSION_5 ? 30 : 1);
    NvscSendNvsp(Adapter, Message, NETVSC_TID_CONTROL | NVSP_MSG1_SEND_NDIS_VERSION, FALSE);

    NtStatus = VmbChannelCreateGpadl(Adapter->Channel, Adapter->ReceiveBuffer,
                                     NETVSC_RECEIVE_BUFFER_SIZE, &Adapter->ReceiveGpadl);
    if (!NT_SUCCESS(NtStatus))
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, NVSP_MSG1_SEND_RECEIVE_BUFFER);
    NvscPut32(Message, 4, Adapter->ReceiveGpadl);
    NvscPut32(Message, 8, NETVSC_RECEIVE_BUFFER_ID);
    Status = NvscNvspRequest(Adapter, Message, NVSP_MSG1_SEND_RECEIVE_BUFFER_DONE);
    if (Status != NDIS_STATUS_SUCCESS || NvscGet32(Adapter->NvspResponse, 4) != NVSP_STATUS_SUCCESS)
    {
        DPRINT1("NETVSC: receive buffer refused\n");
        return NDIS_STATUS_RESOURCES;
    }
    Adapter->ReceiveBufferPosted = TRUE;

    NtStatus = VmbChannelCreateGpadl(Adapter->Channel, Adapter->SendBuffer,
                                     NETVSC_SEND_BUFFER_SIZE, &Adapter->SendGpadl);
    if (!NT_SUCCESS(NtStatus))
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Message, sizeof(Message));
    NvscPut32(Message, 0, NVSP_MSG1_SEND_SEND_BUFFER);
    NvscPut32(Message, 4, Adapter->SendGpadl);
    NvscPut32(Message, 8, NETVSC_SEND_BUFFER_ID);
    Status = NvscNvspRequest(Adapter, Message, NVSP_MSG1_SEND_SEND_BUFFER_DONE);
    if (Status != NDIS_STATUS_SUCCESS || NvscGet32(Adapter->NvspResponse, 4) != NVSP_STATUS_SUCCESS)
    {
        DPRINT1("NETVSC: send buffer refused\n");
        return NDIS_STATUS_RESOURCES;
    }
    Adapter->SendBufferPosted = TRUE;

    Adapter->SendSectionSize = NvscGet32(Adapter->NvspResponse, 8);
    if (Adapter->SendSectionSize < RNDIS_PACKET_HEADER_SIZE + NETVSC_MAX_FRAME_SIZE ||
        Adapter->SendSectionSize > NETVSC_SEND_BUFFER_SIZE)
    {
        DPRINT1("NETVSC: unusable send section size %lu\n", Adapter->SendSectionSize);
        return NDIS_STATUS_RESOURCES;
    }
    Adapter->SendSectionCount = min(NETVSC_SEND_BUFFER_SIZE / Adapter->SendSectionSize,
                                    (ULONG)NETVSC_MAX_SEND_SECTIONS);

    RtlZeroMemory(Rndis, sizeof(Rndis));
    NvscPut32(Rndis, 0, RNDIS_MSG_INITIALIZE);
    NvscPut32(Rndis, 4, sizeof(Rndis));
    NvscPut32(Rndis, 12, 1);
    NvscPut32(Rndis, 16, 0);
    NvscPut32(Rndis, 20, 0x4000);
    Status = NvscRndisRequest(Adapter, Rndis, sizeof(Rndis), RNDIS_MSG_INITIALIZE_CMPLT);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NETVSC: RNDIS initialize failed\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    Status = NvscRndisQuery(Adapter, OID_802_3_PERMANENT_ADDRESS, Adapter->PermanentMacAddress,
                            ETH_LENGTH_OF_ADDRESS, &Returned);
    if (Status != NDIS_STATUS_SUCCESS || Returned != ETH_LENGTH_OF_ADDRESS)
    {
        DPRINT1("NETVSC: MAC address query failed\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    Adapter->MediaState = NdisMediaStateConnected;
    if (NvscRndisQuery(Adapter, OID_GEN_MEDIA_CONNECT_STATUS, &Value, sizeof(Value), &Returned) ==
            NDIS_STATUS_SUCCESS && Returned == sizeof(Value))
    {
        Adapter->MediaState = Value;
    }

    Adapter->LinkSpeed = 10000000000ULL;
    if (NvscRndisQuery(Adapter, OID_GEN_LINK_SPEED, &Value, sizeof(Value), &Returned) == NDIS_STATUS_SUCCESS &&
        Returned == sizeof(Value) && Value != 0)
    {
        Adapter->LinkSpeed = (ULONG64)Value * 100;
    }

    Adapter->Ready = TRUE;
    return NDIS_STATUS_SUCCESS;
}

VOID
NvscStopDevice(
    _Inout_ PNETVSC_ADAPTER Adapter)
{
    UCHAR Message[NVSP_MESSAGE_SIZE];

    Adapter->Ready = FALSE;

    if (Adapter->ChannelOpen)
    {
        if (Adapter->ReceiveBufferPosted)
        {
            RtlZeroMemory(Message, sizeof(Message));
            NvscPut32(Message, 0, NVSP_MSG1_REVOKE_RECEIVE_BUFFER);
            NvscPut32(Message, 4, NETVSC_RECEIVE_BUFFER_ID);
            NvscSendNvsp(Adapter, Message, NETVSC_TID_CONTROL | NVSP_MSG1_REVOKE_RECEIVE_BUFFER, FALSE);
            Adapter->ReceiveBufferPosted = FALSE;
        }

        if (Adapter->SendBufferPosted)
        {
            RtlZeroMemory(Message, sizeof(Message));
            NvscPut32(Message, 0, NVSP_MSG1_REVOKE_SEND_BUFFER);
            NvscPut32(Message, 4, NETVSC_SEND_BUFFER_ID);
            NvscSendNvsp(Adapter, Message, NETVSC_TID_CONTROL | NVSP_MSG1_REVOKE_SEND_BUFFER, FALSE);
            Adapter->SendBufferPosted = FALSE;
        }

        VmbChannelClose(Adapter->Channel);
        Adapter->ChannelOpen = FALSE;
    }

    if (Adapter->ReceiveGpadl != 0)
    {
        VmbChannelDestroyGpadl(Adapter->Channel, Adapter->ReceiveGpadl);
        Adapter->ReceiveGpadl = 0;
    }

    if (Adapter->SendGpadl != 0)
    {
        VmbChannelDestroyGpadl(Adapter->Channel, Adapter->SendGpadl);
        Adapter->SendGpadl = 0;
    }

    if (Adapter->ReceiveBuffer != NULL)
    {
        MmFreeContiguousMemory(Adapter->ReceiveBuffer);
        Adapter->ReceiveBuffer = NULL;
    }

    if (Adapter->SendBuffer != NULL)
    {
        MmFreeContiguousMemory(Adapter->SendBuffer);
        Adapter->SendBuffer = NULL;
    }

    if (Adapter->PacketBuffer != NULL)
    {
        MmFreeContiguousMemory(Adapter->PacketBuffer);
        Adapter->PacketBuffer = NULL;
    }
}
