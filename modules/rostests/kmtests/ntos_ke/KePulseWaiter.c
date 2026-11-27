/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a pulse releases a waiter whose wait block is already queued
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define PULSE_ITERATIONS 1000

typedef struct _PULSE_WAITER_CONTEXT
{
    KEVENT Target;
    KEVENT Ready;
    KEVENT Done;
    volatile LONG Stop;
    NTSTATUS Status;
} PULSE_WAITER_CONTEXT, *PPULSE_WAITER_CONTEXT;

static
VOID
NTAPI
PulseWaiterThread(
    _In_ PVOID Parameter)
{
    PPULSE_WAITER_CONTEXT Context = Parameter;
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -200LL * 10 * 1000;
    for (;;)
    {
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
        if (InterlockedCompareExchange(&Context->Stop, 0, 0))
            break;
        Context->Status = KeWaitForSingleObject(&Context->Target, Executive, KernelMode, FALSE, &Timeout);
        KeSetEvent(&Context->Done, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestPulseQueuedWaiter(
    _In_ EVENT_TYPE Type)
{
    PULSE_WAITER_CONTEXT Context;
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    ULONG Index, Released = 0, Missed = 0, NotQueued = 0;
    ULONG Spin;
    NTSTATUS Status;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeEvent(&Context.Target, Type, FALSE);
    KeInitializeEvent(&Context.Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Done, SynchronizationEvent, FALSE);

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PulseWaiterThread, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    for (Index = 0; Index < PULSE_ITERATIONS; Index++)
    {
        KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, NULL);

        for (Spin = 0; Spin < 10000000; Spin++)
        {
            KeMemoryBarrier();
            if (Context.Target.Header.WaitListHead.Flink != &Context.Target.Header.WaitListHead)
                break;
            YieldProcessor();
        }
        if (Spin == 10000000)
            NotQueued++;

        KePulseEvent(&Context.Target, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(&Context.Done, Executive, KernelMode, FALSE, NULL);

        if (Context.Status == STATUS_SUCCESS)
            Released++;
        else
            Missed++;
    }

    InterlockedExchange(&Context.Stop, 1);
    KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(ThreadObject);

    trace("[KePulseWaiter] type=%u released=%lu missed=%lu not-queued=%lu\n", Type, Released, Missed, NotQueued);
    ok_eq_ulong(NotQueued, 0UL);
    ok_eq_ulong(Missed, 0UL);
    ok_eq_ulong(Released, (ULONG)PULSE_ITERATIONS);
    ok_eq_long(KeReadStateEvent(&Context.Target), 0L);
}

START_TEST(KePulseWaiter)
{
    TestPulseQueuedWaiter(NotificationEvent);
    TestPulseQueuedWaiter(SynchronizationEvent);
}
