/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Deferred-ready processing before a wait switch
 */

#include <kmt_test.h>

typedef struct _DEFERRED_READY_CONTEXT
{
    KEVENT SignalerReady;
    KEVENT SpinnerReady;
    KEVENT StartSignaler;
    KEVENT StartSpinner;
    KEVENT ProducerReady;
    KEVENT FinishSignaler;
    KAFFINITY Processor;
    volatile LONG Observed;
    volatile LONG Expired;
    volatile LONG Failed;
    volatile LONG Stop;
} DEFERRED_READY_CONTEXT;

static DEFERRED_READY_CONTEXT DeferredContext;

static
VOID
NTAPI
DeferredSignaler(_In_ PVOID Parameter)
{
    DEFERRED_READY_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Delay, Timeout;
    NTSTATUS Status;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Processor);
    KeSetPriorityThread(KeGetCurrentThread(), 20);
    KeSetEvent(&Context->SignalerReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->StartSignaler, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
    {
        InterlockedExchange(&Context->Failed, 1);
        goto Exit;
    }
    Delay.QuadPart = -50LL * 1000 * 10;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    if (!Context->Stop)
    {
        KeSetEvent(&Context->StartSpinner, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Context->ProducerReady, IO_NO_INCREMENT, TRUE);
        KeWaitForSingleObject(&Context->FinishSignaler, Executive, KernelMode, FALSE, &Timeout);
    }
Exit:
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
DeferredSpinner(_In_ PVOID Parameter)
{
    DEFERRED_READY_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    KIRQL OldIrql;
    NTSTATUS Status;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->Processor);
    KeSetPriorityThread(KeGetCurrentThread(), 16);
    KeSetEvent(&Context->SpinnerReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->StartSpinner, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
    {
        InterlockedExchange(&Context->Failed, 1);
        goto Exit;
    }
    if (!Context->Stop)
    {
        OldIrql = KeRaiseIrqlToDpcLevel();
        Deadline = KeQueryInterruptTime() + 500LL * 1000 * 10;
        while (!Context->Observed && !Context->Stop && KeQueryInterruptTime() < Deadline)
            YieldProcessor();
        if (!Context->Observed && !Context->Stop)
            InterlockedExchange(&Context->Expired, 1);
        KeLowerIrql(OldIrql);
    }
Exit:
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeDeferredReady)
{
    DEFERRED_READY_CONTEXT *Context = &DeferredContext;
    HANDLE Signaler = NULL, Spinner = NULL;
    KAFFINITY Active, First, Second, PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Active = KeQueryActiveProcessors();
    First = Active & (0 - Active);
    Active &= ~First;
    Second = Active & (0 - Active);
    if (!First || !Second)
    {
        skip(FALSE, "Two processors are required for deferred-ready testing\n");
        return;
    }
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->SignalerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->SpinnerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->StartSignaler, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->StartSpinner, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->ProducerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->FinishSignaler, NotificationEvent, FALSE);
    Context->Processor = First;
    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Second);
    Status = PsCreateSystemThread(&Spinner, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  DeferredSpinner, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = PsCreateSystemThread(&Signaler, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  DeferredSignaler, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->SpinnerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->SignalerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    KeSetEvent(&Context->StartSignaler, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(&Context->ProducerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InterlockedExchange(&Context->Observed, 1);
    KeSetEvent(&Context->FinishSignaler, IO_NO_INCREMENT, FALSE);
    Status = ZwWaitForSingleObject(Spinner, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context->Expired, 0);

Cleanup:
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->StartSignaler, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Context->StartSpinner, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Context->FinishSignaler, IO_NO_INCREMENT, FALSE);
    if (Signaler)
    {
        ZwWaitForSingleObject(Signaler, FALSE, NULL);
        ObCloseHandle(Signaler, KernelMode);
    }
    if (Spinner)
    {
        ZwWaitForSingleObject(Spinner, FALSE, NULL);
        ObCloseHandle(Spinner, KernelMode);
    }
    ok_eq_long(Context->Failed, 0);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
