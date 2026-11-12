/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A waitable timer cancelled and closed while its completion routine is being delivered is torn down safely
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define APC_ROUNDS 16

typedef struct _APC_SETTER
{
    HANDLE Go;
    HANDLE Armed;
    HANDLE Done;
    HANDLE Timer;
    volatile LONG Stop;
    LONG Failures;
} APC_SETTER, *PAPC_SETTER;

static LARGE_INTEGER Frequency;
static DWORD_PTR LastProcessor;

static LONGLONG
Microseconds(VOID)
{
    LARGE_INTEGER Now;

    QueryPerformanceCounter(&Now);
    return Now.QuadPart * 1000000 / Frequency.QuadPart;
}

static VOID
SpinMicroseconds(LONG Count)
{
    LONGLONG Start = Microseconds();

    while (Microseconds() - Start < Count)
    {
        YieldProcessor();
    }
}

static VOID
SpinUntilSignaled(HANDLE Timer)
{
    LONGLONG Start = Microseconds();

    while (WaitForSingleObject(Timer, 0) == WAIT_TIMEOUT && Microseconds() - Start < 20000)
    {
        YieldProcessor();
    }
}

static VOID
PinToProcessor(ULONG Round)
{
    SetThreadAffinityMask(GetCurrentThread(), (Round & 1) ? LastProcessor : 1);
}

static VOID CALLBACK
TimerApc(LPVOID Context, DWORD TimerLowValue, DWORD TimerHighValue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(TimerLowValue);
    UNREFERENCED_PARAMETER(TimerHighValue);
}

static DWORD WINAPI
ApcSetter(LPVOID Parameter)
{
    PAPC_SETTER Setter = Parameter;
    LARGE_INTEGER DueTime;

    for (;;)
    {
        WaitForSingleObject(Setter->Go, INFINITE);
        if (Setter->Stop) break;

        DueTime.QuadPart = -3000;
        if (!SetWaitableTimer(Setter->Timer, &DueTime, 0, TimerApc, NULL, FALSE))
            InterlockedIncrement(&Setter->Failures);
        SetEvent(Setter->Armed);
        SleepEx(2, TRUE);
        SetEvent(Setter->Done);
    }

    return 0;
}

static VOID
TestCancelAtApcDelivery(VOID)
{
    APC_SETTER Setter;
    HANDLE Thread;
    LONG Failures = 0;
    ULONG Round;

    ZeroMemory(&Setter, sizeof(Setter));
    Setter.Go = CreateEventW(NULL, FALSE, FALSE, NULL);
    Setter.Armed = CreateEventW(NULL, FALSE, FALSE, NULL);
    Setter.Done = CreateEventW(NULL, FALSE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ApcSetter, &Setter, 0, NULL);
    ok(Setter.Go && Setter.Armed && Setter.Done && Thread, "setup failed %lu\n", GetLastError());
    if (!Setter.Go || !Setter.Armed || !Setter.Done || !Thread) return;

    for (Round = 0; Round < APC_ROUNDS; Round++)
    {
        PinToProcessor(Round);

        Setter.Timer = CreateWaitableTimerW(NULL, TRUE, NULL);
        if (Setter.Timer == NULL)
        {
            Failures++;
            break;
        }

        SetEvent(Setter.Go);
        WaitForSingleObject(Setter.Armed, 1000);
        SpinUntilSignaled(Setter.Timer);
        SpinMicroseconds((Round % 8) * 25);
        CancelWaitableTimer(Setter.Timer);
        if (!CloseHandle(Setter.Timer)) Failures++;
        WaitForSingleObject(Setter.Done, 1000);
    }

    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)-1);
    Setter.Stop = TRUE;
    SetEvent(Setter.Go);
    WaitForSingleObject(Thread, 1000);
    CloseHandle(Thread);
    CloseHandle(Setter.Done);
    CloseHandle(Setter.Armed);
    CloseHandle(Setter.Go);

    ok(Failures == 0 && Setter.Failures == 0, "%ld/%ld timer calls failed\n", Failures, Setter.Failures);
}

START_TEST(WaitableTimerCancelAtApc)
{
    DWORD_PTR ProcessMask, SystemMask;

    QueryPerformanceFrequency(&Frequency);

    LastProcessor = 1;
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        while ((LastProcessor << 1) != 0 && (ProcessMask & (LastProcessor << 1)) != 0)
            LastProcessor <<= 1;
    }

    TestCancelAtApcDelivery();
}
