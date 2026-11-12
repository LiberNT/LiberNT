/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Quantum end of a thread running at the highest priority
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _QUANTUM_END_CONTEXT
{
    KAFFINITY Processor;
    volatile LONG Ran;
} QUANTUM_END_CONTEXT, *PQUANTUM_END_CONTEXT;

static
VOID
NTAPI
QuantumEndWorker(
    _In_ PVOID Parameter)
{
    PQUANTUM_END_CONTEXT Context = Parameter;
    KAFFINITY PreviousAffinity;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Processor);
    InterlockedExchange(&Context->Ran, 1);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

START_TEST(KeQuantumEnd)
{
    QUANTUM_END_CONTEXT Context;
    PKTHREAD Worker;
    KAFFINITY PreviousAffinity;
    KPRIORITY PreviousPriority;
    ULONGLONG Deadline;
    ULONG Processor;
    LONG RanDuringSpin = 0;

    RtlZeroMemory(&Context, sizeof(Context));
    Processor = KeGetCurrentProcessorNumber();
    Context.Processor = (KAFFINITY)1 << Processor;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context.Processor);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), HIGH_PRIORITY);
    ok_eq_long(KeQueryPriorityThread(KeGetCurrentThread()), HIGH_PRIORITY);

    Worker = KmtStartThread(QuantumEndWorker, &Context);
    if (Worker)
    {
        /* Several quanta at priority 31 while a lower-priority thread is ready on this processor */
        Deadline = KeQueryInterruptTime() + 200ULL * 10 * 1000;
        while (KeQueryInterruptTime() < Deadline)
            KeStallExecutionProcessor(10);
        RanDuringSpin = Context.Ran;
        ok_eq_ulong(KeGetCurrentProcessorNumber(), Processor);
        ok_eq_long(KeQueryPriorityThread(KeGetCurrentThread()), HIGH_PRIORITY);
    }

    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);

    if (Worker)
    {
        ok_eq_long(RanDuringSpin, 0);
        KmtFinishThread(Worker, NULL);
        ok_eq_long(Context.Ran, 1);
    }
}
