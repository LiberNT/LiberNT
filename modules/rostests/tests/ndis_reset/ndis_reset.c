/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise the NDIS 6 reset worker without legacy callbacks
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#include <ndis6_internal.h>
#include <ntimage.h>
#include <ndk/rtlfuncs.h>
#include <pseh/pseh2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "thread_host.h"

static unsigned Failures;
static unsigned ResetCalls, IndicationCount, ReleasedReferences, LegacyCompletions;
static NDIS_STATUS Indications[4];
static NDIS_STATUS ResetResult;
static BOOLEAN CompleteInsideCallback;
static BOOLEAN ResetAddressing, FailIoAllocation, FailPoolAllocation;
static BOOLEAN ExternalCallerOwnsAdapter, RaceRunning, RaceArmed;
static BOOLEAN RetryOnResetEnd;
static unsigned AcquireCalls, DenyAcquire;
static __declspec(thread) unsigned TestLockDepth;
static __declspec(thread) KIRQL TestIrql;
static KIRQL CompletionIrql;
static LONG LiveLifecycle, LiveLegacy;
static unsigned ReleasedLegacy, RestoreCalls, IoAllocations, IoFrees, PoolAllocations, PoolFrees;
static NDIS_STATUS RestoreResult;
static LOGICAL_ADAPTER Adapter;
static NDIS6_ADAPTER_EXT Extension;
static NDIS6_DRIVER_BLOCK Driver;
static DEVICE_OBJECT Device;

typedef struct _TEST_IO_WORKITEM
{
    PDEVICE_OBJECT DeviceObject;
    PIO_WORKITEM_ROUTINE Routine;
    PVOID Context;
} TEST_IO_WORKITEM;

static TEST_IO_WORKITEM *QueuedIoWork;
static PWORK_QUEUE_ITEM QueuedResetWork;

static void Check(const char *Name, BOOLEAN Passed)
{
    printf("%s %s\n", Passed ? "PASS" : "FAIL", Name);
    if (!Passed)
        ++Failures;
}

static void PauseOldTail(void)
{
    if (!RaceArmed || ResetCalls != 1 || Extension.ResetWorkerActive != 0)
        return;
    RaceArmed = FALSE;
    ResetTestHostSignal(0);
    Check("race.old_tail_resumed", ResetTestHostWait(1));
}

static VOID TestAcquireLock(PKSPIN_LOCK Lock, PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Lock);
    *OldIrql = TestIrql;
    Check("reset.lock_not_recursive", TestLockDepth == 0);
    ResetTestHostEnter();
    ++TestLockDepth;
    TestIrql = DISPATCH_LEVEL;
}

static VOID TestReleaseLock(PKSPIN_LOCK Lock, KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Lock);
    --TestLockDepth;
    ResetTestHostLeave();
    TestIrql = OldIrql;
    if (!TestLockDepth)
        PauseOldTail();
}

static VOID TestReleaseRundown(PEX_RUNDOWN_REF Rundown)
{
    Check("reset.reference_owner", Rundown == &Extension.LifecycleRundown ||
          Rundown == &Extension.LegacyRequestRundown);
    if (Rundown == &Extension.LifecycleRundown)
    {
        --LiveLifecycle;
        ++ReleasedReferences;
    }
    else
    {
        --LiveLegacy;
        ++ReleasedLegacy;
    }
    Check("reset.reference_nonnegative", LiveLifecycle >= 0 && LiveLegacy >= 0);
}

static BOOLEAN TestAcquireRundown(PEX_RUNDOWN_REF Rundown)
{
    Check("reset.acquire_owner", Rundown == &Extension.LifecycleRundown ||
          Rundown == &Extension.LegacyRequestRundown);
    if (++AcquireCalls == DenyAcquire)
        return FALSE;
    if (Rundown == &Extension.LifecycleRundown) ++LiveLifecycle;
    else ++LiveLegacy;
    return TRUE;
}

static LONG TestCompare(volatile LONG *Target, LONG Exchange, LONG Comparand)
{
    if (Target == &Extension.ResetState || Target == &Extension.ResetWorkerActive)
        Check("reset.state_access_owned", LiveLifecycle > 0 || ExternalCallerOwnsAdapter);
    return _InterlockedCompareExchange(Target, Exchange, Comparand);
}

static LONG TestExchange(volatile LONG *Target, LONG Value)
{
    LONG Previous;
    if (Target == &Extension.ResetState || Target == &Extension.ResetWorkerActive)
        Check("reset.state_access_owned", LiveLifecycle > 0 || ExternalCallerOwnsAdapter);
    Previous = _InterlockedExchange(Target, Value);
    if (Target == &Extension.ResetWorkerActive && Value == 0 && !TestLockDepth)
        PauseOldTail();
    return Previous;
}

PDEVICE_OBJECT Ndis6GetIoWorkItemDeviceObject(NDIS_HANDLE Handle)
{
    Check("work.adapter_handle", Handle == &Adapter);
    return &Device;
}

static PIO_WORKITEM NTAPI TestIoAllocate(PDEVICE_OBJECT Object)
{
    TEST_IO_WORKITEM *Item;
    Check("work.device", Object == &Device);
    if (FailIoAllocation) return NULL;
    Item = calloc(1, sizeof(*Item));
    if (Item) { Item->DeviceObject = Object; ++IoAllocations; }
    return (PIO_WORKITEM)Item;
}

static VOID NTAPI TestIoFree(PIO_WORKITEM Work)
{
    Check("work.free_not_queued", (TEST_IO_WORKITEM *)Work != QueuedIoWork);
    ++IoFrees;
    free(Work);
}

static VOID NTAPI TestIoQueue(PIO_WORKITEM Work, PIO_WORKITEM_ROUTINE Routine,
                            WORK_QUEUE_TYPE Type, PVOID Context)
{
    TEST_IO_WORKITEM *Item = (TEST_IO_WORKITEM *)Work;
    Check("work.queue_critical", Type == CriticalWorkQueue);
    Check("work.single_queue", QueuedIoWork == NULL);
    Item->Routine = Routine;
    Item->Context = Context;
    QueuedIoWork = Item;
}

static PVOID NTAPI TestPoolAllocate(POOL_TYPE Type, SIZE_T Size, ULONG Tag)
{
    PVOID Result;
    UNREFERENCED_PARAMETER(Tag);
    Check("work.nonpaged", Type == NonPagedPool || Type == NonPagedPoolNx);
    if (FailPoolAllocation) return NULL;
    Result = calloc(1, Size);
    if (Result) ++PoolAllocations;
    return Result;
}

static VOID NTAPI TestPoolFree(PVOID Memory, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    ++PoolFrees;
    free(Memory);
}

static ULONG __cdecl TestDebugPrint(PCSTR Format, ...)
{
    va_list Arguments;
    int Count;
    va_start(Arguments, Format);
    Count = vprintf(Format, Arguments);
    va_end(Arguments);
    return Count > 0 ? (ULONG)Count : 0;
}

#define DbgPrint TestDebugPrint
#define IoAllocateWorkItem TestIoAllocate
#define IoFreeWorkItem TestIoFree
#define IoQueueWorkItem TestIoQueue
#undef ExAllocatePoolWithTag
#undef ExFreePoolWithTag
#define ExAllocatePoolWithTag TestPoolAllocate
#define ExFreePoolWithTag TestPoolFree
#ifndef NDIS_WORKITEM_TEST_SOURCE
#define NDIS_WORKITEM_TEST_SOURCE "../../../../drivers/network/ndis/ndis/workitem.c"
#endif
#include NDIS_WORKITEM_TEST_SOURCE

static void DrainIoWork(void)
{
    TEST_IO_WORKITEM *Item = QueuedIoWork;
    PIO_WORKITEM_ROUTINE Routine;
    PDEVICE_OBJECT Object;
    PVOID Context;
    KIRQL Saved = TestIrql;
    Check("work.queued", Item != NULL);
    if (!Item) return;
    Routine = Item->Routine;
    Object = Item->DeviceObject;
    Context = Item->Context;
    QueuedIoWork = NULL;
    TestIrql = PASSIVE_LEVEL;
    Routine(Object, Context);
    TestIrql = Saved;
}

static VOID TestQueueWorkItem(PWORK_QUEUE_ITEM WorkItem, WORK_QUEUE_TYPE QueueType)
{
    UNREFERENCED_PARAMETER(QueueType);
    QueuedResetWork = WorkItem;
    Check("reset.root_device_work_item", FALSE);
}

static VOID TestIndicateStatus(NDIS_HANDLE Handle, PNDIS_STATUS_INDICATION Status)
{
    Check("reset.indication_unlocked", TestLockDepth == 0);
    Check("reset.modern_handle", Handle == &Adapter && Status->SourceHandle == Handle);
    Check("reset.indication_header",
          Status->Header.Type == NDIS_OBJECT_TYPE_STATUS_INDICATION &&
          Status->Header.Revision == NDIS_STATUS_INDICATION_REVISION_1 &&
          Status->Header.Size == NDIS_SIZEOF_STATUS_INDICATION_REVISION_1);
    Check("reset.indication_payload", Status->PortNumber == 0 && Status->StatusBuffer == NULL &&
          Status->StatusBufferSize == 0 && Status->DestinationHandle == NULL && Status->RequestId == NULL);
    if (IndicationCount < RTL_NUMBER_OF(Indications))
        Indications[IndicationCount] = Status->StatusCode;
    ++IndicationCount;
    if (RetryOnResetEnd && Status->StatusCode == NDIS_STATUS_RESET_END)
        Check("reset.end_reentrant_rejected", Ndis6QueueMiniportReset(&Adapter) == NDIS_STATUS_RESET_IN_PROGRESS);
}

static NDIS_STATUS TestResetHandler(PLOGICAL_ADAPTER Miniport, PBOOLEAN AddressingReset)
{
    Check("reset.callback_unlocked", TestLockDepth == 0);
    Check("reset.callback_handle", Miniport == &Adapter);
    ++ResetCalls;
    *AddressingReset = ResetAddressing;
    if (RaceRunning && ResetCalls == 2)
    {
        Ndis6ResetMiniportComplete(Miniport, NDIS_STATUS_SUCCESS, FALSE);
        ResetTestHostSignal(1);
        Check("race.old_root_returned", ResetTestHostWait(2));
        Check("race.new_reset_rejected", Ndis6QueueMiniportReset(Miniport) == NDIS_STATUS_RESET_IN_PROGRESS);
        return NDIS_STATUS_PENDING;
    }
    if (CompleteInsideCallback)
    {
        KIRQL Saved = TestIrql;
        TestIrql = CompletionIrql;
        Ndis6ResetMiniportComplete(Miniport, NDIS_STATUS_SUCCESS, ResetAddressing);
        TestIrql = Saved;
    }
    return ResetResult;
}

static VOID NTAPI TestLegacyCompletion(NDIS_HANDLE Handle, NDIS_STATUS Status, BOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(AddressingReset);
    ++LegacyCompletions;
}

static NDIS_STATUS NTAPI TestRawReset(NDIS_HANDLE Handle, PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(Handle);
    *AddressingReset = FALSE;
    return NDIS_STATUS_PENDING;
}

static VOID TestRestoreAddressing(PLOGICAL_ADAPTER Miniport)
{
    UNREFERENCED_PARAMETER(Miniport);
    ++RestoreCalls;
    Check("reset.restore_passive", TestIrql == PASSIVE_LEVEL);
    Check("reset.restore_lifetime_owned", LiveLifecycle > 0 && LiveLegacy > 0);
}

static NDIS_STATUS TestRestoreRequest(PLOGICAL_ADAPTER Miniport, PNDIS_REQUEST Request)
{
    Check("reset.restore_adapter", Miniport == &Adapter);
    Check("reset.restore_lookahead", Request->RequestType == NdisRequestSetInformation &&
          Request->DATA.SET_INFORMATION.Oid == OID_GEN_CURRENT_LOOKAHEAD &&
          Request->DATA.SET_INFORMATION.InformationBufferLength == sizeof(ULONG) &&
          Request->DATA.SET_INFORMATION.InformationBuffer != NULL &&
          *(PULONG)Request->DATA.SET_INFORMATION.InformationBuffer == Adapter.NdisMiniportBlock.CurrentLookahead);
    TestRestoreAddressing(Miniport);
    return RestoreResult;
}

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock
#undef ExReleaseRundownProtection
#undef ExAcquireRundownProtection
#define KeAcquireSpinLock TestAcquireLock
#define KeReleaseSpinLock TestReleaseLock
#define ExReleaseRundownProtection TestReleaseRundown
#define ExAcquireRundownProtection TestAcquireRundown
#define ExQueueWorkItem TestQueueWorkItem
#define NdisMIndicateStatusEx TestIndicateStatus
#define Ndis6CallResetHandlerEx TestResetHandler
#define MiniResetComplete TestLegacyCompletion
#define MiniDoAddressingReset TestRestoreAddressing
#define Ndis6LegacyDoRequest TestRestoreRequest
#undef KeGetCurrentIrql
#define KeGetCurrentIrql() TestIrql
#undef InterlockedCompareExchange
#undef InterlockedExchange
#define InterlockedCompareExchange TestCompare
#define InterlockedExchange TestExchange
#ifndef NDIS_RESET_TEST_SOURCE
#define NDIS_RESET_TEST_SOURCE "../../../../drivers/network/ndis/ndis/60reset.c"
#endif
#include NDIS_RESET_TEST_SOURCE

static BOOLEAN RunWorker(void)
{
    NTSTATUS Exception = STATUS_SUCCESS;
    NDIS_STATUS Queued;
    PWORK_QUEUE_ITEM Work;

    _SEH2_TRY
    {
        Queued = Ndis6QueueMiniportReset(&Adapter);
        Check("reset.queue_accepted", Queued == NDIS_STATUS_PENDING);
        if (Queued == NDIS_STATUS_PENDING)
        {
            if (QueuedResetWork)
            {
                Work = QueuedResetWork;
                QueuedResetWork = NULL;
                Work->WorkerRoutine(Work->Parameter);
            }
            else
                DrainIoWork();
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    printf("INFO reset.exception=0x%08lx\n", (ULONG)Exception);
    Check("reset.no_exception", Exception == STATUS_SUCCESS);
    return Exception == STATUS_SUCCESS;
}

static void Initialize(NDIS_STATUS Result, BOOLEAN InlineCompletion)
{
    RtlZeroMemory(&Adapter, sizeof(Adapter));
    RtlZeroMemory(&Extension, sizeof(Extension));
    RtlZeroMemory(Indications, sizeof(Indications));
    Adapter.IsNdis6 = TRUE;
    Adapter.Ndis6Context = &Extension;
    Adapter.NdisMiniportBlock.CurrentLookahead = 128;
    Extension.Adapter = &Adapter;
    RtlZeroMemory(&Driver, sizeof(Driver));
    Extension.DriverBlock = &Driver;
    Driver.Characteristics.ResetHandlerEx = TestRawReset;
    Extension.ResetState = NDIS6_RESET_IDLE;
    ResetCalls = IndicationCount = ReleasedReferences = LegacyCompletions = 0;
    ResetResult = Result;
    CompleteInsideCallback = InlineCompletion;
    ResetAddressing = FailIoAllocation = FailPoolAllocation = FALSE;
    TestIrql = CompletionIrql = PASSIVE_LEVEL;
    LiveLifecycle = LiveLegacy = 0;
    ReleasedLegacy = RestoreCalls = IoAllocations = IoFrees = PoolAllocations = PoolFrees = 0;
    RestoreResult = NDIS_STATUS_SUCCESS;
    QueuedIoWork = NULL;
    QueuedResetWork = NULL;
    ExternalCallerOwnsAdapter = RaceRunning = RaceArmed = RetryOnResetEnd = FALSE;
    AcquireCalls = DenyAcquire = 0;
}

static void CheckFinished(NDIS_STATUS Expected)
{
    Check("reset.driver_called_once", ResetCalls == 1);
    Check("reset.modern_start_end", IndicationCount == 2 &&
          Indications[0] == NDIS_STATUS_RESET_START && Indications[1] == NDIS_STATUS_RESET_END);
    Check("reset.no_legacy_completion", LegacyCompletions == 0);
    Check("reset.references_released", ReleasedReferences == 2);
    Check("reset.halt_references_released", ReleasedLegacy == 2 && LiveLegacy == 0);
    Check("reset.idle", Extension.ResetState == NDIS6_RESET_IDLE && Extension.ResetWorkerActive == 0);
    Check("reset.result", Adapter.NdisMiniportBlock.ResetStatus == Expected);
}

static NDIS_IO_WORKITEM_FUNCTION TestSelfFree;

static VOID TestSelfFree(PVOID Context, NDIS_HANDLE Work)
{
    Check("work.context", Context == &Adapter);
    NdisFreeIoWorkItem(Work);
}

static void CompleteAndStartNext(void)
{
    Check("race.old_tail_reached", ResetTestHostWait(0));
    Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, FALSE);
    RunWorker();
}

int main(void)
{
    NDIS_HANDLE Work;
    unsigned Index;
    int ThreadStarted;
    if (!ResetTestHostInitialize())
    {
        Check("race.host_initialized", FALSE);
        return 1;
    }
    (void)TestIndicateStatus;
    (void)TestRestoreAddressing;
    (void)TestLegacyCompletion;
    (void)TestRestoreRequest;
    (void)TestQueueWorkItem;
    Initialize(NDIS_STATUS_SUCCESS, FALSE);
    printf("CASE io_workitem_self_free\n");
    Work = NdisAllocateIoWorkItem(&Adapter);
    Check("work.allocate", Work != NULL);
    if (Work)
    {
        NdisQueueIoWorkItem(Work, TestSelfFree, &Adapter);
        DrainIoWork();
        Check("work.self_free_released", IoAllocations == IoFrees && PoolAllocations == PoolFrees);
    }
    Initialize(NDIS_STATUS_SUCCESS, FALSE);
    printf("CASE synchronous_success\n");
    if (RunWorker())
        CheckFinished(NDIS_STATUS_SUCCESS);

    Initialize(NDIS_STATUS_FAILURE, FALSE);
    printf("CASE synchronous_failure\n");
    if (RunWorker())
        CheckFinished(NDIS_STATUS_FAILURE);

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE asynchronous_completion\n");
    if (RunWorker())
    {
        Check("reset.pending", Extension.ResetState == NDIS6_RESET_PENDING && ReleasedReferences == 1);
        Check("reset.start_before_completion", IndicationCount == 1 && Indications[0] == NDIS_STATUS_RESET_START);
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, FALSE);
        CheckFinished(NDIS_STATUS_SUCCESS);
        ExternalCallerOwnsAdapter = TRUE;
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_FAILURE, FALSE);
        ExternalCallerOwnsAdapter = FALSE;
        Check("reset.duplicate_completion_ignored", IndicationCount == 2 && ReleasedReferences == 2 &&
              Adapter.NdisMiniportBlock.ResetStatus == NDIS_STATUS_SUCCESS);
    }

    Initialize(NDIS_STATUS_PENDING, TRUE);
    printf("CASE completion_inside_callback\n");
    if (RunWorker())
        CheckFinished(NDIS_STATUS_SUCCESS);

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE dpc_addressing_completion\n");
    ResetAddressing = TRUE;
    if (RunWorker())
    {
        TestIrql = DISPATCH_LEVEL;
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, TRUE);
        Check("reset.dpc_deferred", QueuedIoWork != NULL && RestoreCalls == 0);
        Check("reset.pending_ownership", LiveLifecycle == 1 && LiveLegacy == 1);
        Check("reset.restore_not_idle", Extension.ResetState != NDIS6_RESET_IDLE);
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_FAILURE, TRUE);
        Check("reset.restore_duplicate_ignored", IoAllocations <= 2 && IndicationCount == 1);
        DrainIoWork();
        Check("reset.restored_once", RestoreCalls == 1);
        CheckFinished(NDIS_STATUS_SUCCESS);
        Check("work.resources_released", IoAllocations == IoFrees && PoolAllocations == PoolFrees);
    }

    Initialize(NDIS_STATUS_PENDING, TRUE);
    printf("CASE dpc_completion_inside_callback\n");
    ResetAddressing = TRUE;
    CompletionIrql = DISPATCH_LEVEL;
    if (RunWorker())
    {
        Check("reset.inline_deferred", QueuedIoWork != NULL && RestoreCalls == 0);
        Check("reset.worker_tail_keeps_pending", Extension.ResetState != NDIS6_RESET_IDLE);
        DrainIoWork();
        CheckFinished(NDIS_STATUS_SUCCESS);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE io_allocation_failure\n");
    if (RunWorker())
    {
        FailIoAllocation = TRUE;
        TestIrql = DISPATCH_LEVEL;
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, TRUE);
        Check("reset.allocation_failure_no_restore", RestoreCalls == 0 && QueuedIoWork == NULL);
        CheckFinished(NDIS_STATUS_RESOURCES);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE wrapper_allocation_failure\n");
    if (RunWorker())
    {
        FailPoolAllocation = TRUE;
        TestIrql = DISPATCH_LEVEL;
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, TRUE);
        Check("reset.wrapper_failure_no_restore", RestoreCalls == 0 && QueuedIoWork == NULL);
        Check("work.failed_wrapper_releases_io", IoAllocations == IoFrees);
        CheckFinished(NDIS_STATUS_RESOURCES);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE restore_failure\n");
    RestoreResult = NDIS_STATUS_FAILURE;
    if (RunWorker())
    {
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, TRUE);
        DrainIoWork();
        CheckFinished(NDIS_STATUS_FAILURE);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE previous_failure_preserved\n");
    RestoreResult = NDIS_STATUS_RESOURCES;
    if (RunWorker())
    {
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_FAILURE, TRUE);
        DrainIoWork();
        CheckFinished(NDIS_STATUS_FAILURE);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE restore_adapter_not_ready\n");
    RestoreResult = NDIS_STATUS_ADAPTER_NOT_READY;
    if (RunWorker())
    {
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_SUCCESS, TRUE);
        DrainIoWork();
        CheckFinished(NDIS_STATUS_ADAPTER_NOT_READY);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE previous_failure_restore_success\n");
    if (RunWorker())
    {
        Ndis6ResetMiniportComplete(&Adapter, NDIS_STATUS_FAILURE, TRUE);
        DrainIoWork();
        CheckFinished(NDIS_STATUS_FAILURE);
    }

    Initialize(NDIS_STATUS_SUCCESS, FALSE);
    printf("CASE reentrant_reset_end\n");
    RetryOnResetEnd = TRUE;
    if (RunWorker())
        Check("reset.end_reentrant_unwound", ResetCalls == 1 && IndicationCount == 2 &&
              LiveLifecycle == 0 && LiveLegacy == 0 && Extension.ResetState == NDIS6_RESET_IDLE &&
              Adapter.NdisMiniportBlock.ResetStatus == NDIS_STATUS_SUCCESS);

    for (Index = 1; Index <= 4; ++Index)
    {
        Initialize(NDIS_STATUS_PENDING, FALSE);
        printf("CASE rundown_failure_%u\n", Index);
        DenyAcquire = Index;
        Check("reset.rundown_refused", Ndis6QueueMiniportReset(&Adapter) == NDIS_STATUS_CLOSING);
        Check("reset.rundown_unwound", LiveLifecycle == 0 && LiveLegacy == 0 &&
              Extension.ResetState == NDIS6_RESET_IDLE && QueuedIoWork == NULL && QueuedResetWork == NULL);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE closing_admission\n");
    Extension.ProtocolBindingsClosing = 1;
    Check("reset.closing_refused", Ndis6QueueMiniportReset(&Adapter) == NDIS_STATUS_CLOSING);
    Check("reset.closing_unwound", LiveLifecycle == 0 && LiveLegacy == 0 && QueuedIoWork == NULL);

    for (Index = 0; Index < 2; ++Index)
    {
        Initialize(NDIS_STATUS_PENDING, FALSE);
        printf("CASE root_allocation_failure_%u\n", Index);
        FailIoAllocation = Index == 0;
        FailPoolAllocation = Index == 1;
        Check("reset.root_allocation_refused", Ndis6QueueMiniportReset(&Adapter) == NDIS_STATUS_RESOURCES);
        Check("reset.root_allocation_unwound", LiveLifecycle == 0 && LiveLegacy == 0 &&
              Extension.ResetState == NDIS6_RESET_IDLE && IoAllocations == IoFrees && PoolAllocations == PoolFrees);
    }

    Initialize(NDIS_STATUS_PENDING, FALSE);
    printf("CASE overlapping_reset_generations\n");
    RaceRunning = RaceArmed = TRUE;
    ThreadStarted = ResetTestHostRun(CompleteAndStartNext);
    Check("race.thread_created", ThreadStarted);
    if (ThreadStarted)
    {
        RunWorker();
        ResetTestHostSignal(2);
        if (!ResetTestHostJoin())
        {
            Check("race.thread_finished", FALSE);
            return 1;
        }
        Check("race.thread_finished", TRUE);
        Check("race.both_resets_completed", ResetCalls == 2 && IndicationCount == 4 &&
              LiveLifecycle == 0 && LiveLegacy == 0 && Extension.ResetState == NDIS6_RESET_IDLE &&
              IoAllocations == IoFrees && PoolAllocations == PoolFrees);
    }
    ResetTestHostDestroy();
    printf("RESULT failures=%u\n", Failures);
    return Failures ? 1 : 0;
}
