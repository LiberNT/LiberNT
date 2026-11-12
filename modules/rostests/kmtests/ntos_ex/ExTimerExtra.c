/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended EX_TIMER / KTIMER coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT TimerEvent;
static volatile LONG DpcCount;
static volatile KIRQL ExtendedTimerIrql;

static
VOID
NTAPI
TimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Arg1,
    _In_opt_ PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    if (InterlockedIncrement(&DpcCount) >= (LONG)(ULONG_PTR)Context)
        KeSetEvent(&TimerEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestOneShotDpc(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    KeInitializeTimer(&Timer);
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)1);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 50;
    ok_bool_false(KeSetTimer(&Timer, DueTime, &Dpc), "fresh timer was set");

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(KeCancelTimer(&Timer), "cancel after fire");
    KeFlushQueuedDpcs();
    ok_eq_long(DpcCount, 1);
}

static
VOID
TestPeriodicDpc(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    KeInitializeTimerEx(&Timer, SynchronizationTimer);
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)3);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 30;
    KeSetTimerEx(&Timer, DueTime, 30, &Dpc);

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(KeCancelTimer(&Timer), "cancel armed periodic");
    KeFlushQueuedDpcs();
    ok(DpcCount >= 3, "periodic dpc count %ld\n", DpcCount);
}

static
VOID
TestCancelBeforeFire(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;

    KeInitializeTimer(&Timer);
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)1);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 1000 * 30;
    KeSetTimer(&Timer, DueTime, &Dpc);
    ok_bool_true(KeCancelTimer(&Timer), "cancel before fire");
    KeFlushQueuedDpcs();
    ok_bool_false(KeReadStateTimer(&Timer), "timer not signaled after cancel");
    ok_eq_long(DpcCount, 0L);
}

static
VOID
NTAPI
ExtendedTimerCallback(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    PKEVENT Event = Context;

    UNREFERENCED_PARAMETER(Timer);

    ExtendedTimerIrql = KeGetCurrentIrql();
    InterlockedIncrement(&DpcCount);
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestExtendedTimer(VOID)
{
    static const ULONG Attributes[] = {
        0, EX_TIMER_NOTIFICATION, EX_TIMER_HIGH_RESOLUTION,
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NOTIFICATION,
        EX_TIMER_NO_WAKE, EX_TIMER_NO_WAKE | EX_TIMER_NOTIFICATION
    };
    LARGE_INTEGER Timeout;
    PEX_TIMER Timer;
    BOOLEAN Result;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); ++Index)
    {
        Timer = ExAllocateTimer(NULL, NULL, Attributes[Index]);
        ok(Timer != NULL, "Allocation failed for attributes %lx\n", Attributes[Index]);
        if (Timer)
        {
            Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
            ok_eq_bool(Result, FALSE);
        }
    }

    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    ExtendedTimerIrql = PASSIVE_LEVEL;
    DpcCount = 0;
    Timer = ExAllocateTimer(ExtendedTimerCallback, &TimerEvent, EX_TIMER_NOTIFICATION);
    trace("ExAllocateTimer(notification) returned %p\n", Timer);
    ok(Timer != NULL, "ExAllocateTimer failed\n");
    if (Timer == NULL)
        return;

    Result = ExSetTimer(Timer, -10LL * 1000 * 20, 0, NULL);
    trace("ExSetTimer returned %u\n", Result);
    ok_eq_bool(Result, FALSE);
    Timeout.QuadPart = -10LL * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &Timeout);
    trace("extended timer wait returned 0x%08lx, callbacks %ld, IRQL %u\n", Status, DpcCount, ExtendedTimerIrql);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(DpcCount, 1);
    ok_eq_ulong(ExtendedTimerIrql, DISPATCH_LEVEL);

    Result = ExCancelTimer(Timer, NULL);
    trace("ExCancelTimer after expiry returned %u\n", Result);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
    trace("ExDeleteTimer returned %u\n", Result);
    ok_eq_bool(Result, FALSE);

    Timer = ExAllocateTimer(NULL, NULL, 0);
    ok(Timer != NULL, "ExAllocateTimer failed\n");
    if (!Timer)
        return;
    Result = ExSetTimer(Timer, -30LL * 1000 * 1000 * 10, 0, NULL);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
    ok_eq_bool(Result, TRUE);
}

static KEVENT TimerDeletedEvent;
static volatile LONG DeleteTestCalls;
static volatile LONG CallsBeforeDelete;
static volatile LONG RefusedInCallback;

static
VOID
NTAPI
TimerDeleted(
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    CallsBeforeDelete = DeleteTestCalls;
    KeSetEvent(&TimerDeletedEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
DeleteTestCallback(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    PEXT_DELETE_PARAMETERS Parameters = Context;
    LONG Refused = 0;

    if ((InterlockedIncrement(&DeleteTestCalls) != 1) || (Parameters == NULL))
        return;

    if (!ExDeleteTimer(Timer, FALSE, FALSE, Parameters))
        Refused |= 1;
    if (!ExSetTimer(Timer, -10LL * 1000 * 10, 0, NULL))
        Refused |= 2;
    if (!ExCancelTimer(Timer, NULL))
        Refused |= 4;
    if (!ExDeleteTimer(Timer, TRUE, FALSE, NULL))
        Refused |= 8;
    RefusedInCallback = Refused;
}

static
PEX_TIMER
StartDeleteTest(
    _In_opt_ PVOID Context)
{
    KeInitializeEvent(&TimerDeletedEvent, NotificationEvent, FALSE);
    DeleteTestCalls = 0;
    CallsBeforeDelete = -1;
    RefusedInCallback = 0;
    return ExAllocateTimer(DeleteTestCallback, Context, 0);
}

/* Without Wait the object outlives the expiry and callback it still has, then goes (WDK, Deleting a System-Allocated Timer Object) */
static
VOID
TestDeleteWithoutWaiting(VOID)
{
    EXT_DELETE_PARAMETERS Parameters;
    LARGE_INTEGER Timeout;
    PEX_TIMER Timer;
    NTSTATUS Status;
    BOOLEAN Result;

    ExInitializeDeleteTimerParameters(&Parameters);
    Parameters.DeleteCallback = TimerDeleted;
    Timeout.QuadPart = -10LL * 1000 * 1000;

    Timer = StartDeleteTest(NULL);
    if (skip(Timer != NULL, "ExAllocateTimer failed\n"))
        return;
    Result = ExSetTimer(Timer, -10LL * 1000 * 10, 0, NULL);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, FALSE, FALSE, &Parameters);
    ok_eq_bool(Result, FALSE);
    Status = KeWaitForSingleObject(&TimerDeletedEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(CallsBeforeDelete, 1L);

    Timer = StartDeleteTest(NULL);
    if (skip(Timer != NULL, "ExAllocateTimer failed\n"))
        return;
    Result = ExSetTimer(Timer, -10LL * 1000 * 1000 * 10, 0, NULL);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, TRUE, FALSE, &Parameters);
    ok_eq_bool(Result, TRUE);
    Status = KeWaitForSingleObject(&TimerDeletedEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(CallsBeforeDelete, 0L);

    Timer = StartDeleteTest(&Parameters);
    if (skip(Timer != NULL, "ExAllocateTimer failed\n"))
        return;
    Result = ExSetTimer(Timer, -10LL * 1000 * 10, 10LL * 1000 * 10, NULL);
    ok_eq_bool(Result, FALSE);
    Status = KeWaitForSingleObject(&TimerDeletedEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(RefusedInCallback, 15L);
    ok_eq_long(CallsBeforeDelete, 2L);
    ok_eq_long(DeleteTestCalls, 2L);
}

#define HIGH_RESOLUTION_EXPIRIES 4

typedef VOID (NTAPI *PEX_QUERY_TIMER_RESOLUTION)(PULONG MaximumTime, PULONG MinimumTime, PULONG CurrentTime);

static ULONGLONG HighResolutionTimes[HIGH_RESOLUTION_EXPIRIES];
static volatile LONG HighResolutionCount;

static
VOID
NTAPI
HighResolutionTimerCallback(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    ULONG64 Counter;
    LONG Index = InterlockedIncrement(&HighResolutionCount) - 1;

    UNREFERENCED_PARAMETER(Timer);

    if (Index < HIGH_RESOLUTION_EXPIRIES)
        HighResolutionTimes[Index] = KeQueryInterruptTimePrecise(&Counter);
    if (Index == HIGH_RESOLUTION_EXPIRIES - 1)
        KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
}

/* A high-resolution timer never expires early and keeps its period to the hundred nanoseconds (WDK, High-resolution timers) */
static
VOID
TestHighResolutionTimer(VOID)
{
    const LONGLONG Period = 55000;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"ExQueryTimerResolution");
    PEX_QUERY_TIMER_RESOLUTION QueryTimerResolution;
    ULONG Maximum, Minimum, Before, Armed, After;
    LARGE_INTEGER Timeout;
    ULONGLONG Start;
    ULONG64 Counter;
    PEX_TIMER Timer;
    NTSTATUS Status;
    BOOLEAN Result;
    LONG Index;

    QueryTimerResolution = (PEX_QUERY_TIMER_RESOLUTION)MmGetSystemRoutineAddress(&Name);
    if (skip(QueryTimerResolution != NULL, "ExQueryTimerResolution is not exported\n"))
        return;

    QueryTimerResolution(&Maximum, &Minimum, &Before);

    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    HighResolutionCount = 0;
    Timer = ExAllocateTimer(HighResolutionTimerCallback, &TimerEvent, EX_TIMER_HIGH_RESOLUTION);
    ok(Timer != NULL, "ExAllocateTimer failed\n");
    if (Timer == NULL)
        return;

    Start = KeQueryInterruptTimePrecise(&Counter);
    Result = ExSetTimer(Timer, -Period, Period, NULL);
    ok_eq_bool(Result, FALSE);
    QueryTimerResolution(&Maximum, &Minimum, &Armed);
    trace("resolution %lu before, %lu while a high-resolution timer is set\n", Before, Armed);

    Timeout.QuadPart = -10LL * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Result = ExCancelTimer(Timer, NULL);
    ok_bool_true(Result, "cancel armed periodic");

    for (Index = 0; Index < HIGH_RESOLUTION_EXPIRIES; Index++)
    {
        ok(HighResolutionTimes[Index] >= Start + (ULONGLONG)(Index + 1) * Period,
           "expiry %ld at %I64u, before %I64u\n", Index, HighResolutionTimes[Index] - Start, (ULONGLONG)(Index + 1) * Period);
    }

    Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
    ok_eq_bool(Result, FALSE);

    /* With no high-resolution timer set the clock goes back to the step it ran at */
    QueryTimerResolution(&Maximum, &Minimum, &After);
    ok_eq_ulong(After, Before);
}

/* Fatal contract check, run individually in a disposable guest.
 * Windows 11 stops with TIMER_OR_DPC_INVALID (9, 0, Attributes, 0). */
START_TEST(ExTimerInvalidAttributes)
{
    PEX_TIMER Timer = ExAllocateTimer(NULL, NULL, MAXULONG);

    ok(FALSE, "Invalid timer attributes returned %p instead of bugchecking\n", Timer);
    if (Timer)
        ExDeleteTimer(Timer, TRUE, TRUE, NULL);
}

/* Windows 11 accepts this combination despite the documented restriction.
 * Keep it explicit: older kernels may bugcheck for these attributes. */
START_TEST(ExTimerCombinedAttributes)
{
    static const ULONG Attributes[] = {
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NO_WAKE,
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NO_WAKE | EX_TIMER_NOTIFICATION
    };
    PEX_TIMER Timer;
    BOOLEAN Result;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); ++Index)
    {
        Timer = ExAllocateTimer(NULL, NULL, Attributes[Index]);
        ok(Timer != NULL, "Allocation failed for attributes %lx\n", Attributes[Index]);
        if (Timer)
        {
            Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
            ok_eq_bool(Result, FALSE);
        }
    }
}

START_TEST(ExTimerExtra)
{
    TestOneShotDpc();
    TestPeriodicDpc();
    TestCancelBeforeFire();
    TestExtendedTimer();
    TestDeleteWithoutWaiting();
    TestHighResolutionTimer();
}
