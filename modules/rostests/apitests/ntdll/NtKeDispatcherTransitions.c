/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Dispatcher object state transition tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"

static VOID
TestEvents(VOID)
{
    HANDLE Event;
    NTSTATUS Status;
    LARGE_INTEGER Zero;
    LONG Previous;
    EVENT_TYPE Type;

    Zero.QuadPart = 0;
    for (Type = NotificationEvent; Type <= SynchronizationEvent; Type++)
    {
        Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, Type, FALSE);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) continue;

        Status = NtWaitForSingleObject(Event, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        Previous = -1;
        Status = NtSetEvent(Event, &Previous);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Previous == 0, "type %u set previous %ld\n", Type, Previous);
        Previous = -1;
        Status = NtSetEvent(Event, &Previous);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Previous == 1, "type %u repeat set previous %ld\n", Type, Previous);
        Status = NtWaitForSingleObject(Event, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtWaitForSingleObject(Event, FALSE, &Zero);
        ok_ntstatus(Status, Type == NotificationEvent ? STATUS_SUCCESS : STATUS_TIMEOUT);
        Previous = -1;
        Status = NtResetEvent(Event, &Previous);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Previous == (Type == NotificationEvent ? 1 : 0),
           "type %u reset previous %ld\n", Type, Previous);
        Status = NtWaitForSingleObject(Event, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        NtClose(Event);
    }
}

static VOID
TestSemaphore(VOID)
{
    HANDLE Semaphore;
    NTSTATUS Status;
    LARGE_INTEGER Zero;
    LONG Previous;
    SEMAPHORE_BASIC_INFORMATION Info;

    Zero.QuadPart = 0;
    Status = NtCreateSemaphore(&Semaphore, SEMAPHORE_ALL_ACCESS, NULL, 1, 2);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Previous = -1;
    Status = NtReleaseSemaphore(Semaphore, 1, &Previous);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Previous == 1, "release previous %ld\n", Previous);
    Status = NtReleaseSemaphore(Semaphore, 1, NULL);
    ok_ntstatus(Status, STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    Status = NtQuerySemaphore(Semaphore, SemaphoreBasicInformation, &Info, sizeof(Info), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Info.CurrentCount == 2 && Info.MaximumCount == 2,
           "after overflow count %ld limit %ld\n", Info.CurrentCount, Info.MaximumCount);
    Status = NtWaitForSingleObject(Semaphore, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForSingleObject(Semaphore, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForSingleObject(Semaphore, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    Previous = -1;
    Status = NtReleaseSemaphore(Semaphore, 2, &Previous);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Previous == 0, "refill previous %ld\n", Previous);
    NtClose(Semaphore);
}

static VOID
TestWaits(VOID)
{
    HANDLE Objects[2];
    NTSTATUS Status;
    LARGE_INTEGER Zero;

    Zero.QuadPart = 0;
    Status = NtCreateEvent(&Objects[0], EVENT_ALL_ACCESS, NULL, SynchronizationEvent, TRUE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtCreateEvent(&Objects[1], EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) { NtClose(Objects[0]); return; }

    Status = NtWaitForMultipleObjects(2, Objects, WaitAll, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    Status = NtWaitForSingleObject(Objects[0], FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtSetEvent(Objects[0], NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtSetEvent(Objects[1], NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForMultipleObjects(2, Objects, WaitAny, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_WAIT_0);
    Status = NtWaitForSingleObject(Objects[0], FALSE, &Zero);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    Status = NtWaitForSingleObject(Objects[1], FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtSetEvent(Objects[0], NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtSetEvent(Objects[1], NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForMultipleObjects(2, Objects, WaitAll, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForSingleObject(Objects[0], FALSE, &Zero);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    Status = NtWaitForSingleObject(Objects[1], FALSE, &Zero);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    NtClose(Objects[1]);
    NtClose(Objects[0]);
}

static VOID
TestMutant(VOID)
{
    HANDLE Mutant;
    NTSTATUS Status;
    LARGE_INTEGER Zero;
    LONG Previous;

    Zero.QuadPart = 0;
    Status = NtCreateMutant(&Mutant, MUTANT_ALL_ACCESS, NULL, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtReleaseMutant(Mutant, NULL);
    ok_ntstatus(Status, STATUS_MUTANT_NOT_OWNED);
    Status = NtWaitForSingleObject(Mutant, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtWaitForSingleObject(Mutant, FALSE, &Zero);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Previous = 1;
    Status = NtReleaseMutant(Mutant, &Previous);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Previous == -1, "recursive release previous %ld\n", Previous);
    Previous = 1;
    Status = NtReleaseMutant(Mutant, &Previous);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Previous == 0, "final release previous %ld\n", Previous);
    Status = NtReleaseMutant(Mutant, NULL);
    ok_ntstatus(Status, STATUS_MUTANT_NOT_OWNED);
    NtClose(Mutant);
}

static VOID
TestTimers(VOID)
{
    HANDLE Timer;
    NTSTATUS Status;
    LARGE_INTEGER Zero, Due, Timeout;
    TIMER_TYPE Type;

    Zero.QuadPart = 0;
    Timeout.QuadPart = -50000000;
    for (Type = NotificationTimer; Type <= SynchronizationTimer; Type++)
    {
        Status = NtCreateTimer(&Timer, TIMER_ALL_ACCESS, NULL, Type);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) continue;
        Status = NtWaitForSingleObject(Timer, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        Due.QuadPart = -100000000;
        Status = NtSetTimer(Timer, &Due, NULL, NULL, FALSE, 0, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtCancelTimer(Timer, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtWaitForSingleObject(Timer, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        Due.QuadPart = -100000;
        Status = NtSetTimer(Timer, &Due, NULL, NULL, FALSE, 0, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtWaitForSingleObject(Timer, FALSE, &Timeout);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtWaitForSingleObject(Timer, FALSE, &Zero);
        ok_ntstatus(Status, Type == NotificationTimer ? STATUS_SUCCESS : STATUS_TIMEOUT);
        Due.QuadPart = -100000000;
        Status = NtSetTimer(Timer, &Due, NULL, NULL, FALSE, 0, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtWaitForSingleObject(Timer, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        Status = NtCancelTimer(Timer, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        NtClose(Timer);
    }
}

static VOID NTAPI
TestApcRoutine(PVOID Context, PVOID Argument1, PVOID Argument2)
{
    LONG *Count = Context;
    if (Argument1 == (PVOID)1 && Argument2 == (PVOID)2)
        ++*Count;
}

static VOID
TestAlertableWait(VOID)
{
    HANDLE Event;
    NTSTATUS Status;
    LARGE_INTEGER Zero;
    LONG Count = 0;

    Zero.QuadPart = 0;
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtQueueApcThread(NtCurrentThread(), TestApcRoutine, &Count, (PVOID)1, (PVOID)2);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtWaitForSingleObject(Event, FALSE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
        ok(Count == 0, "nonalertable wait delivered %ld APCs\n", Count);
        Status = NtWaitForSingleObject(Event, TRUE, &Zero);
        ok_ntstatus(Status, STATUS_USER_APC);
        ok(Count == 1, "alertable wait delivered %ld APCs\n", Count);
        Status = NtWaitForSingleObject(Event, TRUE, &Zero);
        ok_ntstatus(Status, STATUS_TIMEOUT);
    }
    NtClose(Event);
}

START_TEST(NtKeDispatcherTransitions)
{
    TestEvents();
    TestSemaphore();
    TestWaits();
    TestMutant();
    TestTimers();
    TestAlertableWait();
}
