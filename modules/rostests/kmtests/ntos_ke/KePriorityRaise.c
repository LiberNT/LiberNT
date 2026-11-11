/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Preemption by a ready thread raised above the running thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _PRIORITY_RAISE_CONTEXT
{
    KAFFINITY Processor;
    volatile LONG Ran;
} PRIORITY_RAISE_CONTEXT, *PPRIORITY_RAISE_CONTEXT;

static
VOID
NTAPI
PriorityRaiseWorker(
    _In_ PVOID Parameter)
{
    PPRIORITY_RAISE_CONTEXT Context = Parameter;
    KAFFINITY PreviousAffinity;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Processor);
    InterlockedExchange(&Context->Ran, 1);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static
BOOLEAN
WaitForReadyOnProcessor(
    _In_ PKTHREAD Thread,
    _In_ ULONG Processor)
{
    ULONGLONG Deadline = KeQueryInterruptTime() + 100ULL * 10 * 1000;

    while ((((volatile KTHREAD *)Thread)->State != Ready) ||
           (((volatile KTHREAD *)Thread)->NextProcessor != Processor))
    {
        if (KeQueryInterruptTime() >= Deadline)
            return FALSE;
        KeStallExecutionProcessor(10);
    }

    return TRUE;
}

START_TEST(KePriorityRaise)
{
    PRIORITY_RAISE_CONTEXT Context;
    PKTHREAD Worker;
    KAFFINITY PreviousAffinity;
    KPRIORITY PreviousPriority;
    ULONG Processor;
    BOOLEAN Queued = FALSE;
    LONG RanWhileQueued = -1;
    LONG RanAfterRaise = -1;

    RtlZeroMemory(&Context, sizeof(Context));
    Processor = KeGetCurrentProcessorNumber();
    Context.Processor = (KAFFINITY)1 << Processor;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context.Processor);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    Worker = KmtStartThread(PriorityRaiseWorker, &Context);
    if (Worker)
    {
        Queued = WaitForReadyOnProcessor(Worker, Processor);
        RanWhileQueued = Context.Ran;
        if (Queued)
        {
            /* Above the running thread it gets this processor before the call returns */
            KeSetPriorityThread(Worker, LOW_REALTIME_PRIORITY + 1);
            RanAfterRaise = Context.Ran;
        }
    }

    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);

    if (Worker)
    {
        ok(Queued, "Worker was not queued on processor %lu\n", Processor);
        ok_eq_long(RanWhileQueued, 0);
        if (Queued)
            ok_eq_long(RanAfterRaise, 1);
        KmtFinishThread(Worker, NULL);
        ok_eq_long(Context.Ran, 1);
    }
}
