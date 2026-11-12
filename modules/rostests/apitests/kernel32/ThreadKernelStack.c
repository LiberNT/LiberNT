/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that threads which ended leave no committed memory behind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define THREAD_BATCH 32
#define LEFT_BEHIND_LIMIT (THREAD_BATCH * 5)
#define SETTLE_STEPS 50
#define SETTLE_STEP_MS 10

static DWORD WINAPI ReturnAtOnce(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 0;
}

static LONGLONG CommittedPages(DWORD PageSize)
{
    MEMORYSTATUSEX Status;

    Status.dwLength = sizeof(Status);
    if (!GlobalMemoryStatusEx(&Status))
        return -1;

    return (LONGLONG)((Status.ullTotalPageFile - Status.ullAvailPageFile) / PageSize);
}

static BOOL RunBatch(void)
{
    HANDLE Threads[THREAD_BATCH];
    DWORD Created = 0;
    DWORD Wait;

    while (Created < THREAD_BATCH)
    {
        Threads[Created] = CreateThread(NULL, 0, ReturnAtOnce, NULL, 0, NULL);
        if (Threads[Created] == NULL)
            break;
        Created++;
    }
    ok(Created == THREAD_BATCH, "CreateThread failed after %lu threads, error %lu\n", Created, GetLastError());
    if (Created == 0)
        return FALSE;

    Wait = WaitForMultipleObjects(Created, Threads, TRUE, 5000);
    ok(Wait == WAIT_OBJECT_0, "The threads did not end: %lu\n", Wait);

    while (Created != 0)
        CloseHandle(Threads[--Created]);

    return (Wait == WAIT_OBJECT_0);
}

START_TEST(ThreadKernelStack)
{
    LONGLONG Before, Left = 0;
    SYSTEM_INFO Info;
    ULONG Step;

    GetSystemInfo(&Info);

    /* The first batch pays for what the process keeps for any later thread */
    if (!RunBatch())
        return;

    Before = CommittedPages(Info.dwPageSize);
    ok(Before > 0, "GlobalMemoryStatusEx failed: %lu\n", GetLastError());
    if (Before <= 0 || !RunBatch())
        return;

    for (Step = 0; Step < SETTLE_STEPS; Step++)
    {
        Left = CommittedPages(Info.dwPageSize) - Before;
        if (Left < LEFT_BEHIND_LIMIT)
            break;
        Sleep(SETTLE_STEP_MS);
    }

    ok(Left < LEFT_BEHIND_LIMIT, "%u ended threads left %I64d committed pages behind\n", THREAD_BATCH, Left);
}
