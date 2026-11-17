/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the CPU share MMCSS leaves to threads outside its tasks
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <ndk/ntndk.h>
#include <avrt.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define MAX_SPINNERS 64

typedef struct _SPINNER
{
    PCWSTR Task;
    ULONG DurationMs;
    ULONG BusyUs;
    ULONG PeriodUs;
    BOOL Yield;
    DWORD_PTR Affinity;
    INT ThreadPriority;
    HANDLE Start;
    LONGLONG HighUs;
    LONGLONG LowUs;
    LONGLONG FirstLowUs;
    LONG LowestPriority;
    LONG HighestLowPriority;
    LONG LowestLowBase;
    LONG HighestLowBase;
    ULONG LowPhases;
    ULONGLONG Iterations;
    ULONGLONG CpuUs;
    HANDLE Pacer;
    volatile LONGLONG *SignalUs;
    LONGLONG MaxWakeUs;
    ULONGLONG WakeUs;
    LONGLONG StallUs;
    LONGLONG MaxStallUs;
    ULONG Periods;
    ULONG Misses;
    LONGLONG BusyWallUs;
} SPINNER;

static HANDLE (WINAPI *pAvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
static BOOL (WINAPI *pAvRevertMmThreadCharacteristics)(HANDLE);
static LARGE_INTEGER Frequency;
static ULONG CpuCount;

static LONGLONG
NowUs(VOID)
{
    LARGE_INTEGER Now;

    QueryPerformanceCounter(&Now);
    return Now.QuadPart * 1000000 / Frequency.QuadPart;
}

static LONG
CurrentPriority(PLONG BasePriority)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status;

    Status = NtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &Info, sizeof(Info), NULL);
    *BasePriority = NT_SUCCESS(Status) ? Info.BasePriority : -1;
    return NT_SUCCESS(Status) ? Info.Priority : -1;
}

static ULONGLONG
ThreadCpuUs(HANDLE Thread)
{
    FILETIME Create, Exit, Kernel, User;

    if (!GetThreadTimes(Thread, &Create, &Exit, &Kernel, &User))
        return 0;
    return (((ULONGLONG)Kernel.dwHighDateTime << 32 | Kernel.dwLowDateTime) +
            ((ULONGLONG)User.dwHighDateTime << 32 | User.dwLowDateTime)) / 10;
}

static DWORD WINAPI
SpinnerThread(PVOID Parameter)
{
    SPINNER *Spinner = Parameter;
    LONGLONG Start, Now, Last, BusyStart, Wake;
    LARGE_INTEGER Due;
    HANDLE Handle = NULL, Timer;
    DWORD Index = 0;
    LONG Priority, Base, Previous = -1;

    Timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (Timer == NULL)
        Timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    ok(Timer != NULL, "no timer: %lu\n", GetLastError());
    Spinner->FirstLowUs = -1;
    Spinner->LowestPriority = MAXLONG;
    Spinner->HighestLowPriority = 0;
    Spinner->LowestLowBase = MAXLONG;
    Spinner->HighestLowBase = 0;
    if (Spinner->Affinity != 0)
        ok(SetThreadAffinityMask(GetCurrentThread(), Spinner->Affinity) != 0, "affinity not set: %lu\n", GetLastError());
    if (Spinner->ThreadPriority != THREAD_PRIORITY_NORMAL)
        ok(SetThreadPriority(GetCurrentThread(), Spinner->ThreadPriority), "priority not set: %lu\n", GetLastError());
    if (Spinner->Task != NULL)
    {
        Handle = pAvSetMmThreadCharacteristicsW(Spinner->Task, &Index);
        ok(Handle != NULL, "%ls: not registered: %lu\n", Spinner->Task, GetLastError());
    }
    WaitForSingleObject(Spinner->Start, INFINITE);
    Start = Last = BusyStart = NowUs();
    do
    {
        Now = NowUs();
        Priority = CurrentPriority(&Base);
        Spinner->Iterations++;
        if (Spinner->Pacer != NULL && Now - Last > 50)
        {
            Spinner->StallUs += Now - Last;
            Spinner->MaxStallUs = max(Spinner->MaxStallUs, Now - Last);
        }
        if (Handle != NULL)
        {
            if (Priority < LOW_REALTIME_PRIORITY)
            {
                if (Previous >= LOW_REALTIME_PRIORITY)
                    Spinner->LowPhases++;
                if (Spinner->FirstLowUs < 0)
                    Spinner->FirstLowUs = Now - Start;
                Spinner->LowUs += Now - Last;
                Spinner->LowestPriority = min(Spinner->LowestPriority, Priority);
                Spinner->HighestLowPriority = max(Spinner->HighestLowPriority, Priority);
                Spinner->LowestLowBase = min(Spinner->LowestLowBase, Base);
                Spinner->HighestLowBase = max(Spinner->HighestLowBase, Base);
            }
            else if (Spinner->FirstLowUs >= 0)
            {
                Spinner->HighUs += Now - Last;
            }
        }
        Previous = Priority;
        Last = Now;
        if (Spinner->Yield)
            SwitchToThread();
        if (Spinner->Pacer != NULL && Now - BusyStart >= Spinner->BusyUs)
        {
            Spinner->Periods++;
            Spinner->BusyWallUs += Now - BusyStart;
            if (WaitForSingleObject(Spinner->Pacer, 0) == WAIT_OBJECT_0)
            {
                Spinner->Misses++;
                BusyStart = Last = NowUs();
            }
            else
            {
                WaitForSingleObject(Spinner->Pacer, INFINITE);
                BusyStart = Last = NowUs();
                Wake = BusyStart - *Spinner->SignalUs;
                Spinner->WakeUs += Wake;
                Spinner->MaxWakeUs = max(Spinner->MaxWakeUs, Wake);
            }
        }
        else if (Spinner->PeriodUs != 0 && Now - BusyStart >= Spinner->BusyUs && Timer != NULL)
        {
            Due.QuadPart = -(LONGLONG)(Spinner->PeriodUs - (Now - BusyStart)) * 10;
            SetWaitableTimer(Timer, &Due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(Timer, INFINITE);
            BusyStart = Last = NowUs();
        }
    } while (Now - Start < (LONGLONG)Spinner->DurationMs * 1000);
    Spinner->CpuUs = ThreadCpuUs(GetCurrentThread());
    if (Handle)
        ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    if (Timer)
        CloseHandle(Timer);
    return 0;
}

static VOID
RunSpinners(SPINNER *Spinners, ULONG Count)
{
    HANDLE Threads[MAX_SPINNERS];
    HANDLE Start;
    ULONG i;

    ok(Count <= MAX_SPINNERS, "%lu spinners\n", Count);
    Count = min(Count, MAX_SPINNERS);
    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < Count; i++)
    {
        Spinners[i].Start = Start;
        Threads[i] = CreateThread(NULL, 0, SpinnerThread, &Spinners[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    Sleep(50);
    SetEvent(Start);
    for (i = 0; i < Count; i++)
    {
        if (Threads[i])
        {
            ok_long(WaitForSingleObject(Threads[i], 30000), WAIT_OBJECT_0);
            CloseHandle(Threads[i]);
        }
    }
    CloseHandle(Start);
}

static VOID
TestBusyThread(PCWSTR Task)
{
    SPINNER Spinner = { Task, 1500 };
    LONGLONG Share;

    RunSpinners(&Spinner, 1);
    ok(Spinner.FirstLowUs >= 0, "%ls: a thread busy for 1.5 s was never lowered\n", Task);
    if (Spinner.FirstLowUs < 0)
        return;
    ok(Spinner.LowestPriority >= 1 && Spinner.HighestLowPriority <= 7,
       "%ls: lowered to %ld..%ld\n", Task, Spinner.LowestPriority, Spinner.HighestLowPriority);
    Share = Spinner.LowUs * 100 / max(Spinner.LowUs + Spinner.HighUs, 1);
    ok(Spinner.LowPhases >= 20, "%ls: lowered %lu times in 1.5 s\n", Task, Spinner.LowPhases);
    ok(Share <= 35, "%ls: %I64d%% of the time lowered\n", Task, Share);
    trace("%ls: first lowered at %I64d us to %ld..%ld, base %ld..%ld, %I64d%% of the time after, %lu phases, %I64d us lowered, %I64d us raised\n",
          Task, Spinner.FirstLowUs, Spinner.LowestPriority, Spinner.HighestLowPriority,
          Spinner.LowestLowBase, Spinner.HighestLowBase, Share,
          Spinner.LowPhases, Spinner.LowUs, Spinner.HighUs);
}

static VOID
TestYieldingThread(VOID)
{
    SPINNER Spinners[2];

    ZeroMemory(Spinners, sizeof(Spinners));
    Spinners[0].Task = L"Pro Audio";
    Spinners[0].DurationMs = 1500;
    Spinners[0].Yield = TRUE;
    Spinners[0].Affinity = 1;
    Spinners[1].DurationMs = 1500;
    Spinners[1].Affinity = 1;
    Spinners[1].ThreadPriority = THREAD_PRIORITY_IDLE;
    RunSpinners(Spinners, 2);
    trace("yielding: first lowered at %I64d us to %ld..%ld, %lu phases, %I64u iterations; idle-priority thread ran %I64u iterations\n",
          Spinners[0].FirstLowUs, Spinners[0].LowestPriority, Spinners[0].HighestLowPriority, Spinners[0].LowPhases,
          Spinners[0].Iterations, Spinners[1].Iterations);
    ok(Spinners[0].Iterations >= 10000, "a registered thread yielding to an idle-priority thread on its processor ran %I64u iterations\n",
       Spinners[0].Iterations);
}

static VOID
TestHalfBusyThread(VOID)
{
    SPINNER Spinner = { L"Pro Audio", 1500, 5000, 10000 };

    RunSpinners(&Spinner, 1);
    ok(Spinner.FirstLowUs < 0, "a thread busy 5 ms of every 10 ms was lowered at %I64d us to %ld\n",
       Spinner.FirstLowUs, Spinner.LowestPriority);
}

typedef struct _PACER
{
    HANDLE Event;
    ULONG PeriodUs;
    volatile LONGLONG SignalUs;
    volatile LONG Stop;
    LONGLONG MaxLateUs;
} PACER;

static DWORD WINAPI
PacerThread(PVOID Parameter)
{
    PACER *Pacer = Parameter;
    LONGLONG Next = NowUs() + Pacer->PeriodUs, Now;

    ok(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL), "priority not set: %lu\n", GetLastError());
    while (!Pacer->Stop)
    {
        Now = NowUs();
        if (Now >= Next)
        {
            Pacer->MaxLateUs = max(Pacer->MaxLateUs, Now - Next);
            Pacer->SignalUs = Now;
            SetEvent(Pacer->Event);
            Next += Pacer->PeriodUs;
        }
    }
    return 0;
}

static VOID
TestWaitingBusyThread(ULONG BusyUs, BOOL Contended)
{
    SPINNER Spinners[MAX_SPINNERS];
    PACER Pacer = { NULL, 1000 };
    HANDLE Thread;
    ULONG i, Count = Contended ? min(CpuCount - 1, MAX_SPINNERS - 1) : 0;
    LONGLONG Duty;

    Pacer.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Pacer.Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (Pacer.Event == NULL)
        return;
    Thread = CreateThread(NULL, 0, PacerThread, &Pacer, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread == NULL)
    {
        CloseHandle(Pacer.Event);
        return;
    }
    ZeroMemory(Spinners, sizeof(Spinners));
    Spinners[0].Task = L"Pro Audio";
    Spinners[0].DurationMs = 1000;
    Spinners[0].BusyUs = BusyUs;
    Spinners[0].Pacer = Pacer.Event;
    Spinners[0].SignalUs = &Pacer.SignalUs;
    for (i = 1; i <= Count; i++)
        Spinners[i].DurationMs = 1000;
    RunSpinners(Spinners, Count + 1);
    Pacer.Stop = TRUE;
    ok_long(WaitForSingleObject(Thread, 30000), WAIT_OBJECT_0);
    CloseHandle(Thread);
    CloseHandle(Pacer.Event);

    Duty = Spinners[0].BusyWallUs * 100 / 1000000;
    trace("busy %lu of every 1000 us%s: %lu periods, %lu missed, %I64d us off the processor while busy, %I64d us at most; "
          "woken %I64u us after the signal on average, %I64d us at most, signalled up to %I64d us late; first lowered at %I64d us to %ld\n",
          BusyUs, Contended ? " beside busy threads" : "", Spinners[0].Periods, Spinners[0].Misses, Spinners[0].StallUs, Spinners[0].MaxStallUs,
          Spinners[0].WakeUs / max(Spinners[0].Periods - Spinners[0].Misses, 1), Spinners[0].MaxWakeUs, Pacer.MaxLateUs,
          Spinners[0].FirstLowUs, Spinners[0].LowestPriority);
    if (!Contended)
    {
        ok(Spinners[0].Periods >= 900, "busy %lu of every 1000 us: %lu periods in 1 s\n", BusyUs, Spinners[0].Periods);
        ok(Duty * 1000 >= BusyUs * 90, "busy %lu of every 1000 us: busy %I64d%% of the time\n", BusyUs, Duty);
        if (Spinners[0].FirstLowUs >= 0)
            trace("busy %lu of every 1000 us: lowered with no other thread ready, which Windows 11 does not do\n", BusyUs);
    }
    else if (BusyUs >= 850)
    {
        ok(Spinners[0].FirstLowUs >= 0 && Spinners[0].LowestPriority >= 1 && Spinners[0].HighestLowPriority <= 7,
           "busy %lu of every 1000 us beside busy threads: first lowered at %I64d us to %ld..%ld\n",
           BusyUs, Spinners[0].FirstLowUs, Spinners[0].LowestPriority, Spinners[0].HighestLowPriority);
    }
    else
    {
        ok(Spinners[0].FirstLowUs < 0, "busy %lu of every 1000 us beside busy threads: lowered at %I64d us to %ld\n",
           BusyUs, Spinners[0].FirstLowUs, Spinners[0].LowestPriority);
    }
}

typedef struct _RESET_WHILE_LOWERED
{
    INT ThreadPriority;
    INT ReportedPriority;
    LONG LoweredPriority;
    LONG LowestPriority;
    LONG HighestPriority;
    LONG RevertedBase;
} RESET_WHILE_LOWERED;

static DWORD WINAPI
ResetWhileLoweredThread(PVOID Parameter)
{
    RESET_WHILE_LOWERED *Reset = Parameter;
    LONGLONG Start;
    HANDLE Handle;
    DWORD Index = 0;
    LONG Priority, Base;

    Reset->LoweredPriority = -1;
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle != NULL, "not registered: %lu\n", GetLastError());
    if (Handle == NULL)
        return 0;

    Start = NowUs();
    do
    {
        Priority = CurrentPriority(&Base);
    } while (Priority >= LOW_REALTIME_PRIORITY && NowUs() - Start < 3000000);
    if (Priority < LOW_REALTIME_PRIORITY)
    {
        Reset->LoweredPriority = Priority;
        ok(SetThreadPriority(GetCurrentThread(), Reset->ThreadPriority), "priority not set: %lu\n", GetLastError());
        Reset->ReportedPriority = GetThreadPriority(GetCurrentThread());
        Reset->LowestPriority = MAXLONG;
        Reset->HighestPriority = 0;
        Start = NowUs();
        do
        {
            Priority = CurrentPriority(&Base);
            Reset->LowestPriority = min(Reset->LowestPriority, Priority);
            Reset->HighestPriority = max(Reset->HighestPriority, Priority);
        } while (NowUs() - Start < 200000);
    }
    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    CurrentPriority(&Reset->RevertedBase);
    return 0;
}

static VOID
TestResetWhileLowered(INT ThreadPriority)
{
    RESET_WHILE_LOWERED Reset = { ThreadPriority };
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, ResetWhileLoweredThread, &Reset, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread == NULL)
        return;
    ok_long(WaitForSingleObject(Thread, 30000), WAIT_OBJECT_0);
    CloseHandle(Thread);
    ok(Reset.LoweredPriority >= 1 && Reset.LoweredPriority < LOW_REALTIME_PRIORITY, "%d: lowered to %ld\n", ThreadPriority, Reset.LoweredPriority);
    if (Reset.LoweredPriority < 0)
        return;
    ok_int(Reset.ReportedPriority, ThreadPriority);
    ok(Reset.HighestPriority == 24, "%d: raised to %ld after the reset\n", ThreadPriority, Reset.HighestPriority);
    ok(Reset.LowestPriority < LOW_REALTIME_PRIORITY, "%d: never lowered after the reset, %ld at the lowest\n", ThreadPriority, Reset.LowestPriority);
    ok_long(Reset.RevertedBase, 0);
}

static VOID
TestContention(PCWSTR Task)
{
    SPINNER Spinners[MAX_SPINNERS];
    ULONGLONG Registered = 0, Normal = 0, Total, RegisteredCpu = 0, NormalCpu = 0;
    ULONG i, Count = min(CpuCount, MAX_SPINNERS / 2);

    ZeroMemory(Spinners, sizeof(Spinners));
    for (i = 0; i < 2 * Count; i++)
    {
        Spinners[i].Task = (i < Count) ? Task : NULL;
        Spinners[i].DurationMs = 2000;
    }
    RunSpinners(Spinners, 2 * Count);
    for (i = 0; i < 2 * Count; i++)
    {
        if (i < Count)
        {
            Registered += Spinners[i].Iterations;
            RegisteredCpu += Spinners[i].CpuUs;
        }
        else
        {
            Normal += Spinners[i].Iterations;
            NormalCpu += Spinners[i].CpuUs;
        }
    }
    Total = max(Registered + Normal, 1);
    ok(Normal * 100 / Total >= 10, "%ls: normal threads ran %I64u of %I64u iterations\n", Task, Normal, Total);
    ok(Registered * 100 / Total >= 50, "%ls: registered threads ran %I64u of %I64u iterations\n", Task, Registered, Total);
    trace("%ls: %lu registered and %lu normal threads busy: %I64u%% of the iterations to the normal ones, cpu %I64u/%I64u us\n",
          Task, Count, Count, Normal * 100 / Total, NormalCpu, RegisteredCpu);
}

typedef struct _MANY_SPINNER
{
    HANDLE Start;
    volatile LONG *Stop;
    BOOL Register;
    BOOL Registered;
    BOOL Idle;
    DWORD_PTR Affinity;
    ULONGLONG Iterations;
    UCHAR Padding[64];
} MANY_SPINNER;

static DWORD WINAPI
ManySpinnerThread(PVOID Parameter)
{
    MANY_SPINNER *Spinner = Parameter;
    HANDLE Handle = NULL;
    DWORD Index = 0;

    if (Spinner->Register)
    {
        Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
        Spinner->Registered = (Handle != NULL);
    }
    if (Spinner->Affinity != 0)
        ok(SetThreadAffinityMask(GetCurrentThread(), Spinner->Affinity) != 0, "affinity not set: %lu\n", GetLastError());
    WaitForSingleObject(Spinner->Start, INFINITE);
    while (!*Spinner->Stop)
    {
        if (Spinner->Idle)
            Sleep(10);
        else
            Spinner->Iterations++;
    }
    if (Handle != NULL)
        pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static LONGLONG
SystemProcessKernelTime(VOID)
{
    PSYSTEM_PROCESS_INFORMATION Process;
    ULONG Length = 1024 * 1024;
    LONGLONG Time = -1;
    PVOID Buffer;
    NTSTATUS Status;

    for (;;)
    {
        Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
        if (Buffer == NULL)
            return -1;
        Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Length, NULL);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        HeapFree(GetProcessHeap(), 0, Buffer);
        Length *= 2;
    }
    for (Process = Buffer; NT_SUCCESS(Status); Process = (PVOID)((PUCHAR)Process + Process->NextEntryOffset))
    {
        if ((ULONG_PTR)Process->UniqueProcessId == 4)
        {
            Time = Process->KernelTime.QuadPart;
            break;
        }
        if (Process->NextEntryOffset == 0)
            break;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Time;
}

static VOID
TestManyRegistrations(ULONG Count)
{
    MANY_SPINNER *Spinners;
    HANDLE *Threads;
    HANDLE Start;
    volatile LONG Stop = FALSE;
    ULONGLONG Registered = 0, Normal = 0, Total;
    ULONG i, Created = 0, Live = 0, All = Count + CpuCount;
    LONGLONG RegisterUs, KernelBefore, KernelAfter;

    Spinners = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, All * sizeof(*Spinners));
    Threads = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, All * sizeof(*Threads));
    ok(Spinners != NULL && Threads != NULL, "no memory\n");
    if (Spinners == NULL || Threads == NULL)
        goto Done;
    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    RegisterUs = NowUs();
    for (i = 0; i < All; i++)
    {
        Spinners[i].Start = Start;
        Spinners[i].Stop = &Stop;
        Spinners[i].Register = (i < Count);
        Threads[i] = CreateThread(NULL, 64 * 1024, ManySpinnerThread, &Spinners[i], STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (Threads[i] != NULL)
            Created++;
    }
    ok_long(Created, All);
    Sleep(500);
    RegisterUs = NowUs() - RegisterUs;
    KernelBefore = SystemProcessKernelTime();
    SetEvent(Start);
    Sleep(1000);
    Stop = TRUE;
    KernelAfter = SystemProcessKernelTime();
    for (i = 0; i < All; i++)
    {
        if (Threads[i] == NULL)
            continue;
        ok_long(WaitForSingleObject(Threads[i], 30000), WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
        if (Spinners[i].Registered)
        {
            Live++;
            Registered += Spinners[i].Iterations;
        }
        else
        {
            Normal += Spinners[i].Iterations;
        }
    }
    CloseHandle(Start);
    Total = max(Registered + Normal, 1);
    trace("%lu threads asked to register, %lu did, beside %lu normal threads: %I64u%% of the iterations to the threads left normal, "
          "System process kernel time %I64d us in 1 s, %I64d us to start them\n",
          Count, Live, All - Live, Normal * 100 / Total, (KernelAfter - KernelBefore) / 10, RegisterUs);
    if (Live <= 32)
        ok(Normal * 100 / Total >= 10, "%lu registered: normal threads ran %I64u of %I64u iterations\n", Live, Normal, Total);
    else
        ok(Normal != 0, "%lu registered: normal threads never ran\n", Live);
Done:
    HeapFree(GetProcessHeap(), 0, Threads);
    HeapFree(GetProcessHeap(), 0, Spinners);
}

static VOID
TestNarrowedRegistrations(ULONG Busy)
{
    MANY_SPINNER Spinners[4];
    HANDLE Threads[4];
    HANDLE Start;
    volatile LONG Stop = FALSE;
    ULONGLONG Registered = 0, Total;
    ULONG i;

    ZeroMemory(Spinners, sizeof(Spinners));
    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < ARRAYSIZE(Spinners); i++)
    {
        Spinners[i].Start = Start;
        Spinners[i].Stop = &Stop;
        Spinners[i].Register = (i < 3);
        Spinners[i].Idle = (i >= Busy && i < 3);
        Spinners[i].Affinity = 1;
        Threads[i] = CreateThread(NULL, 0, ManySpinnerThread, &Spinners[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    Sleep(50);
    SetEvent(Start);
    Sleep(1000);
    Stop = TRUE;
    for (i = 0; i < ARRAYSIZE(Spinners); i++)
    {
        if (Threads[i] == NULL)
            continue;
        ok_long(WaitForSingleObject(Threads[i], 30000), WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
        if (i < 3)
        {
            ok(Spinners[i].Registered, "thread %lu not registered\n", i);
            Registered += Spinners[i].Iterations;
        }
    }
    CloseHandle(Start);
    Total = max(Registered + Spinners[3].Iterations, 1);
    trace("%lu busy and %lu idle registered threads moved to processor 0 beside a normal one: %I64u%% of the iterations to the normal one\n",
          Busy, 3 - Busy, Spinners[3].Iterations * 100 / Total);
    ok(Spinners[3].Iterations * 100 / Total >= 10, "%lu busy: normal thread ran %I64u of %I64u iterations\n", Busy, Spinners[3].Iterations, Total);
    ok(Registered * 100 / Total >= 50, "%lu busy: registered threads ran %I64u of %I64u iterations\n", Busy, Registered, Total);
}

static BOOL
InitAvrt(VOID)
{
    HMODULE Avrt = LoadLibraryW(L"avrt.dll");
    SYSTEM_INFO Info;

    ok(Avrt != NULL, "avrt.dll not loaded: %lu\n", GetLastError());
    if (!Avrt)
        return FALSE;
    pAvSetMmThreadCharacteristicsW = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadCharacteristicsW");
    pAvRevertMmThreadCharacteristics = (PVOID)GetProcAddress(Avrt, "AvRevertMmThreadCharacteristics");
    if (!pAvSetMmThreadCharacteristicsW || !pAvRevertMmThreadCharacteristics)
    {
        ok(0, "avrt export missing\n");
        return FALSE;
    }
    QueryPerformanceFrequency(&Frequency);
    GetSystemInfo(&Info);
    CpuCount = Info.dwNumberOfProcessors;
    return TRUE;
}

START_TEST(SystemResponsiveness)
{
    if (!InitAvrt())
        return;

    TestBusyThread(L"Pro Audio");
    TestBusyThread(L"Audio");
    TestBusyThread(L"Games");
    TestYieldingThread();
    TestHalfBusyThread();
    TestResetWhileLowered(THREAD_PRIORITY_LOWEST);
    TestResetWhileLowered(THREAD_PRIORITY_IDLE);
    TestResetWhileLowered(THREAD_PRIORITY_NORMAL);
    TestContention(L"Pro Audio");
    TestContention(L"Games");
}

START_TEST(BusyWaitingThread)
{
    if (!InitAvrt())
        return;

    TestWaitingBusyThread(800, FALSE);
    TestWaitingBusyThread(900, FALSE);
    TestWaitingBusyThread(950, FALSE);
    TestWaitingBusyThread(500, TRUE);
    TestWaitingBusyThread(850, TRUE);
    TestWaitingBusyThread(900, TRUE);
    TestWaitingBusyThread(950, TRUE);
}

START_TEST(ManyRegistrations)
{
    if (!InitAvrt())
        return;

    TestManyRegistrations(32);
    TestManyRegistrations(1024);
    TestNarrowedRegistrations(1);
    TestNarrowedRegistrations(2);
}
