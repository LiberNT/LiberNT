/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that pool limits set on a process sharing the default quota block do not bound the objects it creates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

#define LIMIT_HEADROOM (32 * 1024)
#define MAX_EVENTS 4096

static HANDLE Events[MAX_EVENTS];

START_TEST(ObjectQuotaLimit)
{
    POOLED_USAGE_AND_LIMITS Pooled;
    QUOTA_LIMITS Limits;
    BOOLEAN WasEnabled;
    NTSTATUS Status;
    ULONG Count, Index;
    DWORD Error = ERROR_SUCCESS;
    HANDLE Again;

    Status = RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("SeIncreaseQuotaPrivilege is not available: 0x%08lx\n", Status);
        return;
    }

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessPooledUsageAndLimits, &Pooled, sizeof(Pooled), NULL);
    ok(Status == STATUS_SUCCESS, "ProcessPooledUsageAndLimits returned 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    ZeroMemory(&Limits, sizeof(Limits));
    Limits.PagedPoolLimit = 64 * 1024 * 1024;
    Limits.NonPagedPoolLimit = Pooled.NonPagedPoolUsage + LIMIT_HEADROOM;
    Limits.PagefileLimit = (SIZE_T)-1;
    Status = NtSetInformationProcess(NtCurrentProcess(), ProcessQuotaLimits, &Limits, sizeof(Limits));
    ok(Status == STATUS_SUCCESS, "ProcessQuotaLimits returned 0x%08lx\n", Status);

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessPooledUsageAndLimits, &Pooled, sizeof(Pooled), NULL);
    ok(Status == STATUS_SUCCESS, "ProcessPooledUsageAndLimits returned 0x%08lx\n", Status);
    ok(Pooled.NonPagedPoolLimit != Limits.NonPagedPoolLimit, "The shared block took the non-paged limit %Iu\n", (size_t)Limits.NonPagedPoolLimit);
    ok(Pooled.PagedPoolLimit != Limits.PagedPoolLimit, "The shared block took the paged limit %Iu\n", (size_t)Limits.PagedPoolLimit);

    for (Count = 0; Count < MAX_EVENTS; Count++)
    {
        Events[Count] = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Events[Count] == NULL)
        {
            Error = GetLastError();
            break;
        }
    }

    ok(Count == MAX_EVENTS, "Only %lu events were created: %lu\n", Count, Error);

    for (Index = 0; Index < Count; Index++)
        CloseHandle(Events[Index]);

    Again = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Again != NULL, "Closing %lu events did not return their quota: %lu\n", Count, GetLastError());
    if (Again != NULL)
        CloseHandle(Again);
}
