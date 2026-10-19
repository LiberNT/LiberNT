/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native NDIS 6 protocol datapath: sends, returns and OID requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* ------------------------------------------------------------------ */
/*  Native NDIS 6 protocol datapath                                   */
/* ------------------------------------------------------------------ */

static ULONG
Ndis6SendFlagsToCompletionFlags(
    _In_ ULONG SendFlags)
{
    ULONG SendCompleteFlags = 0;

    if (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    if (SendFlags & NDIS_SEND_FLAGS_SINGLE_QUEUE)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_SINGLE_QUEUE;
    if (SendFlags & NDIS_SEND_FLAGS_SWITCH_SINGLE_SOURCE)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_SWITCH_SINGLE_SOURCE;
    return SendCompleteFlags;
}

VOID
EXPORT
NdisSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PLOGICAL_ADAPTER        Adapter;
    PNDIS6_ADAPTER_EXT      Ext;
    PNET_BUFFER_LIST        CurrentNbl;
    PNET_BUFFER_LIST        NextNbl;
    ULONG                   SendCompleteFlags;

    if (Binding == NULL || NetBufferList == NULL)
        return;

    SendCompleteFlags = Ndis6SendFlagsToCompletionFlags(SendFlags);

    if (!Ndis6ReferenceProtocolBinding(Binding))
    {
        /* The binding is closing (or already ran down); without a reference
         * it may be freed at any moment, so the completion handler cannot be
         * called. The NBLs are caller-owned — flag them and bail. A protocol
         * racing sends against its own NdisCloseAdapterEx violates the NDIS
         * contract and forfeits the completion callback. */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_CLOSING;
        }
        return;
    }

    if (InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RUNNING, NDIS6_PROTOCOL_STATE_RUNNING) !=
        NDIS6_PROTOCOL_STATE_RUNNING)
    {
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
        }
        if (Binding->DriverBlock != NULL &&
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
        {
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
        }
        Ndis6DereferenceProtocolBinding(Binding);
        return;
    }

    Adapter = Binding->Adapter;
    Ext = (Adapter != NULL && Adapter->IsNdis6) ? NDIS6_EXT(Adapter) : NULL;
    if (Ext == NULL || Ext->DriverBlock == NULL || Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        /* No usable adapter underneath, but the binding reference is held, so
         * the chain can be handed back through the completion handler. */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_FAILURE;
        }
        if (Binding->DriverBlock != NULL &&
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
        {
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
        }
        Ndis6DereferenceProtocolBinding(Binding);
        return;
    }

    /* SourceHandle = Binding marks a native send so completion routes to the
     * protocol's SendNetBufferListsCompleteHandler, not the legacy bridge
     * path. NdisReserved[1] carries the binding reference ownership marker
     * until the completion path releases it. */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        if (!Ndis6ReferenceProtocolBinding(Binding))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_CLOSING;
            if (Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, SendCompleteFlags);
            continue;
        }

        if (!Ndis6ReferenceNativeTransmit(Ext))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
            if (Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, SendCompleteFlags);
            Ndis6DereferenceProtocolBinding(Binding);
            continue;
        }

        CurrentNbl->SourceHandle      = (NDIS_HANDLE)Binding;
        CurrentNbl->NdisReserved[1]   = Binding;

        Ndis6FilterDispatchSend(Adapter, CurrentNbl, PortNumber, SendFlags);
    }

    Ndis6DereferenceProtocolBinding(Binding);
}

VOID
EXPORT
NdisReturnNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNET_BUFFER_LIST        CurrentNbl;
    PNET_BUFFER_LIST        NextNbl;

    if (Binding == NULL || NetBufferLists == NULL)
        return;

    /* Hand the receive NBLs back to the miniport's ReturnNetBufferListsHandler
     * via the filter chain. Each NBL carries the native receive context that
     * keeps this binding alive until the protocol releases its final NBL. */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        Ndis6RxReturnNativeNetBufferList(Binding, CurrentNbl, ReturnFlags);
    }
}

NDIS_STATUS
EXPORT
NdisOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING    Binding;
    PLOGICAL_ADAPTER           Adapter;
    PNDIS6_ADAPTER_EXT         Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                Status;
    KIRQL                      OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || Binding->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* For a real protocol binding we can wire async completion. Allocate
     * a pending context and keep the protocol's RequestId unchanged. */
    if (Binding != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(
            NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID),
            NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
        {
            Ndis6DereferenceProtocolBinding(Binding);
            return NDIS_STATUS_RESOURCES;
        }

        RtlZeroMemory(Pending, sizeof(*Pending));
        Pending->Signature         = NDIS6_PROTOCOL_PENDING_OID_SIGNATURE;
        Pending->Binding           = Binding;
        Pending->References        = 2; /* call stack + pending-list ownership */
        Pending->Listed            = TRUE;
        Pending->DirectRequest     = FALSE;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        /* NdisReserved belongs to NDIS; RequestId remains caller-opaque all
         * the way through filters and the miniport. */
        Ndis6SetPendingOidContext(OidRequest, Pending);
    }

    Status = Ndis6FilterDispatchOidRequest(Adapter, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        /* Async completion will come via NdisMOidRequestComplete →
         * the per-binding pending list → protocol's completion handler. */
        Ndis6DereferencePendingOid(Pending);
        return NDIS_STATUS_PENDING;
    }

    /* Synchronous completion — clear NDIS bookkeeping and pop the entry. */
    if (Pending != NULL)
    {
        BOOLEAN Removed = FALSE;

        Ndis6ClearPendingOidContext(OidRequest);
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        if (Pending->Listed)
        {
            RemoveEntryList(&Pending->ListEntry);
            Pending->Listed = FALSE;
            Removed = TRUE;
        }
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        if (Removed)
            Ndis6DereferencePendingOid(Pending);
        Ndis6DereferencePendingOid(Pending);
    }
    return Status;
}

#if NDIS_SUPPORT_NDIS680
NDIS_STATUS
EXPORT
NdisSynchronousOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING Binding = NdisBindingHandle;
    PLOGICAL_ADAPTER Adapter;
    NDIS_STATUS Status;

    if (OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || NDIS6_EXT(Adapter) == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_CLOSING;
    }

    Status = Ndis6FilterDispatchSynchronousOidRequest(Adapter, OidRequest);
    Ndis6DereferenceProtocolBinding(Binding);
    return Status;
}
#endif

/* ============================================================================
 *  NDIS 6.1 direct OID request path — unserialized OIDs the miniport
 *  declared via the REV_2 characteristics' DirectOidRequestHandler.
 *  Mirrors NdisOidRequest, but dispatches to the direct handler and
 *  completes through NdisMDirectOidRequestComplete → the protocol's
 *  DirectOidRequestCompleteHandler.
 * ============================================================================ */

NDIS_STATUS
EXPORT
NdisDirectOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING     Binding;
    PLOGICAL_ADAPTER            Adapter;
    PNDIS6_ADAPTER_EXT          Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                 Status;
    KIRQL                       OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || Binding->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Binding != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID), NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
        {
            Ndis6DereferenceProtocolBinding(Binding);
            return NDIS_STATUS_RESOURCES;
        }

        RtlZeroMemory(Pending, sizeof(*Pending));
        Pending->Signature         = NDIS6_PROTOCOL_PENDING_OID_SIGNATURE;
        Pending->Binding           = Binding;
        Pending->References        = 2;
        Pending->Listed            = TRUE;
        Pending->DirectRequest     = TRUE;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        Ndis6SetPendingOidContext(OidRequest, Pending);
    }

    Status = Ndis6FilterDispatchDirectOidRequest(Adapter, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        Ndis6DereferencePendingOid(Pending);
        return NDIS_STATUS_PENDING;
    }

    if (Pending != NULL)
    {
        BOOLEAN Removed = FALSE;

        Ndis6ClearPendingOidContext(OidRequest);
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        if (Pending->Listed)
        {
            RemoveEntryList(&Pending->ListEntry);
            Pending->Listed = FALSE;
            Removed = TRUE;
        }
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        if (Removed)
            Ndis6DereferencePendingOid(Pending);
        Ndis6DereferencePendingOid(Pending);
    }
    return Status;
}

VOID
EXPORT
NdisCancelDirectOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID RequestId)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;

    if (Binding == NULL || !Ndis6ReferenceProtocolBinding(Binding))
        return;

    if (Binding->Adapter != NULL)
    {
        Ndis6FilterCancelOidRequestFromProtocol(Binding->Adapter, Binding, RequestId, TRUE);
    }

    Ndis6DereferenceProtocolBinding(Binding);
}

VOID
Ndis6CompleteDirectOidRequestToOrigin(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PNDIS6_PROTOCOL_PENDING_OID Pending;
    PNDIS6_PROTOCOL_BINDING     Binding;
    KIRQL                       OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    /* Direct OIDs issued by protocols carry their binding bookkeeping in
     * NdisReserved. RequestId is never repurposed by NDIS. */
    Pending = Ndis6GetPendingOidContext(OidRequest);
    if (Pending == NULL ||
        Pending->Signature != NDIS6_PROTOCOL_PENDING_OID_SIGNATURE)
        return;
    if (!Pending->DirectRequest)
    {
        DbgPrint("NDIS6: regular OID completed through NdisMDirectOidRequestComplete\n");
        return;
    }

    Binding = Pending->Binding;
    if (Binding == NULL || Binding->DriverBlock == NULL)
        return;

    KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
    if (!Pending->Listed)
    {
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        return;
    }
    RemoveEntryList(&Pending->ListEntry);
    Pending->Listed = FALSE;
    KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

    Ndis6ClearPendingOidContext(OidRequest);
    if (Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler != NULL)
        Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler(Binding->ProtocolBindingContext, OidRequest, Status);
    Ndis6DereferencePendingOid(Pending);
}

VOID
EXPORT
NdisMDirectOidRequestComplete(
    _In_ NDIS_HANDLE       NdisMiniportHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    if (Ndis6FilterCompleteDirectOidFromMiniport(Adapter, OidRequest, Status))
    {
        return;
    }

    Ndis6CompleteDirectOidRequestToOrigin(Adapter, OidRequest, Status);
}

/* EOF */
