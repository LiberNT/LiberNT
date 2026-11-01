/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a null GS selector loaded in user mode does not outlive a context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define FLAT_DATA_SELECTOR 0x2B

typedef struct _GS_HOLDER
{
    PVOID WaitForSingleObject;
    PVOID SetEvent;
    PVOID TerminateThread;
    HANDLE WaitEvent;
    HANDLE ArmedEvent;
    volatile LONG Release;
    USHORT GsBefore;
    USHORT GsAfter;
    LARGE_INTEGER Timeout;
} GS_HOLDER, *PGS_HOLDER;

C_ASSERT(FIELD_OFFSET(GS_HOLDER, SetEvent) == 0x08);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, TerminateThread) == 0x10);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, WaitEvent) == 0x18);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, ArmedEvent) == 0x20);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, Release) == 0x28);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, GsBefore) == 0x2C);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, GsAfter) == 0x2E);
C_ASSERT(FIELD_OFFSET(GS_HOLDER, Timeout) == 0x30);

typedef struct _GS_OBSERVATION
{
    PGS_HOLDER Holder;
    USHORT Gs;
    USHORT CapturedGs;
    ULONG Processor;
} GS_OBSERVATION, *PGS_OBSERVATION;

USHORT ReadGsSelector(VOID);
DWORD WINAPI NullGsWaitHolder(PVOID Parameter);
DWORD WINAPI NullGsSpinHolder(PVOID Parameter);

static GS_HOLDER Holder;
static GS_OBSERVATION Observation;

static
DWORD
WINAPI
ObserverThread(
    _In_ PVOID Parameter)
{
    PGS_OBSERVATION Observed = Parameter;
    CONTEXT Context;

    Observed->Gs = ReadGsSelector();
    Observed->Processor = GetCurrentProcessorNumber();
    RtlCaptureContext(&Context);
    Observed->CapturedGs = Context.SegGs;
    InterlockedExchange(&Observed->Holder->Release, 1);
    return 0;
}

static
HANDLE
StartOnFirstProcessor(
    _In_ LPTHREAD_START_ROUTINE StartRoutine,
    _In_ PVOID Parameter)
{
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, StartRoutine, Parameter, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return NULL;

    ok(SetThreadAffinityMask(Thread, 1) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ResumeThread(Thread);
    return Thread;
}

static
VOID
JoinThread(
    _In_ PCSTR Name,
    _In_ PCSTR Role,
    _In_ HANDLE Thread)
{
    DWORD Result;

    Result = WaitForSingleObject(Thread, 10000);
    ok(Result == WAIT_OBJECT_0, "%s: %s thread wait returned %lu\n", Name, Role, Result);
    if (Result != WAIT_OBJECT_0)
        TerminateThread(Thread, 1);
    CloseHandle(Thread);
}

static
VOID
TestNullGsHolder(
    _In_ PCSTR Name,
    _In_ LPTHREAD_START_ROUTINE HolderRoutine)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HANDLE HolderThread, Observer = NULL;
    DWORD Result;

    RtlZeroMemory(&Holder, sizeof(Holder));
    Holder.WaitForSingleObject = (PVOID)GetProcAddress(Ntdll, "NtWaitForSingleObject");
    Holder.SetEvent = (PVOID)GetProcAddress(Ntdll, "NtSetEvent");
    Holder.TerminateThread = (PVOID)GetProcAddress(Ntdll, "NtTerminateThread");
    Holder.WaitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Holder.ArmedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Holder.GsBefore = 0xFFFF;
    Holder.GsAfter = 0xFFFF;
    Holder.Timeout.QuadPart = -100000000LL;
    Observation.Holder = &Holder;
    Observation.Gs = 0xFFFF;
    Observation.CapturedGs = 0xFFFF;
    Observation.Processor = MAXDWORD;

    ok(Holder.WaitForSingleObject && Holder.SetEvent && Holder.TerminateThread, "%s: ntdll exports missing\n", Name);
    ok(Holder.WaitEvent && Holder.ArmedEvent, "%s: CreateEventW failed with %lu\n", Name, GetLastError());
    if (!Holder.WaitForSingleObject || !Holder.SetEvent || !Holder.TerminateThread ||
        !Holder.WaitEvent || !Holder.ArmedEvent)
    {
        if (Holder.WaitEvent)
            CloseHandle(Holder.WaitEvent);
        if (Holder.ArmedEvent)
            CloseHandle(Holder.ArmedEvent);
        return;
    }

    HolderThread = StartOnFirstProcessor(HolderRoutine, &Holder);
    if (HolderThread)
    {
        Result = WaitForSingleObject(Holder.ArmedEvent, 5000);
        ok(Result == WAIT_OBJECT_0, "%s: holder armed wait returned %lu\n", Name, Result);
        if (Result == WAIT_OBJECT_0)
            Observer = StartOnFirstProcessor(ObserverThread, &Observation);
        if (Observer)
            JoinThread(Name, "observer", Observer);

        InterlockedExchange(&Holder.Release, 1);
        SetEvent(Holder.WaitEvent);
        JoinThread(Name, "holder", HolderThread);

        ok(Holder.GsBefore == FLAT_DATA_SELECTOR, "%s: holder GS before = %#x\n", Name, Holder.GsBefore);
        ok(Observation.Processor == 0, "%s: observer ran on processor %lu\n", Name, Observation.Processor);
        ok(Observation.Gs == FLAT_DATA_SELECTOR, "%s: observer GS = %#x\n", Name, Observation.Gs);
        ok(Observation.CapturedGs == FLAT_DATA_SELECTOR, "%s: observer captured SegGs = %#x\n", Name, Observation.CapturedGs);
        ok(Holder.GsAfter == FLAT_DATA_SELECTOR, "%s: holder GS after the switch = %#x\n", Name, Holder.GsAfter);
    }

    CloseHandle(Holder.WaitEvent);
    CloseHandle(Holder.ArmedEvent);
}

START_TEST(NullGsSelector)
{
    TestNullGsHolder("wait", NullGsWaitHolder);
    TestNullGsHolder("spin", NullGsSpinHolder);
}
