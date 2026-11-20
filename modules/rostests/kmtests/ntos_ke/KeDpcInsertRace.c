/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KeInsertQueueDpc of one DPC from every processor at once
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define DPC_RACE_ITERATIONS 500
#define DPC_RACE_TAG        'RDtK'

typedef struct _DPC_RACE_CONTEXT
{
    KDPC Dpc;
    volatile LONG Inserted;
    volatile LONG Ran;
} DPC_RACE_CONTEXT, *PDPC_RACE_CONTEXT;

static
VOID
NTAPI
RaceDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDPC_RACE_CONTEXT Context = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedIncrement(&Context->Ran);
}

static
ULONG_PTR
NTAPI
RaceInsertWorker(
    _In_ ULONG_PTR Argument)
{
    PDPC_RACE_CONTEXT Context = (PDPC_RACE_CONTEXT)Argument;

    if (KeInsertQueueDpc(&Context->Dpc, NULL, NULL))
        InterlockedIncrement(&Context->Inserted);

    return 0;
}

START_TEST(KeDpcInsertRace)
{
    PDPC_RACE_CONTEXT Context;
    ULONG Iteration;
    LONG Inserted = 0, Ran = 0;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Needs at least two processors\n");
        return;
    }

    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), DPC_RACE_TAG);
    if (!Context)
    {
        skip(FALSE, "Out of memory\n");
        return;
    }

    KeInitializeDpc(&Context->Dpc, RaceDpcRoutine, Context);

    for (Iteration = 0; Iteration < DPC_RACE_ITERATIONS; Iteration++)
    {
        Context->Inserted = 0;
        Context->Ran = 0;

        KeIpiGenericCall(RaceInsertWorker, (ULONG_PTR)Context);
        KeFlushQueuedDpcs();

        Inserted = Context->Inserted;
        Ran = Context->Ran;
        if ((Inserted != 1) || (Ran != 1))
            break;
    }

    ok_eq_ulong(Iteration, DPC_RACE_ITERATIONS);
    ok_eq_long(Inserted, 1);
    ok_eq_long(Ran, 1);
    trace("KeDpcInsertRace: %lu processors, %lu rounds, last round inserted %ld ran %ld\n",
          (ULONG)KeNumberProcessors, Iteration, Inserted, Ran);

    ExFreePoolWithTag(Context, DPC_RACE_TAG);
}
