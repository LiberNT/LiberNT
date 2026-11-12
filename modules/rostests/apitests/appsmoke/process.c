/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests process start, environment and DLL directory inheritance and termination in an address wait
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

#define PROBE_DLL L"apsmkdir.dll"
#define CHILD_COUNT 16
#define CHILD_BUDGET_MS 5000
#define FAIRNESS_WINDOW_MS 500

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *RTL_WAIT_ON_ADDRESS)(const void *, const void *, SIZE_T, const LARGE_INTEGER *);
typedef void (NTAPI *RTL_WAKE_ADDRESS)(const void *);

NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW);

static RTL_WAIT_ON_ADDRESS pRtlWaitOnAddress;
static RTL_WAKE_ADDRESS pRtlWakeAddressSingle;
static volatile LONG WaitValue;
static volatile LONG WaiterState;
static volatile LONG ApcCount;

typedef BOOL (WINAPI *QUERY_THREAD_CYCLE_TIME)(HANDLE, PULONG64);
typedef BOOL (WINAPI *QUERY_IDLE_PROCESSOR_CYCLE_TIME)(PULONG, PULONG64);

static QUERY_THREAD_CYCLE_TIME pQueryThreadCycleTime;
static QUERY_IDLE_PROCESSOR_CYCLE_TIME pQueryIdleProcessorCycleTime;
static volatile LONG StopSpinning;

static DWORD WINAPI ComputeSpinner(void *Context)
{
    volatile ULONG Value = 0;

    while (!StopSpinning)
        Value++;

    return 0;
}

static DWORD WINAPI YieldSpinner(void *Context)
{
    while (!StopSpinning)
        Sleep(0);

    return 0;
}

static ULONGLONG ThreadCycles(HANDLE Thread)
{
    ULONG64 Cycles = 0;

    if (!pQueryThreadCycleTime(Thread, &Cycles))
        return 0;

    return Cycles;
}

static ULONGLONG IdleCycles(void)
{
    ULONG64 Times[MAXIMUM_WAIT_OBJECTS];
    ULONG Length = sizeof(Times), Index;
    ULONGLONG Total = 0;

    if (!pQueryIdleProcessorCycleTime(&Length, Times))
        return 0;

    for (Index = 0; Index < Length / sizeof(Times[0]); Index++)
        Total += Times[Index];

    return Total;
}

START_TEST(yield_fairness)
{
    HANDLE Threads[MAXIMUM_WAIT_OBJECTS];
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    SYSTEM_INFO Info;
    ULONGLONG BusyCycles = 0, YieldCycles, IdleBefore, Idle;
    DWORD Count, Index;

    pQueryThreadCycleTime = (QUERY_THREAD_CYCLE_TIME)GetProcAddress(Kernel32, "QueryThreadCycleTime");
    pQueryIdleProcessorCycleTime = (QUERY_IDLE_PROCESSOR_CYCLE_TIME)GetProcAddress(Kernel32, "QueryIdleProcessorCycleTime");
    if (!pQueryThreadCycleTime || !pQueryIdleProcessorCycleTime)
    {
        skip("Cycle times are not available\n");
        return;
    }

    GetSystemInfo(&Info);
    Count = min(Info.dwNumberOfProcessors, MAXIMUM_WAIT_OBJECTS - 1);
    StopSpinning = 0;
    IdleBefore = IdleCycles();
    for (Index = 0; Index < Count; Index++)
        Threads[Index] = CreateThread(NULL, 0, ComputeSpinner, NULL, 0, NULL);
    Threads[Count] = CreateThread(NULL, 0, YieldSpinner, NULL, 0, NULL);

    Sleep(FAIRNESS_WINDOW_MS);
    Idle = IdleCycles() - IdleBefore;
    StopSpinning = 1;
    ok(WaitForMultipleObjects(Count + 1, Threads, TRUE, 10000) == WAIT_OBJECT_0, "The spinners did not stop\n");

    for (Index = 0; Index < Count; Index++)
        BusyCycles += ThreadCycles(Threads[Index]);
    YieldCycles = ThreadCycles(Threads[Count]);
    for (Index = 0; Index <= Count; Index++)
        CloseHandle(Threads[Index]);

    ok(YieldCycles < BusyCycles / Count / 4, "A yielding thread used %I64u cycles beside %lu busy threads that used %I64u\n",
       YieldCycles, Count, BusyCycles);
    ok(Idle < BusyCycles / 3, "The processors idled %I64u cycles while %lu busy threads used %I64u\n",
       Idle, Count, BusyCycles);
}

START_TEST(os_version)
{
    RTL_OSVERSIONINFOEXW Unsized, Sized;
    NTSTATUS Status;

    memset(&Unsized, 0, sizeof(Unsized));
    Status = RtlGetVersion((PRTL_OSVERSIONINFOW)&Unsized);
    ok(Status == STATUS_SUCCESS, "RtlGetVersion without a size returned %#lx\n", Status);

    memset(&Sized, 0, sizeof(Sized));
    Sized.dwOSVersionInfoSize = sizeof(Sized);
    Status = RtlGetVersion((PRTL_OSVERSIONINFOW)&Sized);
    ok(Status == STATUS_SUCCESS, "RtlGetVersion returned %#lx\n", Status);
    ok(Sized.dwMajorVersion >= 10 && Sized.dwBuildNumber != 0, "The version is %lu.%lu.%lu\n",
       Sized.dwMajorVersion, Sized.dwMinorVersion, Sized.dwBuildNumber);
    ok(Unsized.dwMajorVersion == Sized.dwMajorVersion && Unsized.dwMinorVersion == Sized.dwMinorVersion &&
       Unsized.dwBuildNumber == Sized.dwBuildNumber, "The version without a size is %lu.%lu.%lu\n",
       Unsized.dwMajorVersion, Unsized.dwMinorVersion, Unsized.dwBuildNumber);
    ok(Unsized.wProductType == 0, "The product type was written without a size\n");
}

START_TEST(dll_directory)
{
    WCHAR Temp[MAX_PATH], Directory[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH];
    DWORD ExitCode, Elapsed;
    HMODULE Module;
    BOOL InTime;

    if (AppSmokeIsRole("inherit"))
    {
        Module = LoadLibraryW(PROBE_DLL);
        ok(Module != NULL, "The child did not find the DLL in the inherited directory: %lu\n", GetLastError());
        if (Module)
            FreeLibrary(Module);
        return;
    }

    if (AppSmokeIsRole("default"))
    {
        Module = LoadLibraryW(PROBE_DLL);
        ok(Module == NULL, "The child found the DLL without a DLL directory\n");
        if (Module)
            FreeLibrary(Module);
        return;
    }

    Module = LoadLibraryW(L"version.dll");
    if (!Module || !GetModuleFileNameW(Module, Source, ARRAYSIZE(Source)) ||
        !GetTempPathW(ARRAYSIZE(Temp), Temp))
    {
        skip("No source DLL or temporary directory\n");
        return;
    }

    _snwprintf(Directory, ARRAYSIZE(Directory) - 1, L"%lsapsmk%lu", Temp, GetCurrentProcessId());
    Directory[ARRAYSIZE(Directory) - 1] = 0;
    _snwprintf(Target, ARRAYSIZE(Target) - 1, L"%ls\\" PROBE_DLL, Directory);
    Target[ARRAYSIZE(Target) - 1] = 0;
    if (!CreateDirectoryW(Directory, NULL) || !CopyFileW(Source, Target, FALSE))
    {
        skip("Cannot stage the probe DLL in %ls: %lu\n", Directory, GetLastError());
        RemoveDirectoryW(Directory);
        return;
    }

    Module = LoadLibraryW(PROBE_DLL);
    ok(Module == NULL, "The probe DLL loads before SetDllDirectory\n");
    if (Module)
        FreeLibrary(Module);

    ok(SetDllDirectoryW(Directory), "SetDllDirectory: %lu\n", GetLastError());
    Module = LoadLibraryW(PROBE_DLL);
    ok(Module != NULL, "The probe DLL does not load from the DLL directory: %lu\n", GetLastError());
    if (Module)
        FreeLibrary(Module);

    InTime = AppSmokeRunSelf("dll_directory", "inherit", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "Child with an inherited DLL directory: exit code %#lx after %lu ms\n", ExitCode, Elapsed);

    ok(SetDllDirectoryW(NULL), "SetDllDirectory(NULL): %lu\n", GetLastError());
    InTime = AppSmokeRunSelf("dll_directory", "default", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "Child without a DLL directory: exit code %#lx after %lu ms\n", ExitCode, Elapsed);

    DeleteFileW(Target);
    RemoveDirectoryW(Directory);
}

START_TEST(child_start)
{
    DWORD ExitCode, Elapsed, FirstCode = 0, Slowest = 0;
    UINT Index, Failed = 0;
    BOOL InTime;

    if (AppSmokeIsRole("child"))
    {
        ok(GetModuleHandleW(L"kernel32.dll") != NULL, "kernel32 is not loaded\n");
        ok(GetDesktopWindow() != NULL, "No desktop window\n");
        return;
    }

    for (Index = 0; Index < CHILD_COUNT && Failed < 3; ++Index)
    {
        InTime = AppSmokeRunSelf("child_start", "child", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
        if (!InTime || ExitCode)
        {
            if (!Failed)
                FirstCode = ExitCode;
            ++Failed;
        }
        if (Elapsed > Slowest)
            Slowest = Elapsed;
    }

    ok(Failed == 0, "%u of %u children failed to start; first exit code %#lx\n", Failed, Index, FirstCode);
    trace("Slowest of %u children: %lu ms\n", Index, Slowest);
}

START_TEST(environment)
{
    USHORT ProcessMachine = IMAGE_FILE_MACHINE_UNKNOWN, NativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    const WCHAR *Native = NULL;
    WCHAR Value[64] = L"";
    DWORD ExitCode, Elapsed, Length;
    BOOL InTime;

    if (AppSmokeIsRole("child"))
    {
        Length = GetEnvironmentVariableW(L"APPSMOKE_INHERITED", Value, ARRAYSIZE(Value));
        ok(Length == 3 && !wcscmp(Value, L"yes"), "The child got %lu characters of its parent's variable\n", Length);
        return;
    }

    ok(IsWow64Process2(GetCurrentProcess(), &ProcessMachine, &NativeMachine), "IsWow64Process2: %lu\n", GetLastError());
    if (NativeMachine == IMAGE_FILE_MACHINE_AMD64)
        Native = L"AMD64";
    else if (NativeMachine == IMAGE_FILE_MACHINE_ARM64)
        Native = L"ARM64";

    /* WOW64 Implementation Details: a 32-bit process sees x86, and the native architecture in PROCESSOR_ARCHITEW6432 */
    if (ProcessMachine == IMAGE_FILE_MACHINE_I386 && Native != NULL)
    {
        Length = GetEnvironmentVariableW(L"PROCESSOR_ARCHITECTURE", Value, ARRAYSIZE(Value));
        ok(Length != 0 && !wcscmp(Value, L"x86"), "PROCESSOR_ARCHITECTURE is %ls\n", Length ? Value : L"not set");
        Length = GetEnvironmentVariableW(L"PROCESSOR_ARCHITEW6432", Value, ARRAYSIZE(Value));
        ok(Length != 0 && !wcscmp(Value, Native), "PROCESSOR_ARCHITEW6432 is %ls\n", Length ? Value : L"not set");
    }
    else if (sizeof(void *) == 8)
    {
        Length = GetEnvironmentVariableW(L"PROCESSOR_ARCHITEW6432", Value, ARRAYSIZE(Value));
        ok(Length == 0, "A 64-bit process has PROCESSOR_ARCHITEW6432 %ls\n", Value);
    }

    ok(SetEnvironmentVariableW(L"APPSMOKE_INHERITED", L"yes"), "SetEnvironmentVariable: %lu\n", GetLastError());
    InTime = AppSmokeRunSelf("environment", "child", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "Child of the environment: exit code %#lx after %lu ms\n", ExitCode, Elapsed);
    SetEnvironmentVariableW(L"APPSMOKE_INHERITED", NULL);
}

static DWORD WINAPI BlockedWaiter(void *Context)
{
    LONG Compare = 0;

    InterlockedExchange(&WaiterState, 1);
    while (WaitValue == Compare)
        pRtlWaitOnAddress((const void *)&WaitValue, &Compare, sizeof(WaitValue), NULL);
    InterlockedExchange(&WaiterState, 2);
    return 0;
}

static DWORD WINAPI PollingWaiter(void *Context)
{
    LARGE_INTEGER Timeout;
    LONG Compare = 0;

    Timeout.QuadPart = -10000;
    InterlockedExchange(&WaiterState, 1);
    for (;;)
        pRtlWaitOnAddress((const void *)&WaitValue, &Compare, sizeof(WaitValue), &Timeout);
    return 0;
}

static void CALLBACK CountApc(ULONG_PTR Context)
{
    InterlockedIncrement(&ApcCount);
}

static HANDLE StartWaiter(LPTHREAD_START_ROUTINE Routine)
{
    HANDLE Thread;
    UINT Spin;

    InterlockedExchange(&WaitValue, 0);
    InterlockedExchange(&WaiterState, 0);
    Thread = CreateThread(NULL, 0, Routine, NULL, 0, NULL);
    for (Spin = 0; Thread && WaiterState != 1 && Spin < 500; ++Spin)
        Sleep(1);
    Sleep(30);
    return Thread;
}

START_TEST(alert_wait)
{
    DWORD ExitCode, Elapsed, Start, Wait;
    LARGE_INTEGER Timeout;
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    NTSTATUS Status;
    LONG Compare = 0;
    HANDLE Thread;
    BOOL InTime;

    pRtlWaitOnAddress = (RTL_WAIT_ON_ADDRESS)GetProcAddress(Ntdll, "RtlWaitOnAddress");
    pRtlWakeAddressSingle = (RTL_WAKE_ADDRESS)GetProcAddress(Ntdll, "RtlWakeAddressSingle");
    if (!pRtlWaitOnAddress || !pRtlWakeAddressSingle)
    {
        skip("RtlWaitOnAddress is unavailable\n");
        return;
    }

    if (AppSmokeIsRole("exit"))
    {
        Thread = StartWaiter(BlockedWaiter);
        ok(Thread != NULL && WaiterState == 1, "The waiter did not start\n");
        return;
    }

    if (AppSmokeIsRole("terminate"))
    {
        Thread = StartWaiter(BlockedWaiter);
        ok(Thread != NULL, "The waiter did not start\n");
        if (Thread)
        {
            ok(TerminateThread(Thread, 7), "TerminateThread: %lu\n", GetLastError());
            Wait = WaitForSingleObject(Thread, 5000);
            ExitCode = 0;
            GetExitCodeThread(Thread, &ExitCode);
            ok(Wait == WAIT_OBJECT_0 && ExitCode == 7, "A blocked waiter did not terminate: wait %lu exit code %lu\n", Wait, ExitCode);
            CloseHandle(Thread);
        }
        return;
    }

    if (AppSmokeIsRole("terminate_polling"))
    {
        Thread = StartWaiter(PollingWaiter);
        ok(Thread != NULL, "The waiter did not start\n");
        if (Thread)
        {
            ok(TerminateThread(Thread, 9), "TerminateThread: %lu\n", GetLastError());
            Wait = WaitForSingleObject(Thread, 5000);
            ExitCode = 0;
            GetExitCodeThread(Thread, &ExitCode);
            ok(Wait == WAIT_OBJECT_0 && ExitCode == 9, "A polling waiter did not terminate: wait %lu exit code %lu\n", Wait, ExitCode);
            CloseHandle(Thread);
        }
        return;
    }

    Thread = StartWaiter(BlockedWaiter);
    ok(Thread != NULL && WaiterState == 1, "The waiter did not start\n");
    if (Thread)
    {
        InterlockedExchange(&WaitValue, 1);
        pRtlWakeAddressSingle((const void *)&WaitValue);
        Wait = WaitForSingleObject(Thread, 5000);
        ok(Wait == WAIT_OBJECT_0 && WaiterState == 2, "A woken waiter did not return: wait %lu state %ld\n", Wait, WaiterState);
        if (Wait != WAIT_OBJECT_0)
            TerminateThread(Thread, 1);
        CloseHandle(Thread);
    }

    InTime = AppSmokeRunSelf("alert_wait", "terminate", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "Terminating a blocked waiter failed: exit code %#lx after %lu ms\n", ExitCode, Elapsed);

    InTime = AppSmokeRunSelf("alert_wait", "terminate_polling", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "Terminating a polling waiter failed: exit code %#lx after %lu ms\n", ExitCode, Elapsed);

    InterlockedExchange(&WaitValue, 0);
    InterlockedExchange(&ApcCount, 0);
    ok(QueueUserAPC(CountApc, GetCurrentThread(), 0), "QueueUserAPC: %lu\n", GetLastError());
    Timeout.QuadPart = -100 * 10000;
    Start = GetTickCount();
    Status = pRtlWaitOnAddress((const void *)&WaitValue, &Compare, sizeof(WaitValue), &Timeout);
    Elapsed = GetTickCount() - Start;
    ok(Status == STATUS_TIMEOUT, "An address wait with a queued APC returned %#lx\n", (ULONG)Status);
    ok(Elapsed >= 70, "An address wait with a queued APC lasted %lu ms of 100\n", Elapsed);
    ok(ApcCount == 0, "The address wait delivered %ld APCs\n", ApcCount);
    Wait = SleepEx(0, TRUE);
    ok(Wait == WAIT_IO_COMPLETION && ApcCount == 1, "The queued APC was not delivered afterwards: %lu, %ld\n", Wait, ApcCount);

    InTime = AppSmokeRunSelf("alert_wait", "exit", CHILD_BUDGET_MS, &ExitCode, &Elapsed);
    ok(InTime && !ExitCode, "A process with a blocked waiter did not exit: exit code %#lx after %lu ms\n", ExitCode, Elapsed);
}
