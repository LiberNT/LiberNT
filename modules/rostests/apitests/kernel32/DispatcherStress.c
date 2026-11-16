/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that waits and signals racing on every processor lose no wake and hand no object over twice
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_WORKERS 128
#define SEMAPHORE_COUNT 3
#define RUN_MS 3000
#define JOIN_MS 20000

static HANDLE Mutex, Semaphore, AutoEvent, ManualEvent, NeverSet;
static HANDLE Baton[MAX_WORKERS], Workers[MAX_WORKERS], AbsoluteTimer[MAX_WORKERS];
static ULONG WorkerCount;
static volatile LONG StopWorkers;
static volatile LONG InMutex, SemaphoreHeld;
static volatile LONG MutexEntries, SemaphoreTakes, WaitAllEntries, BatonWaits, ApcsDelivered, Timeouts, Failures;
static volatile LONG AbsoluteExpiries, TimeSets;
static DWORD TimeSetError;

static VOID
Fail(_In_ PCSTR What, _In_ DWORD Value)
{
    if (InterlockedIncrement(&Failures) <= 8)
        trace("DISPATCHER_STRESS failure: %s (%lu)\n", What, Value);
}

static VOID CALLBACK
CountApc(_In_ ULONG_PTR Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedIncrement(&ApcsDelivered);
}

static VOID
EnterMutexSection(VOID)
{
    if (InterlockedIncrement(&InMutex) != 1)
        Fail("two threads own the mutex", 0);
}

static VOID
LeaveMutexSection(VOID)
{
    InterlockedDecrement(&InMutex);
}

static VOID
EnterSemaphoreSection(VOID)
{
    if (InterlockedIncrement(&SemaphoreHeld) > SEMAPHORE_COUNT)
        Fail("more threads hold the semaphore than it counts", 0);
}

static VOID
LeaveSemaphoreSection(VOID)
{
    InterlockedDecrement(&SemaphoreHeld);
}

static DWORD WINAPI
WorkerThread(_In_ PVOID Parameter)
{
    ULONG Self = (ULONG)(ULONG_PTR)Parameter;
    ULONG Seed = 1000 + Self * 7919;
    HANDLE Handles[3];
    DWORD Wait;

    while (!ReadAcquire(&StopWorkers))
    {
        Seed = Seed * 1103515245 + 12345;

        switch ((Seed >> 8) % 8)
        {
            case 0:
                Wait = WaitForSingleObject(Mutex, INFINITE);
                if (Wait != WAIT_OBJECT_0)
                    Fail("mutex wait", Wait);
                EnterMutexSection();
                InterlockedIncrement(&MutexEntries);
                if ((Seed >> 12) % 4 == 0)
                {
                    /* An owner goes through its own mutex without waiting */
                    Wait = WaitForSingleObject(Mutex, 0);
                    if (Wait != WAIT_OBJECT_0)
                        Fail("recursive mutex wait", Wait);
                    ReleaseMutex(Mutex);
                }
                LeaveMutexSection();
                if (!ReleaseMutex(Mutex))
                    Fail("ReleaseMutex", GetLastError());
                break;

            case 1:
                Wait = WaitForSingleObject(Semaphore, INFINITE);
                if (Wait != WAIT_OBJECT_0)
                    Fail("semaphore wait", Wait);
                EnterSemaphoreSection();
                InterlockedIncrement(&SemaphoreTakes);
                LeaveSemaphoreSection();
                if (!ReleaseSemaphore(Semaphore, 1, NULL))
                    Fail("ReleaseSemaphore", GetLastError());
                break;

            case 2:
                /* Both or neither: a wait for all must never be left holding one of the two */
                Handles[0] = Mutex;
                Handles[1] = Semaphore;
                Wait = WaitForMultipleObjects(2, Handles, TRUE, INFINITE);
                if (Wait != WAIT_OBJECT_0)
                    Fail("wait for all", Wait);
                EnterMutexSection();
                EnterSemaphoreSection();
                InterlockedIncrement(&WaitAllEntries);
                LeaveSemaphoreSection();
                LeaveMutexSection();
                if (!ReleaseSemaphore(Semaphore, 1, NULL))
                    Fail("ReleaseSemaphore after wait for all", GetLastError());
                if (!ReleaseMutex(Mutex))
                    Fail("ReleaseMutex after wait for all", GetLastError());
                break;

            case 3:
                /* Wake the next worker, then wait for this one's own baton or a shared event */
                Handles[0] = Baton[Self];
                Handles[1] = AutoEvent;
                Handles[2] = ManualEvent;
                SetEvent(Baton[(Self + 1) % WorkerCount]);
                SetEvent(AutoEvent);
                Wait = WaitForMultipleObjectsEx(3, Handles, FALSE, 50, TRUE);
                if (Wait != WAIT_IO_COMPLETION && Wait != WAIT_TIMEOUT && Wait > WAIT_OBJECT_0 + 2)
                    Fail("wait for any", Wait);
                InterlockedIncrement(&BatonWaits);
                break;

            case 4:
                if ((Seed >> 12) & 1)
                    SetEvent(ManualEvent);
                else
                    ResetEvent(ManualEvent);
                break;

            case 5:
                PulseEvent(AutoEvent);
                SetEvent(Baton[(Seed >> 12) % WorkerCount]);
                break;

            case 6:
                QueueUserAPC(CountApc, Workers[(Seed >> 12) % WorkerCount], 0);
                break;

            default:
                if ((Seed >> 12) & 1)
                {
                    /* Nothing ever sets this event, so only the clock can end the wait */
                    Wait = WaitForSingleObject(NeverSet, 1);
                    if (Wait != WAIT_TIMEOUT)
                        Fail("timed wait", Wait);
                    InterlockedIncrement(&Timeouts);
                }
                else
                {
                    /* An absolute due time is the kind a change of the system time has to move */
                    FILETIME Now;
                    LARGE_INTEGER Due;

                    GetSystemTimeAsFileTime(&Now);
                    Due.LowPart = Now.dwLowDateTime;
                    Due.HighPart = (LONG)Now.dwHighDateTime;
                    Due.QuadPart += 20000;
                    if (!SetWaitableTimer(AbsoluteTimer[Self], &Due, 0, NULL, NULL, FALSE))
                        Fail("SetWaitableTimer", GetLastError());
                    Wait = WaitForSingleObject(AbsoluteTimer[Self], 2000);
                    if (Wait != WAIT_OBJECT_0)
                        Fail("absolute timer", Wait);
                    InterlockedIncrement(&AbsoluteExpiries);
                }
                break;
        }
    }

    return 0;
}

/* Sets the clock to what it already reads, so every armed absolute timer is moved while the workers arm, cancel and expire theirs */
static DWORD WINAPI
ClockThread(_In_ PVOID Parameter)
{
    SYSTEMTIME Now;

    UNREFERENCED_PARAMETER(Parameter);

    while (!ReadAcquire(&StopWorkers))
    {
        GetSystemTime(&Now);
        if (!SetSystemTime(&Now))
        {
            TimeSetError = GetLastError();
            break;
        }
        InterlockedIncrement(&TimeSets);
        Sleep(20);
    }

    return 0;
}

START_TEST(DispatcherStress)
{
    HANDLE Clock;
    SYSTEM_INFO SystemInfo;
    DWORD Start, Wait, Index, Left;
    LONG Previous;

    GetSystemInfo(&SystemInfo);
    WorkerCount = min(max(SystemInfo.dwNumberOfProcessors * 2, 4), MAX_WORKERS);

    Mutex = CreateMutexW(NULL, FALSE, NULL);
    Semaphore = CreateSemaphoreW(NULL, SEMAPHORE_COUNT, SEMAPHORE_COUNT, NULL);
    AutoEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    ManualEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    NeverSet = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Mutex && Semaphore && AutoEvent && ManualEvent && NeverSet, "Creating the objects failed with %lu\n", GetLastError());
    if (!Mutex || !Semaphore || !AutoEvent || !ManualEvent || !NeverSet)
        return;

    for (Index = 0; Index < WorkerCount; Index++)
    {
        Baton[Index] = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(Baton[Index] != NULL, "CreateEventW failed with %lu\n", GetLastError());
        AbsoluteTimer[Index] = CreateWaitableTimerW(NULL, TRUE, NULL);
        ok(AbsoluteTimer[Index] != NULL, "CreateWaitableTimerW failed with %lu\n", GetLastError());
        if (!Baton[Index] || !AbsoluteTimer[Index])
            return;
    }

    for (Index = 0; Index < WorkerCount; Index++)
    {
        Workers[Index] = CreateThread(NULL, 0, WorkerThread, (PVOID)(ULONG_PTR)Index, CREATE_SUSPENDED, NULL);
        ok(Workers[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (!Workers[Index])
        {
            WorkerCount = Index;
            break;
        }
    }

    Clock = CreateThread(NULL, 0, ClockThread, NULL, 0, NULL);
    ok(Clock != NULL, "CreateThread failed with %lu\n", GetLastError());

    for (Index = 0; Index < WorkerCount; Index++)
        ResumeThread(Workers[Index]);

    Sleep(RUN_MS);
    InterlockedExchange(&StopWorkers, 1);

    if (Clock)
    {
        ok(WaitForSingleObject(Clock, JOIN_MS) == WAIT_OBJECT_0, "The clock thread never came back\n");
        CloseHandle(Clock);
    }
    if (TimeSetError == ERROR_PRIVILEGE_NOT_HELD)
        skip("Setting the system time needs SeSystemtimePrivilege\n");
    else
        ok(TimeSets != 0 && TimeSetError == 0, "SetSystemTime failed with %lu after %ld sets\n", TimeSetError, TimeSets);

    /* Every worker's waits end by themselves or by a timeout, so a worker still here later is a lost wake or a stuck lock */
    Start = GetTickCount();
    Left = WorkerCount;
    while (Left != 0 && GetTickCount() - Start < JOIN_MS)
    {
        for (Index = 0; Index < WorkerCount; Index++)
            SetEvent(Baton[Index]);
        SetEvent(ManualEvent);

        Left = 0;
        for (Index = 0; Index < WorkerCount; Index++)
        {
            if (WaitForSingleObject(Workers[Index], 0) != WAIT_OBJECT_0)
                Left++;
        }
        if (Left != 0)
            Sleep(10);
    }
    ok(Left == 0, "%lu of %lu workers never came back from a wait\n", Left, WorkerCount);

    trace("DISPATCHER_STRESS workers=%lu mutex=%ld semaphore=%ld waitall=%ld baton=%ld apcs=%ld timeouts=%ld absolute=%ld timesets=%ld\n",
          WorkerCount, MutexEntries, SemaphoreTakes, WaitAllEntries, BatonWaits, ApcsDelivered, Timeouts, AbsoluteExpiries, TimeSets);

    ok(Failures == 0, "%ld checks failed while the workers ran\n", Failures);
    ok(MutexEntries != 0 && SemaphoreTakes != 0 && WaitAllEntries != 0 && BatonWaits != 0 && Timeouts != 0 && AbsoluteExpiries != 0,
       "A kind of wait never completed\n");

    if (Left == 0)
    {
        /* With everybody gone the objects are back where they started: the mutex free, the semaphore full */
        Wait = WaitForSingleObject(Mutex, 0);
        ok(Wait == WAIT_OBJECT_0, "The mutex was left owned: %lu\n", Wait);
        if (Wait == WAIT_OBJECT_0)
            ReleaseMutex(Mutex);

        for (Index = 0; Index < SEMAPHORE_COUNT; Index++)
        {
            Wait = WaitForSingleObject(Semaphore, 0);
            ok(Wait == WAIT_OBJECT_0, "The semaphore was short of count %lu: %lu\n", Index, Wait);
        }
        Wait = WaitForSingleObject(Semaphore, 0);
        ok(Wait == WAIT_TIMEOUT, "The semaphore counted more than %d: %lu\n", SEMAPHORE_COUNT, Wait);
        ok(ReleaseSemaphore(Semaphore, SEMAPHORE_COUNT, &Previous) && Previous == 0,
           "ReleaseSemaphore failed with %lu, previous count %ld\n", GetLastError(), Previous);
    }

    for (Index = 0; Index < WorkerCount; Index++)
    {
        CloseHandle(Workers[Index]);
        CloseHandle(Baton[Index]);
        CloseHandle(AbsoluteTimer[Index]);
    }
    CloseHandle(NeverSet);
    CloseHandle(ManualEvent);
    CloseHandle(AutoEvent);
    CloseHandle(Semaphore);
    CloseHandle(Mutex);
}
