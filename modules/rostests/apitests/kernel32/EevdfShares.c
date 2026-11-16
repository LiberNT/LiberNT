/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the experimental EEVDF scheduler shares a processor between priorities by weight
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MEASURE_MS 2000

static volatile LONG StopSpinning;

static DWORD WINAPI
SpinnerThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    while (!ReadAcquire(&StopSpinning))
        YieldProcessor();
    return 0;
}

static BOOL
IsEevdfSelected(VOID)
{
    WCHAR Options[512];
    DWORD Size = sizeof(Options) - sizeof(WCHAR);
    DWORD Type;
    HKEY Key;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    Error = RegQueryValueExW(Key, L"SystemStartOptions", NULL, &Type, (LPBYTE)Options, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_SZ)
        return FALSE;

    Options[Size / sizeof(WCHAR)] = UNICODE_NULL;
    _wcsupr(Options);
    return wcsstr(Options, L"SCHEDULER=EEVDF") != NULL;
}

static ULONG64
RunTimeOf(_In_ HANDLE Thread)
{
    FILETIME Creation, Exit, Kernel, User;
    BOOL Success;

    Success = GetThreadTimes(Thread, &Creation, &Exit, &Kernel, &User);
    ok(Success, "GetThreadTimes failed with %lu\n", GetLastError());
    if (!Success)
        return 0;

    return (((ULONG64)Kernel.dwHighDateTime << 32) | Kernel.dwLowDateTime) +
           (((ULONG64)User.dwHighDateTime << 32) | User.dwLowDateTime);
}

START_TEST(EevdfShares)
{
    static const int Priorities[2] = { THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL };
    HANDLE Spinners[2] = { NULL };
    ULONG64 Before[2], Used[2];
    DWORD_PTR ProcessMask, SystemMask;
    ULONG Index, Permille;
    BOOL Success;

    if (!IsEevdfSelected())
    {
        skip("The EEVDF scheduler was not selected at boot\n");
        return;
    }

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;
    Success = SetProcessAffinityMask(GetCurrentProcess(), ProcessMask & (0 - ProcessMask));
    ok(Success, "SetProcessAffinityMask failed with %lu\n", GetLastError());

    for (Index = 0; Index < 2; Index++)
    {
        Spinners[Index] = CreateThread(NULL, 0, SpinnerThread, NULL, CREATE_SUSPENDED, NULL);
        ok(Spinners[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (!Spinners[Index])
            goto Cleanup;

        ok(SetThreadPriority(Spinners[Index], Priorities[Index]), "SetThreadPriority failed with %lu\n", GetLastError());
        ok(SetThreadPriorityBoost(Spinners[Index], TRUE), "SetThreadPriorityBoost failed with %lu\n", GetLastError());
    }

    for (Index = 0; Index < 2; Index++)
        ok(ResumeThread(Spinners[Index]) == 1, "ResumeThread failed with %lu\n", GetLastError());

    Sleep(300);
    for (Index = 0; Index < 2; Index++)
        Before[Index] = RunTimeOf(Spinners[Index]);

    Sleep(MEASURE_MS);
    for (Index = 0; Index < 2; Index++)
        Used[Index] = RunTimeOf(Spinners[Index]) - Before[Index];

    trace("EEVDF_SHARES normal_ms=%lu above_normal_ms=%lu\n", (ULONG)(Used[0] / 10000), (ULONG)(Used[1] / 10000));

    /* Strict priority would give the lower thread nothing; a priority step is a factor of two in weight */
    ok(Used[0] != 0, "The thread of the lower priority did not run\n");
    ok(Used[1] != 0, "The thread of the higher priority did not run\n");
    if (Used[0] != 0 && Used[1] != 0)
    {
        Permille = (ULONG)((Used[1] * 1000) / Used[0]);
        ok(Permille >= 1600 && Permille <= 2500,
           "One priority step shared the processor %lu.%03lu to 1, expected 2 to 1\n",
           Permille / 1000, Permille % 1000);
    }

Cleanup:
    InterlockedExchange(&StopSpinning, 1);
    for (Index = 0; Index < 2; Index++)
    {
        if (!Spinners[Index])
            continue;
        ResumeThread(Spinners[Index]);
        WaitForSingleObject(Spinners[Index], INFINITE);
        CloseHandle(Spinners[Index]);
    }
}
