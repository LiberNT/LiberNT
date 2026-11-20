/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60thunk_tx.c
 * PURPOSE:     NDIS 5 -> NDIS 6 send thunk for legacy protocols on
 *              NDIS 6 miniports.
 *
 *              When tcpip.sys (or any other legacy NDIS 5 protocol) calls
 *              NdisSend on an adapter that is actually an NDIS 6 miniport
 *              (e1000e, virtio-net 6.x, ...), the legacy proSendPacketToMiniport
 *              path would dereference Adapter->NdisMiniportBlock.DriverHandle
 *              which is NULL on NDIS 6 adapters and crash. The IsNdis6 gate
 *              in protocol.c forwards into Ndis6TxSendPacket here instead.
 *
 *              We wrap the legacy NDIS_PACKET (a chain of NDIS_BUFFER, which
 *              are themselves PMDLs) in a freshly allocated NET_BUFFER_LIST
 *              from Ext->TxWrapperNblPool, store bridge-owned state in
 *              Nbl->NdisReserved[1], track the wrapper on Ext->InFlightNblsTx
 *              so HaltEx can drain pending sends, and hand the NBL to the
 *              miniport's SendNetBufferListsHandler.
 *
 *              When the miniport completes the send by calling
 *              NdisMSendNetBufferListsComplete, we walk back to the original
 *              NDIS_PACKET, remove the wrapper from the in-flight list, free
 *              the wrapper NBL, and call MiniSendComplete to signal the
 *              legacy protocol's SendCompleteHandler.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 5↔6
 *              datapath thunking work (Phase 3 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* MiniSendComplete is declared in miniport.h (pulled in via ndis6_internal.h)
 * and is the canonical way to wake up a legacy NDIS 5 protocol's
 * SendCompleteHandler. */

#define NDIS6_TX_WRAPPER_TAG 'wTNn'

typedef struct _NDIS6_TX_WRAPPER_CONTEXT
{
    LIST_ENTRY       Link;
    PNET_BUFFER_LIST Nbl;
    PNDIS_PACKET     Packet;
    ULONG_PTR        BindingHandle;
} NDIS6_TX_WRAPPER_CONTEXT, *PNDIS6_TX_WRAPPER_CONTEXT;

BOOLEAN
Ndis6ReferenceNativeTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    BOOLEAN Referenced = FALSE;

    if (Ext == NULL)
        return FALSE;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    if (Ext->TxAccepting)
    {
        if (Ext->TxInFlightCount++ == 0)
            KeClearEvent(&Ext->TxDrainEvent);
        Referenced = TRUE;
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);

    return Referenced;
}

VOID
Ndis6DereferenceTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    ASSERT(Ext->TxInFlightCount > 0);
    if (--Ext->TxInFlightCount == 0)
        KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

static BOOLEAN
Ndis6TrackTransmitWrapper(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNDIS6_TX_WRAPPER_CONTEXT TxContext)
{
    KIRQL OldIrql;
    BOOLEAN Tracked = FALSE;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    if (Ext->TxAccepting)
    {
        InsertTailList(&Ext->InFlightNblsTx, &TxContext->Link);
        if (Ext->TxInFlightCount++ == 0)
            KeClearEvent(&Ext->TxDrainEvent);
        Tracked = TRUE;
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);

    return Tracked;
}

static VOID
Ndis6UntrackTransmitWrapper(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNDIS6_TX_WRAPPER_CONTEXT TxContext)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    RemoveEntryList(&TxContext->Link);
    TxContext->Link.Flink = NULL;
    TxContext->Link.Blink = NULL;
    ASSERT(Ext->TxInFlightCount > 0);
    if (--Ext->TxInFlightCount == 0)
        KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

/* ============================================================================
 *  Ndis6TxSendPacket — wraps an NDIS_PACKET in an NBL and sends it to the
 *  NDIS 6 miniport. Called from protocol.c:proSendPacketToMiniport via the
 *  IsNdis6 gate.
 *
 *  Returns:
 *    NDIS_STATUS_PENDING — the wrapped NBL has been handed off; the legacy
 *      protocol will eventually be woken via MiniSendComplete.
 *    NDIS_STATUS_RESOURCES — wrapper allocation failed; the protocol should
 *      treat the packet as failed (legacy ProSend handles this case).
 *    NDIS_STATUS_FAILURE — invalid arguments or driver has no send handler.
 * ============================================================================ */

NDIS_STATUS
Ndis6TxSendPacket(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_PACKET     Packet)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNDIS6_TX_WRAPPER_CONTEXT TxContext;
    PNET_BUFFER_LIST    Nbl;
    PNDIS_BUFFER        FirstBuffer;
    UINT                TotalLength;

    if (Adapter == NULL || Packet == NULL)
        return NDIS_STATUS_FAILURE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->TxWrapperNblPool == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        return NDIS_STATUS_FAILURE;
    }

    /* Walk the legacy NDIS_PACKET to get the head NDIS_BUFFER (PMDL) and
     * the total payload length. NDIS_BUFFER is just an MDL, so the chain
     * is already in the format NET_BUFFER expects. */
    NdisQueryPacket(Packet, NULL, NULL, &FirstBuffer, &TotalLength);
    if (FirstBuffer == NULL || TotalLength == 0)
        return NDIS_STATUS_FAILURE;

    /* Allocate the wrapper NBL from our per-adapter pool. The NB is
     * embedded in the NBL allocation (pool was created with
     * fAllocateNetBuffer = TRUE in 60adapter.c). DataOffset = 0 because
     * NDIS_BUFFER MDLs always start at the data, and DataLength is the
     * full packet payload. */
    Nbl = NdisAllocateNetBufferAndNetBufferList(
        Ext->TxWrapperNblPool,
        0,                      /* ContextSize */
        0,                      /* ContextBackFill */
        (PMDL)FirstBuffer,      /* MdlChain */
        0,                      /* DataOffset */
        TotalLength);

    if (Nbl == NULL)
        return NDIS_STATUS_RESOURCES;

    TxContext = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(*TxContext),
                                      NDIS6_TX_WRAPPER_TAG);
    if (TxContext == NULL)
    {
        NdisFreeNetBufferList(Nbl);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(TxContext, sizeof(*TxContext));
    TxContext->Nbl = Nbl;
    TxContext->Packet = Packet;
    TxContext->BindingHandle = Packet->Reserved[1];
    Nbl->NdisReserved[1] = TxContext;

    /* B2: copy the legacy packet's checksum/LSO offload info onto the
     * wrapper NBL's NetBufferListInfo[] so the miniport sees the same
     * offload requests whether the send came from a legacy protocol
     * or a native NDIS 6 protocol. The bit layouts of
     * NDIS_TCP_IP_CHECKSUM_PACKET_INFO.Transmit and
     * NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO.Transmit are identical
     * for the bottom 5 bits (IPv4/IPv6/TCP/UDP/IP header), so a straight
     * PVOID copy does the job. Same for RX. */
    {
        PVOID ChecksumValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
            Packet, TcpIpChecksumPacketInfo);
        if (ChecksumValue != NULL)
            NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = ChecksumValue;

        /* Large-send / LSO — same pattern, same layout. */
        {
            PVOID LsoValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpLargeSendPacketInfo);
            if (LsoValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpLargeSendNetBufferListInfo) = LsoValue;
        }

        /* D1: VLAN tag. Legacy IEEE_8021Q_INFO and NBL
         * Ieee8021QNetBufferListInfo share the same union-over-PVOID
         * layout (TCI + canonical format bits). Copy through. */
        {
            PVOID VlanValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, Ieee8021QInfo);
            if (VlanValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo) = VlanValue;
        }
    }

    /* Track the wrapper on the in-flight list so HaltEx can wait for
     * outstanding sends to drain before tearing down the adapter.
     * A1: also increment TxInFlightCount and clear the drain event so
     * HaltEx will actually wait. */
    if (!Ndis6TrackTransmitWrapper(Ext, TxContext))
    {
        Nbl->NdisReserved[1] = NULL;
        ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
        NdisFreeNetBufferList(Nbl);
        return NDIS_STATUS_PAUSED;
    }

    /* Phase 8: route through the filter chain. If no filters are
     * attached, Ndis6FilterDispatchSend short-circuits straight to
     * Ndis6FilterTerminalSend, which calls the miniport's
     * SendNetBufferListsHandler. With filters attached, each filter's
     * SendNetBufferListsHandler runs in turn before the miniport. */
    Ndis6FilterDispatchSend(Adapter, Nbl, 0, 0);

    /* Either way the call is asynchronous from the legacy protocol's
     * perspective — the SendCompleteHandler will eventually fire via
     * MiniSendComplete. */
    return NDIS_STATUS_PENDING;
}

/* ============================================================================
 *  Ndis6FilterTerminalSend — bottom-of-chain TX handler. Called by
 *  Ndis6FilterDispatchSend (when no filters are attached) and by
 *  NdisFSendNetBufferLists (when the bottommost filter's call falls
 *  off the end of the chain). Hands the NBL to the miniport.
 * ============================================================================ */

VOID
Ndis6FilterTerminalSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        return;
    }

    Ext->DriverBlock->Characteristics.SendNetBufferListsHandler(Ext->MiniportAdapterContext, NetBufferList, PortNumber, SendFlags);
}

/* ============================================================================
 *  NdisMSendNetBufferListsComplete — driver-side completion callback.
 *
 *  Called by the NDIS 6 miniport (or its TX completion DPC) when one or more
 *  wrapper NBLs have been transmitted. Walks the chain, recovers the original
 *  NDIS_PACKET pointer from each NBL, removes from the in-flight list, frees
 *  the wrapper NBL, and calls MiniSendComplete to signal the legacy protocol.
 *
 *  This replaces the no-op stub that lived in 60stubs.c.
 * ============================================================================ */

VOID
NTAPI
NdisMSendNetBufferListsComplete(
    _In_ NDIS_HANDLE      NdisMiniportHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PLOGICAL_ADAPTER  Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNET_BUFFER_LIST  CurrentNbl;
    PNET_BUFFER_LIST  NextNbl;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    /* Phase 8: walk the chain in reverse order — bottommost filter sees
     * the completion first, then up through each filter, finally to
     * Ndis6FilterTerminalSendComplete which routes to MiniSendComplete.
     *
     * The driver may complete a chain of NBLs in one call. We split the
     * chain so each NBL gets its own walk through the filter stack —
     * filters are allowed to reorder, drop, or hold NBLs independently. */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        Ndis6FilterDispatchSendComplete(Adapter, CurrentNbl, SendCompleteFlags);
    }
}

/* ============================================================================
 *  Ndis6FilterTerminalSendComplete — top-of-chain TX completion handler.
 *  Called by Ndis6FilterDispatchSendComplete when the chain is empty,
 *  and by NdisFSendNetBufferListsComplete when the topmost filter's
 *  call falls off the head of the chain. Recovers the original
 *  NDIS_PACKET from the wrapper NBL, frees the wrapper, and signals
 *  the legacy protocol via MiniSendComplete.
 * ============================================================================ */

VOID
Ndis6FilterTerminalSendComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_TX_WRAPPER_CONTEXT TxContext;
    PNDIS_PACKET       Packet;
    ULONG_PTR          BindingHandle;
    NDIS_STATUS        Status;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Native sends carry the referenced binding in NdisReserved[1] until
     * completion. The binding and adapter TX references remain held across
     * the protocol callback, so close/halt cannot free either owner here. */
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            (PNDIS6_PROTOCOL_BINDING)NetBufferList->SourceHandle;

        if (Binding != NULL && NetBufferList->NdisReserved[1] == Binding)
        {
            NetBufferList->NdisReserved[1] = NULL;
            NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
            if (Binding->DriverBlock != NULL && Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
            Ndis6DereferenceTransmit(Ext);
            Ndis6DereferenceProtocolBinding(Binding);
            return;
        }
    }

    /* Legacy bridge wrapper completion (tcpip.sys path). */
    TxContext = (PNDIS6_TX_WRAPPER_CONTEXT)NetBufferList->NdisReserved[1];
    Packet = TxContext ? TxContext->Packet : NULL;
    BindingHandle = TxContext ? TxContext->BindingHandle : 0;
    Status = NET_BUFFER_LIST_STATUS(NetBufferList);

    /* Detach from the in-flight list. A1: decrement TxInFlightCount; if it
     * hits zero, signal the drain event so HaltEx can finish waiting. */
    if (TxContext != NULL &&
        TxContext->Link.Flink != NULL &&
        TxContext->Link.Blink != NULL)
        Ndis6UntrackTransmitWrapper(Ext, TxContext);

    /* Free the wrapper NBL. The MDL chain it pointed at is owned by
     * the original legacy NDIS_PACKET and stays alive until
     * MiniSendComplete returns. */
    NetBufferList->NdisReserved[1] = NULL;
    NdisFreeNetBufferList(NetBufferList);

    /* Wake up the legacy protocol via the existing MiniSendComplete
     * machinery. Restore Packet->Reserved[1] because the legacy packet
     * is owned by tcpip and may have been reused internally while the
     * NDIS 6 miniport held the wrapper NBL. */
    if (Packet != NULL && BindingHandle != 0)
    {
        Packet->Reserved[1] = BindingHandle;
        MiniSendComplete(Adapter, Packet, Status);
    }
    else
        DbgPrint("NDIS6-TX: dropping send completion with missing binding packet=%p\n",
                 Packet);

    if (TxContext != NULL)
        ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
}

/* ============================================================================
 *  B1: Ndis6TxSendPackets — batch send entry
 *
 *  Called from protocol.c:NDIS_SendPackets when a legacy protocol hands
 *  us an array of NDIS_PACKETs. Wrap each in an NBL, chain them via
 *  NET_BUFFER_LIST_NEXT_NBL, and hand the whole chain to the miniport's
 *  SendNetBufferListsHandler in ONE call. Saves the per-packet call
 *  overhead and lets the driver batch descriptor fills on its TX ring.
 *
 *  Packets that fail to wrap are completed immediately via MiniSendComplete
 *  with NDIS_STATUS_RESOURCES; the caller sees the Private.Flags status
 *  and won't wait for them.
 * ============================================================================ */

ULONG
Ndis6TxSendPackets(
    _In_                         PLOGICAL_ADAPTER Adapter,
    _In_reads_(NumberOfPackets)  PPNDIS_PACKET    PacketArray,
    _In_                         UINT             NumberOfPackets)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNET_BUFFER_LIST    HeadNbl = NULL;
    PNET_BUFFER_LIST    TailNbl = NULL;
    ULONG               Wrapped = 0;
    UINT                i;

    if (Adapter == NULL || PacketArray == NULL || NumberOfPackets == 0)
        return 0;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->TxWrapperNblPool == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        /* Complete everything with FAILURE so the caller doesn't hang. */
        for (i = 0; i < NumberOfPackets; i++)
            MiniSendComplete(Adapter, PacketArray[i], NDIS_STATUS_FAILURE);
        return 0;
    }

    for (i = 0; i < NumberOfPackets; i++)
    {
        PNDIS_PACKET     Packet = PacketArray[i];
        PNDIS_BUFFER     FirstBuffer;
        UINT             TotalLength;
        PNET_BUFFER_LIST Nbl;
        PNDIS6_TX_WRAPPER_CONTEXT TxContext;

        if (Packet == NULL)
            continue;

        NdisQueryPacket(Packet, NULL, NULL, &FirstBuffer, &TotalLength);
        if (FirstBuffer == NULL || TotalLength == 0)
        {
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_FAILURE);
            continue;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(
            Ext->TxWrapperNblPool,
            0, 0, (PMDL)FirstBuffer, 0, TotalLength);
        if (Nbl == NULL)
        {
            /* Out of wrapper NBLs — drop with RESOURCES so the caller
             * can retry. */
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_RESOURCES);
            continue;
        }

        TxContext = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*TxContext),
                                          NDIS6_TX_WRAPPER_TAG);
        if (TxContext == NULL)
        {
            NdisFreeNetBufferList(Nbl);
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_RESOURCES);
            continue;
        }

        RtlZeroMemory(TxContext, sizeof(*TxContext));
        TxContext->Nbl = Nbl;
        TxContext->Packet = Packet;
        TxContext->BindingHandle = Packet->Reserved[1];
        Nbl->NdisReserved[1] = TxContext;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        /* B2 + D1: carry offload request + VLAN info across. */
        {
            PVOID ChecksumValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpIpChecksumPacketInfo);
            if (ChecksumValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = ChecksumValue;
        }
        {
            PVOID LsoValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpLargeSendPacketInfo);
            if (LsoValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpLargeSendNetBufferListInfo) = LsoValue;
        }
        {
            PVOID VlanValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, Ieee8021QInfo);
            if (VlanValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo) = VlanValue;
        }

        if (!Ndis6TrackTransmitWrapper(Ext, TxContext))
        {
            Nbl->NdisReserved[1] = NULL;
            ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
            NdisFreeNetBufferList(Nbl);
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_PAUSED);
            continue;
        }

        /* Chain onto the batch. */
        if (HeadNbl == NULL)
        {
            HeadNbl = Nbl;
            TailNbl = Nbl;
        }
        else
        {
            NET_BUFFER_LIST_NEXT_NBL(TailNbl) = Nbl;
            TailNbl = Nbl;
        }
        Wrapped++;
    }

    if (HeadNbl != NULL)
    {
        /* Route through the filter chain. With no filters attached, this
         * goes straight to Ndis6FilterTerminalSend which hands the chain
         * to SendNetBufferListsHandler in one call. */
        Ndis6FilterDispatchSend(Adapter, HeadNbl, 0, 0);
    }

    return Wrapped;
}

/* ============================================================================
 *  A5: NdisMCancelSendNetBufferLists / NdisCancelSendNetBufferLists
 *
 *  The caller (protocol or miniport) tags in-flight sends with a
 *  CancelId via NDIS_SET_NET_BUFFER_LIST_CANCEL_ID; this call walks
 *  the in-flight list and marks matching NBLs with NDIS_STATUS_SEND_ABORTED
 *  so the driver's send-completion path sees the status and drops them.
 *  We DO NOT forcibly complete the NBL here because the driver still
 *  owns the MDLs; we just leave a marker and let the driver's own DPC
 *  finish the job.
 * ============================================================================ */

VOID
Ndis6FilterTerminalCancelSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PVOID       CancelId)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PLIST_ENTRY         entry;
    KIRQL               OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Forward to the driver's CancelSendHandler if it has one. The driver
     * is responsible for actually cancelling; the bridge just delivers
     * the notification. */
    if (Ext->DriverBlock != NULL &&
        Ext->DriverBlock->Characteristics.CancelSendHandler != NULL &&
        Ext->MiniportAdapterContext != NULL)
    {
        Ext->DriverBlock->Characteristics.CancelSendHandler(
            Ext->MiniportAdapterContext, CancelId);
    }

    /* Also walk the bridge's in-flight list and mark any wrapper NBL
     * whose original NDIS_PACKET carries the matching CancelId. The
     * driver's completion path will see NDIS_STATUS_SEND_ABORTED on
     * the NBL and route the cancellation back to the protocol. */
    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    for (entry = Ext->InFlightNblsTx.Flink;
         entry != &Ext->InFlightNblsTx;
         entry = entry->Flink)
    {
        PNDIS6_TX_WRAPPER_CONTEXT TxContext =
            CONTAINING_RECORD(entry, NDIS6_TX_WRAPPER_CONTEXT, Link);
        PNET_BUFFER_LIST Nbl = TxContext->Nbl;

        if (Nbl != NULL &&
            NET_BUFFER_LIST_INFO(Nbl, NetBufferListCancelId) == CancelId)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
        }
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

VOID
NTAPI
NdisMCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PVOID CancelId)
{
    Ndis6FilterTerminalCancelSend((PLOGICAL_ADAPTER)NdisMiniportHandle, CancelId);
}

VOID
NTAPI
NdisCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID       CancelId)
{
    PNDIS6_PROTOCOL_BINDING Binding = NdisBindingHandle;
    PLOGICAL_ADAPTER Adapter;

    if (!Ndis6ReferenceProtocolBinding(Binding))
        return;

    Adapter = Binding->Adapter;
    if (Adapter != NULL && Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
        Ndis6FilterDispatchCancelSend(Adapter, CancelId);

    Ndis6DereferenceProtocolBinding(Binding);
}

/* EOF */

#define NDIS6_LEGACY_TX_TAG '5tNn'
#define NDIS6_LEGACY_TX_MAX_FRAME 65535UL

typedef struct _NDIS6_LEGACY_TX_PARENT
{
    PNDIS6_PROTOCOL_BINDING Binding;
    PNET_BUFFER_LIST Nbl;
    ULONG CompletionFlags;
    volatile LONG Pending;
    volatile LONG Result;
} NDIS6_LEGACY_TX_PARENT, *PNDIS6_LEGACY_TX_PARENT;

typedef struct _NDIS6_LEGACY_TX_PACKET
{
    LIST_ENTRY Link;
    LIST_ENTRY DeferLink;
    PNDIS_PACKET Packet;
    PMDL Mdl;
    PVOID Data;
    PSCATTER_GATHER_LIST ScatterGatherList;
    PNDIS6_LEGACY_TX_PARENT Parent;
    NDIS_STATUS CompletionStatus;
    BOOLEAN CompletionArmed;
    volatile LONG References;
} NDIS6_LEGACY_TX_PACKET, *PNDIS6_LEGACY_TX_PACKET;

static VOID
Ndis6LegacyTxRecordStatus(PNDIS6_LEGACY_TX_PARENT Parent, NDIS_STATUS Status)
{
    if (Status != NDIS_STATUS_SUCCESS)
        (VOID)InterlockedCompareExchange(&Parent->Result, (LONG)Status,
                                         (LONG)NDIS_STATUS_SUCCESS);
}

static VOID
Ndis6LegacyTxReleaseParent(PNDIS6_LEGACY_TX_PARENT Parent, NDIS_STATUS Status)
{
    PNDIS6_PROTOCOL_BINDING Binding;
    ULONG Flags;

    Ndis6LegacyTxRecordStatus(Parent, Status);
    if (InterlockedDecrement(&Parent->Pending) != 0)
        return;

    Binding = Parent->Binding;
    Flags = Parent->CompletionFlags;
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
        Flags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    NET_BUFFER_LIST_STATUS(Parent->Nbl) = (NDIS_STATUS)Parent->Result;
    if (Binding->DriverBlock != NULL &&
        Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
    {
        Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
            Binding->ProtocolBindingContext, Parent->Nbl, Flags);
    }
    Ndis6DereferenceProtocolBinding(Binding);
    ExFreePoolWithTag(Parent, NDIS6_LEGACY_TX_TAG);
}

static VOID
Ndis6LegacyTxReleasePacket(PNDIS6_LEGACY_TX_PACKET Tx)
{
    PNDIS6_LEGACY_TX_PARENT Parent;
    NDIS_STATUS CompletionStatus;
    BOOLEAN CompletionArmed;

    if (InterlockedDecrement(&Tx->References) != 0)
        return;

    Parent = Tx->Parent;
    CompletionStatus = Tx->CompletionStatus;
    CompletionArmed = Tx->CompletionArmed;

    if (Tx->ScatterGatherList != NULL)
    {
        PDMA_ADAPTER DmaAdapter =
            Parent->Binding->Adapter->NdisMiniportBlock.SystemAdapterObject;
        KIRQL OldIrql;
        BOOLEAN RaisedIrql = FALSE;

        ASSERT(DmaAdapter != NULL);
        ASSERT(DmaAdapter->DmaOperations != NULL);
        ASSERT(DmaAdapter->DmaOperations->PutScatterGatherList != NULL);
        if (Tx->Packet != NULL)
            NDIS_PER_PACKET_INFO_FROM_PACKET(Tx->Packet,
                                             ScatterGatherListPacketInfo) = NULL;

        ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
        if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        {
            KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
            RaisedIrql = TRUE;
        }
        DmaAdapter->DmaOperations->PutScatterGatherList(
            DmaAdapter, Tx->ScatterGatherList, TRUE);
        if (RaisedIrql)
            KeLowerIrql(OldIrql);
        Tx->ScatterGatherList = NULL;
    }

    if (Tx->Packet != NULL)
    {
        PNDIS_BUFFER Buffer = NULL;
        NdisUnchainBufferAtFront(Tx->Packet, &Buffer);
        NdisFreePacket(Tx->Packet);
    }
    if (Tx->Mdl != NULL)
        IoFreeMdl(Tx->Mdl);
    if (Tx->Data != NULL)
        ExFreePoolWithTag(Tx->Data, NDIS6_LEGACY_TX_TAG);
    ExFreePoolWithTag(Tx, NDIS6_LEGACY_TX_TAG);

    if (CompletionArmed)
        Ndis6LegacyTxReleaseParent(Parent, CompletionStatus);
}

BOOLEAN
Ndis6LegacyTxComplete(PLOGICAL_ADAPTER Adapter, PNDIS_PACKET Packet,
                      NDIS_STATUS Status)
{
    PLIST_ENTRY Entry;
    PNDIS6_LEGACY_TX_PACKET Tx = NULL;
    KIRQL OldIrql;

    if (Adapter == NULL || Adapter->IsNdis6 || Packet == NULL ||
        Adapter->Ndis6LegacyTxPacketPool == NULL)
        return FALSE;

    KeAcquireSpinLock(&Adapter->Ndis6LegacyTxPendingLock, &OldIrql);
    for (Entry = Adapter->Ndis6LegacyTxPendingList.Flink;
         Entry != &Adapter->Ndis6LegacyTxPendingList; Entry = Entry->Flink)
    {
        PNDIS6_LEGACY_TX_PACKET Candidate =
            CONTAINING_RECORD(Entry, NDIS6_LEGACY_TX_PACKET, Link);
        if (Candidate->Packet == Packet)
        {
            RemoveEntryList(&Candidate->Link);
            Candidate->Link.Flink = NULL;
            Candidate->Link.Blink = NULL;
            Tx = Candidate;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);

    if (Tx == NULL)
        return FALSE;

    Tx->CompletionStatus = Status;
    Ndis6LegacyTxReleasePacket(Tx);
    return TRUE;
}

static BOOLEAN
Ndis6LegacyTxCopyNetBuffer(PNET_BUFFER Nb, PVOID Destination, ULONG Length)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(Nb);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(Nb);
    ULONG Remaining = Length;
    PUCHAR Output = Destination;

    while (Mdl != NULL && Remaining != 0)
    {
        ULONG Available = MmGetMdlByteCount(Mdl);
        ULONG Chunk;
        PUCHAR Source;

        if (Offset >= Available)
        {
            Offset = 0;
            Mdl = Mdl->Next;
            continue;
        }
        Chunk = Available - Offset;
        if (Chunk > Remaining)
            Chunk = Remaining;
        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (Source == NULL)
            return FALSE;
        RtlCopyMemory(Output, Source + Offset, Chunk);
        Output += Chunk;
        Remaining -= Chunk;
        Offset = 0;
        Mdl = Mdl->Next;
    }
    return Remaining == 0;
}

static NDIS_STATUS
Ndis6LegacyTxCallMiniport(PLOGICAL_ADAPTER Adapter, PNDIS6_LEGACY_TX_PACKET Tx, BOOLEAN Serialized)
{
    if (Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.SendPacketsHandler != NULL)
    {
        PNDIS_PACKET Packet = Tx->Packet;

        Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.SendPacketsHandler(
            Adapter->NdisMiniportBlock.MiniportAdapterContext, &Packet, 1);
        return Serialized ? NDIS_GET_PACKET_STATUS(Tx->Packet) : NDIS_STATUS_PENDING;
    }

    return Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.SendHandler(
        Adapter->NdisMiniportBlock.MiniportAdapterContext,
        Tx->Packet, Tx->Packet->Private.Flags);
}

static VOID
Ndis6LegacyTxFinishDispatch(PLOGICAL_ADAPTER Adapter, PNDIS6_LEGACY_TX_PACKET Tx, NDIS_STATUS Status)
{
    if (Status != NDIS_STATUS_PENDING)
        (VOID)Ndis6LegacyTxComplete(Adapter, Tx->Packet, Status);
    Ndis6LegacyTxReleasePacket(Tx);
}

static VOID
Ndis6LegacyTxDrain(PLOGICAL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    for (;;)
    {
        BOOLEAN Again;

        if (InterlockedCompareExchange(&Adapter->Ndis6LegacyTxSerializedBusy, 1, 0) != 0)
            return;

        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        for (;;)
        {
            PNDIS6_LEGACY_TX_PACKET Tx;
            NDIS_STATUS Status;
            ULONG ResumeCount;

            KeAcquireSpinLockAtDpcLevel(&Adapter->Ndis6LegacyTxPendingLock);
            if (Adapter->Ndis6LegacyTxStalled ||
                IsListEmpty(&Adapter->Ndis6LegacyTxDeferredList))
            {
                KeReleaseSpinLockFromDpcLevel(&Adapter->Ndis6LegacyTxPendingLock);
                break;
            }
            Tx = CONTAINING_RECORD(RemoveHeadList(&Adapter->Ndis6LegacyTxDeferredList),
                                   NDIS6_LEGACY_TX_PACKET, DeferLink);
            ResumeCount = Adapter->Ndis6LegacyTxResumeCount;
            KeReleaseSpinLockFromDpcLevel(&Adapter->Ndis6LegacyTxPendingLock);

            Status = Ndis6LegacyTxCallMiniport(Adapter, Tx, TRUE);
            if (Status == NDIS_STATUS_RESOURCES)
            {
                KeAcquireSpinLockAtDpcLevel(&Adapter->Ndis6LegacyTxPendingLock);
                InsertHeadList(&Adapter->Ndis6LegacyTxDeferredList, &Tx->DeferLink);
                if (Adapter->Ndis6LegacyTxResumeCount == ResumeCount)
                    Adapter->Ndis6LegacyTxStalled = TRUE;
                KeReleaseSpinLockFromDpcLevel(&Adapter->Ndis6LegacyTxPendingLock);
                continue;
            }
            Ndis6LegacyTxFinishDispatch(Adapter, Tx, Status);
        }
        KeLowerIrql(OldIrql);

        InterlockedExchange(&Adapter->Ndis6LegacyTxSerializedBusy, 0);

        KeAcquireSpinLock(&Adapter->Ndis6LegacyTxPendingLock, &OldIrql);
        Again = !Adapter->Ndis6LegacyTxStalled &&
                !IsListEmpty(&Adapter->Ndis6LegacyTxDeferredList);
        KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);
        if (!Again)
            return;
    }
}

VOID
Ndis6LegacyTxResume(PLOGICAL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    if (Adapter == NULL || Adapter->IsNdis6 || Adapter->Ndis6LegacyTxPacketPool == NULL)
        return;

    KeAcquireSpinLock(&Adapter->Ndis6LegacyTxPendingLock, &OldIrql);
    Adapter->Ndis6LegacyTxResumeCount++;
    Adapter->Ndis6LegacyTxStalled = FALSE;
    KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);

    Ndis6LegacyTxDrain(Adapter);
}

static VOID
Ndis6LegacyTxDispatchMiniport(PNDIS6_LEGACY_TX_PACKET Tx)
{
    PLOGICAL_ADAPTER Adapter = Tx->Parent->Binding->Adapter;
    KIRQL OldIrql;

    InterlockedIncrement(&Tx->References);

    if (Adapter->NdisMiniportBlock.Flags & NDIS_ATTRIBUTE_DESERIALIZE)
    {
        Ndis6LegacyTxFinishDispatch(Adapter, Tx, Ndis6LegacyTxCallMiniport(Adapter, Tx, FALSE));
        return;
    }

    KeAcquireSpinLock(&Adapter->Ndis6LegacyTxPendingLock, &OldIrql);
    InsertTailList(&Adapter->Ndis6LegacyTxDeferredList, &Tx->DeferLink);
    KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);

    Ndis6LegacyTxDrain(Adapter);
}

static VOID NTAPI
Ndis6LegacyTxScatterGatherReady(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PSCATTER_GATHER_LIST ScatterGather,
    IN PVOID Context)
{
    PNDIS6_LEGACY_TX_PACKET Tx = Context;
    PLOGICAL_ADAPTER Adapter = Tx->Parent->Binding->Adapter;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    Tx->ScatterGatherList = ScatterGather;
    if (ScatterGather == NULL || ScatterGather->NumberOfElements == 0)
    {
        (VOID)Ndis6LegacyTxComplete(Adapter, Tx->Packet, NDIS_STATUS_RESOURCES);
        return;
    }

    NDIS_PER_PACKET_INFO_FROM_PACKET(Tx->Packet,
                                     ScatterGatherListPacketInfo) = ScatterGather;
    Ndis6LegacyTxDispatchMiniport(Tx);
}

static NDIS_STATUS
Ndis6LegacyTxSendOne(PNDIS6_LEGACY_TX_PARENT Parent, PNET_BUFFER Nb)
{
    PLOGICAL_ADAPTER Adapter = Parent->Binding->Adapter;
    PNDIS6_LEGACY_TX_PACKET Tx;
    ULONG Length = NET_BUFFER_DATA_LENGTH(Nb);
    NDIS_STATUS Status;
    KIRQL OldIrql;
    BOOLEAN RaisedIrql = FALSE;
    PDMA_ADAPTER DmaAdapter;

    if (Adapter == NULL || Adapter->IsNdis6 ||
        Length < 14 || Length > NDIS6_LEGACY_TX_MAX_FRAME ||
        Adapter->Ndis6LegacyTxPacketPool == NULL)
        return NDIS_STATUS_INVALID_LENGTH;

    if (Adapter->NdisMiniportBlock.DriverHandle == NULL ||
        (Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.SendHandler == NULL &&
         Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.SendPacketsHandler == NULL))
        return NDIS_STATUS_NOT_SUPPORTED;

    DmaAdapter = Adapter->NdisMiniportBlock.SystemAdapterObject;
    if (Adapter->NdisMiniportBlock.ScatterGatherListSize != 0 &&
        (DmaAdapter == NULL || DmaAdapter->DmaOperations == NULL ||
         DmaAdapter->DmaOperations->GetScatterGatherList == NULL ||
         DmaAdapter->DmaOperations->PutScatterGatherList == NULL ||
         Adapter->NdisMiniportBlock.PhysicalDeviceObject == NULL))
    {
        DbgPrint("NDIS6-LEGACY: SG-DMA requested but DMA interface is unavailable\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    Tx = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Tx), NDIS6_LEGACY_TX_TAG);
    if (Tx == NULL)
        return NDIS_STATUS_RESOURCES;
    RtlZeroMemory(Tx, sizeof(*Tx));
    Tx->References = 2;
    Tx->Parent = Parent;
    Tx->Data = ExAllocatePoolWithTag(NonPagedPool, Length, NDIS6_LEGACY_TX_TAG);
    if (Tx->Data == NULL || !Ndis6LegacyTxCopyNetBuffer(Nb, Tx->Data, Length))
        goto NoResources;

    Tx->Mdl = IoAllocateMdl(Tx->Data, Length, FALSE, FALSE, NULL);
    if (Tx->Mdl == NULL)
        goto NoResources;
    MmBuildMdlForNonPagedPool(Tx->Mdl);

    NdisAllocatePacket(&Status, &Tx->Packet, Adapter->Ndis6LegacyTxPacketPool);
    if (Status != NDIS_STATUS_SUCCESS || Tx->Packet == NULL)
        goto NoResources;
    NdisChainBufferAtFront(Tx->Packet, Tx->Mdl);
    NDIS_SET_PACKET_STATUS(Tx->Packet, NDIS_STATUS_PENDING);

    KeAcquireSpinLock(&Adapter->Ndis6LegacyTxPendingLock, &OldIrql);
    if (InterlockedCompareExchange(&Adapter->Ndis6LegacyBindingsClosing, 0, 0) != 0)
    {
        KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);
        goto NoResources;
    }
    Tx->CompletionArmed = TRUE;
    InsertTailList(&Adapter->Ndis6LegacyTxPendingList, &Tx->Link);
    InterlockedIncrement(&Parent->Pending);
    KeReleaseSpinLock(&Adapter->Ndis6LegacyTxPendingLock, OldIrql);

    if (Adapter->NdisMiniportBlock.ScatterGatherListSize != 0)
    {
        if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        {
            KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
            RaisedIrql = TRUE;
        }
        KeFlushIoBuffers(Tx->Mdl, FALSE, TRUE);
        Status = (NDIS_STATUS)DmaAdapter->DmaOperations->GetScatterGatherList(
            DmaAdapter,
            Adapter->NdisMiniportBlock.PhysicalDeviceObject,
            Tx->Mdl,
            MmGetMdlVirtualAddress(Tx->Mdl),
            Length,
            Ndis6LegacyTxScatterGatherReady,
            Tx,
            TRUE);
        if (RaisedIrql)
            KeLowerIrql(OldIrql);

        if (!NT_SUCCESS((NTSTATUS)Status))
        {
            DbgPrint("NDIS6-LEGACY: GetScatterGatherList failed (%08lx)\n",
                     (ULONG)Status);
            if (Tx->ScatterGatherList == NULL)
                (VOID)Ndis6LegacyTxComplete(Adapter, Tx->Packet, Status);
        }
    }
    else
    {
        Ndis6LegacyTxDispatchMiniport(Tx);
    }

    Ndis6LegacyTxReleasePacket(Tx);
    return NDIS_STATUS_SUCCESS;

NoResources:
    Ndis6LegacyTxReleasePacket(Tx);
    Ndis6LegacyTxReleasePacket(Tx);
    return NDIS_STATUS_RESOURCES;
}

VOID
Ndis6LegacySendNetBufferLists(PNDIS6_PROTOCOL_BINDING Binding,
                              PNET_BUFFER_LIST NetBufferLists,
                              NDIS_PORT_NUMBER PortNumber,
                              ULONG SendFlags,
                              ULONG SendCompleteFlags)
{
    PNET_BUFFER_LIST Nbl, Next;
    UNREFERENCED_PARAMETER(SendFlags);

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
        PNDIS6_LEGACY_TX_PARENT Parent;
        PNET_BUFFER Nb;
        NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        if (PortNumber != 0 || !Ndis6ReferenceProtocolBinding(Binding))
        {
            NET_BUFFER_LIST_STATUS(Nbl) = PortNumber != 0 ?
                NDIS_STATUS_NOT_SUPPORTED : NDIS_STATUS_CLOSING;
            if (Binding->DriverBlock != NULL &&
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
                    Binding->ProtocolBindingContext, Nbl, SendCompleteFlags);
            continue;
        }

        Parent = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Parent), NDIS6_LEGACY_TX_TAG);
        if (Parent == NULL)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_RESOURCES;
            if (Binding->DriverBlock != NULL &&
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
                    Binding->ProtocolBindingContext, Nbl, SendCompleteFlags);
            Ndis6DereferenceProtocolBinding(Binding);
            continue;
        }
        RtlZeroMemory(Parent, sizeof(*Parent));
        Parent->Binding = Binding;
        Parent->Nbl = Nbl;
        Parent->CompletionFlags = SendCompleteFlags;
        Parent->Pending = 1;
        Parent->Result = NDIS_STATUS_SUCCESS;

        if (NET_BUFFER_LIST_FIRST_NB(Nbl) == NULL)
            Ndis6LegacyTxRecordStatus(Parent, NDIS_STATUS_INVALID_LENGTH);
        for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
        {
            Status = Ndis6LegacyTxSendOne(Parent, Nb);
            Ndis6LegacyTxRecordStatus(Parent, Status);
        }
        Ndis6LegacyTxReleaseParent(Parent, NDIS_STATUS_SUCCESS);
    }
}

BOOLEAN
Ndis6LegacyDatapathStart(PLOGICAL_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    NET_BUFFER_LIST_POOL_PARAMETERS Params;
    ULONG Mtu = 0, LinkSpeed100bps = 0, MediaState = NdisMediaStateDisconnected;
    ULONG Written = 0;

    if (Adapter == NULL || Adapter->IsNdis6)
        return FALSE;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Adapter->Ndis6LegacyMtu = 0;
    Adapter->Ndis6LegacyLinkSpeed = 0;
    Adapter->Ndis6LegacyMediaState = NdisMediaStateDisconnected;

    Status = MiniQueryInformation(Adapter, OID_GEN_MAXIMUM_FRAME_SIZE,
                                  sizeof(Mtu), &Mtu, &Written);
    if (Status != NDIS_STATUS_SUCCESS || Written != sizeof(Mtu) ||
        Mtu == 0 || Mtu > 65521)
    {
        DbgPrint("NDIS6-LEGACY: no valid maximum frame size (status %08lx, MTU %lu)\n",
                 (ULONG)Status, Mtu);
        return FALSE;
    }
    Adapter->Ndis6LegacyMtu = Mtu;

    Written = 0;
    Status = MiniQueryInformation(Adapter, OID_GEN_LINK_SPEED,
                                  sizeof(LinkSpeed100bps), &LinkSpeed100bps,
                                  &Written);
    if (Status == NDIS_STATUS_SUCCESS && Written == sizeof(LinkSpeed100bps))
        Adapter->Ndis6LegacyLinkSpeed = (ULONGLONG)LinkSpeed100bps * 100;

    Written = 0;
    Status = MiniQueryInformation(Adapter, OID_GEN_MEDIA_CONNECT_STATUS,
                                  sizeof(MediaState), &MediaState, &Written);
    if (Status == NDIS_STATUS_SUCCESS && Written == sizeof(MediaState) &&
        MediaState == NdisMediaStateConnected)
        Adapter->Ndis6LegacyMediaState = NdisMediaStateConnected;

    InitializeListHead(&Adapter->Ndis6LegacyTxPendingList);
    InitializeListHead(&Adapter->Ndis6LegacyTxDeferredList);
    KeInitializeSpinLock(&Adapter->Ndis6LegacyTxPendingLock);
    InterlockedExchange(&Adapter->Ndis6LegacyTxSerializedBusy, 0);
    Adapter->Ndis6LegacyTxStalled = FALSE;
    Adapter->Ndis6LegacyTxResumeCount = 0;
    Adapter->Ndis6LegacyTxPacketPool = NULL;
    Adapter->Ndis6LegacyRxNblPool = NULL;

    NdisAllocatePacketPool(&Status, &Adapter->Ndis6LegacyTxPacketPool, 128, 0);
    if (Status != NDIS_STATUS_SUCCESS || Adapter->Ndis6LegacyTxPacketPool == NULL)
    {
        if (Adapter->Ndis6LegacyTxPacketPool != NULL)
            NdisFreePacketPool(Adapter->Ndis6LegacyTxPacketPool);
        Adapter->Ndis6LegacyTxPacketPool = NULL;
        return FALSE;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Params.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Params.fAllocateNetBuffer = TRUE;
    Params.PoolTag = NDIS6_LEGACY_TX_TAG;
    Adapter->Ndis6LegacyRxNblPool = NdisAllocateNetBufferListPool(NULL, &Params);
    if (Adapter->Ndis6LegacyRxNblPool == NULL)
    {
        NdisFreePacketPool(Adapter->Ndis6LegacyTxPacketPool);
        Adapter->Ndis6LegacyTxPacketPool = NULL;
        return FALSE;
    }
    return TRUE;
}

VOID
Ndis6LegacyDatapathStop(PLOGICAL_ADAPTER Adapter)
{
    if (Adapter == NULL || Adapter->IsNdis6)
        return;
    if (Adapter->Ndis6LegacyTxPacketPool != NULL ||
        Adapter->Ndis6LegacyRxNblPool != NULL)
    {
        ASSERT(IsListEmpty(&Adapter->Ndis6LegacyTxPendingList));
        ASSERT(IsListEmpty(&Adapter->Ndis6LegacyTxDeferredList));
    }
    if (Adapter->Ndis6LegacyRxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->Ndis6LegacyRxNblPool);
        Adapter->Ndis6LegacyRxNblPool = NULL;
    }
    if (Adapter->Ndis6LegacyTxPacketPool != NULL)
    {
        NdisFreePacketPool(Adapter->Ndis6LegacyTxPacketPool);
        Adapter->Ndis6LegacyTxPacketPool = NULL;
    }
}
