/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DPC initialization and KeRemoveQueueDpcEx tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

typedef BOOLEAN (NTAPI *PKMT_KE_REMOVE_QUEUE_DPC_EX)(
    _Inout_ PRKDPC Dpc,
    _In_ BOOLEAN WaitIfActive);

typedef struct _DPC_REMOVE_CONTEXT
{
    PKMT_KE_REMOVE_QUEUE_DPC_EX RemoveQueueDpcEx;
    KTIMER Timer;
    KEVENT WaitEvent;
    KDPC Dpc;
    volatile LONG CallbackStarted;
    volatile LONG CallbackRelease;
    volatile LONG CallbackFinished;
    volatile LONG CallbackTimedOut;
    volatile LONG WaitReady;
    volatile LONG WaitStarted;
    volatile LONG WaitFinished;
    BOOLEAN WaitValue;
    BOOLEAN WaitResult;
    BOOLEAN WaitInterruptsBefore;
    BOOLEAN WaitInterruptsAfter;
    BOOLEAN SelfResult;
    BOOLEAN SelfInterruptsBefore;
    BOOLEAN SelfInterruptsAfter;
    KAFFINITY WaitAffinity;
    KIRQL WaitRaiseIrql;
    KIRQL WaitIrql;
    ULONG WaitCpu;
    ULONG CallbackCpu;
    KIRQL CallbackIrql;
    BOOLEAN CallbackInterruptsEnabled;
    PVOID CallbackDpcData;
    PVOID CallbackDpcListNext;
    ULONG_PTR CallbackHistory;
} DPC_REMOVE_CONTEXT, *PDPC_REMOVE_CONTEXT;

static VOID NTAPI RemoveDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDPC_REMOVE_CONTEXT Context = DeferredContext;
    ULONG Spins;

    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Context->CallbackCpu = KeGetCurrentProcessorNumber();
    Context->CallbackIrql = KeGetCurrentIrql();
    Context->CallbackInterruptsEnabled = KmtAreInterruptsEnabled();
    Context->CallbackDpcData = Dpc->DpcData;
    Context->CallbackDpcListNext = Dpc->DpcListEntry.Next;
    Context->CallbackHistory = Dpc->ProcessorHistory;
    InterlockedExchange(&Context->CallbackStarted, 1);

    for (Spins = 0; Spins < 500000 && !Context->CallbackRelease; Spins++)
        KeStallExecutionProcessor(10);
    if (!Context->CallbackRelease)
        InterlockedExchange(&Context->CallbackTimedOut, 1);
    InterlockedExchange(&Context->CallbackFinished, 1);
}

static VOID DpcInitializationCheck(VOID)
{
    KDPC Dpc;
    PVOID PoisonPointer = (PVOID)(ULONG_PTR)0x5555555555555555ULL;

    RtlFillMemory(&Dpc, sizeof(Dpc), 0x55);
    KeInitializeDpc(&Dpc, RemoveDpcRoutine, &Dpc);
    ok_eq_uint(Dpc.Type, DpcObject);
    ok_eq_uint(Dpc.Importance, MediumImportance);
    ok_eq_uint(Dpc.Number, 0);
    ok_eq_pointer(Dpc.DpcListEntry.Next, PoisonPointer);
    ok_eq_ulonglong(Dpc.ProcessorHistory, 0);
    ok_eq_pointer(Dpc.DeferredRoutine, RemoveDpcRoutine);
    ok_eq_pointer(Dpc.DeferredContext, &Dpc);
    ok_eq_pointer(Dpc.SystemArgument1, PoisonPointer);
    ok_eq_pointer(Dpc.SystemArgument2, PoisonPointer);
    ok_eq_pointer(Dpc.DpcData, NULL);

    RtlFillMemory(&Dpc, sizeof(Dpc), 0x55);
    KeInitializeThreadedDpc(&Dpc, RemoveDpcRoutine, &Dpc);
    ok_eq_uint(Dpc.Type, ThreadedDpcObject);
    ok_eq_uint(Dpc.Importance, MediumImportance);
    ok_eq_uint(Dpc.Number, 0);
    ok_eq_pointer(Dpc.DpcListEntry.Next, PoisonPointer);
    ok_eq_ulonglong(Dpc.ProcessorHistory, 0);
    ok_eq_pointer(Dpc.DeferredRoutine, RemoveDpcRoutine);
    ok_eq_pointer(Dpc.DeferredContext, &Dpc);
    ok_eq_pointer(Dpc.SystemArgument1, PoisonPointer);
    ok_eq_pointer(Dpc.SystemArgument2, PoisonPointer);
    ok_eq_pointer(Dpc.DpcData, NULL);
}

static BOOLEAN WaitForValue(_In_ volatile LONG *Value, _In_ LONG Expected)
{
    ULONG Spins;

    for (Spins = 0; Spins < 1000000; Spins++)
    {
        if (*Value == Expected)
            return TRUE;
        KeStallExecutionProcessor(1);
    }

    return FALSE;
}

static VOID NTAPI RemoveDpcWaitThread(_In_ PVOID Parameter)
{
    PDPC_REMOVE_CONTEXT Context = Parameter;
    KIRQL OldIrql = PASSIVE_LEVEL;

    if (Context->WaitRaiseIrql > PASSIVE_LEVEL)
        KeRaiseIrql(Context->WaitRaiseIrql, &OldIrql);
    Context->WaitCpu = KeGetCurrentProcessorNumber();
    Context->WaitIrql = KeGetCurrentIrql();
    Context->WaitInterruptsBefore = KmtAreInterruptsEnabled();
    InterlockedExchange(&Context->WaitStarted, 1);
    Context->WaitResult = Context->RemoveQueueDpcEx(&Context->Dpc, Context->WaitValue);
    Context->WaitInterruptsAfter = KmtAreInterruptsEnabled();
    if (Context->WaitRaiseIrql > PASSIVE_LEVEL)
        KeLowerIrql(OldIrql);
    InterlockedExchange(&Context->WaitFinished, 1);
}

static VOID NTAPI RemoveDpcSignaledWaitThread(_In_ PVOID Parameter)
{
    PDPC_REMOVE_CONTEXT Context = Parameter;

    KeSetSystemAffinityThread(Context->WaitAffinity);
    InterlockedExchange(&Context->WaitReady, 1);
    KeWaitForSingleObject(&Context->WaitEvent, Executive, KernelMode, FALSE, NULL);
    RemoveDpcWaitThread(Parameter);
    KeRevertToUserAffinityThread();
}

static VOID NTAPI RemoveDpcSelfRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDPC_REMOVE_CONTEXT Context = DeferredContext;

    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Context->CallbackCpu = KeGetCurrentProcessorNumber();
    Context->CallbackIrql = KeGetCurrentIrql();
    Context->CallbackDpcData = Dpc->DpcData;
    Context->CallbackHistory = Dpc->ProcessorHistory;
    InterlockedExchange(&Context->CallbackStarted, 1);
    Context->SelfInterruptsBefore = KmtAreInterruptsEnabled();
    Context->SelfResult = Context->RemoveQueueDpcEx(Dpc, TRUE);
    Context->SelfInterruptsAfter = KmtAreInterruptsEnabled();
    InterlockedExchange(&Context->CallbackFinished, 1);
}

static VOID QueuedDpcRemovalCheck(
    _In_ PKMT_KE_REMOVE_QUEUE_DPC_EX RemoveQueueDpcEx,
    _In_ BOOLEAN WaitIfActive)
{
    DPC_REMOVE_CONTEXT Context;
    BOOLEAN Inserted;
    BOOLEAN Removed;
    BOOLEAN InterruptsBefore;
    BOOLEAN InterruptsAfter;
    PVOID QueuedDpcData;
    PVOID RemovedDpcData;
    ULONG_PTR QueuedHistory;
    ULONG_PTR RemovedHistory;
    KIRQL OldIrql;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeDpc(&Context.Dpc, RemoveDpcRoutine, &Context);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    InterruptsBefore = KmtAreInterruptsEnabled();
    Inserted = KeInsertQueueDpc(&Context.Dpc, NULL, NULL);
    QueuedDpcData = Context.Dpc.DpcData;
    QueuedHistory = Context.Dpc.ProcessorHistory;
    Removed = RemoveQueueDpcEx(&Context.Dpc, WaitIfActive);
    RemovedDpcData = Context.Dpc.DpcData;
    RemovedHistory = Context.Dpc.ProcessorHistory;
    InterruptsAfter = KmtAreInterruptsEnabled();
    KeLowerIrql(OldIrql);

    ok_bool_true(Inserted, "KeInsertQueueDpc at HIGH_LEVEL");
    ok(QueuedDpcData != NULL, "DpcData was NULL while queued\n");
    ok_bool_true(Removed, "KeRemoveQueueDpcEx should remove a queued DPC");
    ok_eq_pointer(RemovedDpcData, NULL);
    ok_eq_ulonglong(QueuedHistory, 1);
    ok_eq_ulonglong(RemovedHistory, 1);
    ok_eq_uint(InterruptsBefore, InterruptsAfter);
    ok_eq_long(Context.CallbackStarted, 0);
    ok_bool_false(RemoveQueueDpcEx(&Context.Dpc, WaitIfActive), "second KeRemoveQueueDpcEx");
    dump_trace("[KeDpcRemove] queued wait=%u history=0x%I64x\n", WaitIfActive, (ULONGLONG)QueuedHistory);
}

static VOID TimerDpcRemovalCheck(
    _In_ PKMT_KE_REMOVE_QUEUE_DPC_EX RemoveQueueDpcEx,
    _In_ KIRQL WaitIrql)
{
    DPC_REMOVE_CONTEXT Context;
    LARGE_INTEGER DueTime;
    PKTHREAD WaitThread;
    PVOID PoisonPointer = (PVOID)(ULONG_PTR)0x5555555555555555ULL;
    BOOLEAN Started;
    LONG FinishedBeforeRelease;
    ULONG Spins;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.RemoveQueueDpcEx = RemoveQueueDpcEx;
    Context.WaitValue = TRUE;
    Context.WaitRaiseIrql = WaitIrql;
    Context.WaitAffinity = (KAFFINITY)1 << ((KeNumberProcessors >= 3) ? 2 : 1);
    KeInitializeEvent(&Context.WaitEvent, NotificationEvent, FALSE);
    KeInitializeTimer(&Context.Timer);
    KeInitializeDpc(&Context.Dpc, RemoveDpcRoutine, &Context);
    Context.Dpc.DpcListEntry.Next = PoisonPointer;
    WaitThread = KmtStartThread(RemoveDpcSignaledWaitThread, &Context);
    if (!WaitThread)
        return;
    Started = WaitForValue(&Context.WaitReady, 1);
    ok_bool_true(Started, "timer DPC wait thread should be ready");
    if (!Started)
    {
        KeSetEvent(&Context.WaitEvent, IO_NO_INCREMENT, FALSE);
        KmtFinishThread(WaitThread, NULL);
        return;
    }
    DueTime.QuadPart = -10 * 1000 * 200;
    ok_bool_false(KeSetTimer(&Context.Timer, DueTime, &Context.Dpc), "fresh timer was set");
    KeSetSystemAffinityThread((KAFFINITY)1 << 1);

    Started = WaitForValue(&Context.CallbackStarted, 1);
    ok_bool_true(Started, "timer DPC should start");
    if (!Started)
    {
        KeCancelTimer(&Context.Timer);
        InterlockedExchange(&Context.CallbackRelease, 1);
        KeSetEvent(&Context.WaitEvent, IO_NO_INCREMENT, FALSE);
        KmtFinishThread(WaitThread, NULL);
        KeSetSystemAffinityThread((KAFFINITY)1);
        return;
    }

    ok_eq_ulong(Context.CallbackCpu, 0);
    ok_eq_uint(Context.CallbackIrql, DISPATCH_LEVEL);
    ok_bool_true(Context.CallbackInterruptsEnabled, "timer DPC callback interrupts");
    ok_eq_pointer(Context.CallbackDpcData, NULL);
    ok_eq_pointer(Context.CallbackDpcListNext, PoisonPointer);
    ok_eq_ulonglong(Context.CallbackHistory, 1);
    ok_bool_false(RemoveQueueDpcEx(&Context.Dpc, FALSE), "active timer DPC with WaitIfActive=FALSE");

    KeSetEvent(&Context.WaitEvent, IO_NO_INCREMENT, FALSE);
    ok_bool_true(WaitForValue(&Context.WaitStarted, 1), "timer DPC wait thread should start");
    for (Spins = 0; Spins < 20000 && !Context.WaitFinished; Spins++)
        KeStallExecutionProcessor(1);
    FinishedBeforeRelease = Context.WaitFinished;
    InterlockedExchange(&Context.CallbackRelease, 1);
    ok_bool_true(WaitForValue(&Context.CallbackFinished, 1), "timer DPC should finish");
    ok_bool_true(WaitForValue(&Context.WaitFinished, 1), "timer DPC waiter should finish");
    KmtFinishThread(WaitThread, NULL);

    ok_bool_false(Context.WaitResult, "active timer DPC waiter result");
    ok_eq_uint(Context.WaitIrql, WaitIrql);
    ok_eq_ulong(Context.WaitCpu, (KeNumberProcessors >= 3) ? 2 : 1);
    ok_eq_long(FinishedBeforeRelease, WaitIrql >= DISPATCH_LEVEL);
    ok_bool_true(Context.WaitInterruptsBefore, "timer DPC wait thread interrupts before call");
    ok_bool_true(Context.WaitInterruptsAfter, "timer DPC wait thread interrupts after call");
    ok_eq_long(Context.CallbackTimedOut, 0);
    ok_eq_ulonglong(Context.Dpc.ProcessorHistory, 1);
    ok_bool_false(KeCancelTimer(&Context.Timer), "cancel expired timer");
    dump_trace("[KeDpcRemove] timer direct cpu=%lu list=%p history=0x%I64x wait_irql=%u wait_cpu=%lu early=%ld irq=%u/%u\n", Context.CallbackCpu, Context.CallbackDpcListNext, (ULONGLONG)Context.CallbackHistory, Context.WaitIrql, Context.WaitCpu, FinishedBeforeRelease, Context.WaitInterruptsBefore, Context.WaitInterruptsAfter);
    KeSetSystemAffinityThread((KAFFINITY)1);
}

static VOID RemoveQueueDpcExCheck(VOID)
{
    DPC_REMOVE_CONTEXT Context;
    PKMT_KE_REMOVE_QUEUE_DPC_EX RemoveQueueDpcEx;
    PKTHREAD WaitThread;
    BOOLEAN Started;
    BOOLEAN WaitStarted;
    BOOLEAN Removed;
    BOOLEAN InterruptsBefore;
    BOOLEAN InterruptsAfter;
    ULONG_PTR ActiveHistory;
    ULONG_PTR AccumulatedHistory;
    ULONG_PTR FinishedHistory;
    ULONG TargetCpu;
    ULONG Spins;
    KIRQL OldIrql;
    BOOLEAN Inserted;

    RemoveQueueDpcEx = (PKMT_KE_REMOVE_QUEUE_DPC_EX)KmtGetSystemRoutineAddress(L"KeRemoveQueueDpcEx");
    ok(RemoveQueueDpcEx != NULL, "KeRemoveQueueDpcEx is not exported\n");
    if (!RemoveQueueDpcEx)
        return;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.RemoveQueueDpcEx = RemoveQueueDpcEx;
    KeInitializeDpc(&Context.Dpc, RemoveDpcRoutine, &Context);
    InterruptsBefore = KmtAreInterruptsEnabled();
    ok_bool_false(RemoveQueueDpcEx(&Context.Dpc, FALSE), "fresh DPC with WaitIfActive=FALSE");
    ok_bool_false(RemoveQueueDpcEx(&Context.Dpc, TRUE), "fresh DPC with WaitIfActive=TRUE");
    ok_bool_false(RemoveQueueDpcEx(&Context.Dpc, 2), "fresh DPC with WaitIfActive=2");
    InterruptsAfter = KmtAreInterruptsEnabled();
    ok_bool_true(InterruptsBefore, "PASSIVE_LEVEL interrupts before removal");
    ok_eq_uint(InterruptsBefore, InterruptsAfter);
    ok_eq_pointer(Context.Dpc.DpcData, NULL);
    ok_eq_ulonglong(Context.Dpc.ProcessorHistory, 0);

    KeSetSystemAffinityThread((KAFFINITY)1);
    ok_eq_ulong(KeGetCurrentProcessorNumber(), 0);
    QueuedDpcRemovalCheck(RemoveQueueDpcEx, FALSE);
    QueuedDpcRemovalCheck(RemoveQueueDpcEx, TRUE);

    if (KeNumberProcessors < 2)
    {
        KeRevertToUserAffinityThread();
        skip(FALSE, "Single CPU -- skipping active remote DPC checks\n");
        return;
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.RemoveQueueDpcEx = RemoveQueueDpcEx;
    Context.WaitValue = 2;
    TargetCpu = 1;
    KeInitializeDpc(&Context.Dpc, RemoveDpcRoutine, &Context);
    KeSetImportanceDpc(&Context.Dpc, HighImportance);
    KeSetTargetProcessorDpc(&Context.Dpc, (CCHAR)TargetCpu);
    ok_bool_true(KeInsertQueueDpc(&Context.Dpc, NULL, NULL), "queue remote active DPC");
    Started = WaitForValue(&Context.CallbackStarted, 1);
    ok_bool_true(Started, "remote DPC should start");
    if (!Started)
    {
        InterlockedExchange(&Context.CallbackRelease, 1);
        KeRevertToUserAffinityThread();
        return;
    }

    ActiveHistory = Context.Dpc.ProcessorHistory;
    InterruptsBefore = KmtAreInterruptsEnabled();
    Removed = RemoveQueueDpcEx(&Context.Dpc, FALSE);
    InterruptsAfter = KmtAreInterruptsEnabled();
    ok_bool_false(Removed, "active DPC with WaitIfActive=FALSE");
    ok_eq_uint(InterruptsBefore, InterruptsAfter);
    ok_eq_long(Context.CallbackFinished, 0);
    ok_eq_ulong(Context.CallbackCpu, TargetCpu);
    ok_eq_uint(Context.CallbackIrql, DISPATCH_LEVEL);
    ok_bool_true(Context.CallbackInterruptsEnabled, "remote DPC callback interrupts");
    ok_eq_pointer(Context.CallbackDpcData, NULL);

    WaitThread = KmtStartThread(RemoveDpcWaitThread, &Context);
    if (!WaitThread)
    {
        InterlockedExchange(&Context.CallbackRelease, 1);
        WaitForValue(&Context.CallbackFinished, 1);
        KeRevertToUserAffinityThread();
        return;
    }

    WaitStarted = WaitForValue(&Context.WaitStarted, 1);
    ok_bool_true(WaitStarted, "wait thread should start");
    for (Spins = 0; Spins < 20000 && !Context.WaitFinished; Spins++)
        KeStallExecutionProcessor(1);
    ok_eq_long(Context.WaitFinished, 0);
    InterlockedExchange(&Context.CallbackRelease, 1);
    ok_bool_true(WaitForValue(&Context.CallbackFinished, 1), "remote DPC should finish");
    ok_bool_true(WaitForValue(&Context.WaitFinished, 1), "KeRemoveQueueDpcEx waiter should finish");
    KmtFinishThread(WaitThread, NULL);
    FinishedHistory = Context.Dpc.ProcessorHistory;

    ok_bool_false(Context.WaitResult, "active DPC waiter result");
    ok_eq_uint(Context.WaitIrql, PASSIVE_LEVEL);
    ok_bool_true(Context.WaitInterruptsBefore, "wait thread interrupts before call");
    ok_bool_true(Context.WaitInterruptsAfter, "wait thread interrupts after call");
    ok_eq_long(Context.CallbackTimedOut, 0);
    ok_eq_ulonglong(Context.CallbackHistory, (ULONG_PTR)1 << TargetCpu);
    ok_eq_ulonglong(ActiveHistory, (ULONG_PTR)1 << TargetCpu);
    ok_eq_ulonglong(FinishedHistory, (ULONG_PTR)1 << TargetCpu);
    dump_trace("[KeDpcRemove] active history callback=0x%I64x observed=0x%I64x finished=0x%I64x\n", (ULONGLONG)Context.CallbackHistory, (ULONGLONG)ActiveHistory, (ULONGLONG)FinishedHistory);

    KeSetTargetProcessorDpc(&Context.Dpc, 0);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    Inserted = KeInsertQueueDpc(&Context.Dpc, NULL, NULL);
    AccumulatedHistory = Context.Dpc.ProcessorHistory;
    Removed = RemoveQueueDpcEx(&Context.Dpc, FALSE);
    KeLowerIrql(OldIrql);
    ok_bool_true(Inserted, "requeue DPC on CPU 0");
    ok_bool_true(Removed, "remove requeued DPC on CPU 0");
    ok_eq_ulonglong(AccumulatedHistory, ((ULONG_PTR)1 << TargetCpu) | 1);

    TimerDpcRemovalCheck(RemoveQueueDpcEx, PASSIVE_LEVEL);
    TimerDpcRemovalCheck(RemoveQueueDpcEx, APC_LEVEL);
    TimerDpcRemovalCheck(RemoveQueueDpcEx, DISPATCH_LEVEL);
    TimerDpcRemovalCheck(RemoveQueueDpcEx, SYNCH_LEVEL);
    TimerDpcRemovalCheck(RemoveQueueDpcEx, HIGH_LEVEL);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.RemoveQueueDpcEx = RemoveQueueDpcEx;
    KeInitializeDpc(&Context.Dpc, RemoveDpcSelfRoutine, &Context);
    KeSetImportanceDpc(&Context.Dpc, HighImportance);
    KeSetTargetProcessorDpc(&Context.Dpc, (CCHAR)TargetCpu);
    ok_bool_true(KeInsertQueueDpc(&Context.Dpc, NULL, NULL), "queue self-removing DPC");
    ok_bool_true(WaitForValue(&Context.CallbackFinished, 1), "self-removing DPC should finish");
    ok_bool_false(Context.SelfResult, "active DPC self-removal result");
    ok_bool_true(Context.SelfInterruptsBefore, "self-removing DPC interrupts before call");
    ok_bool_true(Context.SelfInterruptsAfter, "self-removing DPC interrupts after call");
    ok_eq_ulong(Context.CallbackCpu, TargetCpu);
    ok_eq_uint(Context.CallbackIrql, DISPATCH_LEVEL);
    ok_eq_pointer(Context.CallbackDpcData, NULL);
    ok_eq_ulonglong(Context.CallbackHistory, (ULONG_PTR)1 << TargetCpu);
    KeRevertToUserAffinityThread();
}

START_TEST(KeDpcRemove)
{
    DpcInitializationCheck();
    RemoveQueueDpcExCheck();
}
