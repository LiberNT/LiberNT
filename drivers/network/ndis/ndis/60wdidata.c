/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI host framework: TX/RX data path over the IHV target adaptation layer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_wdi.h"

#define NDIS6_WDI_DRAIN_TIMEOUT_MS  5000
#define NDIS6_WDI_TX_ROUNDS         64
#define NDIS6_WDI_RX_ROUNDS         64
#define NDIS6_WDI_FRAME_PEEK        34

typedef struct _NDIS6_WDI_TX_DONE
{
    PWDI_FRAME_METADATA Metadata;
    BOOLEAN Injected;
    WDI_TX_FRAME_STATUS FrameStatus;
    NDIS_STATUS Status;
} NDIS6_WDI_TX_DONE, *PNDIS6_WDI_TX_DONE;

static
BOOLEAN
Ndis6WdiPortQueueing(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    return Wdi->TxRxCaps.TransmitCapabilities.TargetPriorityQueueing != FALSE;
}

static
ULONG
Ndis6WdiCurrentSendFlags(VOID)
{
    return KeGetCurrentIrql() == DISPATCH_LEVEL ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
}

static
VOID
Ndis6WdiTxReference(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    if (InterlockedIncrement(&Wdi->TxOutstanding) == 1)
        KeClearEvent(&Wdi->TxIdleEvent);
}

static
VOID
Ndis6WdiTxDereference(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    if (InterlockedDecrement(&Wdi->TxOutstanding) == 0)
        KeSetEvent(&Wdi->TxIdleEvent, IO_NO_INCREMENT, FALSE);
}

static
PWDI_FRAME_METADATA
NTAPI
Ndis6WdiAllocateMetadata(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PWDI_FRAME_METADATA Metadata;

    if (Wdi == NULL || !Wdi->LookasideReady)
        return NULL;

    Metadata = ExAllocateFromNPagedLookasideList(&Wdi->MetadataLookaside);
    if (Metadata != NULL)
        RtlZeroMemory(Metadata, Wdi->MetadataSize);
    return Metadata;
}

static
VOID
NTAPI
Ndis6WdiFreeMetadata(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ PWDI_FRAME_METADATA Metadata)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;

    if (Wdi != NULL && Metadata != NULL && Wdi->LookasideReady)
        ExFreeToNPagedLookasideList(&Wdi->MetadataLookaside, Metadata);
}

static
PNDIS6_WDI_TX_FRAME
Ndis6WdiSlot(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ WDI_FRAME_ID FrameId)
{
    PNDIS6_WDI_TX_FRAME Frame = &Wdi->TxFrames[FrameId & (NDIS6_WDI_TX_SLOTS - 1)];

    if (Frame->Metadata == NULL || Frame->Metadata->FrameID != FrameId)
        return NULL;
    return Frame;
}

static
BOOLEAN
Ndis6WdiAllocateSlot(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PWDI_FRAME_METADATA Metadata,
    _In_ BOOLEAN Injected)
{
    ULONG Index;
    ULONG i;
    UCHAR Generation;

    for (i = 0; i < NDIS6_WDI_TX_SLOTS; i++)
    {
        Index = (Wdi->TxSlotCursor + i) & (NDIS6_WDI_TX_SLOTS - 1);
        if (Wdi->TxFrames[Index].Metadata != NULL)
            continue;

        Wdi->TxSlotCursor = (Index + 1) & (NDIS6_WDI_TX_SLOTS - 1);
        Generation = (UCHAR)((Wdi->TxGeneration[Index] + 1) & ((1 << (16 - NDIS6_WDI_TX_SLOT_BITS)) - 1));
        Wdi->TxGeneration[Index] = Generation;
        Metadata->FrameID = (WDI_FRAME_ID)(((ULONG)Generation << NDIS6_WDI_TX_SLOT_BITS) | Index);

        RtlZeroMemory(&Wdi->TxFrames[Index], sizeof(Wdi->TxFrames[Index]));
        Wdi->TxFrames[Index].Metadata = Metadata;
        Wdi->TxFrames[Index].Injected = Injected;
        Wdi->TxFrames[Index].SendExpected = !Wdi->TxRxCaps.TransmitCapabilities.ExplicitSendCompleteFlagRequired;
        return TRUE;
    }

    return FALSE;
}

static
PNDIS6_WDI_PEER
Ndis6WdiPeerById(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ WDI_PEER_ID PeerId)
{
    ULONG i;

    for (i = 0; i < NDIS6_WDI_MAX_PEERS; i++)
    {
        if (Wdi->Peers[i].Valid && Wdi->Peers[i].PeerId == PeerId)
            return &Wdi->Peers[i];
    }
    return NULL;
}

BOOLEAN
Ndis6WdiDataFindPeer(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(6) const UCHAR *Address,
    _Out_ WDI_PEER_ID *PeerId)
{
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;
    ULONG i;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    for (i = 0; i < NDIS6_WDI_MAX_PEERS; i++)
    {
        if (Wdi->Peers[i].Valid && RtlEqualMemory(Wdi->Peers[i].Address, Address, sizeof(DOT11_MAC_ADDRESS)))
        {
            *PeerId = Wdi->Peers[i].PeerId;
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
    return Found;
}

static
BOOLEAN
Ndis6WdiFramePaused(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PWDI_FRAME_METADATA Metadata)
{
    PNDIS6_WDI_PEER Peer;

    if (Wdi->TxPauseReasons != 0)
        return TRUE;
    if (Ndis6WdiPortQueueing(Wdi))
        return FALSE;

    Peer = Ndis6WdiPeerById(Wdi, Metadata->u.txMetaData.PeerID);
    return Peer != NULL && Peer->PauseReasons != 0;
}

static
VOID
Ndis6WdiTxComplete(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_(Count) PNDIS6_WDI_TX_DONE Done,
    _In_ ULONG Count)
{
    PNET_BUFFER_LIST Nbl;
    KIRQL OldIrql;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Nbl = Done[i].Metadata->pNBL;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        KeAcquireSpinLock(&Wdi->TxDescLock, &OldIrql);
        Wdi->Tal.TxTargetDescDeInitHandler(Wdi->TalContext, Nbl);
        KeReleaseSpinLock(&Wdi->TxDescLock, OldIrql);

        if (Done[i].Injected)
        {
            if (Wdi->Tal.TxTalSendCompleteHandler != NULL)
                Wdi->Tal.TxTalSendCompleteHandler(Wdi->TalContext, Nbl, Done[i].FrameStatus);
        }
        else
        {
            if (Done[i].Status == NDIS_STATUS_SUCCESS)
            {
                PNET_BUFFER Nb;

                Wdi->TxOk++;
                for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
                    Wdi->TxBytes += NET_BUFFER_DATA_LENGTH(Nb);
            }
            else
            {
                Wdi->TxError++;
            }
            Nbl->MiniportReserved[0] = NULL;
            Nbl->MiniportReserved[1] = NULL;
            NET_BUFFER_LIST_STATUS(Nbl) = Done[i].Status;
            NdisMSendNetBufferListsComplete(Wdi->Adapter, Nbl, Ndis6WdiCurrentSendFlags());
        }

        Ndis6WdiFreeMetadata(Wdi, Done[i].Metadata);
        Ndis6WdiTxDereference(Wdi);
    }
}

static
NDIS_STATUS
Ndis6WdiFrameStatusToNdis(
    _In_ WDI_TX_FRAME_STATUS FrameStatus)
{
    switch (FrameStatus)
    {
        case WDI_TxFrameStatus_Ok:
            return NDIS_STATUS_SUCCESS;
        case WDI_TxFrameStatus_TransferCancelled:
        case WDI_TxFrameStatus_SendCancelled:
            return NDIS_STATUS_SEND_ABORTED;
        default:
            return NDIS_STATUS_FAILURE;
    }
}

static
VOID
Ndis6WdiTxKick(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PWDI_FRAME_METADATA Head;
    PLIST_ENTRY Entry;
    WDI_PORT_ID PortId;
    WDI_PEER_ID PeerId;
    WDI_EXTENDED_TID ExTid;
    BOOLEAN Injected;
    BOOLEAN PortQueueing = Ndis6WdiPortQueueing(Wdi);
    ULONG Before;
    ULONG Matching;
    ULONG Active;
    ULONG Rounds;
    KIRQL OldIrql;

    InterlockedExchange(&Wdi->TxKickPending, 1);
    if (InterlockedCompareExchange(&Wdi->TxSending, 1, 0) != 0)
        return;

    for (;;)
    {
        InterlockedExchange(&Wdi->TxKickPending, 0);

        for (Rounds = 0; Rounds < NDIS6_WDI_TX_ROUNDS; Rounds++)
        {
            Head = NULL;
            Matching = 0;

            KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
            if (!Wdi->DataPathRunning || !Wdi->TalStarted)
            {
                KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
                break;
            }

            for (Entry = Wdi->TxQueue.Flink; Entry != &Wdi->TxQueue; Entry = Entry->Flink)
            {
                PWDI_FRAME_METADATA Metadata = CONTAINING_RECORD(Entry, WDI_FRAME_METADATA, Linkage);

                if (Head == NULL)
                {
                    if (Ndis6WdiFramePaused(Wdi, Metadata))
                        continue;
                    Head = Metadata;
                }
                if (PortQueueing ||
                    (Metadata->u.txMetaData.PeerID == Head->u.txMetaData.PeerID &&
                     Metadata->u.txMetaData.ExTID == Head->u.txMetaData.ExTID))
                {
                    Matching++;
                }
            }

            if (Head == NULL)
            {
                KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
                break;
            }

            PortId = Head->u.txMetaData.PortID;
            PeerId = PortQueueing ? WDI_PEER_ANY : Head->u.txMetaData.PeerID;
            ExTid = PortQueueing ? WDI_EXT_TID_UNKNOWN : Head->u.txMetaData.ExTID;
            Injected = Wdi->TxFrames[Head->FrameID & (NDIS6_WDI_TX_SLOTS - 1)].Injected;
            Wdi->TxSendAll = PortQueueing;
            Wdi->TxSendPeer = Head->u.txMetaData.PeerID;
            Wdi->TxSendTid = Head->u.txMetaData.ExTID;
            Before = Wdi->TxQueueLength;
            Active = Wdi->TxQueueLength;
            KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

            if (Injected && Head->u.txMetaData.ExTID >= 17 && Head->u.txMetaData.ExTID <= 24 &&
                Wdi->Tal.TxTalSendHandler != NULL)
            {
                Wdi->Tal.TxTalSendHandler(Wdi->TalContext, PortId, PeerId, ExTid, (UINT16)min(Matching, MAXUSHORT), Active, FALSE);
            }
            else
            {
                Wdi->Tal.TxDataSendHandler(Wdi->TalContext, PortId, PeerId, ExTid, (UINT16)min(Matching, MAXUSHORT), Active, FALSE);
            }

            KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
            Active = Wdi->TxQueueLength;
            KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
            if (Active >= Before)
                break;
        }

        InterlockedExchange(&Wdi->TxSending, 0);
        if (InterlockedCompareExchange(&Wdi->TxKickPending, 0, 0) == 0)
            break;
        if (InterlockedCompareExchange(&Wdi->TxSending, 1, 0) != 0)
            break;
    }
}

static
VOID
Ndis6WdiClassifyFrame(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNET_BUFFER_LIST Nbl,
    _Inout_ PWDI_TX_METADATA Tx)
{
    UCHAR Storage[NDIS6_WDI_FRAME_PEEK];
    PNET_BUFFER Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    PDOT11_EXTSTA_SEND_CONTEXT SendContext;
    PUCHAR Frame = NULL;
    ULONG Length;
    ULONG HeaderLength = 24;
    BOOLEAN Qos = FALSE;

    Tx->PortID = Wdi->PortId;
    Tx->PeerID = WDI_PEER_ANY;
    Tx->ExTID = Ndis6WdiPortQueueing(Wdi) ? WDI_EXT_TID_UNKNOWN : WDI_EXT_TID_NON_QOS;
    Tx->IsUnicast = TRUE;

    if (Nb != NULL)
    {
        Length = min(NET_BUFFER_DATA_LENGTH(Nb), sizeof(Storage));
        if (Length >= 24)
            Frame = NdisGetDataBuffer(Nb, Length, Storage, 1, 0);
    }

    if (Frame != NULL)
    {
        Tx->IsUnicast = (Frame[4] & 0x01) == 0;
        if ((Frame[0] & 0x0C) == 0x08 && (Frame[0] & 0x80))
        {
            Qos = TRUE;
            HeaderLength = 26;
        }
        if (!Ndis6WdiPortQueueing(Wdi))
        {
            if (Qos && Length >= 26)
                Tx->ExTID = Frame[24] & 0x0F;
            if (!Ndis6WdiDataFindPeer(Wdi, Frame + 4, &Tx->PeerID))
            {
                KIRQL OldIrql;
                ULONG i;

                KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
                for (i = 0; i < NDIS6_WDI_MAX_PEERS; i++)
                {
                    if (Wdi->Peers[i].Valid && Wdi->Peers[i].PortId == Wdi->PortId)
                    {
                        Tx->PeerID = Wdi->Peers[i].PeerId;
                        break;
                    }
                }
                KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
            }
        }
        if (Length >= HeaderLength + 8 &&
            Frame[HeaderLength] == 0xAA && Frame[HeaderLength + 1] == 0xAA && Frame[HeaderLength + 2] == 0x03)
        {
            RtlCopyMemory(&Tx->Ethertype, &Frame[HeaderLength + 6], sizeof(Tx->Ethertype));
            Tx->bAllowLegacyRates = Frame[HeaderLength + 6] == 0x88 && Frame[HeaderLength + 7] == 0x8E;
        }
    }

    Tx->ExemptionAction = WDI_EXEMPT_NO_EXEMPTION;

    SendContext = NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation);
    if (SendContext != NULL &&
        SendContext->Header.Type == NDIS_OBJECT_TYPE_DEFAULT &&
        SendContext->Header.Size >= RTL_SIZEOF_THROUGH_FIELD(DOT11_EXTSTA_SEND_CONTEXT, usExemptionActionType))
    {
        Tx->ExemptionAction = (WDI_EXEMPTION_ACTION_TYPE)SendContext->usExemptionActionType;
    }
}

static
NDIS_STATUS
Ndis6WdiQueueFrame(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNET_BUFFER_LIST Nbl,
    _In_ BOOLEAN Injected,
    _In_opt_ PWDI_TX_METADATA Given)
{
    PWDI_FRAME_METADATA Metadata;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    KIRQL OldIrql;

    if (!Wdi->DataPathRunning || !Wdi->TalStarted || Wdi->Halting)
        return NDIS_STATUS_PAUSED;

    Metadata = Ndis6WdiAllocateMetadata(Wdi);
    if (Metadata == NULL)
        return NDIS_STATUS_RESOURCES;

    Metadata->pNBL = Nbl;
    if (Given != NULL)
        Metadata->u.txMetaData = *Given;
    else
        Ndis6WdiClassifyFrame(Wdi, Nbl, &Metadata->u.txMetaData);
    Metadata->u.txMetaData.bTxCompleteRequired = FALSE;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    if (Wdi->TxQueueLength >= NDIS6_WDI_TX_QUEUE_LIMIT || !Ndis6WdiAllocateSlot(Wdi, Metadata, Injected))
        Status = NDIS_STATUS_RESOURCES;
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        Ndis6WdiFreeMetadata(Wdi, Metadata);
        return Status;
    }

    Ndis6WdiTxReference(Wdi);
    Nbl->MiniportReserved[0] = Metadata;
    Nbl->MiniportReserved[1] = NULL;

    KeAcquireSpinLock(&Wdi->TxDescLock, &OldIrql);
    Wdi->Tal.TxTargetDescInitHandler(Wdi->TalContext, Nbl, &Status);
    KeReleaseSpinLock(&Wdi->TxDescLock, OldIrql);

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Wdi->TxFrames[Metadata->FrameID & (NDIS6_WDI_TX_SLOTS - 1)].Metadata = NULL;
        KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
        Nbl->MiniportReserved[0] = NULL;
        Ndis6WdiFreeMetadata(Wdi, Metadata);
        Ndis6WdiTxDereference(Wdi);
        return Status;
    }
    InsertTailList(&Wdi->TxQueue, &Metadata->Linkage);
    Wdi->TxQueueLength++;
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
Ndis6WdiSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Failed = NULL;
    PNET_BUFFER_LIST Tail = NULL;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST Next;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(PortNumber);

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        Status = Ndis6WdiQueueFrame(Wdi, Nbl, FALSE, NULL);
        if (Status == NDIS_STATUS_SUCCESS)
            continue;

        Wdi->TxError++;
        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        if (Tail == NULL)
            Failed = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(Tail) = Nbl;
        Tail = Nbl;
    }

    if (Failed != NULL)
    {
        NdisMSendNetBufferListsComplete(Wdi->Adapter, Failed,
                                        NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }

    Ndis6WdiTxKick(Wdi);
}

static
VOID
Ndis6WdiDrainMatching(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ BOOLEAN All,
    _In_ BOOLEAN MatchPeer,
    _In_ WDI_PEER_ID PeerId,
    _In_opt_ PVOID CancelId,
    _In_ NDIS_STATUS Status)
{
    NDIS6_WDI_TX_DONE Done[32];
    ULONG Count;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    KIRQL OldIrql;

    do
    {
        Count = 0;
        KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
        for (Entry = Wdi->TxQueue.Flink; Entry != &Wdi->TxQueue && Count < ARRAYSIZE(Done); Entry = Next)
        {
            PWDI_FRAME_METADATA Metadata = CONTAINING_RECORD(Entry, WDI_FRAME_METADATA, Linkage);
            PNDIS6_WDI_TX_FRAME Frame = &Wdi->TxFrames[Metadata->FrameID & (NDIS6_WDI_TX_SLOTS - 1)];

            Next = Entry->Flink;
            if (!All &&
                !(MatchPeer && Metadata->u.txMetaData.PeerID == PeerId) &&
                !(CancelId != NULL && !Frame->Injected && NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Metadata->pNBL) == CancelId))
            {
                continue;
            }

            RemoveEntryList(Entry);
            Wdi->TxQueueLength--;
            Done[Count].Metadata = Metadata;
            Done[Count].Injected = Frame->Injected;
            Done[Count].FrameStatus = WDI_TxFrameStatus_TransferCancelled;
            Done[Count].Status = Status;
            Frame->Metadata = NULL;
            Count++;
        }
        KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

        Ndis6WdiTxComplete(Wdi, Done, Count);
    } while (Count == ARRAYSIZE(Done));
}

VOID
Ndis6WdiDataFlushQueue(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status)
{
    if (!Wdi->TalInitialized)
        return;
    Ndis6WdiDrainMatching(Wdi, TRUE, FALSE, 0, NULL, Status);
}

VOID
NTAPI
Ndis6WdiCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;

    Ndis6WdiDrainMatching(Wdi, FALSE, FALSE, 0, CancelId, NDIS_STATUS_SEND_ABORTED);
}

static
ULONG
Ndis6WdiEffectiveLength(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNET_BUFFER_LIST Nbl)
{
    ULONG Granularity = Wdi->TxRxCaps.TransmitCapabilities.FrameSizeGranularity;
    ULONG Length = 0;
    PNET_BUFFER Nb;

    for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
        Length += NET_BUFFER_DATA_LENGTH(Nb);

    if (Length < Wdi->TxRxCaps.TransmitCapabilities.MinEffectiveSize)
        Length = Wdi->TxRxCaps.TransmitCapabilities.MinEffectiveSize;
    if (Granularity > 1)
        Length = (Length + Granularity - 1) & ~(Granularity - 1);
    return Length;
}

static
VOID
NTAPI
Ndis6WdiTxDequeue(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ UINT32 Quantum,
    _In_ UINT8 MaxNumFrames,
    _In_ UINT16 Credit,
    _Out_ PNET_BUFFER_LIST *ppNBL)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PNET_BUFFER_LIST Head = NULL;
    PNET_BUFFER_LIST Tail = NULL;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    ULONG Count = 0;
    ULONG Bytes = 0;
    ULONG Credits = 0;
    ULONG Length;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    for (Entry = Wdi->TxQueue.Flink; Entry != &Wdi->TxQueue; Entry = Next)
    {
        PWDI_FRAME_METADATA Metadata = CONTAINING_RECORD(Entry, WDI_FRAME_METADATA, Linkage);

        Next = Entry->Flink;
        if (!Wdi->TxSendAll &&
            (Metadata->u.txMetaData.PeerID != Wdi->TxSendPeer || Metadata->u.txMetaData.ExTID != Wdi->TxSendTid))
        {
            continue;
        }

        if (MaxNumFrames != WDI_TX_MAX_FRAME_COUNT_INVALID && Count >= MaxNumFrames)
            break;
        if (Credit != WDI_TX_CREDIT_INVALID && Credits + Metadata->u.txMetaData.TxCost > Credit)
            break;

        Length = Ndis6WdiEffectiveLength(Wdi, Metadata->pNBL);
        if (Quantum != WDI_QUANTUM_INVALID && Count != 0 && Bytes + Length > Quantum)
            break;

        RemoveEntryList(Entry);
        Wdi->TxQueueLength--;
        Wdi->TxFrames[Metadata->FrameID & (NDIS6_WDI_TX_SLOTS - 1)].InTarget = TRUE;
        NET_BUFFER_LIST_NEXT_NBL(Metadata->pNBL) = NULL;
        if (Tail == NULL)
            Head = Metadata->pNBL;
        else
            NET_BUFFER_LIST_NEXT_NBL(Tail) = Metadata->pNBL;
        Tail = Metadata->pNBL;

        Count++;
        Bytes += Length;
        Credits += Metadata->u.txMetaData.TxCost;
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

    *ppNBL = Head;
}

static
VOID
NTAPI
Ndis6WdiTxTransferComplete(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_TX_FRAME_STATUS WifiTxFrameStatus,
    _In_ PNET_BUFFER_LIST pNBL)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    NDIS6_WDI_TX_DONE Done[32];
    PNET_BUFFER_LIST Nbl = pNBL;
    PNET_BUFFER_LIST Next;
    ULONG Count;
    KIRQL OldIrql;

    while (Nbl != NULL)
    {
        Count = 0;
        KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
        for (; Nbl != NULL && Count < ARRAYSIZE(Done); Nbl = Next)
        {
            PWDI_FRAME_METADATA Metadata = Nbl->MiniportReserved[0];
            PNDIS6_WDI_TX_FRAME Frame;

            Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
            if (Metadata == NULL || (Frame = Ndis6WdiSlot(Wdi, Metadata->FrameID)) == NULL)
                continue;

            Frame->TransferDone = TRUE;
            if (WifiTxFrameStatus != WDI_TxFrameStatus_Ok)
                Frame->Status = WifiTxFrameStatus;
            if (WifiTxFrameStatus != WDI_TxFrameStatus_Ok || !Frame->SendExpected || Frame->SendDone)
            {
                Done[Count].Metadata = Metadata;
                Done[Count].Injected = Frame->Injected;
                Done[Count].FrameStatus = Frame->Status;
                Done[Count].Status = Ndis6WdiFrameStatusToNdis(Frame->Status);
                Frame->Metadata = NULL;
                Count++;
            }
        }
        KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

        Ndis6WdiTxComplete(Wdi, Done, Count);
    }

    Ndis6WdiTxKick(Wdi);
}

static
VOID
NTAPI
Ndis6WdiTxSendComplete(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_TX_FRAME_STATUS WifiTxFrameStatus,
    _In_ UINT16 NumCompletedSends,
    _In_reads_(NumCompletedSends) WDI_FRAME_ID *WifiTxFrameIdList,
    _In_reads_opt_(NumCompletedSends) WDI_TX_COMPLETE_DATA *WifiTxCompleteList)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    NDIS6_WDI_TX_DONE Done[32];
    ULONG Index = 0;
    ULONG Count;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(WifiTxCompleteList);

    while (Index < NumCompletedSends)
    {
        Count = 0;
        KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
        for (; Index < NumCompletedSends && Count < ARRAYSIZE(Done); Index++)
        {
            PNDIS6_WDI_TX_FRAME Frame = Ndis6WdiSlot(Wdi, WifiTxFrameIdList[Index]);

            if (Frame == NULL)
                continue;

            Frame->SendDone = TRUE;
            if (WifiTxFrameStatus != WDI_TxFrameStatus_Ok)
                Frame->Status = WifiTxFrameStatus;
            if (Frame->TransferDone)
            {
                Done[Count].Metadata = Frame->Metadata;
                Done[Count].Injected = Frame->Injected;
                Done[Count].FrameStatus = Frame->Status;
                Done[Count].Status = Ndis6WdiFrameStatusToNdis(Frame->Status);
                Frame->Metadata = NULL;
                Count++;
            }
        }
        KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

        Ndis6WdiTxComplete(Wdi, Done, Count);
    }

    Ndis6WdiTxKick(Wdi);
}

static
VOID
NTAPI
Ndis6WdiTxQueryRaTidState(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ WDI_EXTENDED_TID ExTid,
    _Out_ NDIS_STATUS *pWifiStatus,
    _Out_ PUINT16 pQueueLength)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    for (Entry = Wdi->TxQueue.Flink; Entry != &Wdi->TxQueue; Entry = Entry->Flink)
    {
        PWDI_FRAME_METADATA Metadata = CONTAINING_RECORD(Entry, WDI_FRAME_METADATA, Linkage);

        if ((PortId == WDI_PORT_ANY || Metadata->u.txMetaData.PortID == PortId) &&
            (PeerId == WDI_PEER_ANY || Metadata->u.txMetaData.PeerID == PeerId) &&
            (ExTid == WDI_EXT_TID_UNKNOWN || Metadata->u.txMetaData.ExTID == ExTid))
        {
            Count++;
        }
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

    *pQueueLength = (UINT16)min(Count, MAXUSHORT);
    *pWifiStatus = NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
Ndis6WdiTxQuerySuspectFrame(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ UINT64 SuspectFrameContext,
    _In_ PNET_BUFFER_LIST pNBL,
    _Out_ BOOLEAN *pIsTransferCompleteNeeded,
    _Out_ BOOLEAN *pIsSendCompleteNeeded,
    _Out_ NDIS_STATUS *pWifiStatus)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PWDI_FRAME_METADATA Metadata = pNBL != NULL ? pNBL->MiniportReserved[0] : NULL;
    PNDIS6_WDI_TX_FRAME Frame;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(SuspectFrameContext);

    *pIsTransferCompleteNeeded = FALSE;
    *pIsSendCompleteNeeded = FALSE;
    *pWifiStatus = NDIS_STATUS_INVALID_DATA;

    if (Metadata == NULL)
        return;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    Frame = Ndis6WdiSlot(Wdi, Metadata->FrameID);
    if (Frame != NULL)
    {
        *pIsTransferCompleteNeeded = !Frame->TransferDone;
        *pIsSendCompleteNeeded = Frame->SendExpected && !Frame->SendDone;
        *pWifiStatus = NDIS_STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
}

static
VOID
Ndis6WdiSetPause(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ WDI_PEER_ID PeerId,
    _In_ ULONG Reasons,
    _In_ BOOLEAN Pause)
{
    PNDIS6_WDI_PEER Peer;
    KIRQL OldIrql;
    ULONG i;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    if (PeerId == WDI_PEER_ANY)
    {
        if (Pause)
            Wdi->TxPauseReasons |= Reasons;
        else
            Wdi->TxPauseReasons &= ~Reasons;
        if (!Pause)
        {
            for (i = 0; i < NDIS6_WDI_MAX_PEERS; i++)
                Wdi->Peers[i].PauseReasons &= ~Reasons;
        }
    }
    else if ((Peer = Ndis6WdiPeerById(Wdi, PeerId)) != NULL)
    {
        if (Pause)
            Peer->PauseReasons |= Reasons;
        else
            Peer->PauseReasons &= ~Reasons;
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);
}

static
VOID
NTAPI
Ndis6WdiTxSendPause(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ UINT32 ExTidBitmask,
    _In_ WDI_TX_PAUSE_REASON TxPauseReason)
{
    UNREFERENCED_PARAMETER(PortId);
    UNREFERENCED_PARAMETER(ExTidBitmask);

    Ndis6WdiSetPause((PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle, PeerId, (ULONG)TxPauseReason, TRUE);
}

static
VOID
NTAPI
Ndis6WdiTxSendRestart(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ UINT32 ExTidBitmask,
    _In_ WDI_TX_PAUSE_REASON TxRestartReason)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;

    UNREFERENCED_PARAMETER(PortId);
    UNREFERENCED_PARAMETER(ExTidBitmask);

    Ndis6WdiSetPause(Wdi, PeerId, (ULONG)TxRestartReason, FALSE);
    Ndis6WdiTxKick(Wdi);
}

static
VOID
NTAPI
Ndis6WdiTxReleaseFrames(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ UINT32 ExTidBitmask,
    _In_ UINT8 MaxNumFrames,
    _In_ UINT16 Credit,
    _Out_ PNET_BUFFER_LIST *ppNBL)
{
    UNREFERENCED_PARAMETER(NdisMiniportDataPathHandle);
    UNREFERENCED_PARAMETER(PortId);
    UNREFERENCED_PARAMETER(PeerId);
    UNREFERENCED_PARAMETER(ExTidBitmask);
    UNREFERENCED_PARAMETER(MaxNumFrames);
    UNREFERENCED_PARAMETER(Credit);

    *ppNBL = NULL;
}

static
VOID
NTAPI
Ndis6WdiTxInjectFrame(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ WDI_EXTENDED_TID ExTid,
    _In_ PNET_BUFFER_LIST pNBL,
    _In_ BOOLEAN bIsUnicast,
    _In_ BOOLEAN bUseLegacyRates,
    _In_ UINT16 Ethertype,
    _In_ WDI_EXEMPTION_ACTION_TYPE ExemptionAction)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    WDI_TX_METADATA Tx;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST Next;

    RtlZeroMemory(&Tx, sizeof(Tx));
    Tx.PortID = PortId;
    Tx.PeerID = PeerId;
    Tx.ExTID = ExTid;
    Tx.IsUnicast = bIsUnicast;
    Tx.bAllowLegacyRates = bUseLegacyRates;
    Tx.Ethertype = Ethertype;
    Tx.ExemptionAction = ExemptionAction;

    for (Nbl = pNBL; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        if (Ndis6WdiQueueFrame(Wdi, Nbl, TRUE, &Tx) != NDIS_STATUS_SUCCESS &&
            Wdi->Tal.TxTalSendCompleteHandler != NULL)
        {
            Wdi->Tal.TxTalSendCompleteHandler(Wdi->TalContext, Nbl, WDI_TxFrameStatus_TransferCancelled);
        }
    }

    Ndis6WdiTxKick(Wdi);
}

static
VOID
NTAPI
Ndis6WdiTxAbortConfirm(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;

    KeSetEvent(&Wdi->TxAbortEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
Ndis6WdiAttachRecvContext(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNET_BUFFER_LIST Nbl)
{
    PWDI_FRAME_METADATA Metadata = Nbl->MiniportReserved[0];
    PDOT11_EXTSTA_RECV_CONTEXT Context;
    PNET_BUFFER Nb;

    Nbl->SourceHandle = Wdi->Adapter;
    for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
        Wdi->RxBytes += NET_BUFFER_DATA_LENGTH(Nb);
    Wdi->RxOk++;

    if (Metadata == NULL)
        return;

    Context = (PDOT11_EXTSTA_RECV_CONTEXT)((PUCHAR)Metadata + Wdi->RecvContextOffset);
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Context->Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    Context->Header.Size = sizeof(*Context);
    Context->usNumberOfMPDUsReceived = 1;
    Context->ullTimestamp = KeQueryInterruptTime();
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = Context;
}

static
VOID
Ndis6WdiDetachRecvContexts(
    _In_ PNET_BUFFER_LIST Chain)
{
    PNET_BUFFER_LIST Nbl;

    for (Nbl = Chain; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = NULL;
}

static
VOID
NTAPI
Ndis6WdiRxInorderData(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_RX_INDICATION_LEVEL IndicationLevel,
    _In_ WDI_PEER_ID PeerId,
    _In_ WDI_EXTENDED_TID ExTid,
    _In_ PNDIS_RECEIVE_THROTTLE_PARAMETERS pRxThrottleParams,
    _Out_ NDIS_STATUS *pWifiStatus)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    BOOLEAN Resources = ((ULONG)IndicationLevel & (ULONG)WDI_RX_INDICATION_FLAG_RESOURCES) != 0;
    ULONG Flags = KeGetCurrentIrql() == DISPATCH_LEVEL ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0;
    PNET_BUFFER_LIST Chain;
    PNET_BUFFER_LIST Nbl;
    ULONG Rounds;
    ULONG Count;

    UNREFERENCED_PARAMETER(pRxThrottleParams);

    *pWifiStatus = NDIS_STATUS_SUCCESS;
    if (!Wdi->TalStarted)
        return;

    for (Rounds = 0; Rounds < NDIS6_WDI_RX_ROUNDS; Rounds++)
    {
        Chain = NULL;
        Wdi->Tal.RxGetMpdusHandler(Wdi->TalContext, PeerId, ExTid, &Chain);
        if (Chain == NULL)
            break;

        if (!Wdi->DataPathRunning || Wdi->Halting)
        {
            Wdi->Tal.RxReturnFramesHandler(Wdi->TalContext, Chain);
            continue;
        }

        if (Resources)
        {
            PNET_BUFFER_LIST Next;

            for (Nbl = Chain; Nbl != NULL; Nbl = Next)
            {
                Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
                NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
                Ndis6WdiAttachRecvContext(Wdi, Nbl);
                NdisMIndicateReceiveNetBufferLists(Wdi->Adapter, Nbl, NDIS_DEFAULT_PORT_NUMBER, 1,
                                                   Flags | NDIS_RECEIVE_FLAGS_RESOURCES);
                Ndis6WdiDetachRecvContexts(Nbl);
                Wdi->Tal.RxReturnFramesHandler(Wdi->TalContext, Nbl);
            }
            continue;
        }

        Count = 0;
        for (Nbl = Chain; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        {
            Ndis6WdiAttachRecvContext(Wdi, Nbl);
            Count++;
        }

        if (InterlockedExchangeAdd(&Wdi->RxOutstanding, (LONG)Count) == 0)
            KeClearEvent(&Wdi->RxIdleEvent);
        NdisMIndicateReceiveNetBufferLists(Wdi->Adapter, Chain, NDIS_DEFAULT_PORT_NUMBER, Count, Flags);
    }
}

VOID
NTAPI
Ndis6WdiReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    LONG Count = 0;

    UNREFERENCED_PARAMETER(ReturnFlags);

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = NULL;
        Count++;
    }

    Wdi->Tal.RxReturnFramesHandler(Wdi->TalContext, NetBufferLists);

    if (InterlockedExchangeAdd(&Wdi->RxOutstanding, -Count) == Count)
        KeSetEvent(&Wdi->RxIdleEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
Ndis6WdiRxStopConfirm(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;

    KeSetEvent(&Wdi->RxStopEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
Ndis6WdiRxFlushConfirm(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;

    KeSetEvent(&Wdi->RxFlushEvent, IO_NO_INCREMENT, FALSE);
}

VOID
Ndis6WdiDataPeerConfigure(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    WDI_TXRX_PEER_CFG Config;
    DOT11_MAC_ADDRESS Bssid;
    WDI_PEER_ID PeerId;
    BOOLEAN Configure;
    KIRQL OldIrql;

    if (Wdi->Tal.TalTxRxPeerConfigHandler == NULL)
        return;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Configure = Wdi->Dot11.Associated && !Wdi->Dot11.PeerConfigured;
    RtlCopyMemory(Bssid, Wdi->Dot11.Bssid, sizeof(Bssid));
    Config.PeerQoSConfig = Wdi->Dot11.Qos ? WDI_TXRX_PeerCfgQosCapable : WDI_TXRX_PeerCfgQosNone;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (!Configure || !Ndis6WdiDataFindPeer(Wdi, Bssid, &PeerId))
        return;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Configure = !Wdi->Dot11.PeerConfigured;
    Wdi->Dot11.PeerConfigured = TRUE;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (Configure)
        Wdi->Tal.TalTxRxPeerConfigHandler(Wdi->TalContext, Wdi->PortId, PeerId, &Config);
}

static
VOID
NTAPI
Ndis6WdiPeerCreate(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _In_ WDI_MAC_ADDRESS PeerAddr)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PNDIS6_WDI_PEER Peer;
    KIRQL OldIrql;
    ULONG i;

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    Peer = Ndis6WdiPeerById(Wdi, PeerId);
    for (i = 0; Peer == NULL && i < NDIS6_WDI_MAX_PEERS; i++)
    {
        if (!Wdi->Peers[i].Valid)
            Peer = &Wdi->Peers[i];
    }
    if (Peer != NULL)
    {
        Peer->Valid = TRUE;
        Peer->PortId = PortId;
        Peer->PeerId = PeerId;
        Peer->PauseReasons = Ndis6WdiPortQueueing(Wdi) ? 0 : WDI_TX_PAUSE_REASON_PEER_CREATE;
        RtlCopyMemory(Peer->Address, PeerAddr.Address, sizeof(Peer->Address));
    }
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

    if (Peer == NULL)
    {
        DbgPrint("WDI: peer table full, peer %u dropped\n", PeerId);
        return;
    }

    Ndis6WdiDataPeerConfigure(Wdi);
}

static
VOID
NTAPI
Ndis6WdiPeerDelete(
    _In_ NDIS_HANDLE NdisMiniportDataPathHandle,
    _In_ WDI_PORT_ID PortId,
    _In_ WDI_PEER_ID PeerId,
    _Out_ NDIS_STATUS *pWifiStatus)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)NdisMiniportDataPathHandle;
    PNDIS6_WDI_PEER Peer;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(PortId);

    if (!Ndis6WdiPortQueueing(Wdi))
        Ndis6WdiDrainMatching(Wdi, FALSE, TRUE, PeerId, NULL, NDIS_STATUS_MEDIA_DISCONNECTED);

    KeAcquireSpinLock(&Wdi->TxLock, &OldIrql);
    Peer = Ndis6WdiPeerById(Wdi, PeerId);
    if (Peer != NULL)
        RtlZeroMemory(Peer, sizeof(*Peer));
    KeReleaseSpinLock(&Wdi->TxLock, OldIrql);

    *pWifiStatus = NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6WdiDataInitialize(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    UINT32 ExtraSpace = 0;
    NDIS_STATUS Status;

    Wdi->DataApi.Header.Type = NDIS_OBJECT_TYPE_WDI_DATA_API;
    Wdi->DataApi.Header.Revision = NDIS_OBJECT_TYPE_WDI_DATA_API_REVISION_2;
    Wdi->DataApi.Header.Size = NDIS_SIZEOF_WDI_DATA_API_REVISION_2;
    Wdi->DataApi.TxDequeueIndication = Ndis6WdiTxDequeue;
    Wdi->DataApi.TxTransferCompleteIndication = Ndis6WdiTxTransferComplete;
    Wdi->DataApi.TxSendCompleteIndication = Ndis6WdiTxSendComplete;
    Wdi->DataApi.TxQueryRATIDState = Ndis6WdiTxQueryRaTidState;
    Wdi->DataApi.TxSendPauseIndication = Ndis6WdiTxSendPause;
    Wdi->DataApi.TxSendRestartIndication = Ndis6WdiTxSendRestart;
    Wdi->DataApi.TxReleaseFrameIndication = Ndis6WdiTxReleaseFrames;
    Wdi->DataApi.TxInjectFrameIndication = Ndis6WdiTxInjectFrame;
    Wdi->DataApi.TxAbortConfirm = Ndis6WdiTxAbortConfirm;
    Wdi->DataApi.RxInorderDataIndication = Ndis6WdiRxInorderData;
    Wdi->DataApi.RxStopConfirm = Ndis6WdiRxStopConfirm;
    Wdi->DataApi.RxFlushConfirm = Ndis6WdiRxFlushConfirm;
    Wdi->DataApi.PeerCreateIndication = Ndis6WdiPeerCreate;
    Wdi->DataApi.PeerDeleteIndication = Ndis6WdiPeerDelete;
    Wdi->DataApi.AllocateWiFiFrameMetaData = Ndis6WdiAllocateMetadata;
    Wdi->DataApi.FreeWiFiFrameMetaData = Ndis6WdiFreeMetadata;
    Wdi->DataApi.TxQuerySuspectFrameCompleteStatus = Ndis6WdiTxQuerySuspectFrame;

    RtlZeroMemory(&Wdi->Tal, sizeof(Wdi->Tal));
    Wdi->Tal.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_WDI_DATA_HANDLERS;
    Wdi->Tal.Header.Revision = NDIS_OBJECT_TYPE_MINIPORT_WDI_DATA_HANDLERS_REVISION_2;
    Wdi->Tal.Header.Size = NDIS_SIZEOF_MINIPORT_WDI_DATA_HANDLERS_REVISION_2;

    Status = Wdi->Wdi->TalTxRxInitializeHandler(Wdi->IhvContext,
                                                (NDIS_HANDLE)Wdi,
                                                &Wdi->DataApi,
                                                &Wdi->TalContext,
                                                &Wdi->Tal,
                                                &ExtraSpace);
    DbgPrint("WDI: data path initialize 0x%08lx (metadata extra %lu)\n", Status, ExtraSpace);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Wdi->TalInitialized = TRUE;

    if (Wdi->Tal.TxTargetDescInitHandler == NULL ||
        Wdi->Tal.TxTargetDescDeInitHandler == NULL ||
        Wdi->Tal.TxDataSendHandler == NULL ||
        Wdi->Tal.RxGetMpdusHandler == NULL ||
        Wdi->Tal.RxReturnFramesHandler == NULL ||
        Wdi->Tal.TalTxRxStartHandler == NULL)
    {
        DbgPrint("WDI: data path handlers incomplete\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    Wdi->MetadataExtraSpace = ExtraSpace;
    Wdi->RecvContextOffset = ALIGN_UP_BY(sizeof(WDI_FRAME_METADATA) + ExtraSpace, sizeof(ULONG64));
    Wdi->MetadataSize = Wdi->RecvContextOffset + sizeof(DOT11_EXTSTA_RECV_CONTEXT);
    ExInitializeNPagedLookasideList(&Wdi->MetadataLookaside, NULL, NULL, 0, Wdi->MetadataSize, NDIS6_WDI_TAG, 0);
    Wdi->LookasideReady = TRUE;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6WdiDataStart(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    WDI_TXRX_TARGET_CONFIGURATION Configuration;
    NDIS_STATUS Status;

    RtlZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.TxRxParams.TxRxCapabilities = Wdi->TxRxCaps;
    Configuration.MaxNumPorts = Wdi->MaxNumPorts;
    Configuration.MaxNumPeers = Wdi->MaxNumPeers;

    Status = Wdi->Tal.TalTxRxStartHandler(Wdi->TalContext, &Configuration, &Wdi->TalParameters);
    DbgPrint("WDI: data path start 0x%08lx (outstanding transfers %u)\n", Status, Wdi->TalParameters.MaxOutstandingTransfers);
    if (Status == NDIS_STATUS_SUCCESS)
        Wdi->TalStarted = TRUE;
    return Status;
}

static
VOID
Ndis6WdiWaitIdle(
    _In_ PKEVENT Event,
    _In_ volatile LONG *Count)
{
    LARGE_INTEGER Timeout;
    ULONG Waited = 0;

    while (*Count > 0 && Waited < NDIS6_WDI_DRAIN_TIMEOUT_MS)
    {
        Timeout.QuadPart = -100LL * 10000;
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
        Waited += 100;
    }
}

VOID
Ndis6WdiDataPause(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    InterlockedExchange(&Wdi->DataPathRunning, 0);
    Ndis6WdiDataFlushQueue(Wdi, NDIS_STATUS_PAUSED);
    Ndis6WdiWaitIdle(&Wdi->TxIdleEvent, &Wdi->TxOutstanding);
    Ndis6WdiWaitIdle(&Wdi->RxIdleEvent, &Wdi->RxOutstanding);
    if (Wdi->TxOutstanding > 0 || Wdi->RxOutstanding > 0)
        DbgPrint("WDI: pause with %ld transmit and %ld receive frames outstanding\n", Wdi->TxOutstanding, Wdi->RxOutstanding);
}

VOID
Ndis6WdiDataRestart(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    InterlockedExchange(&Wdi->DataPathRunning, 1);
    Ndis6WdiTxKick(Wdi);
}

VOID
Ndis6WdiDataCleanup(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    if (Wdi->LookasideReady)
    {
        ExDeleteNPagedLookasideList(&Wdi->MetadataLookaside);
        Wdi->LookasideReady = FALSE;
    }
}
