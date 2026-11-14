/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Serialize ordinary OID callbacks with private traversal ownership
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#pragma once

struct _NDIS6_OID_STAGE
{
    LIST_ENTRY Entry;
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_FILTER_OID_FRAME Frame;
    PNDIS6_FILTER_MODULE Module;
    PNDIS_OID_REQUEST Request;
    PVOID RequestId;
    NDIS_HANDLE WorkItem;
    volatile LONG References;
    ULONG Calls;
    BOOLEAN Queued;
    BOOLEAN Active;
    BOOLEAN WorkQueued;
    BOOLEAN Submitted;
    BOOLEAN Completed;
    BOOLEAN Cancelled;
    BOOLEAN CancelForwarded;
    BOOLEAN Retired;
};

static NDIS_IO_WORKITEM_FUNCTION Ndis6OidStageWorker;

static VOID Ndis6OidStageReference(PNDIS6_OID_STAGE Stage)
{
    InterlockedIncrement(&Stage->References);
}

static VOID Ndis6OidStageDereference(PNDIS6_OID_STAGE Stage)
{
    if (InterlockedDecrement(&Stage->References) == 0)
    {
        PNDIS6_FILTER_OID_CONTEXT Context = Stage->Context;
        PNDIS6_FILTER_OID_FRAME Frame = Stage->Frame;
        ExFreePoolWithTag(Stage, NDIS6_FILTER_OID_CONTEXT_TAG);
        if (Frame != NULL)
            Ndis6FilterOidDereferenceFrame(Frame);
        Ndis6FilterOidDereferenceContext(Context);
    }
}

static PNDIS6_OID_STAGE
Ndis6OidStageSelectLocked(PNDIS6_ADAPTER_EXT Ext, PNDIS6_FILTER_MODULE Module)
{
    PLIST_ENTRY Entry, Link;
    PNDIS6_OID_STAGE First = NULL;

    for (Entry = Ext->FilterOidContextList.Flink;
         Entry != &Ext->FilterOidContextList; Entry = Entry->Flink)
    {
        PNDIS6_FILTER_OID_CONTEXT Context =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_OID_CONTEXT, ListEntry);
        for (Link = Context->Stages.Flink; Link != &Context->Stages; Link = Link->Flink)
        {
            PNDIS6_OID_STAGE Stage = CONTAINING_RECORD(Link, NDIS6_OID_STAGE, Entry);
            if (Stage->Module != Module)
                continue;
            if (Stage->Active)
                return NULL;
            if (First == NULL && Stage->Queued)
                First = Stage;
        }
    }
    if (First != NULL)
    {
        First->Queued = FALSE;
        First->Active = TRUE;
        First->WorkQueued = TRUE;
    }
    return First;
}

static VOID Ndis6OidStageSchedule(PNDIS6_OID_STAGE Stage)
{
    if (Stage != NULL)
        NdisQueueIoWorkItem(Stage->WorkItem, Ndis6OidStageWorker, Stage);
}

static BOOLEAN
Ndis6OidStageRetireLocked(PNDIS6_OID_STAGE Stage, PBOOLEAN RemoveContext,
                         PNDIS6_OID_STAGE *Next)
{
    PNDIS6_FILTER_OID_CONTEXT Context = Stage->Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);

    *RemoveContext = FALSE;
    *Next = NULL;
    if (Stage->Retired || !Stage->Completed || Stage->Calls != 0 ||
        Stage->Queued || Stage->WorkQueued)
        return FALSE;

    Stage->Retired = TRUE;
    Stage->Active = FALSE;
    RemoveEntryList(&Stage->Entry);
    if (Stage->Frame != NULL && Stage->Frame->Stage == Stage)
        Stage->Frame->Stage = NULL;
    if (Context->MiniportStage == Stage)
        Context->MiniportStage = NULL;
    if (Context->DetachRequested && IsListEmpty(&Context->Stages) && Context->Listed)
    {
        RemoveEntryList(&Context->ListEntry);
        InitializeListHead(&Context->ListEntry);
        InterlockedExchange(&Context->Listed, FALSE);
        *RemoveContext = TRUE;
    }
    *Next = Ndis6OidStageSelectLocked(Ext, Stage->Module);
    return TRUE;
}

static VOID
Ndis6OidStageReleaseOwner(PNDIS6_OID_STAGE Stage, BOOLEAN RemoveContext,
                         PNDIS6_OID_STAGE Next)
{
    PNDIS6_FILTER_OID_CONTEXT Context = Stage->Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);

    Ndis6OidStageSchedule(Next);
    if (RemoveContext)
        Ndis6FilterOidDereferenceContext(Context);
    Ndis6OidStageDereference(Stage);
    ExReleaseRundownProtection(&Ext->LegacyRequestRundown);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);
}

static BOOLEAN Ndis6OidStageCanCancel(PNDIS6_OID_STAGE Stage)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Stage->Context->Adapter);
    return Stage->Module != NULL
        ? Stage->Module->DriverBlock->Characteristics.CancelOidRequestHandler != NULL
        : Ext->DriverBlock->Characteristics.CancelOidRequestHandler != NULL;
}

static VOID Ndis6OidStageInvokeCancel(PNDIS6_OID_STAGE Stage)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Stage->Context->Adapter);
    PVOID Id = Stage->RequestId;

    if (Stage->Module != NULL)
        Stage->Module->DriverBlock->Characteristics.CancelOidRequestHandler(
            Stage->Module->FilterModuleContext, Id);
    else
        Ext->DriverBlock->Characteristics.CancelOidRequestHandler(Ext->MiniportAdapterContext, Id);
}

static VOID Ndis6OidStageLeave(PNDIS6_OID_STAGE Stage)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    BOOLEAN Cancel, Retired, RemoveContext;
    PNDIS6_OID_STAGE Next;

    if (Stage == NULL)
        return;
    Ext = NDIS6_EXT(Stage->Context->Adapter);
    for (;;)
    {
        KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
        ASSERT(Stage->Calls != 0);
        --Stage->Calls;
        Cancel = Stage->Calls == 0 && Stage->Cancelled && Stage->Submitted &&
                 !Stage->Completed && !Stage->CancelForwarded && Ndis6OidStageCanCancel(Stage);
        if (Cancel)
        {
            Stage->CancelForwarded = TRUE;
            ++Stage->Calls;
        }
        Retired = Ndis6OidStageRetireLocked(Stage, &RemoveContext, &Next);
        KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
        if (Retired)
        {
            Ndis6OidStageReleaseOwner(Stage, RemoveContext, Next);
            break;
        }
        if (!Cancel)
            break;
        Ndis6OidStageInvokeCancel(Stage);
    }
    Ndis6OidStageDereference(Stage);
}

static PNDIS6_OID_STAGE
Ndis6OidStageEnter(PNDIS6_FILTER_OID_CONTEXT Context, PNDIS6_FILTER_OID_FRAME Frame)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);
    PNDIS6_OID_STAGE Stage;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Stage = Frame != NULL ? Frame->Stage : Context->MiniportStage;
    if (Stage != NULL)
    {
        Ndis6OidStageReference(Stage);
        ++Stage->Calls;
    }
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    return Stage;
}

static PNDIS6_OID_STAGE Ndis6OidStageEnterFrame(PNDIS6_FILTER_OID_FRAME Frame)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Frame->Module->Adapter);
    PNDIS6_OID_STAGE Stage;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Stage = Frame->Stage;
    if (Stage != NULL)
    {
        Ndis6OidStageReference(Stage);
        ++Stage->Calls;
    }
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    return Stage;
}

static PNDIS6_OID_STAGE Ndis6OidStageEnterMiniport(PNDIS6_FILTER_OID_CONTEXT Context)
{
    return Ndis6OidStageEnter(Context, NULL);
}

static VOID Ndis6OidStageComplete(PNDIS6_OID_STAGE Stage)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    if (Stage == NULL)
        return;
    Ext = NDIS6_EXT(Stage->Context->Adapter);
    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Stage->Completed = TRUE;
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
}

static VOID Ndis6OidStageCompleteFrame(PNDIS6_FILTER_OID_FRAME Frame)
{
    PNDIS6_OID_STAGE Stage = Ndis6OidStageEnterFrame(Frame);
    Ndis6OidStageComplete(Stage);
    Ndis6OidStageLeave(Stage);
}

static VOID
Ndis6OidStageCancelReferenced(PNDIS6_OID_STAGE Stage)
{
    PNDIS6_FILTER_OID_CONTEXT Context = Stage->Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);
    BOOLEAN Schedule = FALSE, Invoke = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    if (Stage->Retired || Stage->Completed)
    {
        KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
        Ndis6OidStageDereference(Stage);
        return;
    }
    Stage->Cancelled = TRUE;
    if (Stage->Queued)
    {
        Stage->Queued = FALSE;
        Stage->WorkQueued = TRUE;
        Schedule = TRUE;
    }
    else if (Stage->Submitted && !Stage->Completed && Stage->Calls == 0 &&
             !Stage->CancelForwarded && Ndis6OidStageCanCancel(Stage))
    {
        Stage->CancelForwarded = TRUE;
        ++Stage->Calls;
        Invoke = TRUE;
    }
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    if (Schedule)
        Ndis6OidStageSchedule(Stage);
    if (Invoke)
    {
        Ndis6OidStageInvokeCancel(Stage);
        Ndis6OidStageLeave(Stage);
    }
    else
        Ndis6OidStageDereference(Stage);
}

static BOOLEAN Ndis6OidStageCancelFrame(PNDIS6_FILTER_OID_FRAME Frame)
{
    PNDIS6_OID_STAGE Stage;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Frame->Module->Adapter);
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Stage = Frame->Stage;
    if (Stage != NULL)
        Ndis6OidStageReference(Stage);
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    if (Stage == NULL)
        return TRUE;
    Ndis6OidStageCancelReferenced(Stage);
    return TRUE;
}

static BOOLEAN Ndis6OidStageCancelMiniport(PNDIS6_FILTER_OID_CONTEXT Context)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);
    PNDIS6_OID_STAGE Stage;
    KIRQL OldIrql;
    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Stage = Context->MiniportStage;
    if (Stage == NULL)
    {
        PLIST_ENTRY Link;
        Context->QueueCancelled = TRUE;
        for (Link = Context->Stages.Flink; Link != &Context->Stages; Link = Link->Flink)
        {
            PNDIS6_OID_STAGE Candidate = CONTAINING_RECORD(Link, NDIS6_OID_STAGE, Entry);
            if (!Candidate->Submitted && !Candidate->Completed)
            {
                Stage = Candidate;
                break;
            }
        }
    }
    if (Stage != NULL)
        Ndis6OidStageReference(Stage);
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    if (Stage != NULL)
        Ndis6OidStageCancelReferenced(Stage);
    return TRUE;
}

_Use_decl_annotations_
static VOID NTAPI Ndis6OidStageWorker(PVOID Parameter, NDIS_HANDLE WorkItem)
{
    PNDIS6_OID_STAGE Stage = Parameter;
    PNDIS6_FILTER_OID_CONTEXT Context = Stage->Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Context->Adapter);
    PNDIS_OID_REQUEST Request;
    NDIS_STATUS Status;
    BOOLEAN Completed;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Ndis6OidStageReference(Stage);
    ++Stage->Calls;
    Stage->WorkQueued = FALSE;
    Stage->WorkItem = NULL;
    Request = Stage->Request;
    Status = (Stage->Cancelled || Context->QueueCancelled) ? NDIS_STATUS_REQUEST_ABORTED :
             (!Ext->Initialized || Ext->ProtocolBindingsClosing || Ext->MiniportAdapterContext == NULL)
             ? NDIS_STATUS_ADAPTER_NOT_READY : NDIS_STATUS_SUCCESS;
    if (Status == NDIS_STATUS_SUCCESS)
        Stage->Submitted = TRUE;
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    NdisFreeIoWorkItem(WorkItem);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        Ndis6FilterOidSetActiveRequest(Context, Request, Stage->Module == NULL);
        Status = Stage->Module != NULL
            ? Stage->Module->DriverBlock->Characteristics.OidRequestHandler(Stage->Module->FilterModuleContext, Request)
            : Ext->DriverBlock->Characteristics.OidRequestHandler(Ext->MiniportAdapterContext, Request);
    }
    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    Completed = Stage->Completed;
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    if (Status != NDIS_STATUS_PENDING && !Completed)
    {
        if (Stage->Module != NULL)
            NdisFOidRequestComplete(Stage->Module, Request, Status);
        else
            Ndis6FilterCompleteOidFromMiniport(Context->Adapter, Request, Status);
    }
    Ndis6OidStageLeave(Stage);
}

static NDIS_STATUS
Ndis6OidStageQueue(PNDIS6_FILTER_OID_CONTEXT Context, PNDIS6_FILTER_OID_FRAME Frame,
                  PNDIS6_FILTER_MODULE Module, PNDIS_OID_REQUEST Request)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_OID_STAGE Stage, Next;
    KIRQL OldIrql;

    if (Context == NULL || Context->DirectRequest)
        return NDIS_STATUS_INVALID_STATE;
    Ext = NDIS6_EXT(Context->Adapter);
    if (!ExAcquireRundownProtection(&Ext->LifecycleRundown))
        return NDIS_STATUS_CLOSING;
    if (!ExAcquireRundownProtection(&Ext->LegacyRequestRundown))
    {
        ExReleaseRundownProtection(&Ext->LifecycleRundown);
        return NDIS_STATUS_CLOSING;
    }
    Stage = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stage), NDIS6_FILTER_OID_CONTEXT_TAG);
    if (Stage == NULL)
        goto Resources;
    RtlZeroMemory(Stage, sizeof(*Stage));
    Stage->WorkItem = NdisAllocateIoWorkItem(Context->Adapter);
    if (Stage->WorkItem == NULL)
    {
        ExFreePoolWithTag(Stage, NDIS6_FILTER_OID_CONTEXT_TAG);
        goto Resources;
    }
    Stage->Context = Context;
    Stage->Frame = Frame;
    Stage->Module = Module;
    Stage->Request = Request;
    Stage->RequestId = Request->RequestId;
    Stage->References = 1;
    Stage->Queued = TRUE;
    Ndis6FilterOidReferenceContext(Context);
    if (Frame != NULL)
        Ndis6FilterOidReferenceFrame(Frame);

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    ASSERT(Frame != NULL ? Frame->Stage == NULL : Context->MiniportStage == NULL);
    if (Frame != NULL)
        Frame->Stage = Stage;
    else
        Context->MiniportStage = Stage;
    Stage->Cancelled = Context->QueueCancelled;
    if (Frame != NULL)
    {
        KIRQL ContextIrql;
        KeAcquireSpinLock(&Context->Lock, &ContextIrql);
        Stage->Cancelled |= Frame->CancelIndicated;
        KeReleaseSpinLock(&Context->Lock, ContextIrql);
    }
    InsertTailList(&Context->Stages, &Stage->Entry);
    Next = Ndis6OidStageSelectLocked(Ext, Module);
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    Ndis6OidStageSchedule(Next);
    return NDIS_STATUS_PENDING;

Resources:
    ExReleaseRundownProtection(&Ext->LegacyRequestRundown);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);
    return NDIS_STATUS_RESOURCES;
}

static BOOLEAN Ndis6OidStageHasPendingLocked(PNDIS6_FILTER_OID_CONTEXT Context)
{
    PLIST_ENTRY Link;
    for (Link = Context->Stages.Flink; Link != &Context->Stages; Link = Link->Flink)
    {
        PNDIS6_OID_STAGE Stage = CONTAINING_RECORD(Link, NDIS6_OID_STAGE, Entry);
        if (!Stage->Completed)
            return TRUE;
    }
    return FALSE;
}
