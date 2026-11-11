/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread's selected CPU sets restrict where it runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/psfuncs.h>

#define CPU_SET_ID(Index) (0x100 + (Index))
#define SAMPLES 20

typedef struct _WAKER
{
    HANDLE Ask;
    HANDLE Wake;
} WAKER, *PWAKER;

static
DWORD
WINAPI
WakerThread(
    _In_ LPVOID Parameter)
{
    PWAKER Waker = Parameter;
    ULONG Round;

    for (Round = 0; Round < SAMPLES; Round++)
    {
        if (WaitForSingleObject(Waker->Ask, 1000) != WAIT_OBJECT_0)
            break;

        SetEvent(Waker->Wake);
    }

    return 0;
}

static
VOID
CheckSelection(
    _In_ ULONG Expected,
    _In_reads_(Expected) const ULONG *Ids)
{
    ULONG Read[8];
    ULONG ReturnLength = 0xdeadbeef;
    ULONG Index;
    NTSTATUS Status;

    RtlFillMemory(Read, sizeof(Read), 0x55);
    Status = NtQueryInformationThread(GetCurrentThread(), ThreadSelectedCpuSets, Read, sizeof(Read), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, Expected * (ULONG)sizeof(ULONG));

    for (Index = 0; (Index < Expected) && (ReturnLength == Expected * sizeof(ULONG)); Index++)
        ok(Read[Index] == Ids[Index], "id %lu: 0x%lx, expected 0x%lx\n", Index, Read[Index], Ids[Index]);
}

START_TEST(PsThreadCpuSets)
{
    SYSTEM_INFO Info;
    ULONG Single;
    ULONG Pair[2];
    ULONG Last;
    ULONG Elsewhere = 0;
    ULONG Round;
    WAKER Waker;
    HANDLE Thread;
    DWORD Wait;
    NTSTATUS Status;

    GetSystemInfo(&Info);
    if (skip(Info.dwNumberOfProcessors >= 2, "needs two processors\n"))
        return;

    Last = Info.dwNumberOfProcessors - 1;

    Single = CPU_SET_ID(Last);
    Status = NtSetInformationThread(GetCurrentThread(), ThreadSelectedCpuSets, &Single, sizeof(Single));
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckSelection(1, &Single);

    if (NT_SUCCESS(Status))
    {
        Waker.Ask = CreateEventW(NULL, FALSE, FALSE, NULL);
        Waker.Wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        Thread = CreateThread(NULL, 0, WakerThread, &Waker, 0, NULL);
        ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());

        for (Round = 0; (Thread != NULL) && (Round < SAMPLES); Round++)
        {
            Wait = SignalObjectAndWait(Waker.Ask, Waker.Wake, 1000, FALSE);
            ok_eq_ulong(Wait, WAIT_OBJECT_0);
            if (GetCurrentProcessorNumber() != Last) Elsewhere++;
        }

        ok(Elsewhere == 0, "ran %lu of %u times off processor %lu\n", Elsewhere, SAMPLES, Last);

        if (Thread != NULL)
        {
            Wait = WaitForSingleObject(Thread, 1000);
            ok_eq_ulong(Wait, WAIT_OBJECT_0);
            CloseHandle(Thread);
        }
        CloseHandle(Waker.Wake);
        CloseHandle(Waker.Ask);
    }

    Pair[0] = CPU_SET_ID(0);
    Pair[1] = CPU_SET_ID(Last);
    Status = NtSetInformationThread(GetCurrentThread(), ThreadSelectedCpuSets, Pair, sizeof(Pair));
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckSelection(2, Pair);

    Status = NtSetInformationThread(GetCurrentThread(), ThreadSelectedCpuSets, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckSelection(0, NULL);
}
