/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test timer expiry, interrupt time and processor times across long idle periods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TICK_100NS 156250ULL
#define TIMER_EARLY_100NS TICK_100NS
#define TIMER_LATE_100NS (100 * 10000ULL)
#define ACCOUNTING_SLACK_100NS (3 * TICK_100NS)
#define LONG_IDLE_MS 1500
#define SPIN_100NS (50 * 10000ULL)
#define MAX_TEST_PROCESSORS 64

typedef VOID (WINAPI *PQUERY_INTERRUPT_TIME)(PULONGLONG);

static LONGLONG QpcFrequency;
static PQUERY_INTERRUPT_TIME pQueryInterruptTime;
static PQUERY_INTERRUPT_TIME pQueryInterruptTimePrecise;

static ULONGLONG
QpcNow100ns(VOID)
{
    LARGE_INTEGER Counter;

    QueryPerformanceCounter(&Counter);
    return (ULONGLONG)((Counter.QuadPart / QpcFrequency) * 10000000LL +
                       ((Counter.QuadPart % QpcFrequency) * 10000000LL) / QpcFrequency);
}

static ULONGLONG
ThreadCharged100ns(VOID)
{
    FILETIME Creation, Exit, Kernel, User;

    if (!GetThreadTimes(GetCurrentThread(), &Creation, &Exit, &Kernel, &User))
    {
        ok(FALSE, "GetThreadTimes failed: %lu\n", GetLastError());
        return 0;
    }
    return (((ULONGLONG)Kernel.dwHighDateTime << 32) | Kernel.dwLowDateTime) +
           (((ULONGLONG)User.dwHighDateTime << 32) | User.dwLowDateTime);
}

static DWORD WINAPI
WakeProcessorThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 0;
}

static VOID
WakeEveryProcessor(_In_ DWORD_PTR ProcessorMask)
{
    HANDLE Thread;
    ULONG Processor;

    for (Processor = 0; Processor < MAX_TEST_PROCESSORS; Processor++)
    {
        if (!(ProcessorMask & ((DWORD_PTR)1 << Processor)))
            continue;
        Thread = CreateThread(NULL, 0, WakeProcessorThread, NULL, CREATE_SUSPENDED, NULL);
        ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (Thread == NULL)
            continue;
        ok(SetThreadAffinityMask(Thread, (DWORD_PTR)1 << Processor) != 0,
           "SetThreadAffinityMask(%lu) failed: %lu\n", Processor, GetLastError());
        ok(ResumeThread(Thread) == 1, "ResumeThread failed: %lu\n", GetLastError());
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "Processor %lu did not run the thread\n", Processor);
        CloseHandle(Thread);
    }
}

static BOOL
QueryProcessorTimes(
    _Out_writes_(Count) PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Information,
    _In_ ULONG Count)
{
    NTSTATUS Status;
    ULONG Length = 0;

    Status = NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                      Information,
                                      Count * sizeof(*Information),
                                      &Length);
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned 0x%lx\n", Status);
    ok(Length == Count * sizeof(*Information), "Length %lu, expected %lu\n", Length, (ULONG)(Count * sizeof(*Information)));
    return NT_SUCCESS(Status) && (Length == Count * sizeof(*Information));
}

static VOID
TestSleepAcrossIdle(_In_ DWORD Milliseconds)
{
    ULONGLONG Start, Elapsed, TickStart, Ticks, Coarse, Precise, ChargedStart, Charged;
    ULONGLONG Requested = Milliseconds * 10000ULL;

    ChargedStart = ThreadCharged100ns();
    TickStart = GetTickCount64();
    Start = QpcNow100ns();
    Sleep(Milliseconds);
    Elapsed = QpcNow100ns() - Start;
    Ticks = GetTickCount64() - TickStart;

    ok(Elapsed + TIMER_EARLY_100NS >= Requested,
       "Sleep(%lu) returned after %I64u us\n", Milliseconds, Elapsed / 10);
    ok(Elapsed <= Requested + TIMER_LATE_100NS,
       "Sleep(%lu) returned after %I64u us\n", Milliseconds, Elapsed / 10);
    ok(Ticks * 10000ULL + 2 * TICK_100NS >= Requested,
       "GetTickCount64 advanced %I64u ms across Sleep(%lu)\n", Ticks, Milliseconds);

    if (pQueryInterruptTime && pQueryInterruptTimePrecise)
    {
        pQueryInterruptTime(&Coarse);
        pQueryInterruptTimePrecise(&Precise);
        ok(Precise >= Coarse, "Precise interrupt time %I64u is behind %I64u\n", Precise, Coarse);
        ok(Precise - Coarse <= 2 * TICK_100NS,
           "Interrupt time is %I64u us stale after Sleep(%lu)\n", (Precise - Coarse) / 10, Milliseconds);
    }

    Start = QpcNow100ns();
    while (QpcNow100ns() - Start < SPIN_100NS)
        YieldProcessor();
    Charged = ThreadCharged100ns() - ChargedStart;
    ok(Charged <= SPIN_100NS + ACCOUNTING_SLACK_100NS,
       "Sleep(%lu) and a %I64u us spin charged the thread %I64u us\n", Milliseconds, SPIN_100NS / 10, Charged / 10);
}

static VOID
TestProcessorTimesAcrossIdle(_In_ DWORD_PTR ProcessorMask, _In_ ULONG Count)
{
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Before[MAX_TEST_PROCESSORS];
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION After[MAX_TEST_PROCESSORS];
    ULONGLONG Start, Wall, Total, Idle, IdleSum = 0;
    ULONG Processor;

    WakeEveryProcessor(ProcessorMask);
    if (!QueryProcessorTimes(Before, Count))
        return;
    Start = QpcNow100ns();
    Sleep(LONG_IDLE_MS);
    WakeEveryProcessor(ProcessorMask);
    Wall = QpcNow100ns() - Start;
    if (!QueryProcessorTimes(After, Count))
        return;

    for (Processor = 0; Processor < Count; Processor++)
    {
        Total = (After[Processor].KernelTime.QuadPart + After[Processor].UserTime.QuadPart) -
                (Before[Processor].KernelTime.QuadPart + Before[Processor].UserTime.QuadPart);
        Idle = After[Processor].IdleTime.QuadPart - Before[Processor].IdleTime.QuadPart;
        ok(Total + ACCOUNTING_SLACK_100NS >= Wall,
           "Processor %lu was charged %I64u us over %I64u us\n", Processor, Total / 10, Wall / 10);
        ok(Total <= Wall + ACCOUNTING_SLACK_100NS,
           "Processor %lu was charged %I64u us over %I64u us\n", Processor, Total / 10, Wall / 10);
        ok(Idle <= Total + TICK_100NS,
           "Processor %lu idle %I64u us exceeds its total %I64u us\n", Processor, Idle / 10, Total / 10);
        IdleSum += Idle;
    }

    ok(IdleSum + ACCOUNTING_SLACK_100NS >= Wall,
       "Only %I64u us idle over %I64u us on %lu processors\n", IdleSum / 10, Wall / 10, Count);
}

START_TEST(IdleClockAccounting)
{
    LARGE_INTEGER Frequency;
    DWORD_PTR ProcessMask, SystemMask;
    SYSTEM_INFO SystemInfo;
    HMODULE Module;

    if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0)
    {
        skip("No performance counter\n");
        return;
    }
    QpcFrequency = Frequency.QuadPart;

    Module = GetModuleHandleW(L"kernelbase.dll");
    if (Module == NULL)
        Module = GetModuleHandleW(L"kernel32.dll");
    pQueryInterruptTime = (PQUERY_INTERRUPT_TIME)GetProcAddress(Module, "QueryInterruptTime");
    pQueryInterruptTimePrecise = (PQUERY_INTERRUPT_TIME)GetProcAddress(Module, "QueryInterruptTimePrecise");
    ok(pQueryInterruptTime != NULL, "QueryInterruptTime is not exported\n");
    ok(pQueryInterruptTimePrecise != NULL, "QueryInterruptTimePrecise is not exported\n");

    TestSleepAcrossIdle(20);
    TestSleepAcrossIdle(300);
    TestSleepAcrossIdle(LONG_IDLE_MS);

    GetSystemInfo(&SystemInfo);
    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask) ||
        (SystemInfo.dwNumberOfProcessors > MAX_TEST_PROCESSORS) ||
        (ProcessMask != SystemMask))
    {
        skip("Cannot reach every processor\n");
        return;
    }
    TestProcessorTimesAcrossIdle(ProcessMask, SystemInfo.dwNumberOfProcessors);
}
