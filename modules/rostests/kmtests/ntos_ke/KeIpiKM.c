/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IPI and generic DPC call API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG IpiCalls;
static volatile LONG64 IpiMask;

static
ULONG_PTR
NTAPI
IpiBroadcastWorker(
    _In_ ULONG_PTR Argument)
{
    PROCESSOR_NUMBER Processor;

    KeGetCurrentProcessorNumberEx(&Processor);
    ok_eq_ulongptr(Argument, (ULONG_PTR)0x1BADD00D);
    InterlockedIncrement(&IpiCalls);
    if (Processor.Group == 0)
        InterlockedOr64(&IpiMask, (LONG64)(1ULL << Processor.Number));
    return 0x77;
}

static
VOID
TestIpiGenericCall(VOID)
{
    ULONG_PTR Result;
    ULONG Round;
    ULONG Delivered = 0;
    LONG Total = (LONG)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    for (Round = 0; Round < 4; Round++)
    {
        IpiCalls = 0;
        IpiMask = 0;
        Result = KeIpiGenericCall(IpiBroadcastWorker, (ULONG_PTR)0x1BADD00D);
        ok_eq_ulongptr(Result, (ULONG_PTR)0x77);
        ok_eq_long(IpiCalls, Total);
        ok_eq_ulonglong((ULONGLONG)IpiMask, (ULONGLONG)KeQueryActiveProcessors());
    }

    /* One send following another is the case the local APIC has to be let
     * finish, so run them with nothing in between. */
    for (Round = 0; Round < 256; Round++)
    {
        IpiCalls = 0;
        IpiMask = 0;
        Result = KeIpiGenericCall(IpiBroadcastWorker, (ULONG_PTR)0x1BADD00D);
        if (Result == (ULONG_PTR)0x77 &&
            IpiCalls == Total &&
            (ULONGLONG)IpiMask == (ULONGLONG)KeQueryActiveProcessors())
        {
            Delivered++;
        }
    }
    ok_eq_ulong(Delivered, 256ul);
}

static volatile LONG *DpcVisits;
static ULONG DpcProcessorCount;
static volatile LONG DpcCalls;
static volatile LONG DpcSyncObserved;

static
VOID
NTAPI
GenericDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG Cpu = KeGetCurrentProcessorNumberEx(NULL);

    UNREFERENCED_PARAMETER(Dpc);
    ok(Cpu < DpcProcessorCount, "unexpected processor %lu\n", Cpu);
    if (Cpu < DpcProcessorCount)
        InterlockedIncrement(&DpcVisits[Cpu]);
    ok_eq_pointer(DeferredContext, (PVOID)(ULONG_PTR)0xD00DFEED);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);

    InterlockedIncrement(&DpcCalls);
    if (KeSignalCallDpcSynchronize(SystemArgument2))
        InterlockedIncrement(&DpcSyncObserved);
    KeSignalCallDpcDone(SystemArgument1);
}

static
VOID
TestGenericCallDpc(VOID)
{
    ULONG Round;
    ULONG Reached = 0, Cpu;

    DpcProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    DpcVisits = ExAllocatePoolZero(NonPagedPool, DpcProcessorCount * sizeof(*DpcVisits), 'vDpK');
    if (skip(DpcVisits != NULL, "No memory\n")) return;

    DpcCalls = 0;
    DpcSyncObserved = 0;

    KeGenericCallDpc(GenericDpcRoutine, (PVOID)(ULONG_PTR)0xD00DFEED);

    ok_eq_long(DpcCalls, (LONG)DpcProcessorCount);
    ok_eq_long(DpcSyncObserved, 1L);

    KeFlushQueuedDpcs();

    /* A processor that stops ending its interrupts answers the first round
     * and no other, so run them back to back. */
    for (Round = 0; Round < 64; Round++)
    {
        DpcCalls = 0;
        DpcSyncObserved = 0;
        KeGenericCallDpc(GenericDpcRoutine, (PVOID)(ULONG_PTR)0xD00DFEED);
        if (DpcCalls == (LONG)DpcProcessorCount && DpcSyncObserved == 1L)
            Reached++;
        KeFlushQueuedDpcs();
    }
    ok_eq_ulong(Reached, 64ul);
    for (Cpu = 0; Cpu < DpcProcessorCount; Cpu++)
        ok_eq_long(DpcVisits[Cpu], (LONG)Round + 1);
    ExFreePoolWithTag((PVOID)DpcVisits, 'vDpK');
}

#define IDLE_LOCK_ROUNDS 8

static volatile LONG IdleLockDpcRuns;
static volatile LONG IdleLockCalls;

static
VOID
NTAPI
IdleLockDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedIncrement(&IdleLockDpcRuns);
}

static
ULONG_PTR
NTAPI
IdleLockWorker(
    _In_ ULONG_PTR Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    InterlockedIncrement(&IdleLockCalls);
    return 0;
}

/* Whoever holds a processor's lock can be interrupted by a call that waits for every processor, so that processor answers the call while it waits for its lock */
static
VOID
TestIpiReachesProcessorWaitingForItsLock(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Self, Other, PreviousAffinity;
    LONG Total = (LONG)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ULONG OtherNumber = 0, Round, Spin, Idle = 0, Answered = 0;
    PKPRCB Prcb;
    KIRQL OldIrql;
    LONG Expected;
    KDPC Dpc;

    Self = Active & (0 - Active);
    Other = Active & ~Self;
    Other &= (0 - Other);
    if (!Other)
    {
        skip(FALSE, "Two processors are required to hold the lock of another one\n");
        return;
    }
    while (((KAFFINITY)1 << OtherNumber) != Other)
        OtherNumber++;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Other);
    Prcb = KeGetCurrentPrcb();
    KeSetSystemAffinityThreadEx(Self);

    KeInitializeDpc(&Dpc, IdleLockDpcRoutine, NULL);
    KeSetTargetProcessorDpc(&Dpc, (CCHAR)OtherNumber);
    KeSetImportanceDpc(&Dpc, HighImportance);

    for (Round = 0; Round < IDLE_LOCK_ROUNDS; Round++)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

        while (InterlockedExchange((PLONG)&Prcb->PrcbLock, 1) != 0)
            YieldProcessor();

        /* The deferred routine sends a sleeping processor through its idle loop, which then meets the held lock; one already waiting for the lock cannot run it */
        Expected = IdleLockDpcRuns + 1;
        KeInsertQueueDpc(&Dpc, NULL, NULL);
        for (Spin = 0; (Spin < 200) && (IdleLockDpcRuns != Expected); Spin++)
            KeStallExecutionProcessor(10);
        KeStallExecutionProcessor(500);
        if (Prcb->CurrentThread == Prcb->IdleThread)
            Idle++;

        IdleLockCalls = 0;
        KeIpiGenericCall(IdleLockWorker, 0);
        if (IdleLockCalls == Total)
            Answered++;

        InterlockedAnd((PLONG)&Prcb->PrcbLock, 0);
        KeLowerIrql(OldIrql);
        KeRemoveQueueDpc(&Dpc);
    }

    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    KeFlushQueuedDpcs();

    ok_eq_ulong(Answered, (ULONG)IDLE_LOCK_ROUNDS);
    if (!Idle)
        skip(FALSE, "Processor %lu was never idle while its lock was held\n", OtherNumber);
}

START_TEST(KeIpiKM)
{
    TestIpiGenericCall();
    TestGenericCallDpc();
    TestIpiReachesProcessorWaitingForItsLock();
}
