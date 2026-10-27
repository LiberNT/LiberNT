/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Threaded DPC test
 *
 * Threaded DPCs normally run inside a system worker thread at PASSIVE_LEVEL.
 * When threaded DPCs are disabled, Windows can dispatch them as ordinary DPCs
 * at DISPATCH_LEVEL, so the test accepts both documented dispatch contexts.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

/* DpcType values from ntddk's KOBJECTS enum:
 *   DpcObject         = 19
 *   ThreadedDpcObject = 24
 * The KDPC.Type byte is initialized by KeInitializeThreadedDpc to
 * ThreadedDpcObject so the dispatcher routes it to the worker thread. */
#define KMT_THREADED_DPC_TYPE (GetNTVersion() >= _WIN32_WINNT_WIN10 ? 26 : 24)
#define KMT_DPC_OBJECT_TYPE   19

typedef VOID (NTAPI *PKE_INIT_THREADED_DPC)(
    PRKDPC Dpc,
    PKDEFERRED_ROUTINE DeferredRoutine,
    PVOID DeferredContext);

typedef BOOLEAN (NTAPI *PKE_IS_EXECUTING_DPC)(VOID);
typedef BOOLEAN (NTAPI *PKE_REMOVE_QUEUE_DPC_EX)(PRKDPC Dpc, BOOLEAN WaitIfActive);

#define TDPC_REQUEUE        1000
#define TDPC_STRESS_ROUNDS  100

static volatile LONG ThreadedDpcRanCount;
static volatile KIRQL ThreadedDpcIrql;
static volatile PKTHREAD ThreadedDpcThread;
static KDPC ThreadedDpc;
static KEVENT ThreadedDpcDoneEvent;

static PKE_INIT_THREADED_DPC pKeInitializeThreadedDpc;
static PKE_IS_EXECUTING_DPC pKeIsExecutingDpc;
static PKE_REMOVE_QUEUE_DPC_EX pKeRemoveQueueDpcEx;
static BOOLEAN ThreadedDpcBroken;

typedef struct _TDPC_SAMPLE
{
    KEVENT Done;
    volatile LONG Remaining;
    volatile LONG OrderIndex;
    LONG Order[4];
    KIRQL Irql;
    ULONG Cpu;
    PKTHREAD Thread;
    PEPROCESS Process;
    KPRIORITY Priority;
    UCHAR ExecutingDpc;
    BOOLEAN ApcsDisabled;
    BOOLEAN AllApcsDisabled;
} TDPC_SAMPLE;

static TDPC_SAMPLE Sample;
static KDPC SampleDpcs[4];
static KDPC NormalDpc;
static volatile LONG NormalRan;
static LONG NormalRanBeforeReturn;
static KIRQL NormalIrql;
static ULONG NormalCpu;
static KDPC SlowDpc;
static volatile LONG SlowStarted;
static volatile LONG SlowFinished;
static volatile LONG SlowRelease;
static LONGLONG SlowHoldMs;
static KTIMER ThreadedTimer;
static KDPC RequeueDpc;
static KEVENT RequeueDone;
static volatile LONG RequeueCount;
static volatile LONG RequeueBadIrql;
static volatile LONG RequeueBadCpu;
static volatile LONG RequeueFailed;
static volatile LONG SpinSawDpc;
static KEVENT SpinReady;
static ULONG SpinCpu;
static volatile LONG SpinStop;
static volatile LONG SpinCount;
static LONG SpinCountDuringDpc;
static LONGLONG SpinDpcStartMs;

static KDEFERRED_ROUTINE ThreadedDpcRoutine;

static
VOID
NTAPI
ThreadedDpcRoutine(
    IN PRKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    /* Capture the dispatch context before waking the waiting test thread. */
    ThreadedDpcIrql = KeGetCurrentIrql();
    ThreadedDpcThread = KeGetCurrentThread();
    InterlockedIncrement(&ThreadedDpcRanCount);

    ok_eq_pointer(SystemArgument1, (PVOID)0xCAFE1);
    ok_eq_pointer(SystemArgument2, (PVOID)0xCAFE2);

    KeSetEvent(&ThreadedDpcDoneEvent, IO_NO_INCREMENT, FALSE);
}

static VOID
ResetSample(
    _In_ LONG Remaining)
{
    RtlZeroMemory(&Sample, sizeof(Sample));
    KeInitializeEvent(&Sample.Done, NotificationEvent, FALSE);
    Sample.Remaining = Remaining;
    Sample.Irql = 0xFF;
    Sample.Cpu = MAXULONG;
}

static BOOLEAN
WaitSample(
    _In_ PKDPC Dpc)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -5LL * SECOND;
    Status = KeWaitForSingleObject(&Sample.Done, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
        return TRUE;
    if (!KeRemoveQueueDpc(Dpc))
        ThreadedDpcBroken = TRUE;
    return FALSE;
}

static VOID NTAPI
SampleRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    LONG Index;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Sample.Irql = KeGetCurrentIrql();
    Sample.Cpu = KeGetCurrentProcessorNumber();
    Sample.Thread = KeGetCurrentThread();
    Sample.Process = PsGetCurrentProcess();
    Sample.Priority = KeQueryPriorityThread(Sample.Thread);
    Sample.ExecutingDpc = pKeIsExecutingDpc ? pKeIsExecutingDpc() : 0xFF;
    Sample.ApcsDisabled = KeAreApcsDisabled();
    Sample.AllApcsDisabled = KeAreAllApcsDisabled();
    Index = InterlockedIncrement(&Sample.OrderIndex) - 1;
    if (Index < (LONG)RTL_NUMBER_OF(Sample.Order))
        Sample.Order[Index] = (LONG)(ULONG_PTR)SystemArgument1;
    if (!InterlockedDecrement(&Sample.Remaining))
        KeSetEvent(&Sample.Done, IO_NO_INCREMENT, FALSE);
}

static VOID
TestTargeted(VOID)
{
    PKTHREAD First[MAXIMUM_PROCESSORS] = {0};
    ULONG Cpu, Pass;

    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        for (Pass = 0; Pass < 2; Pass++)
        {
            ResetSample(1);
            pKeInitializeThreadedDpc(&SampleDpcs[0], SampleRoutine, NULL);
            KeSetTargetProcessorDpc(&SampleDpcs[0], (CCHAR)Cpu);
            ok_bool_true(KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1, NULL), "targeted threaded DPC queued");
            if (!WaitSample(&SampleDpcs[0]))
                return;

            ok_eq_uint(Sample.Irql, PASSIVE_LEVEL);
            ok_eq_ulong(Sample.Cpu, Cpu);
            ok_eq_pointer(Sample.Process, PsInitialSystemProcess);
            ok(Sample.Thread != KeGetCurrentThread(), "Threaded DPC ran on the queueing thread\n");
            ok_eq_long(Sample.Priority, HIGH_PRIORITY);
            ok_eq_uint(Sample.ExecutingDpc, FALSE);
            ok_eq_uint(Sample.ApcsDisabled, TRUE);
            ok_eq_uint(Sample.AllApcsDisabled, TRUE);
            if (!Pass)
                First[Cpu] = Sample.Thread;
            else
                ok_eq_pointer(Sample.Thread, First[Cpu]);
        }
        if (Cpu)
            ok(First[Cpu] != First[Cpu - 1], "CPU %lu shares the threaded DPC thread of CPU %lu\n", Cpu, Cpu - 1);
    }
}

static VOID
TestDispatchQueue(VOID)
{
    BOOLEAN Inserted[4], Reinserted, Removed, RemovedAgain;
    LONG RanAtDispatch, RemainingAfterLower;
    KIRQL OldIrql;
    ULONG Index;
    BOOLEAN Done;

    ResetSample(3);
    for (Index = 0; Index < RTL_NUMBER_OF(SampleDpcs); Index++)
        pKeInitializeThreadedDpc(&SampleDpcs[Index], SampleRoutine, NULL);
    KeSetImportanceDpc(&SampleDpcs[1], HighImportance);
    KeSetImportanceDpc(&SampleDpcs[2], LowImportance);

    KeSetSystemAffinityThread(1);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Inserted[0] = KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1, NULL);
    Inserted[1] = KeInsertQueueDpc(&SampleDpcs[1], (PVOID)2, NULL);
    Inserted[2] = KeInsertQueueDpc(&SampleDpcs[2], (PVOID)3, NULL);
    Inserted[3] = KeInsertQueueDpc(&SampleDpcs[3], (PVOID)4, NULL);
    Reinserted = KeInsertQueueDpc(&SampleDpcs[0], (PVOID)5, NULL);
    Removed = KeRemoveQueueDpc(&SampleDpcs[3]);
    RemovedAgain = KeRemoveQueueDpc(&SampleDpcs[3]);
    RanAtDispatch = Sample.OrderIndex;
    KeLowerIrql(OldIrql);
    RemainingAfterLower = Sample.Remaining;
    Done = WaitSample(&SampleDpcs[0]);
    KeRevertToUserAffinityThread();

    ok_bool_true(Inserted[0], "medium threaded DPC queued");
    ok_bool_true(Inserted[1], "high threaded DPC queued");
    ok_bool_true(Inserted[2], "low threaded DPC queued");
    ok_bool_true(Inserted[3], "removable threaded DPC queued");
    ok_bool_false(Reinserted, "queued threaded DPC queued twice");
    ok_bool_true(Removed, "queued threaded DPC removed");
    ok_bool_false(RemovedAgain, "threaded DPC removed twice");
    ok_eq_long(RanAtDispatch, 0);
    if (!Done)
    {
        KeRemoveQueueDpc(&SampleDpcs[1]);
        KeRemoveQueueDpc(&SampleDpcs[2]);
        return;
    }
    ok_eq_long(Sample.OrderIndex, 3);
    ok_eq_ulong(Sample.Cpu, 0);
    ok_eq_uint(Sample.Irql, PASSIVE_LEVEL);
    ok_eq_long(Sample.Order[0], 2);
    ok_eq_long(Sample.Order[1], 1);
    ok_eq_long(Sample.Order[2], 3);
    ok_eq_long(RemainingAfterLower, 0);
}

static VOID NTAPI
NormalRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    NormalIrql = KeGetCurrentIrql();
    NormalCpu = KeGetCurrentProcessorNumber();
    InterlockedExchange(&NormalRan, 1);
}

static VOID NTAPI
PreemptRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    KeInitializeDpc(&NormalDpc, NormalRoutine, NULL);
    KeSetTargetProcessorDpc(&NormalDpc, (CCHAR)KeGetCurrentProcessorNumber());
    KeInsertQueueDpc(&NormalDpc, NULL, NULL);
    NormalRanBeforeReturn = NormalRan;
    SampleRoutine(Dpc, DeferredContext, SystemArgument1, SystemArgument2);
}

static VOID
TestPreemptByDpc(VOID)
{
    ULONG Target = KeNumberProcessors - 1;

    NormalRan = 0;
    NormalRanBeforeReturn = -1;
    NormalIrql = 0xFF;
    NormalCpu = MAXULONG;
    ResetSample(1);
    pKeInitializeThreadedDpc(&SampleDpcs[0], PreemptRoutine, NULL);
    KeSetTargetProcessorDpc(&SampleDpcs[0], (CCHAR)Target);
    ok_bool_true(KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1, NULL), "preempted threaded DPC queued");
    if (!WaitSample(&SampleDpcs[0]))
        return;

    ok_eq_uint(Sample.Irql, PASSIVE_LEVEL);
    ok_eq_ulong(Sample.Cpu, Target);
    ok_eq_long(NormalRan, 1);
    ok_eq_uint(NormalIrql, DISPATCH_LEVEL);
    ok_eq_ulong(NormalCpu, Target);
    ok_eq_long(NormalRanBeforeReturn, 1);
}

static VOID NTAPI
SpinThread(
    _In_ PVOID Parameter)
{
    LARGE_INTEGER Frequency;
    LONGLONG Deadline;

    UNREFERENCED_PARAMETER(Parameter);

    KeSetSystemAffinityThread((KAFFINITY)1 << SpinCpu);
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY + 14);
    Deadline = KeQueryPerformanceCounter(&Frequency).QuadPart;
    Deadline += Frequency.QuadPart / 2;
    KeSetEvent(&SpinReady, IO_NO_INCREMENT, FALSE);
    while (KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
    {
        if (InterlockedCompareExchange(&Sample.OrderIndex, 0, 0))
        {
            InterlockedExchange(&SpinSawDpc, 1);
            break;
        }
        YieldProcessor();
    }
    KeRevertToUserAffinityThread();
}

static VOID
TestPreemptThread(VOID)
{
    PKTHREAD Thread;

    SpinCpu = KeNumberProcessors - 1;
    SpinSawDpc = 0;
    ResetSample(1);
    KeInitializeEvent(&SpinReady, NotificationEvent, FALSE);
    pKeInitializeThreadedDpc(&SampleDpcs[0], SampleRoutine, NULL);
    KeSetTargetProcessorDpc(&SampleDpcs[0], (CCHAR)SpinCpu);
    KeSetSystemAffinityThread(1);
    Thread = KmtStartThread(SpinThread, NULL);
    if (!Thread)
    {
        KeRevertToUserAffinityThread();
        return;
    }
    KeWaitForSingleObject(&SpinReady, Executive, KernelMode, FALSE, NULL);
    ok_bool_true(KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1, NULL), "threaded DPC queued behind a realtime thread");
    WaitSample(&SampleDpcs[0]);
    KmtFinishThread(Thread, NULL);
    KeRevertToUserAffinityThread();

    ok_eq_ulong(Sample.Cpu, SpinCpu);
    ok_eq_long(SpinSawDpc, 1);
}

static VOID NTAPI
Priority31SpinThread(
    _In_ PVOID Parameter)
{
    LARGE_INTEGER Frequency;
    LONGLONG Deadline;

    UNREFERENCED_PARAMETER(Parameter);

    KeSetSystemAffinityThread((KAFFINITY)1 << SpinCpu);
    KeSetPriorityThread(KeGetCurrentThread(), HIGH_PRIORITY);
    Deadline = KeQueryPerformanceCounter(&Frequency).QuadPart;
    Deadline += Frequency.QuadPart * 3;
    KeSetEvent(&SpinReady, IO_NO_INCREMENT, FALSE);
    while (!InterlockedCompareExchange(&SpinStop, 0, 0) &&
           KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
    {
        SpinCount++;
        YieldProcessor();
    }
    KeRevertToUserAffinityThread();
}

static VOID NTAPI
NoPreemptRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    LARGE_INTEGER Frequency, Now;
    LONGLONG Deadline;
    LONG Before;

    Now = KeQueryPerformanceCounter(&Frequency);
    SpinDpcStartMs = (Now.QuadPart - (LONGLONG)(ULONG_PTR)SystemArgument2) * 1000 / Frequency.QuadPart;
    Before = SpinCount;
    Deadline = Now.QuadPart + Frequency.QuadPart / 5;
    while (KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    SpinCountDuringDpc = SpinCount - Before;
    InterlockedExchange(&SpinStop, 1);
    SampleRoutine(Dpc, DeferredContext, SystemArgument1, NULL);
}

static VOID
TestNoThreadPreemption(VOID)
{
    PKTHREAD Thread;

    SpinCpu = KeNumberProcessors - 1;
    SpinStop = SpinCount = 0;
    SpinCountDuringDpc = -1;
    SpinDpcStartMs = -1;
    ResetSample(1);
    KeInitializeEvent(&SpinReady, NotificationEvent, FALSE);
    pKeInitializeThreadedDpc(&SampleDpcs[0], NoPreemptRoutine, NULL);
    KeSetTargetProcessorDpc(&SampleDpcs[0], (CCHAR)SpinCpu);
    KeSetSystemAffinityThread(1);
    Thread = KmtStartThread(Priority31SpinThread, NULL);
    if (!Thread)
    {
        KeRevertToUserAffinityThread();
        return;
    }
    KeWaitForSingleObject(&SpinReady, Executive, KernelMode, FALSE, NULL);
    ok_bool_true(KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1,
                                  (PVOID)(ULONG_PTR)KeQueryPerformanceCounter(NULL).QuadPart),
                 "threaded DPC queued behind a priority 31 thread");
    WaitSample(&SampleDpcs[0]);
    InterlockedExchange(&SpinStop, 1);
    KmtFinishThread(Thread, NULL);
    KeRevertToUserAffinityThread();

    ok_eq_ulong(Sample.Cpu, SpinCpu);
    ok_eq_long(SpinCountDuringDpc, 0);
    trace("THREADED_DPC dpc_start_ms=%I64d\n", SpinDpcStartMs);
}

static VOID
TestTimer(VOID)
{
    LARGE_INTEGER DueTime;

    ResetSample(1);
    pKeInitializeThreadedDpc(&SampleDpcs[0], SampleRoutine, NULL);
    KeInitializeTimer(&ThreadedTimer);
    DueTime.QuadPart = -10 * MILLISECOND;
    KeSetTimer(&ThreadedTimer, DueTime, &SampleDpcs[0]);
    if (!WaitSample(&SampleDpcs[0]))
    {
        KeCancelTimer(&ThreadedTimer);
        return;
    }
    ok_eq_uint(Sample.Irql, PASSIVE_LEVEL);
    ok_eq_pointer(Sample.Process, PsInitialSystemProcess);
    ok(Sample.Thread != KeGetCurrentThread(), "Threaded timer DPC ran on the test thread\n");
}

static VOID NTAPI
SlowRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    LARGE_INTEGER Frequency;
    LONGLONG Deadline;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedExchange(&SlowStarted, 1);
    Deadline = KeQueryPerformanceCounter(&Frequency).QuadPart;
    Deadline += Frequency.QuadPart * SlowHoldMs / 1000;
    while (!InterlockedCompareExchange(&SlowRelease, 0, 0) &&
           KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
    {
        YieldProcessor();
    }
    InterlockedExchange(&SlowFinished, 1);
}

static BOOLEAN
WaitSlowStarted(VOID)
{
    LARGE_INTEGER Delay;
    ULONG Attempt;

    Delay.QuadPart = -1 * MILLISECOND;
    for (Attempt = 0; Attempt < 2000; Attempt++)
    {
        if (InterlockedCompareExchange(&SlowStarted, 0, 0))
            return TRUE;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    return FALSE;
}

static VOID
TestFlush(VOID)
{
    ULONG Target = KeNumberProcessors - 1;
    LONG FinishedAtReturn, RanAtReturn;

    SlowStarted = SlowFinished = SlowRelease = 0;
    SlowHoldMs = 100;
    ResetSample(1);
    pKeInitializeThreadedDpc(&SlowDpc, SlowRoutine, NULL);
    pKeInitializeThreadedDpc(&SampleDpcs[0], SampleRoutine, NULL);
    KeSetTargetProcessorDpc(&SlowDpc, (CCHAR)Target);
    KeSetTargetProcessorDpc(&SampleDpcs[0], (CCHAR)Target);
    KeSetSystemAffinityThread(1);
    ok_bool_true(KeInsertQueueDpc(&SlowDpc, NULL, NULL), "slow threaded DPC queued");
    ok_bool_true(KeInsertQueueDpc(&SampleDpcs[0], (PVOID)1, NULL), "trailing threaded DPC queued");
    KeFlushQueuedDpcs();
    FinishedAtReturn = SlowFinished;
    RanAtReturn = Sample.OrderIndex;
    KeRevertToUserAffinityThread();

    ok_eq_long(FinishedAtReturn, 1);
    ok_eq_long(RanAtReturn, 1);
    ok_eq_ulong(Sample.Cpu, Target);
    WaitSample(&SampleDpcs[0]);
}

static VOID
TestRemoveQueueDpcEx(VOID)
{
    ULONG Target = KeNumberProcessors - 1;
    BOOLEAN Removed, Started;
    LONG FinishedAtReturn;
    KIRQL OldIrql;
    ULONG Pass;

    if (skip(pKeRemoveQueueDpcEx != NULL, "KeRemoveQueueDpcEx not exported\n"))
        return;

    for (Pass = 0; Pass < 2; Pass++)
    {
        SlowStarted = SlowFinished = SlowRelease = 0;
        SlowHoldMs = Pass == 0 ? 100 : 2000;
        pKeInitializeThreadedDpc(&SlowDpc, SlowRoutine, NULL);
        KeSetTargetProcessorDpc(&SlowDpc, (CCHAR)Target);
        KeSetSystemAffinityThread(1);
        ok_bool_true(KeInsertQueueDpc(&SlowDpc, NULL, NULL), "slow threaded DPC queued");
        Started = WaitSlowStarted();
        Removed = pKeRemoveQueueDpcEx(&SlowDpc, Pass == 0);
        FinishedAtReturn = SlowFinished;
        InterlockedExchange(&SlowRelease, 1);
        KeRevertToUserAffinityThread();

        ok_bool_true(Started, "slow threaded DPC started");
        ok_bool_false(Removed, "running threaded DPC removed");
        ok_eq_long(FinishedAtReturn, Pass == 0 ? 1 : 0);
        if (!Started)
        {
            if (!KeRemoveQueueDpc(&SlowDpc))
                ThreadedDpcBroken = TRUE;
            return;
        }
        pKeRemoveQueueDpcEx(&SlowDpc, TRUE);
    }

    pKeInitializeThreadedDpc(&SlowDpc, SlowRoutine, NULL);
    SlowStarted = SlowFinished = SlowRelease = 0;
    SlowHoldMs = 100;
    KeSetSystemAffinityThread(1);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ok_bool_true(KeInsertQueueDpc(&SlowDpc, NULL, NULL), "local threaded DPC queued");
    Removed = pKeRemoveQueueDpcEx(&SlowDpc, TRUE);
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    ok_bool_true(Removed, "queued threaded DPC removed at DISPATCH_LEVEL");
    ok_eq_long(SlowStarted, 0);
}

static VOID NTAPI
RequeueRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG Cpu = (ULONG)(ULONG_PTR)SystemArgument1;

    UNREFERENCED_PARAMETER(SystemArgument2);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        InterlockedIncrement(&RequeueBadIrql);
    if (KeGetCurrentProcessorNumber() != Cpu)
        InterlockedIncrement(&RequeueBadCpu);
    if (InterlockedIncrement(&RequeueCount) >= TDPC_REQUEUE)
    {
        KeSetEvent(&RequeueDone, IO_NO_INCREMENT, FALSE);
        return;
    }
    if (DeferredContext)
    {
        Cpu = (Cpu + 1) % (ULONG)KeNumberProcessors;
        KeSetTargetProcessorDpc(Dpc, (CCHAR)Cpu);
    }
    if (!KeInsertQueueDpc(Dpc, (PVOID)(ULONG_PTR)Cpu, NULL))
    {
        InterlockedIncrement(&RequeueFailed);
        KeSetEvent(&RequeueDone, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
TestRequeue(
    _In_ BOOLEAN Rotate)
{
    LARGE_INTEGER Timeout, Start, End, Frequency;
    NTSTATUS Status;

    RequeueCount = RequeueBadIrql = RequeueBadCpu = RequeueFailed = 0;
    KeInitializeEvent(&RequeueDone, NotificationEvent, FALSE);
    pKeInitializeThreadedDpc(&RequeueDpc, RequeueRoutine, Rotate ? (PVOID)1 : NULL);
    KeSetTargetProcessorDpc(&RequeueDpc, 0);
    Start = KeQueryPerformanceCounter(&Frequency);
    ok_bool_true(KeInsertQueueDpc(&RequeueDpc, (PVOID)0, NULL), "requeueing threaded DPC queued");
    Timeout.QuadPart = -30LL * SECOND;
    Status = KeWaitForSingleObject(&RequeueDone, Executive, KernelMode, FALSE, &Timeout);
    End = KeQueryPerformanceCounter(NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS && !KeRemoveQueueDpc(&RequeueDpc))
        ThreadedDpcBroken = TRUE;
    ok_eq_long(RequeueCount, TDPC_REQUEUE);
    ok_eq_long(RequeueBadIrql, 0);
    ok_eq_long(RequeueBadCpu, 0);
    ok_eq_long(RequeueFailed, 0);
    trace("THREADED_DPC requeue rotate=%u count=%ld ms=%I64d\n", Rotate, RequeueCount,
          (End.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart);
}

typedef struct _TDPC_STRESS
{
    KEVENT Done;
    PKEVENT Go;
    PKDPC Dpcs;
    ULONG Cpu;
    volatile LONG Remaining;
    volatile LONG BadIrql;
    volatile LONG BadCpu;
    ULONG Rounds;
    ULONG Errors;
} TDPC_STRESS;

static TDPC_STRESS StressWorkers[MAXIMUM_PROCESSORS];

static VOID NTAPI
StressRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    TDPC_STRESS *Stress = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        InterlockedIncrement(&Stress->BadIrql);
    if (KeGetCurrentProcessorNumber() != (ULONG)(ULONG_PTR)SystemArgument1)
        InterlockedIncrement(&Stress->BadCpu);
    if (!InterlockedDecrement(&Stress->Remaining))
        KeSetEvent(&Stress->Done, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
StressThread(
    _In_ PVOID Parameter)
{
    TDPC_STRESS *Stress = Parameter;
    ULONG Cpu, Round, Count = KeNumberProcessors;
    LARGE_INTEGER Timeout;
    KIRQL OldIrql = PASSIVE_LEVEL;

    KeSetSystemAffinityThread((KAFFINITY)1 << Stress->Cpu);
    KeWaitForSingleObject(Stress->Go, Executive, KernelMode, FALSE, NULL);
    Timeout.QuadPart = -5LL * SECOND;
    for (Round = 0; Round < TDPC_STRESS_ROUNDS; Round++)
    {
        Stress->Remaining = Count;
        if (Round & 1)
            KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        for (Cpu = 0; Cpu < Count; Cpu++)
        {
            pKeInitializeThreadedDpc(&Stress->Dpcs[Cpu], StressRoutine, Stress);
            KeSetTargetProcessorDpc(&Stress->Dpcs[Cpu], (CCHAR)Cpu);
            KeSetImportanceDpc(&Stress->Dpcs[Cpu], (KDPC_IMPORTANCE)(Round % 3));
            if (!KeInsertQueueDpc(&Stress->Dpcs[Cpu], (PVOID)(ULONG_PTR)Cpu, NULL))
                Stress->Errors++;
        }
        if (Round & 1)
            KeLowerIrql(OldIrql);
        if (KeWaitForSingleObject(&Stress->Done, Executive, KernelMode, FALSE, &Timeout) != STATUS_SUCCESS)
        {
            Stress->Errors++;
            for (Cpu = 0; Cpu < Count; Cpu++)
                KeRemoveQueueDpc(&Stress->Dpcs[Cpu]);
            break;
        }
        Stress->Rounds++;
    }
    KeRevertToUserAffinityThread();
}

static VOID
TestStress(VOID)
{
    PKTHREAD Threads[MAXIMUM_PROCESSORS];
    ULONG Cpu, Created = 0, Count = KeNumberProcessors;
    LARGE_INTEGER Start, End, Frequency;
    KEVENT Go;

    KeInitializeEvent(&Go, NotificationEvent, FALSE);
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        RtlZeroMemory(&StressWorkers[Cpu], sizeof(StressWorkers[Cpu]));
        KeInitializeEvent(&StressWorkers[Cpu].Done, SynchronizationEvent, FALSE);
        StressWorkers[Cpu].Go = &Go;
        StressWorkers[Cpu].Cpu = Cpu;
        StressWorkers[Cpu].Dpcs = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(KDPC), 'sDTK');
        if (!StressWorkers[Cpu].Dpcs)
            break;
        Threads[Cpu] = KmtStartThread(StressThread, &StressWorkers[Cpu]);
        if (!Threads[Cpu])
        {
            ExFreePoolWithTag(StressWorkers[Cpu].Dpcs, 'sDTK');
            break;
        }
        Created++;
    }
    ok_eq_ulong(Created, Count);
    Start = KeQueryPerformanceCounter(&Frequency);
    KeSetEvent(&Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Created; Cpu++)
    {
        KmtFinishThread(Threads[Cpu], NULL);
        ok_eq_ulong(StressWorkers[Cpu].Rounds, TDPC_STRESS_ROUNDS);
        ok_eq_ulong(StressWorkers[Cpu].Errors, 0);
        ok_eq_long(StressWorkers[Cpu].BadIrql, 0);
        ok_eq_long(StressWorkers[Cpu].BadCpu, 0);
        if (!StressWorkers[Cpu].Errors)
            ExFreePoolWithTag(StressWorkers[Cpu].Dpcs, 'sDTK');
        else
            ThreadedDpcBroken = TRUE;
    }
    End = KeQueryPerformanceCounter(NULL);
    trace("THREADED_DPC stress workers=%lu rounds=%lu ms=%I64d\n", Created, (ULONG)TDPC_STRESS_ROUNDS,
          (End.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart);
}

START_TEST(KeThreadedDpc)
{
    BOOLEAN Inserted;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    PKTHREAD QueueingThread;

    pKeInitializeThreadedDpc =
        (PKE_INIT_THREADED_DPC)KmtGetSystemRoutineAddress(L"KeInitializeThreadedDpc");

    if (skip(pKeInitializeThreadedDpc != NULL,
             "KeInitializeThreadedDpc not exported\n"))
    {
        return;
    }

    trace("KeInitializeThreadedDpc = %p, NT version = 0x%04x\n",
          pKeInitializeThreadedDpc, GetNTVersion());

    ThreadedDpcRanCount = 0;
    ThreadedDpcIrql = 0xFF;
    ThreadedDpcThread = NULL;
    KeInitializeEvent(&ThreadedDpcDoneEvent, NotificationEvent, FALSE);

    /* Pre-fill so we can confirm the init writes the right Type byte. */
    memset(&ThreadedDpc, 0xAA, sizeof(ThreadedDpc));
    pKeInitializeThreadedDpc(&ThreadedDpc, ThreadedDpcRoutine, NULL);

    ok_eq_uint(ThreadedDpc.Type, KMT_THREADED_DPC_TYPE);
    ok_eq_pointer(ThreadedDpc.DeferredRoutine, ThreadedDpcRoutine);
    ok_eq_pointer(ThreadedDpc.DeferredContext, NULL);

    /* Queue from PASSIVE_LEVEL. */
    ok_irql(PASSIVE_LEVEL);
    QueueingThread = KeGetCurrentThread();
    Inserted = KeInsertQueueDpc(&ThreadedDpc, (PVOID)0xCAFE1, (PVOID)0xCAFE2);
    ok_bool_true(Inserted, "KeInsertQueueDpc on threaded DPC");

    /* Wait up to 5 seconds for the worker thread to dispatch us. */
    Timeout.QuadPart = -5LL * SECOND;
    Status = KeWaitForSingleObject(&ThreadedDpcDoneEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (Status != STATUS_SUCCESS && Inserted)
        KeRemoveQueueDpc(&ThreadedDpc);

    if (skip(Status == STATUS_SUCCESS, "Threaded DPC never ran\n"))
        return;

    ok_eq_long(ThreadedDpcRanCount, 1L);

    ok(ThreadedDpcIrql == PASSIVE_LEVEL || ThreadedDpcIrql == DISPATCH_LEVEL,
       "Threaded DPC ran at IRQL %u, expected PASSIVE_LEVEL (0) or DISPATCH_LEVEL (2)\n",
       ThreadedDpcIrql);
    if (ThreadedDpcIrql == PASSIVE_LEVEL)
    {
        ok(ThreadedDpcThread != QueueingThread,
           "Threaded DPC ran on the queueing thread %p (should be a worker)\n",
           ThreadedDpcThread);
    }
    else
    {
        trace("Threaded DPC dispatched as an ordinary DPC at DISPATCH_LEVEL\n");
    }

    trace("Threaded DPC ran on thread %p at IRQL %u (queueing thread = %p)\n",
          ThreadedDpcThread, ThreadedDpcIrql, QueueingThread);

    if (skip(KeNumberProcessors >= 2, "Two processors required\n"))
        return;

    pKeIsExecutingDpc = (PKE_IS_EXECUTING_DPC)KmtGetSystemRoutineAddress(L"KeIsExecutingDpc");
    pKeRemoveQueueDpcEx = (PKE_REMOVE_QUEUE_DPC_EX)KmtGetSystemRoutineAddress(L"KeRemoveQueueDpcEx");
    ThreadedDpcBroken = FALSE;

    TestTargeted();
    if (!ThreadedDpcBroken)
        TestDispatchQueue();
    if (!ThreadedDpcBroken)
        TestPreemptByDpc();
    if (!ThreadedDpcBroken)
        TestPreemptThread();
    if (!ThreadedDpcBroken)
        TestNoThreadPreemption();
    if (!ThreadedDpcBroken)
        TestTimer();
    if (!ThreadedDpcBroken)
        TestFlush();
    if (!ThreadedDpcBroken)
        TestRemoveQueueDpcEx();
    if (!ThreadedDpcBroken)
        TestRequeue(FALSE);
    if (!ThreadedDpcBroken)
        TestRequeue(TRUE);
    if (!ThreadedDpcBroken)
        TestStress();
    ok_bool_false(ThreadedDpcBroken, "threaded DPC left running or lost");
}
