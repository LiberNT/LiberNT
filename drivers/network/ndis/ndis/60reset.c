/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 miniport reset and completion
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 *              Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#include "ndis6_internal.h"

#define NDIS6_RESET_IDLE       0
#define NDIS6_RESET_QUEUED     1
#define NDIS6_RESET_PENDING    2
#define NDIS6_RESET_COMPLETING 3
#define NDIS6_RESET_FINALIZING 4

static NDIS_IO_WORKITEM_FUNCTION Ndis6ResetWorker;
static NDIS_IO_WORKITEM_FUNCTION Ndis6ResetAddressingWorker;

static VOID
Ndis6ReleaseResetOwner(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    ExReleaseRundownProtection(&Ext->LegacyRequestRundown);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);
}

static BOOLEAN
Ndis6ReferenceResetOwners(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    ULONG Owners;

    for (Owners = 0; Owners < 2; ++Owners)
    {
        if (!ExAcquireRundownProtection(&Ext->LifecycleRundown))
            break;
        if (!ExAcquireRundownProtection(&Ext->LegacyRequestRundown))
        {
            ExReleaseRundownProtection(&Ext->LifecycleRundown);
            break;
        }
    }
    if (Owners == 2)
        return TRUE;
    while (Owners != 0)
    {
        --Owners;
        Ndis6ReleaseResetOwner(Ext);
    }
    return FALSE;
}

static VOID
Ndis6IndicateResetStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    Indication.SourceHandle = Adapter;
    Indication.StatusCode = StatusCode;
    NdisMIndicateStatusEx(Adapter, &Indication);
}

static VOID
Ndis6FinishReset(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    Adapter->NdisMiniportBlock.ResetStatus = Status;
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    Ndis6IndicateResetStatus(Adapter, NDIS_STATUS_RESET_END);
    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    InterlockedExchange(&Ext->ResetState, NDIS6_RESET_COMPLETING);

    if (InterlockedCompareExchange(&Ext->ResetWorkerActive, 0, 0) == 0)
    {
        InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_IDLE,
                                   NDIS6_RESET_COMPLETING);
    }
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
    Ndis6ReleaseResetOwner(Ext);
}

_Use_decl_annotations_
static VOID NTAPI
Ndis6ResetAddressingWorker(PVOID Context, NDIS_HANDLE WorkItem)
{
    PLOGICAL_ADAPTER Adapter = Context;
    NDIS_REQUEST Request;
    NDIS_STATUS Status, RestoreStatus;
    ULONG Lookahead;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    Status = Adapter->NdisMiniportBlock.ResetStatus;
    Lookahead = Adapter->NdisMiniportBlock.CurrentLookahead;
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    RtlZeroMemory(&Request, sizeof(Request));
    Request.RequestType = NdisRequestSetInformation;
    Request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_LOOKAHEAD;
    Request.DATA.SET_INFORMATION.InformationBuffer = &Lookahead;
    Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(Lookahead);
    RestoreStatus = Ndis6LegacyDoRequest(Adapter, &Request);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = RestoreStatus;

    NdisFreeIoWorkItem(WorkItem);
    Ndis6FinishReset(Adapter, Status);
}

VOID
Ndis6ResetMiniportComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_STATUS Status,
    _In_ BOOLEAN AddressingReset)
{
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_HANDLE WorkItem;
    KIRQL OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        (Ext = NDIS6_EXT(Adapter)) == NULL)
    {
        return;
    }
    if (InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_FINALIZING,
                                   NDIS6_RESET_PENDING) != NDIS6_RESET_PENDING)
    {
        return;
    }
    if (AddressingReset)
    {
        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
        Adapter->NdisMiniportBlock.ResetStatus = Status;
        KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

        WorkItem = NdisAllocateIoWorkItem(Adapter);
        if (WorkItem != NULL)
        {
            NdisQueueIoWorkItem(WorkItem, Ndis6ResetAddressingWorker, Adapter);
            return;
        }
        if (Status == NDIS_STATUS_SUCCESS)
            Status = NDIS_STATUS_RESOURCES;
    }
    Ndis6FinishReset(Adapter, Status);
}

_Use_decl_annotations_
static VOID NTAPI
Ndis6ResetWorker(PVOID Context, NDIS_HANDLE WorkItem)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    NDIS_STATUS Status;
    BOOLEAN AddressingReset = FALSE;
    KIRQL OldIrql;

    InterlockedExchange(&Ext->ResetWorkerActive, 1);
    InterlockedExchange(&Ext->ResetState, NDIS6_RESET_PENDING);

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    Adapter->NdisMiniportBlock.ResetStatus = NDIS_STATUS_PENDING;
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    Ndis6IndicateResetStatus(Adapter, NDIS_STATUS_RESET_START);

    Status = Ndis6CallResetHandlerEx(Adapter, &AddressingReset);
    if (Status != NDIS_STATUS_PENDING)
        Ndis6ResetMiniportComplete(Adapter, Status, AddressingReset);

    NdisFreeIoWorkItem(WorkItem);
    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    InterlockedExchange(&Ext->ResetWorkerActive, 0);
    InterlockedCompareExchange(&Ext->ResetState,
                               NDIS6_RESET_IDLE,
                               NDIS6_RESET_COMPLETING);
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
    Ndis6ReleaseResetOwner(Ext);
}

NDIS_STATUS
Ndis6QueueMiniportReset(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_HANDLE WorkItem;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        (Ext = NDIS6_EXT(Adapter)) == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.ResetHandlerEx == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (!Ndis6ReferenceResetOwners(Ext))
        return NDIS_STATUS_CLOSING;

    if (InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0)
    {
        Ndis6ReleaseResetOwner(Ext);
        Ndis6ReleaseResetOwner(Ext);
        return NDIS_STATUS_CLOSING;
    }

    if (InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_QUEUED,
                                   NDIS6_RESET_IDLE) != NDIS6_RESET_IDLE)
    {
        Ndis6ReleaseResetOwner(Ext);
        Ndis6ReleaseResetOwner(Ext);
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }

    WorkItem = NdisAllocateIoWorkItem(Adapter);
    if (WorkItem == NULL)
    {
        InterlockedExchange(&Ext->ResetState, NDIS6_RESET_IDLE);
        Ndis6ReleaseResetOwner(Ext);
        Ndis6ReleaseResetOwner(Ext);
        return NDIS_STATUS_RESOURCES;
    }
    NdisQueueIoWorkItem(WorkItem, Ndis6ResetWorker, Adapter);
    return NDIS_STATUS_PENDING;
}

VOID
NTAPI
NdisMResetMiniport(
    _In_ NDIS_HANDLE NdisMiniportHandle)
{
    Ndis6QueueMiniportReset((PLOGICAL_ADAPTER)NdisMiniportHandle);
}
