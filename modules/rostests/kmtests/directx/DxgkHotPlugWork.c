/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     dxgkrnl hot-plug rebuild worker-state tests
 */

#include <kmt_test.h>
#include "hotplug_work_core.h"

static VOID DxgkHotPlugWorkTestCoalescing(VOID)
{
    volatile LONG64 Generation = 0;
    volatile LONG Active = 0;
    LONG64 ObservedGeneration;

    { LONGLONG Observed = DxgkHotPlugWorkCorePublishLocked(&Generation); ok_eq_longlong(Observed, 1); }
    ok_bool_true(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "idle publication activates the worker");
    ok_bool_false(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "an active worker cannot be queued twice");
    ObservedGeneration = Generation;
    { LONGLONG Observed = DxgkHotPlugWorkCorePublishLocked(&Generation); ok_eq_longlong(Observed, 2); }
    ok_bool_false(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, ObservedGeneration, TRUE), "publication during rebuild keeps the worker active");
    ok_eq_long(Active, 1);
    ObservedGeneration = Generation;
    ok_bool_true(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, ObservedGeneration, TRUE), "a stable generation retires the worker");
    ok_eq_long(Active, 0);
}

static VOID DxgkHotPlugWorkTestDrainWithoutRebuild(VOID)
{
    volatile LONG64 Generation = 0;
    volatile LONG Active = 0;
    LONG64 Rebuilt = 0;

    ok_bool_false(DxgkHotPlugWorkCoreNeedsRebuildLocked(&Generation, Rebuilt), "a connection-change drain alone needs no rebuild");
    ok_bool_true(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "a drain request activates the worker");
    ok_bool_true(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, Generation, TRUE), "the drain retires without a new generation");
    ok_eq_long(Active, 0);
    { LONGLONG Observed = DxgkHotPlugWorkCorePublishLocked(&Generation); ok_eq_longlong(Observed, 1); }
    ok_bool_true(DxgkHotPlugWorkCoreNeedsRebuildLocked(&Generation, Rebuilt), "a topology change needs a rebuild");
    Rebuilt = Generation;
    ok_bool_false(DxgkHotPlugWorkCoreNeedsRebuildLocked(&Generation, Rebuilt), "a rebuilt generation is not rebuilt again");
}

static VOID DxgkHotPlugWorkTestRebuildAwaitsEnumeration(VOID)
{
    volatile LONG64 Epoch = 0;
    volatile LONG Enumerated = 0;
    volatile LONG64 Generation = 0;
    LONG64 ExpectedEpoch;

    { LONGLONG Observed = DxgkHotPlugWorkCorePublishLocked(&Generation); ok_eq_longlong(Observed, 1); }
    ok_bool_true(DxgkHotPlugWorkCoreAwaitsEnumerationLocked(&Enumerated), "a rebuild before the first child enumeration is left to it");
    ExpectedEpoch = DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(&Epoch, &Enumerated);
    ok_bool_true(DxgkHotPlugWorkCoreAwaitsEnumerationLocked(&Enumerated), "a rebuild during a child enumeration is left to it");
    ok_bool_true(DxgkHotPlugWorkCorePublishEnumerationLocked(&Epoch, &Enumerated, ExpectedEpoch), "the enumeration publishes its children");
    ok_bool_false(DxgkHotPlugWorkCoreAwaitsEnumerationLocked(&Enumerated), "a rebuild after the enumeration runs");
}

static VOID DxgkHotPlugWorkTestLifecycleClose(VOID)
{
    volatile LONG64 Generation = 7;
    volatile LONG Active = 0;

    ok_bool_true(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "worker activates before stop");
    { LONGLONG Observed = DxgkHotPlugWorkCorePublishLocked(&Generation); ok_eq_longlong(Observed, 8); }
    ok_bool_true(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, 7, FALSE), "stop retires the worker even when a generation is pending");
    ok_eq_long(Active, 0);
    ok_bool_true(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "restart can activate the retained generation");
    ok_bool_true(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, Generation, TRUE), "restart drains the retained generation");
}

static VOID DxgkHotPlugWorkTestEnumerationEpoch(VOID)
{
    volatile LONG64 Epoch = 0;
    volatile LONG Enumerated = 1;
    LONG64 FirstEpoch;
    LONG64 SecondEpoch;

    FirstEpoch = DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(&Epoch, &Enumerated);
    ok_eq_longlong(FirstEpoch, 1);
    ok_eq_long(Enumerated, 0);
    { NTSTATUS Observed = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Epoch, &Enumerated, FirstEpoch); ok_eq_hex(Observed, STATUS_DEVICE_NOT_READY); }
    ok_bool_true(DxgkHotPlugWorkCorePublishEnumerationLocked(&Epoch, &Enumerated, FirstEpoch), "the current start epoch publishes once");
    { NTSTATUS Observed = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Epoch, &Enumerated, FirstEpoch); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkHotPlugWorkCorePublishEnumerationLocked(&Epoch, &Enumerated, FirstEpoch), "an enumeration epoch cannot publish twice");
    SecondEpoch = DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(&Epoch, &Enumerated);
    ok_eq_longlong(SecondEpoch, 2);
    { NTSTATUS Observed = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Epoch, &Enumerated, FirstEpoch); ok_eq_hex(Observed, STATUS_RETRY); }
    ok_bool_false(DxgkHotPlugWorkCorePublishEnumerationLocked(&Epoch, &Enumerated, FirstEpoch), "a stale start cannot publish observations");
}

static VOID DxgkHotPlugWorkTestZeroPresentEnumeration(VOID)
{
    volatile LONG64 Epoch = 4;
    volatile LONG Enumerated = 0;

    ok_bool_true(DxgkHotPlugWorkCorePublishEnumerationLocked(&Epoch, &Enumerated, Epoch), "an empty but completed child enumeration is publishable");
    { NTSTATUS Observed = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Epoch, &Enumerated, Epoch); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID DxgkHotPlugWorkTestStopRecoveryOrdering(VOID)
{
    volatile LONG64 Generation = 11;
    volatile LONG Active = 0;

    ok_bool_true(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Active), "stop may acquire Level3 while no hot-plug worker owns rundown");
    ok_bool_true(DxgkHotPlugWorkCoreTryActivateLocked(&Active), "rollback recovery owns the worker slot");
    ok_bool_false(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Active), "stop must drain rollback recovery before Level3");
    ok_bool_true(DxgkHotPlugWorkCoreCompleteLocked(&Generation, &Active, Generation, FALSE), "rundown retires rollback recovery");
    ok_bool_true(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Active), "Level3 is safe after worker drain");
}

static VOID DxgkHotPlugWorkTestBoundedRetry(VOID)
{
    ULONG RetryCount;
    ULONG PreviousDelay = 0;

    for (RetryCount = 0; RetryCount < DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES; ++RetryCount)
    {
        ULONG DelayMs = DxgkHotPlugWorkCoreRetryDelayMs(RetryCount);

        ok(DxgkHotPlugWorkCoreShouldRetry(STATUS_INSUFFICIENT_RESOURCES, RetryCount), "transient retry %lu remains admitted\n", RetryCount);
        ok(DelayMs >= PreviousDelay && DelayMs <= DXGK_HOTPLUG_RETRY_MAX_MS, "retry delay %lu is bounded after %lu\n", DelayMs, PreviousDelay);
        PreviousDelay = DelayMs;
    }
    ok_bool_false(DxgkHotPlugWorkCoreShouldRetry(STATUS_INSUFFICIENT_RESOURCES, DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES), "the retry budget is finite");
    ok_bool_false(DxgkHotPlugWorkCoreShouldRetry(STATUS_NOT_SUPPORTED, 0), "permanent failures are not retried");
}

static VOID DxgkHotPlugWorkTestDesktopOutput(VOID)
{
    ok_bool_false(DxgkHotPlugWorkCorePrefersOutput(FALSE, TRUE, 9, TRUE, TRUE, 3), "a newly connected output does not take the desktop from the output showing it");
    ok_bool_true(DxgkHotPlugWorkCorePrefersOutput(TRUE, FALSE, 1, FALSE, TRUE, 5), "the output the topology drives keeps the desktop");
    ok_bool_true(DxgkHotPlugWorkCorePrefersOutput(FALSE, TRUE, 1, FALSE, FALSE, 5), "an output with an EDID wins among undriven outputs");
    ok_bool_true(DxgkHotPlugWorkCorePrefersOutput(FALSE, TRUE, 7, FALSE, TRUE, 5), "the last connected output wins among equal undriven outputs");
    ok_bool_false(DxgkHotPlugWorkCorePrefersOutput(TRUE, TRUE, 5, TRUE, TRUE, 5), "an equal output does not replace the chosen one");
}

static VOID DxgkHotPlugWorkTestPathImportance(VOID)
{
    ok_eq_ulong(DxgkHotPlugWorkCorePathImportance(0), 1UL);
    ok_eq_ulong(DxgkHotPlugWorkCorePathImportance(1), 2UL);
    ok_eq_ulong(DxgkHotPlugWorkCorePathImportance(15), 16UL);
}

static VOID DxgkHotPlugWorkTestExtendSource(VOID)
{
    ok_eq_ulong(DxgkHotPlugWorkCoreExtraPathSource(1, 4), 1UL);
    ok_eq_ulong(DxgkHotPlugWorkCoreExtraPathSource(3, 4), 3UL);
    ok_eq_ulong(DxgkHotPlugWorkCoreExtraPathSource(1, 1), 0UL);
    ok_eq_ulong(DxgkHotPlugWorkCoreExtraPathSource(4, 4), 0UL);
}

START_TEST(DxgkHotPlugWork)
{
    DxgkHotPlugWorkTestCoalescing();
    DxgkHotPlugWorkTestDrainWithoutRebuild();
    DxgkHotPlugWorkTestRebuildAwaitsEnumeration();
    DxgkHotPlugWorkTestLifecycleClose();
    DxgkHotPlugWorkTestEnumerationEpoch();
    DxgkHotPlugWorkTestZeroPresentEnumeration();
    DxgkHotPlugWorkTestStopRecoveryOrdering();
    DxgkHotPlugWorkTestBoundedRetry();
    DxgkHotPlugWorkTestDesktopOutput();
    DxgkHotPlugWorkTestPathImportance();
    DxgkHotPlugWorkTestExtendSource();
}
