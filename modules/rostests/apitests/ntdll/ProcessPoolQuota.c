/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that object pool quota charged from several threads is returned exactly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193.001@gmail.com>
 */

#include <apitest.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

#define QUOTA_THREADS           4
#define QUOTA_EVENTS            256
#define PRIVATE_BLOCK_USAGE     (1024 * 1024)

typedef struct _QUOTA_WORKER
{
    HANDLE Start;
    HANDLE Close;
    HANDLE Events[QUOTA_EVENTS];
    volatile LONG *Created;
    volatile LONG *Closed;
    BOOL Failed;
} QUOTA_WORKER;

static DWORD WINAPI
QuotaWorkerThread(_In_ PVOID Parameter)
{
    QUOTA_WORKER *Worker = Parameter;
    ULONG Index;

    WaitForSingleObject(Worker->Start, 30000);
    for (Index = 0; Index < QUOTA_EVENTS; Index++)
    {
        Worker->Events[Index] = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Worker->Events[Index] == NULL)
            Worker->Failed = TRUE;
    }
    InterlockedIncrement(Worker->Created);

    WaitForSingleObject(Worker->Close, 30000);
    for (Index = 0; Index < QUOTA_EVENTS; Index++)
    {
        if (Worker->Events[Index] != NULL)
            CloseHandle(Worker->Events[Index]);
    }
    InterlockedIncrement(Worker->Closed);
    return 0;
}

static BOOL
QueryPooledUsage(_Out_ POOLED_USAGE_AND_LIMITS *Usage)
{
    NTSTATUS Status;

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessPooledUsageAndLimits, Usage, sizeof(*Usage), NULL);
    ok(Status == STATUS_SUCCESS, "ProcessPooledUsageAndLimits returned 0x%08lx\n", Status);
    return NT_SUCCESS(Status);
}

static BOOL
WaitForCount(_In_ volatile LONG *Count, _In_ LONG Expected)
{
    DWORD Start = GetTickCount();

    while (InterlockedCompareExchange(Count, 0, 0) != Expected)
    {
        if ((GetTickCount() - Start) > 30000)
            return FALSE;
        Sleep(1);
    }

    return TRUE;
}

START_TEST(ProcessPoolQuota)
{
    QUOTA_WORKER Workers[QUOTA_THREADS];
    HANDLE Threads[QUOTA_THREADS];
    POOLED_USAGE_AND_LIMITS Baseline, Held, After;
    QUOTA_LIMITS Limits;
    volatile LONG Created = 0, Closed = 0;
    HANDLE Start, Close;
    BOOLEAN WasEnabled;
    NTSTATUS Status;
    ULONG Index;
    HANDLE Warm;

    Status = RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("SeIncreaseQuotaPrivilege is not available: 0x%08lx\n", Status);
        return;
    }

    ZeroMemory(&Limits, sizeof(Limits));
    Limits.PagedPoolLimit = 16 * 1024 * 1024;
    Limits.NonPagedPoolLimit = 16 * 1024 * 1024;
    Limits.PagefileLimit = (SIZE_T)-1;
    Status = NtSetInformationProcess(NtCurrentProcess(), ProcessQuotaLimits, &Limits, sizeof(Limits));
    if (!NT_SUCCESS(Status))
    {
        skip("Could not give the process its own quota block: 0x%08lx\n", Status);
        return;
    }

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    Close = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Start != NULL && Close != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Start == NULL || Close == NULL)
        return;

    for (Index = 0; Index < QUOTA_THREADS * QUOTA_EVENTS; Index++)
    {
        Warm = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Warm == NULL)
            break;
        Workers[Index % QUOTA_THREADS].Events[Index / QUOTA_THREADS] = Warm;
    }
    for (Index = 0; Index < QUOTA_THREADS * QUOTA_EVENTS; Index++)
        CloseHandle(Workers[Index % QUOTA_THREADS].Events[Index / QUOTA_THREADS]);

    for (Index = 0; Index < QUOTA_THREADS; Index++)
    {
        ZeroMemory(&Workers[Index], sizeof(Workers[Index]));
        Workers[Index].Start = Start;
        Workers[Index].Close = Close;
        Workers[Index].Created = &Created;
        Workers[Index].Closed = &Closed;
        Threads[Index] = CreateThread(NULL, 0, QuotaWorkerThread, &Workers[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Threads[Index] == NULL)
            return;
    }

    if (!QueryPooledUsage(&Baseline))
        return;

    SetEvent(Start);
    ok(WaitForCount(&Created, QUOTA_THREADS), "Workers did not create their events\n");
    if (QueryPooledUsage(&Held))
    {
        ok(Held.NonPagedPoolUsage + Held.PagedPoolUsage > Baseline.NonPagedPoolUsage + Baseline.PagedPoolUsage,
           "Creating %u events charged no pool quota\n", QUOTA_THREADS * QUOTA_EVENTS);
        ok(Held.PeakNonPagedPoolUsage >= Held.NonPagedPoolUsage, "Non-paged peak %Iu below usage %Iu\n",
           (size_t)Held.PeakNonPagedPoolUsage, (size_t)Held.NonPagedPoolUsage);
        ok(Held.PeakPagedPoolUsage >= Held.PagedPoolUsage, "Paged peak %Iu below usage %Iu\n",
           (size_t)Held.PeakPagedPoolUsage, (size_t)Held.PagedPoolUsage);
    }

    SetEvent(Close);
    ok(WaitForCount(&Closed, QUOTA_THREADS), "Workers did not close their events\n");
    if (QueryPooledUsage(&After))
    {
        ok(After.NonPagedPoolUsage + After.PagedPoolUsage < Held.NonPagedPoolUsage + Held.PagedPoolUsage,
           "Closing the events returned no pool quota\n");
        if (Baseline.NonPagedPoolUsage + Baseline.PagedPoolUsage < PRIVATE_BLOCK_USAGE)
        {
            ok(After.NonPagedPoolUsage == Baseline.NonPagedPoolUsage,
               "Non-paged quota usage %Iu after closing, baseline %Iu\n",
               (size_t)After.NonPagedPoolUsage, (size_t)Baseline.NonPagedPoolUsage);
            ok(After.PagedPoolUsage == Baseline.PagedPoolUsage,
               "Paged quota usage %Iu after closing, baseline %Iu\n",
               (size_t)After.PagedPoolUsage, (size_t)Baseline.PagedPoolUsage);
        }
        else
        {
            trace("The quota block is shared (usage %Iu), exact balance not checked\n",
                  (size_t)(Baseline.NonPagedPoolUsage + Baseline.PagedPoolUsage));
        }
    }

    ok(WaitForMultipleObjects(QUOTA_THREADS, Threads, TRUE, 30000) == WAIT_OBJECT_0, "Workers did not exit\n");
    for (Index = 0; Index < QUOTA_THREADS; Index++)
    {
        ok(!Workers[Index].Failed, "Worker %lu could not create all its events\n", Index);
        CloseHandle(Threads[Index]);
    }
    CloseHandle(Close);
    CloseHandle(Start);
}
