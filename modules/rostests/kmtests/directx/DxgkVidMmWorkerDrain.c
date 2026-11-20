/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     dxgkrnl VidMm destroy-worker drain tests
 */

#include <kmt_test.h>
#include "vidmm_worker_drain_core.h"

typedef struct _DXGK_VIDMM_WORKER_DRAIN_TEST_STATE
{
    LONG ActiveWorkers;
    BOOLEAN AdmissionBlocked;
    BOOLEAN DrainedEventSignaled;
} DXGK_VIDMM_WORKER_DRAIN_TEST_STATE, *PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE;

static BOOLEAN DxgkVidMmWorkerDrainTestAdmit(_Inout_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    BOOLEAN ResetEvent;
    BOOLEAN Admitted;

    Admitted = DxgkVidMmWorkerDrainCoreTryAdmitLocked(&State->ActiveWorkers, State->AdmissionBlocked, &ResetEvent);
    if (ResetEvent)
        State->DrainedEventSignaled = FALSE;
    return Admitted;
}

static BOOLEAN DxgkVidMmWorkerDrainTestRetire(_Inout_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    BOOLEAN SetEvent;
    BOOLEAN Retired;

    Retired = DxgkVidMmWorkerDrainCoreRetireLocked(&State->ActiveWorkers, &SetEvent);
    if (SetEvent)
        State->DrainedEventSignaled = TRUE;
    return Retired;
}

static VOID DxgkVidMmWorkerDrainTestInitialize(_Out_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->DrainedEventSignaled = TRUE;
}

static VOID DxgkVidMmWorkerDrainTestTransitions(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "zero workers begin drained");
    ok_bool_true(State.DrainedEventSignaled, "zero workers begin with a signaled event");
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "first worker is admitted");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "zero-to-one admission resets the event");
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "second worker is admitted");
    ok_eq_long(State.ActiveWorkers, 2);
    ok_bool_false(State.DrainedEventSignaled, "nonzero admission leaves the event reset");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "first worker retires");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "nonfinal retirement leaves the event reset");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "last worker retires");
    ok_eq_long(State.ActiveWorkers, 0);
    ok_bool_true(State.DrainedEventSignaled, "one-to-zero retirement signals the event");
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "last retirement is drained");
}

static VOID DxgkVidMmWorkerDrainTestQuiesceInterleaving(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "worker is admitted before quiesce");
    State.AdmissionBlocked = TRUE;
    ok_bool_false(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "quiesce observes the live worker under the lock");
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "closed admission rejects a racing worker");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "rejected admission does not corrupt the event");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "live worker retires after admission closes");
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "lock recheck observes the drain");
    ok_bool_true(State.DrainedEventSignaled, "the final retirement wakes quiesce");
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "admission remains closed after the wake");
    State.AdmissionBlocked = FALSE;
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "resume admits a new worker");
    ok_bool_false(State.DrainedEventSignaled, "new admission resets the prior drain signal before unlock");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "resumed worker retires");
    ok_bool_true(State.DrainedEventSignaled, "resumed worker restores the drained state");
}

static VOID DxgkVidMmWorkerDrainTestInvalidTransitions(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_false(DxgkVidMmWorkerDrainTestRetire(&State), "zero worker count cannot underflow");
    ok_eq_long(State.ActiveWorkers, 0);
    ok_bool_true(State.DrainedEventSignaled, "underflow rejection leaves the event signaled");
    State.ActiveWorkers = MAXLONG;
    State.DrainedEventSignaled = FALSE;
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "maximum worker count cannot overflow");
    ok_eq_long(State.ActiveWorkers, MAXLONG);
    ok_bool_false(State.DrainedEventSignaled, "overflow rejection leaves the event unchanged");
    State.ActiveWorkers = -1;
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "corrupt negative count rejects admission");
    ok_bool_false(DxgkVidMmWorkerDrainTestRetire(&State), "corrupt negative count rejects retirement");
}

typedef struct _DXGK_VIDMM_WORKER_DRAIN_RESET_TEST
{
    FAST_MUTEX Lock;
    LONG ActiveWorkers;
    LONG AdmissionBlocked;
    KEVENT DrainedEvent;
    DXGK_VIDMM_WORKER_DRAIN_GATE Gate;
    KMUTEX ResetMutex;
    KEVENT ResetOwned;
    KEVENT WorkerWaiting;
    BOOLEAN Reentered;
    LONG WorkersAfterReenter;
} DXGK_VIDMM_WORKER_DRAIN_RESET_TEST, *PDXGK_VIDMM_WORKER_DRAIN_RESET_TEST;

static VOID NTAPI DxgkVidMmWorkerDrainTestResetWorker(_In_ PVOID Context)
{
    PDXGK_VIDMM_WORKER_DRAIN_RESET_TEST Test = Context;

    KeWaitForSingleObject(&Test->ResetOwned, Executive, KernelMode, FALSE, NULL);
    DxgkVidMmWorkerDrainCoreYield(&Test->Gate);
    KeSetEvent(&Test->WorkerWaiting, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Test->ResetMutex, Executive, KernelMode, FALSE, NULL);
    KeReleaseMutex(&Test->ResetMutex, FALSE);
    Test->Reentered = DxgkVidMmWorkerDrainCoreReenter(&Test->Gate);
    Test->WorkersAfterReenter = Test->ActiveWorkers;
    if (Test->Reentered)
        DxgkVidMmWorkerDrainCoreYield(&Test->Gate);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* The reset owns the boundary and drains counted workers while a counted worker waits for that boundary. */
static VOID DxgkVidMmWorkerDrainTestResetBoundary(_In_ BOOLEAN ResetReopensAdmission)
{
    PDXGK_VIDMM_WORKER_DRAIN_RESET_TEST Test;
    LARGE_INTEGER Timeout;
    PKTHREAD Thread;
    NTSTATUS Status;

    Test = ExAllocatePoolZero(NonPagedPool, sizeof(*Test), 'tDmV');
    if (skip(Test != NULL, "No memory\n"))
        return;
    ExInitializeFastMutex(&Test->Lock);
    KeInitializeEvent(&Test->DrainedEvent, NotificationEvent, TRUE);
    KeInitializeMutex(&Test->ResetMutex, 0);
    KeInitializeEvent(&Test->ResetOwned, NotificationEvent, FALSE);
    KeInitializeEvent(&Test->WorkerWaiting, NotificationEvent, FALSE);
    Test->Gate.Lock = &Test->Lock;
    Test->Gate.ActiveWorkers = &Test->ActiveWorkers;
    Test->Gate.AdmissionBlocked = &Test->AdmissionBlocked;
    Test->Gate.DrainedEvent = &Test->DrainedEvent;

    ok_bool_true(DxgkVidMmWorkerDrainCoreReenter(&Test->Gate), "the worker is counted before the reset");
    ok_eq_long(Test->ActiveWorkers, 1);
    ok_eq_long(KeReadStateEvent(&Test->DrainedEvent), 0);
    Thread = KmtStartThread(DxgkVidMmWorkerDrainTestResetWorker, Test);

    KeWaitForSingleObject(&Test->ResetMutex, Executive, KernelMode, FALSE, NULL);
    ExAcquireFastMutex(&Test->Lock);
    InterlockedExchange(&Test->AdmissionBlocked, 1);
    ExReleaseFastMutex(&Test->Lock);
    KeSetEvent(&Test->ResetOwned, IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart = -5LL * 10 * 1000 * 1000;
    Status = KeWaitForSingleObject(&Test->DrainedEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Test->ActiveWorkers, 0);
    Status = KeWaitForSingleObject(&Test->WorkerWaiting, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (ResetReopensAdmission)
    {
        ExAcquireFastMutex(&Test->Lock);
        InterlockedExchange(&Test->AdmissionBlocked, 0);
        ExReleaseFastMutex(&Test->Lock);
    }
    KeReleaseMutex(&Test->ResetMutex, FALSE);
    KmtFinishThread(Thread, NULL);

    ok_eq_bool(Test->Reentered, ResetReopensAdmission);
    ok_eq_long(Test->WorkersAfterReenter, ResetReopensAdmission ? 1 : 0);
    ok_eq_long(Test->ActiveWorkers, 0);
    ok(KeReadStateEvent(&Test->DrainedEvent) != 0, "the drained event is signaled once the worker leaves\n");
    ExFreePoolWithTag(Test, 'tDmV');
}

START_TEST(DxgkVidMmWorkerDrain)
{
    DxgkVidMmWorkerDrainTestTransitions();
    DxgkVidMmWorkerDrainTestQuiesceInterleaving();
    DxgkVidMmWorkerDrainTestInvalidTransitions();
    DxgkVidMmWorkerDrainTestResetBoundary(TRUE);
    DxgkVidMmWorkerDrainTestResetBoundary(FALSE);
}
