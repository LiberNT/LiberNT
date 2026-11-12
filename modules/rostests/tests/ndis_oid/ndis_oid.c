/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin ordinary miniport OID serialization and completion ownership
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#include <ndis6_internal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static unsigned Failures, Allocations, Frees, Calls, Completions;
static KIRQL TestIrql;
static unsigned FailAllocation, FailRundown;
static LONG ModulePins, BindingPins, LifecyclePins, LegacyPins;
static LOGICAL_ADAPTER Adapter;
static NDIS6_ADAPTER_EXT Extension;
static NDIS6_DRIVER_BLOCK Driver;
static PNDIS_OID_REQUEST Active;
static NDIS_OID_REQUEST Requests[2];
static ULONG Identities[2];
static NDIS6_FILTER_MODULE Filter;
static NDIS6_FILTER_DRIVER_BLOCK FilterDriver;
static unsigned FilterCalls;
static PNDIS_OID_REQUEST FilterActive;
static NDIS_STATUS ExpectedStatus[2];
static unsigned MiniportMode, FilterMode, CancelCalls, FilterCancelCalls;
static BOOLEAN InMiniport, HoldFilterCompletion, InFilterCompletion;
static PNDIS_OID_REQUEST CloneOriginal, CloneRequest;
static ULONG CloneIdentity;
static void (*CompletionHook)(void);
static void (*MiniportHook)(void);
static PKSPIN_LOCK ReleaseHookLock;
static void (*ReleaseHook)(void);
static void CompleteStatus(PNDIS_OID_REQUEST Request, NDIS_STATUS Status);

static void Check(const char *Name, BOOLEAN Passed)
{
    printf("%s %s\n", Passed ? "PASS" : "FAIL", Name);
    if (!Passed) ++Failures;
}

static VOID TestAcquire(PKSPIN_LOCK Lock, PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Lock);
    *OldIrql = TestIrql;
    TestIrql = DISPATCH_LEVEL;
}

static VOID TestRelease(PKSPIN_LOCK Lock, KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Lock);
    TestIrql = OldIrql;
    if (ReleaseHook && Lock == ReleaseHookLock)
    {
        void (*Hook)(void) = ReleaseHook;
        ReleaseHook = NULL;
        Hook();
    }
}

static PVOID NTAPI TestAllocate(POOL_TYPE Type, SIZE_T Size, ULONG Tag)
{
    PVOID Result;
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Tag);
    if (FailAllocation && --FailAllocation == 0) return NULL;
    Result = calloc(1, Size);
    if (Result) ++Allocations;
    return Result;
}

static VOID NTAPI TestFree(PVOID Memory, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    ++Frees;
    free(Memory);
}

typedef struct _TEST_JOB
{
    NDIS_IO_WORKITEM_ROUTINE Routine;
    PVOID Context;
    struct _TEST_JOB *Next;
} TEST_JOB;

static TEST_JOB *Jobs, **JobsTail = &Jobs;
static LONG Rundowns;

static BOOLEAN NTAPI TestAcquireRundown(PEX_RUNDOWN_REF Rundown)
{
    if (FailRundown && --FailRundown == 0) return FALSE;
    if (Rundown == &Extension.LifecycleRundown) ++LifecyclePins;
    else if (Rundown == &Extension.LegacyRequestRundown) ++LegacyPins;
    else Check("oid.known_rundown", FALSE);
    ++Rundowns;
    return TRUE;
}

static VOID NTAPI TestReleaseRundown(PEX_RUNDOWN_REF Rundown)
{
    if (Rundown == &Extension.LifecycleRundown)
        Check("oid.lifecycle_owned", LifecyclePins-- > 0);
    else if (Rundown == &Extension.LegacyRequestRundown)
        Check("oid.legacy_owned", LegacyPins-- > 0);
    else Check("oid.known_rundown", FALSE);
    Check("oid.rundown_owned", Rundowns > 0);
    --Rundowns;
}

static NDIS_HANDLE NTAPI TestAllocateWork(NDIS_HANDLE Handle)
{
    Check("oid.work_adapter", Handle == &Adapter);
    return TestAllocate(NonPagedPool, sizeof(TEST_JOB), 0);
}

static VOID NTAPI TestQueueWork(NDIS_HANDLE Handle, NDIS_IO_WORKITEM_ROUTINE Routine, PVOID Context)
{
    TEST_JOB *Job = Handle;
    Job->Routine = Routine;
    Job->Context = Context;
    *JobsTail = Job;
    JobsTail = &Job->Next;
}

static VOID NTAPI TestFreeWork(NDIS_HANDLE Handle)
{
    TestFree(Handle, 0);
}

static void DrainJobs(void)
{
    KIRQL Previous = TestIrql;
    TestIrql = PASSIVE_LEVEL;
    while (Jobs)
    {
        TEST_JOB *Job = Jobs;
        NDIS_IO_WORKITEM_ROUTINE Routine = Job->Routine;
        PVOID Context = Job->Context;
        Jobs = Job->Next;
        if (!Jobs) JobsTail = &Jobs;
        Routine(Context, Job);
    }
    TestIrql = Previous;
}

static BOOLEAN TestReferenceFilterModule(PNDIS6_FILTER_MODULE Module)
{
    UNREFERENCED_PARAMETER(Module);
    ++ModulePins;
    return TRUE;
}

static VOID TestDereferenceFilterModule(PNDIS6_FILTER_MODULE Module)
{
    UNREFERENCED_PARAMETER(Module);
    Check("oid.module_owned", ModulePins-- > 0);
}

static BOOLEAN TestReferenceProtocolBinding(PNDIS6_PROTOCOL_BINDING Binding)
{
    UNREFERENCED_PARAMETER(Binding);
    ++BindingPins;
    return TRUE;
}

static VOID TestDereferenceProtocolBinding(PNDIS6_PROTOCOL_BINDING Binding)
{
    UNREFERENCED_PARAMETER(Binding);
    Check("oid.binding_owned", BindingPins-- > 0);
}

VOID Ndis6CompleteOidRequestToOrigin(PLOGICAL_ADAPTER Miniport,
                                   PNDIS_OID_REQUEST Request, NDIS_STATUS Status)
{
    Check("oid.origin_adapter", Miniport == &Adapter);
    Check("oid.origin_request", Request == &Requests[0] || Request == &Requests[1]);
    Check("oid.origin_identity", Request->RequestId == &Identities[Request == &Requests[1]]);
    Check("oid.origin_status", Status == ExpectedStatus[Request == &Requests[1]]);
    ++Completions;
}

VOID Ndis6CompleteDirectOidRequestToOrigin(PLOGICAL_ADAPTER Miniport,
                                         PNDIS_OID_REQUEST Request, NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(Miniport);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Status);
    Check("oid.unexpected_direct_completion", FALSE);
}

static NDIS_STATUS NTAPI TestMiniport(NDIS_HANDLE Context, PNDIS_OID_REQUEST Request)
{
    Check("oid.miniport_context", Context == &Adapter);
    Check("oid.miniport_passive", TestIrql == PASSIVE_LEVEL);
    Check("oid.no_parallel_regular_request", Active == NULL);
    ++Calls;
    if (Active == NULL) Active = Request;
    InMiniport = TRUE;
    if (MiniportHook) MiniportHook();
    if (MiniportMode == 2)
        CompleteStatus(Request, NDIS_STATUS_SUCCESS);
    InMiniport = FALSE;
    if (MiniportMode == 1)
    {
        Active = NULL;
        return NDIS_STATUS_SUCCESS;
    }
    return NDIS_STATUS_PENDING;
}

static VOID NTAPI TestCancel(NDIS_HANDLE Context, PVOID Id)
{
    Check("oid.cancel_context", Context == &Adapter);
    Check("oid.cancel_after_handler_returns", !InMiniport);
    Check("oid.cancel_request_id", Active && Active->RequestId == Id);
    ++CancelCalls;
    if (Active) CompleteStatus(Active, NDIS_STATUS_REQUEST_ABORTED);
}

static NDIS_STATUS NTAPI TestFilter(NDIS_HANDLE Context, PNDIS_OID_REQUEST Request)
{
    Check("oid.filter_context", Context == &Filter);
    Check("oid.filter_irql", TestIrql <= DISPATCH_LEVEL);
    Check("oid.no_parallel_filter_request", FilterActive == NULL);
    ++FilterCalls;
    if (!FilterActive) FilterActive = Request;
    if (FilterMode == 1)
        return NdisFOidRequest(&Filter, Request);
    if (FilterMode == 2)
    {
        Check("oid.clone_allocated", NdisAllocateCloneOidRequest(&Filter, Request, 'tONn', &CloneRequest) == NDIS_STATUS_SUCCESS);
        CloneOriginal = Request;
        CloneRequest->RequestId = &CloneIdentity;
        return NdisFOidRequest(&Filter, CloneRequest);
    }
    return NDIS_STATUS_PENDING;
}

static VOID NTAPI TestFilterCancel(NDIS_HANDLE Context, PVOID Id)
{
    Check("oid.filter_cancel_context", Context == &Filter);
    Check("oid.cancel_after_upcall_returns", !InFilterCompletion);
    Check("oid.filter_cancel_was_dispatched", FilterActive && FilterActive->RequestId == Id);
    ++FilterCancelCalls;
    FilterActive = NULL;
    NdisFOidRequestComplete(&Filter, Id == Requests[1].RequestId ? &Requests[1] : &Requests[0], NDIS_STATUS_REQUEST_ABORTED);
}

static VOID NTAPI TestFilterComplete(NDIS_HANDLE Context, PNDIS_OID_REQUEST Request, NDIS_STATUS Status)
{
    PNDIS_OID_REQUEST Original = Request;
    Check("oid.upcall_context", Context == &Filter);
    if (FilterMode == 2)
    {
        Check("oid.upcall_clone", Request == CloneRequest);
        Original = CloneOriginal;
        NdisFreeCloneOidRequest(&Filter, Request);
        CloneRequest = NULL;
    }
    Check("oid.upcall_is_active", FilterActive == Original);
    InFilterCompletion = TRUE;
    if (CompletionHook) CompletionHook();
    InFilterCompletion = FALSE;
    if (!HoldFilterCompletion)
    {
        FilterActive = NULL;
        NdisFOidRequestComplete(&Filter, Original, Status);
    }
}

static ULONG_PTR TestUnexpected(const char *Name, ...)
{
    Check(Name, FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

static ULONG __cdecl TestPrint(PCSTR Format, ...)
{
    int Count;
    va_list Arguments;
    va_start(Arguments, Format);
    Count = vprintf(Format, Arguments);
    va_end(Arguments);
    return Count > 0 ? (ULONG)Count : 0;
}

const UCHAR Ndis6ProtocolOidContextMarker;

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock
#undef ExAllocatePoolWithTag
#undef ExFreePoolWithTag
#undef ASSERT
#define ASSERT(Expression) Check("oid.assert." #Expression, !!(Expression))
#define KeAcquireSpinLock TestAcquire
#define KeReleaseSpinLock TestRelease
#define ExAllocatePoolWithTag TestAllocate
#define ExFreePoolWithTag TestFree
#define Ndis6ReferenceFilterModule TestReferenceFilterModule
#define Ndis6DereferenceFilterModule TestDereferenceFilterModule
#define Ndis6ReferenceProtocolBinding TestReferenceProtocolBinding
#define Ndis6DereferenceProtocolBinding TestDereferenceProtocolBinding
#define DbgPrint TestPrint
#define ExAcquireRundownProtection TestAcquireRundown
#define ExReleaseRundownProtection TestReleaseRundown
#define NdisAllocateIoWorkItem TestAllocateWork
#define NdisQueueIoWorkItem TestQueueWork
#define NdisFreeIoWorkItem TestFreeWork
#define KeSetEvent(...) ((LONG)TestUnexpected("unexpected.KeSetEvent", __VA_ARGS__))
#define KeClearEvent(...) TestUnexpected("unexpected.KeClearEvent", __VA_ARGS__)
#define KeWaitForSingleObject(...) ((NTSTATUS)TestUnexpected("unexpected.KeWaitForSingleObject", __VA_ARGS__))
#define ExQueueWorkItem(...) TestUnexpected("unexpected.ExQueueWorkItem", __VA_ARGS__)
#define KeReleaseMutex(...) ((LONG)TestUnexpected("unexpected.KeReleaseMutex", __VA_ARGS__))
#define Ndis6FilterTerminalSend(...) TestUnexpected("unexpected.send", __VA_ARGS__)
#define Ndis6FilterTerminalSendComplete(...) TestUnexpected("unexpected.send_complete", __VA_ARGS__)
#define Ndis6FilterTerminalReceive(...) TestUnexpected("unexpected.receive", __VA_ARGS__)
#define Ndis6FilterTerminalReturn(...) TestUnexpected("unexpected.return", __VA_ARGS__)
#define Ndis6FilterTerminalCancelSend(...) TestUnexpected("unexpected.cancel_send", __VA_ARGS__)
#define Ndis6FilterTerminalStatus(...) TestUnexpected("unexpected.status", __VA_ARGS__)
#define Ndis6FilterTerminalDevicePnPEvent(...) TestUnexpected("unexpected.device_pnp", __VA_ARGS__)
#define Ndis6FilterTerminalNetPnPEvent(...) ((NDIS_STATUS)TestUnexpected("unexpected.net_pnp", __VA_ARGS__))
#define Ndis6PauseDriverStackLocked(...) ((NDIS_STATUS)TestUnexpected("unexpected.pause", __VA_ARGS__))
#define Ndis6RestartDriverStackLocked(...) ((NDIS_STATUS)TestUnexpected("unexpected.restart", __VA_ARGS__))
#ifndef NDIS_FILTER_TEST_SOURCE
#define NDIS_FILTER_TEST_SOURCE "../../../../drivers/network/ndis/ndis/60filter.c"
#endif
#include NDIS_FILTER_TEST_SOURCE

static void CompleteStatus(PNDIS_OID_REQUEST Request, NDIS_STATUS Status)
{
    if (Active == Request) Active = NULL;
    Check("oid.completion_recognized",
          Ndis6FilterCompleteOidFromMiniport(&Adapter, Request, Status));
}

static void Complete(PNDIS_OID_REQUEST Request)
{
    CompleteStatus(Request, NDIS_STATUS_SUCCESS);
}

static void CancelFromHandler(void)
{
    PNDIS6_FILTER_OID_CONTEXT Context = Ndis6FilterOidGetContext(Active);
    Ndis6FilterOidCancelContext(Context, NULL);
    Check("oid.cancel_deferred_in_handler", CancelCalls == 0);
    MiniportHook = NULL;
}

static void CancelFromUpcall(void)
{
    PNDIS6_FILTER_OID_CONTEXT Context = Ndis6FilterOidGetContext(FilterActive);
    Ndis6FilterOidCancelContext(Context, NULL);
    Check("oid.filter_cancel_deferred_in_upcall", FilterCancelCalls == 0);
    CompletionHook = NULL;
}

static void RetireSelectedFilter(void)
{
    FilterActive = NULL;
    NdisFOidRequestComplete(&Filter, &Requests[0], NDIS_STATUS_SUCCESS);
}

static void CheckClean(void)
{
    Check("oid.all_resources_released", Allocations == Frees);
    Check("oid.context_registry_empty", IsListEmpty(&Extension.FilterOidContextList));
    Check("oid.all_rundowns_released", Rundowns == 0);
    Check("oid.no_queued_work", Jobs == NULL);
    Check("oid.all_module_pins_released", ModulePins == 0 && BindingPins == 0);
    Check("oid.all_lifetime_pins_released", LifecyclePins == 0 && LegacyPins == 0);
}

int main(void)
{
    unsigned Index;
    Adapter.IsNdis6 = TRUE;
    Adapter.Ndis6Context = &Extension;
    Extension.Adapter = &Adapter;
    Extension.DriverBlock = &Driver;
    Extension.Initialized = TRUE;
    Extension.MiniportAdapterContext = &Adapter;
    InitializeListHead(&Extension.FilterModuleList);
    InitializeListHead(&Extension.FilterOidContextList);
    Driver.Characteristics.OidRequestHandler = TestMiniport;
    for (Index = 0; Index < 2; ++Index)
    {
        Requests[Index].Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
        Requests[Index].Header.Revision = NDIS_OID_REQUEST_REVISION_1;
        Requests[Index].Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
        Requests[Index].RequestType = NdisRequestSetInformation;
        Requests[Index].RequestId = &Identities[Index];
        Requests[Index].DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_LOOKAHEAD;
        Requests[Index].DATA.SET_INFORMATION.InformationBuffer = &Identities[Index];
        Requests[Index].DATA.SET_INFORMATION.InformationBufferLength = sizeof(ULONG);
    }
    printf("CASE ordinary_oid_serialization\n");
    Check("oid.first_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.first_dispatched", Calls == 1);
    Check("oid.second_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[1]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.second_waits_for_completion", Calls == 1);
    Complete(&Requests[0]);
    DrainJobs();
    Check("oid.next_dispatched_after_completion", Calls == 2);
    Complete(&Requests[1]);
    Check("oid.completed_once_each", Completions == 2);
    Check("oid.resources_released", Allocations == Frees && IsListEmpty(&Extension.FilterOidContextList));

    printf("CASE dispatch_request_at_dispatch_level\n");
    TestIrql = DISPATCH_LEVEL;
    Check("oid.dpc_request_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    TestIrql = PASSIVE_LEVEL;
    DrainJobs();
    Complete(&Requests[0]);
    DrainJobs();

    printf("CASE filter_holds_request_without_forwarding\n");
    Filter.Signature = NDIS6_FILTER_MODULE_SIGNATURE;
    Filter.Adapter = &Adapter;
    Filter.DriverBlock = &FilterDriver;
    Filter.FilterModuleContext = &Filter;
    Filter.State = NDIS6_FILTER_STATE_RUNNING;
    FilterDriver.Characteristics.OidRequestHandler = TestFilter;
    InsertTailList(&Extension.FilterModuleList, &Filter.ListEntry);
    Check("oid.filter_first_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.filter_first_dispatched", FilterCalls == 1);
    Check("oid.filter_second_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[1]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.filter_second_waits", FilterCalls == 1);
    FilterActive = NULL;
    NdisFOidRequestComplete(&Filter, &Requests[0], NDIS_STATUS_SUCCESS);
    DrainJobs();
    Check("oid.filter_next_after_completion", FilterCalls == 2);
    FilterActive = NULL;
    NdisFOidRequestComplete(&Filter, &Requests[1], NDIS_STATUS_SUCCESS);
    RemoveEntryList(&Filter.ListEntry);
    Check("oid.filter_resources_released", Allocations == Frees && IsListEmpty(&Extension.FilterOidContextList));
    CheckClean();
    printf("CASE synchronous_miniport\n");
    MiniportMode = 1;
    Check("oid.sync_accepted", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    CheckClean();

    printf("CASE inline_completion_before_handler_return\n");
    MiniportMode = 2;
    Check("oid.inline_accepted", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    CheckClean();
    MiniportMode = 0;

    printf("CASE cancel_selected_before_worker\n");
    ExpectedStatus[0] = NDIS_STATUS_REQUEST_ABORTED;
    Index = Calls;
    Check("oid.selected_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    Ndis6FilterOidCancelContext(Ndis6FilterOidGetContext(&Requests[0]), NULL);
    DrainJobs();
    Check("oid.selected_not_submitted", Calls == Index);
    CheckClean();

    printf("CASE cancel_queued_behind_active\n");
    ExpectedStatus[0] = NDIS_STATUS_SUCCESS;
    ExpectedStatus[1] = NDIS_STATUS_REQUEST_ABORTED;
    Check("oid.active_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Index = Calls;
    Check("oid.queued_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[1]) == NDIS_STATUS_PENDING);
    Ndis6FilterOidCancelContext(Ndis6FilterOidGetContext(&Requests[1]), NULL);
    DrainJobs();
    Check("oid.queued_not_submitted", Calls == Index);
    Check("oid.active_request_preserved", Active == &Requests[0]);
    Complete(&Requests[0]);
    DrainJobs();
    CheckClean();

    printf("CASE cancel_during_miniport_handler\n");
    ExpectedStatus[0] = NDIS_STATUS_REQUEST_ABORTED;
    Driver.Characteristics.CancelOidRequestHandler = TestCancel;
    MiniportHook = CancelFromHandler;
    Check("oid.handler_cancel_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.handler_cancel_once", CancelCalls == 1);
    CheckClean();

    printf("CASE queued_filter_cancel_before_dispatch\n");
    InsertTailList(&Extension.FilterModuleList, &Filter.ListEntry);
    FilterDriver.Characteristics.CancelOidRequestHandler = TestFilterCancel;
    ExpectedStatus[0] = NDIS_STATUS_SUCCESS;
    Check("oid.filter_active_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Index = FilterCalls;
    Check("oid.filter_queued_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[1]) == NDIS_STATUS_PENDING);
    Ndis6FilterOidCancelContext(Ndis6FilterOidGetContext(&Requests[1]), NULL);
    DrainJobs();
    Check("oid.filter_queued_not_submitted", FilterCalls == Index);
    Check("oid.no_cancel_before_filter_dispatch", FilterCancelCalls == 0);
    FilterActive = NULL;
    NdisFOidRequestComplete(&Filter, &Requests[0], NDIS_STATUS_SUCCESS);
    DrainJobs();
    CheckClean();

    printf("CASE cancel_frame_retired_after_selection\n");
    {
        PNDIS6_FILTER_OID_CONTEXT Context;
        Check("oid.retire_race_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
        DrainJobs();
        Context = Ndis6FilterOidGetContext(&Requests[0]);
        Ndis6FilterOidReferenceContext(Context);
        ReleaseHookLock = &Context->Lock;
        ReleaseHook = RetireSelectedFilter;
        Ndis6FilterOidCancelContext(Context, NULL);
        Ndis6FilterOidDereferenceContext(Context);
        Check("oid.no_cancel_after_stage_retired", FilterCancelCalls == 0);
        CheckClean();
    }

    printf("CASE filter_forward_and_completion_chain\n");
    FilterMode = 1;
    FilterDriver.Characteristics.OidRequestCompleteHandler = TestFilterComplete;
    ExpectedStatus[1] = NDIS_STATUS_SUCCESS;
    Check("oid.forward_first_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    Check("oid.forward_second_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[1]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Complete(&Requests[0]);
    DrainJobs();
    Complete(&Requests[1]);
    DrainJobs();
    CheckClean();

    printf("CASE cancel_during_filter_completion_upcall\n");
    HoldFilterCompletion = TRUE;
    CompletionHook = CancelFromUpcall;
    ExpectedStatus[0] = NDIS_STATUS_REQUEST_ABORTED;
    Check("oid.upcall_cancel_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Complete(&Requests[0]);
    DrainJobs();
    Check("oid.upcall_cancel_once", FilterCancelCalls == 1);
    HoldFilterCompletion = FALSE;
    CheckClean();

    printf("CASE clone_freed_by_completion_before_worker_tail\n");
    FilterMode = 2;
    MiniportMode = 2;
    ExpectedStatus[0] = NDIS_STATUS_SUCCESS;
    Check("oid.clone_inline_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.clone_freed_inline", CloneRequest == NULL);
    CheckClean();
    MiniportMode = 1;
    Check("oid.clone_sync_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    DrainJobs();
    Check("oid.clone_freed_sync", CloneRequest == NULL);
    CheckClean();
    printf("CASE cancel_filter_before_queue_publication\n");
    {
        PNDIS6_FILTER_OID_CONTEXT Context = Ndis6FilterOidCreateContext(&Adapter, &Requests[0], FALSE, FALSE);
        ExpectedStatus[0] = NDIS_STATUS_REQUEST_ABORTED;
        Index = FilterCalls;
        Check("oid.prepublication_context", Context != NULL);
        Check("oid.prepublication_frame", Ndis6FilterOidPushFrame(Context, &Filter, &Requests[0], NULL) == NDIS_STATUS_SUCCESS);
        Ndis6FilterOidCancelContext(Context, NULL);
        Check("oid.prepublication_no_raw_cancel", FilterCancelCalls == 1);
        Check("oid.prepublication_queued", Ndis6OidStageQueue(Context, Context->Top, &Filter, &Requests[0]) == NDIS_STATUS_PENDING);
        Ndis6FilterOidDereferenceContext(Context);
        DrainJobs();
        Check("oid.prepublication_not_submitted", FilterCalls == Index);
        CheckClean();
    }
    RemoveEntryList(&Filter.ListEntry);
    MiniportMode = 0;

    printf("CASE resource_failure_cleanup\n");
    for (Index = 1; Index <= 3; ++Index)
    {
        FailAllocation = Index;
        Check("oid.allocation_failure_returned", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_RESOURCES);
        FailAllocation = 0;
        CheckClean();
    }
    for (Index = 1; Index <= 3; ++Index)
    {
        FailRundown = Index;
        Check("oid.rundown_failure_returned", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) != NDIS_STATUS_PENDING);
        FailRundown = 0;
        CheckClean();
    }

    printf("CASE adapter_closes_before_worker_dispatch\n");
    ExpectedStatus[0] = NDIS_STATUS_ADAPTER_NOT_READY;
    Check("oid.closing_pending", Ndis6FilterDispatchOidRequest(&Adapter, &Requests[0]) == NDIS_STATUS_PENDING);
    Extension.ProtocolBindingsClosing = TRUE;
    Index = Calls;
    DrainJobs();
    Check("oid.closing_not_submitted", Calls == Index);
    Extension.ProtocolBindingsClosing = FALSE;
    CheckClean();
    printf("RESULT failures=%u\n", Failures);
    return Failures ? 1 : 0;
}
