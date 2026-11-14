/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wakeups across processors in every idle and busy state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define WAKE_RING_MAX 8
#define WAKE_ROUNDS 1500

typedef struct _WAKE_RING
{
    HANDLE Events[WAKE_RING_MAX];
    ULONG Members;
    ULONG Rounds;
    volatile LONG TimedOut;
    volatile LONG Passes;
    volatile LONG StopSpinners;
} WAKE_RING, *PWAKE_RING;

typedef struct _WAKE_MEMBER
{
    PWAKE_RING Ring;
    ULONG Index;
} WAKE_MEMBER, *PWAKE_MEMBER;

static VOID
WakeSpinMicroseconds(_In_ ULONG Microseconds)
{
    LARGE_INTEGER Frequency, Start, Now;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    do
    {
        YieldProcessor();
        QueryPerformanceCounter(&Now);
    } while ((ULONGLONG)(Now.QuadPart - Start.QuadPart) * 1000000 < (ULONGLONG)Microseconds * (ULONGLONG)Frequency.QuadPart);
}

static DWORD WINAPI
WakeRingMember(_In_ PVOID Parameter)
{
    PWAKE_MEMBER Member = Parameter;
    PWAKE_RING Ring = Member->Ring;
    ULONG Round;

    for (Round = 0; Round < Ring->Rounds; ++Round)
    {
        if (WaitForSingleObject(Ring->Events[Member->Index], 5000) != WAIT_OBJECT_0)
        {
            InterlockedIncrement(&Ring->TimedOut);
            return 1;
        }
        if (Ring->TimedOut)
            return 1;

        switch (Round % 4)
        {
            case 1:
                WakeSpinMicroseconds(20);
                break;
            case 2:
                WakeSpinMicroseconds(300);
                break;
            default:
                break;
        }

        InterlockedIncrement(&Ring->Passes);
        SetEvent(Ring->Events[(Member->Index + 1) % Ring->Members]);
    }
    return 0;
}

static DWORD WINAPI
WakeSpinner(_In_ PVOID Parameter)
{
    PWAKE_RING Ring = Parameter;

    while (!InterlockedCompareExchange(&Ring->StopSpinners, 0, 0))
        YieldProcessor();
    return 0;
}

static VOID
RunWakeRing(_In_ ULONG Processors, _In_ BOOLEAN BusyTargets)
{
    WAKE_MEMBER Members[WAKE_RING_MAX];
    HANDLE Threads[WAKE_RING_MAX];
    HANDLE Spinners[WAKE_RING_MAX];
    WAKE_RING Ring;
    ULONG i;
    DWORD Wait;

    RtlZeroMemory(&Ring, sizeof(Ring));
    Ring.Members = min(Processors, WAKE_RING_MAX);
    Ring.Rounds = WAKE_ROUNDS;

    for (i = 0; i < Ring.Members; ++i)
    {
        Ring.Events[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(Ring.Events[i] != NULL, "CreateEventW failed with %lu\n", GetLastError());
        Spinners[i] = NULL;
        if (BusyTargets)
        {
            Spinners[i] = CreateThread(NULL, 0, WakeSpinner, &Ring, CREATE_SUSPENDED, NULL);
            ok(Spinners[i] != NULL, "CreateThread failed with %lu\n", GetLastError());
            SetThreadAffinityMask(Spinners[i], (DWORD_PTR)1 << i);
            SetThreadPriority(Spinners[i], THREAD_PRIORITY_IDLE);
            ResumeThread(Spinners[i]);
        }
    }

    for (i = 0; i < Ring.Members; ++i)
    {
        Members[i].Ring = &Ring;
        Members[i].Index = i;
        Threads[i] = CreateThread(NULL, 0, WakeRingMember, &Members[i], CREATE_SUSPENDED, NULL);
        ok(Threads[i] != NULL, "CreateThread failed with %lu\n", GetLastError());
        ok(SetThreadAffinityMask(Threads[i], (DWORD_PTR)1 << i) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
        ResumeThread(Threads[i]);
    }

    SetEvent(Ring.Events[0]);
    Wait = WaitForMultipleObjects(Ring.Members, Threads, TRUE, 120000);
    ok_long(Wait, WAIT_OBJECT_0);
    ok_long(Ring.TimedOut, 0);
    ok_long(Ring.Passes, (LONG)(Ring.Members * Ring.Rounds));

    InterlockedExchange(&Ring.StopSpinners, 1);
    for (i = 0; i < Ring.Members; ++i)
    {
        if (Spinners[i])
        {
            ok_long(WaitForSingleObject(Spinners[i], 10000), WAIT_OBJECT_0);
            CloseHandle(Spinners[i]);
        }
        CloseHandle(Threads[i]);
        CloseHandle(Ring.Events[i]);
    }
}

START_TEST(WakeIdleProcessor)
{
    SYSTEM_INFO Info;

    GetSystemInfo(&Info);
    if (Info.dwNumberOfProcessors < 2)
    {
        skip("Needs at least two processors\n");
        return;
    }

    RunWakeRing(Info.dwNumberOfProcessors, FALSE);
    RunWakeRing(Info.dwNumberOfProcessors, TRUE);
}
