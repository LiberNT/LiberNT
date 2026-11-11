/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a process's pool quota limit bounds the objects it creates
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
    if (!NT_SUCCESS(Status))
    {
        skip("Could not give the process its own quota block: 0x%08lx\n", Status);
        return;
    }

    for (Count = 0; Count < MAX_EVENTS; Count++)
    {
        Events[Count] = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Events[Count] == NULL)
        {
            Error = GetLastError();
            break;
        }
    }

    ok(Count < MAX_EVENTS, "%u events fit in %u bytes of non-paged pool quota\n", MAX_EVENTS, LIMIT_HEADROOM);
    ok(Error == ERROR_NOT_ENOUGH_QUOTA, "The event past the limit failed with %lu\n", Error);

    for (Index = 0; Index < Count; Index++)
        CloseHandle(Events[Index]);

    Again = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Again != NULL, "Closing %lu events did not return their quota: %lu\n", Count, GetLastError());
    if (Again != NULL)
        CloseHandle(Again);
}
