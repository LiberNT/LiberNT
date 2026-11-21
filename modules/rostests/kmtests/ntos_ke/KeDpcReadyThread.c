/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Readying a thread whose ideal processor is running a DPC
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define READY_DPC_MS    300
#define READY_PASSES    3

typedef struct _READY_CONTEXT
{
    KDPC Dpc;
    KEVENT Ready;
    KEVENT Wake;
    ULONG TargetCpu;
    LONGLONG Frequency;
    volatile LONG DpcStarted;
    LONGLONG DpcEnd;
    LONGLONG WakeTime;
    ULONG WakeCpu;
    NTSTATUS IdealStatus;
} READY_CONTEXT, *PREADY_CONTEXT;

static VOID NTAPI
BusyDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PREADY_CONTEXT Context = DeferredContext;
    LONGLONG Deadline;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context->Frequency * READY_DPC_MS / 1000;
    InterlockedExchange(&Context->DpcStarted, 1);
    while (KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    Context->DpcEnd = KeQueryPerformanceCounter(NULL).QuadPart;
}

static VOID NTAPI
WaiterThread(
    _In_ PVOID Parameter)
{
    PREADY_CONTEXT Context = Parameter;
    ULONG Ideal = Context->TargetCpu;

    Context->IdealStatus = ZwSetInformationThread(NtCurrentThread(), ThreadIdealProcessor, &Ideal, sizeof(Ideal));
    KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Context->Wake, Executive, KernelMode, FALSE, NULL);
    Context->WakeTime = KeQueryPerformanceCounter(NULL).QuadPart;
    Context->WakeCpu = KeGetCurrentProcessorNumberEx(NULL);
}

START_TEST(KeDpcReadyThread)
{
    READY_CONTEXT Context;
    LARGE_INTEGER Frequency, Delay;
    PKTHREAD Thread;
    LONGLONG SetTime, Deadline;
    BOOLEAN Started;
    ULONG Pass;

    if (skip(KeNumberProcessors >= 3, "Three processors required\n"))
        return;

    KeSetSystemAffinityThread(1);
    for (Pass = 0; Pass < READY_PASSES; Pass++)
    {
        RtlZeroMemory(&Context, sizeof(Context));
        KeQueryPerformanceCounter(&Frequency);
        Context.Frequency = Frequency.QuadPart;
        Context.TargetCpu = 1;
        KeInitializeEvent(&Context.Ready, NotificationEvent, FALSE);
        KeInitializeEvent(&Context.Wake, NotificationEvent, FALSE);
        KeInitializeDpc(&Context.Dpc, BusyDpc, &Context);
        KeSetTargetProcessorDpc(&Context.Dpc, (CCHAR)Context.TargetCpu);
        KeSetImportanceDpc(&Context.Dpc, HighImportance);

        Thread = KmtStartThread(WaiterThread, &Context);
        if (!Thread)
            break;
        KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, NULL);
        Delay.QuadPart = -20 * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        ok_bool_true(KeInsertQueueDpc(&Context.Dpc, NULL, NULL), "busy DPC queued");
        Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context.Frequency * 2;
        while (!(Started = (InterlockedCompareExchange(&Context.DpcStarted, 0, 0) != 0)) &&
               KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        {
            YieldProcessor();
        }
        SetTime = KeQueryPerformanceCounter(NULL).QuadPart;
        KeSetEvent(&Context.Wake, IO_NO_INCREMENT, FALSE);
        KmtFinishThread(Thread, NULL);
        KeFlushQueuedDpcs();

        ok_bool_true(Started, "busy DPC started");
        ok(NT_SUCCESS(Context.IdealStatus), "ThreadIdealProcessor returned 0x%lx\n", Context.IdealStatus);
        ok(Context.WakeCpu != Context.TargetCpu, "Thread woke on processor %lu, which was running a DPC\n", Context.WakeCpu);
        ok(Context.WakeTime < Context.DpcEnd, "Thread waited for the DPC on its ideal processor\n");
        trace("READY_DPC pass=%lu wake_cpu=%lu latency_ms=%I64d\n",
              Pass, Context.WakeCpu, (Context.WakeTime - SetTime) * 1000 / Context.Frequency);
    }
    KeRevertToUserAffinityThread();
}
