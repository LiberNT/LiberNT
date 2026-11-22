/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread queued behind a higher priority on its ideal processor reaches another one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define BALANCE_HOG_PRIORITY     20
#define BALANCE_WORKER_PRIORITY  10
#define BALANCE_SPINNER_PRIORITY 5

typedef struct _BALANCE_CONTEXT
{
    KEVENT SpinnerReady;
    KEVENT WorkerReady;
    KEVENT Go;
    KAFFINITY Hog;
    KAFFINITY Other;
    ULONG HogNumber;
    volatile LONG Stop;
    volatile LONG Ran;
    volatile LONG RanOn;
    volatile LONG Failed;
} BALANCE_CONTEXT, *PBALANCE_CONTEXT;

static BALANCE_CONTEXT BalanceContext;

static
VOID
NTAPI
BalanceSpinner(
    _In_ PVOID Parameter)
{
    PBALANCE_CONTEXT Context = Parameter;
    KAFFINITY PreviousAffinity;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Other);
    KeSetPriorityThread(KeGetCurrentThread(), BALANCE_SPINNER_PRIORITY);
    KeSetEvent(&Context->SpinnerReady, IO_NO_INCREMENT, FALSE);
    while (!Context->Stop)
        YieldProcessor();
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static
VOID
NTAPI
BalanceWorker(
    _In_ PVOID Parameter)
{
    PBALANCE_CONTEXT Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    /* Before the affinity: a system affinity keeps an ideal processor it contains */
    KeSetIdealProcessorThread(KeGetCurrentThread(), (UCHAR)Context->HogNumber);
    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Hog | Context->Other);
    KeSetPriorityThread(KeGetCurrentThread(), BALANCE_WORKER_PRIORITY);
    KeSetEvent(&Context->WorkerReady, IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);

    InterlockedExchange(&Context->RanOn, (LONG)KeGetCurrentProcessorNumberEx(NULL));
    InterlockedExchange(&Context->Ran, 1);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

START_TEST(KeQuantumBalance)
{
    PBALANCE_CONTEXT Context = &BalanceContext;
    PKTHREAD Spinner = NULL, Worker = NULL;
    KAFFINITY Active, PreviousAffinity;
    KPRIORITY PreviousPriority;
    LARGE_INTEGER Timeout, Delay;
    ULONGLONG Deadline;
    ULONG OtherNumber = 0;
    LONG RanWhileHogging = 0, RanOn = -1;
    NTSTATUS Status;

    RtlZeroMemory(Context, sizeof(*Context));
    Active = KeQueryActiveProcessors();
    Context->Hog = Active & (0 - Active);
    Active &= ~Context->Hog;
    Context->Other = Active & (0 - Active);
    if (!Context->Hog || !Context->Other)
    {
        skip(FALSE, "Two processors are required to balance ready threads\n");
        return;
    }

    while (((KAFFINITY)1 << Context->HogNumber) != Context->Hog)
        Context->HogNumber++;
    while (((KAFFINITY)1 << OtherNumber) != Context->Other)
        OtherNumber++;

    KeInitializeEvent(&Context->SpinnerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->WorkerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->Go, NotificationEvent, FALSE);
    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Hog);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), BALANCE_HOG_PRIORITY);

    Spinner = KmtStartThread(BalanceSpinner, Context);
    Worker = KmtStartThread(BalanceWorker, Context);
    if (!Spinner || !Worker)
        goto Cleanup;

    Status = KeWaitForSingleObject(&Context->SpinnerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        goto Cleanup;
    Status = KeWaitForSingleObject(&Context->WorkerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        goto Cleanup;

    /* The worker outranks the spinner, so this is enough for it to reach its wait */
    Delay.QuadPart = -50LL * 1000 * 10;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
    Deadline = KeQueryInterruptTime() + 3ULL * 1000 * 1000 * 10;
    while (!Context->Ran && KeQueryInterruptTime() < Deadline)
        YieldProcessor();
    RanWhileHogging = Context->Ran;
    RanOn = Context->RanOn;
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), Context->HogNumber);

Cleanup:
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);

    if (Worker)
        KmtFinishThread(Worker, NULL);
    if (Spinner)
        KmtFinishThread(Spinner, NULL);

    if (Spinner && Worker)
    {
        ok_eq_long(Context->Failed, 0);
        ok_eq_long(RanWhileHogging, 1);
        ok_eq_long(RanOn, (LONG)OtherNumber);
    }
}
