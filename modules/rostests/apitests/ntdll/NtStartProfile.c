/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test NtCreateProfile, NtStartProfile and NtStopProfile
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define BUCKET_SHIFT 7
#define RANGE_SIZE 1024
#define BUCKET_COUNT (RANGE_SIZE >> BUCKET_SHIFT)
#define TARGET_HITS 40
#define PHASE_LIMIT_MS 2000
#define START_STOP_WINDOW_MS 250

static volatile ULONG Sink[256];
static ULONG Hits[BUCKET_COUNT];
static KAFFINITY SystemAffinityMask;

static
DECLSPEC_NOINLINE
VOID
Spin(
    _In_ ULONG Rounds)
{
    ULONG Round;
    ULONG Index;

    for (Round = 0; Round < Rounds; Round++)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Sink); Index++)
            Sink[Index]++;
    }
}

static
ULONG
TotalHits(VOID)
{
    ULONG Total = 0;
    ULONG Index;

    for (Index = 0; Index < BUCKET_COUNT; Index++)
        Total += ((volatile ULONG *)Hits)[Index];
    return Total;
}

static
NTSTATUS
CreateSpinProfile(
    _Out_ PHANDLE ProfileHandle)
{
    RtlZeroMemory(Hits, sizeof(Hits));
    return NtCreateProfile(ProfileHandle,
                           NtCurrentProcess(),
                           (PVOID)Spin,
                           RANGE_SIZE,
                           BUCKET_SHIFT,
                           Hits,
                           sizeof(Hits),
                           ProfileTime,
                           SystemAffinityMask);
}

static
BOOLEAN
ProfilingSupported(VOID)
{
    HANDLE ProfileHandle;
    NTSTATUS Status;

    Status = CreateSpinProfile(&ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Status = NtStartProfile(ProfileHandle);
    if (NT_SUCCESS(Status))
        ok_hex(NtStopProfile(ProfileHandle), STATUS_SUCCESS);
    else
        ok_hex(Status, STATUS_NOT_SUPPORTED);
    NtClose(ProfileHandle);
    return NT_SUCCESS(Status);
}

static
VOID
TestRepeatedStartStop(VOID)
{
    HANDLE ProfileHandle;
    ULONG Iteration;
    ULONG StartFailures = 0;
    ULONG StopFailures = 0;
    ULONGLONG StartTime;
    NTSTATUS Status;

    Status = CreateSpinProfile(&ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    StartTime = GetTickCount64();
    for (Iteration = 0; Iteration < 20000 && GetTickCount64() - StartTime < START_STOP_WINDOW_MS; Iteration++)
    {
        if (!NT_SUCCESS(NtStartProfile(ProfileHandle)))
            StartFailures++;
        if (!NT_SUCCESS(NtStopProfile(ProfileHandle)))
            StopFailures++;
    }
    ok(Iteration != 0, "no start/stop iteration ran\n");
    ok(StartFailures == 0, "NtStartProfile failed %lu of %lu times\n", StartFailures, Iteration);
    ok(StopFailures == 0, "NtStopProfile failed %lu of %lu times\n", StopFailures, Iteration);

    Status = NtClose(ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestLiveProfile(VOID)
{
    HANDLE ProfileHandle;
    ULONG Chunks = 0;
    ULONG Chunk;
    ULONG FirstHits;
    ULONG SecondHits;
    ULONG StoppedHits;
    ULONGLONG StartTime;
    NTSTATUS Status;

    Status = CreateSpinProfile(&ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = NtStartProfile(ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        NtClose(ProfileHandle);
        return;
    }

    StartTime = GetTickCount64();
    while (TotalHits() < TARGET_HITS && GetTickCount64() - StartTime < PHASE_LIMIT_MS)
    {
        Spin(1000);
        Chunks++;
    }
    FirstHits = TotalHits();
    ok(FirstHits != 0, "the profile buffer was not updated while profiling\n");

    for (Chunk = 0; Chunk < Chunks / 5 + 1; Chunk++)
        Spin(1000);
    SecondHits = TotalHits();
    ok(SecondHits > FirstHits, "a further run added no hits (%lu)\n", FirstHits);

    Status = NtStopProfile(ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
    StoppedHits = TotalHits();
    ok(StoppedHits < 2 * FirstHits, "a run a fifth as long doubled the hits: %lu, then %lu\n", FirstHits, StoppedHits);

    for (Chunk = 0; Chunk < Chunks / 5 + 1; Chunk++)
        Spin(1000);
    ok(TotalHits() == StoppedHits, "hits kept growing after NtStopProfile: %lu, then %lu\n", StoppedHits, TotalHits());
    trace("%lu hits after %lu chunks, %lu after a fifth more\n", FirstHits, Chunks, StoppedHits);

    Status = NtClose(ProfileHandle);
    ok_hex(Status, STATUS_SUCCESS);
}

START_TEST(NtStartProfile)
{
    DWORD_PTR ProcessAffinityMask;
    NTSTATUS Status;

    GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffinityMask, &SystemAffinityMask);

    Status = NtStartProfile(NULL);
    ok_hex(Status, STATUS_INVALID_HANDLE);
    Status = NtStopProfile(NULL);
    ok_hex(Status, STATUS_INVALID_HANDLE);

    if (!ProfilingSupported())
    {
        skip("Time profiling is not supported\n");
        return;
    }
    TestRepeatedStartStop();
    TestLiveProfile();
}
