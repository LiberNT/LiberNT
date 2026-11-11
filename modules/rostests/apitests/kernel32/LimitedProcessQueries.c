/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process queries documented to work through a limited-information handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(LimitedProcessQueries)
{
    MEMORY_PRIORITY_INFORMATION Priority;
    USHORT Groups[4];
    USHORT Count = ARRAYSIZE(Groups);
    HANDLE Process;

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed %lu\n", GetLastError());
    if (!Process) return;

    ok(GetProcessGroupAffinity(Process, &Count, Groups) && Count >= 1,
       "GetProcessGroupAffinity failed %lu, count %u\n", GetLastError(), Count);

    ZeroMemory(&Priority, sizeof(Priority));
    ok(GetProcessInformation(Process, ProcessMemoryPriority, &Priority, sizeof(Priority)),
       "ProcessMemoryPriority failed %lu\n", GetLastError());
    ok(Priority.MemoryPriority == MEMORY_PRIORITY_NORMAL, "memory priority %lu\n", Priority.MemoryPriority);

    CloseHandle(Process);
}
