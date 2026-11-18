/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that concurrent queries of the working set watch return well-formed fault lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define QUERY_THREADS 2
#define RUN_MS 1500
#define FAULT_PAGES 64
#define MAX_RECORDS 8192

static volatile LONG StopWatching;
static volatile LONG Queries, Malformed, Records;

static DWORD WINAPI
FaultingThread(_In_ PVOID Parameter)
{
    volatile UCHAR *Pages;
    SIZE_T Offset;

    UNREFERENCED_PARAMETER(Parameter);
    while (!ReadAcquire(&StopWatching))
    {
        Pages = VirtualAlloc(NULL, FAULT_PAGES * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Pages == NULL)
            break;
        for (Offset = 0; Offset < FAULT_PAGES * 4096; Offset += 4096)
            Pages[Offset] = 1;
        VirtualFree((PVOID)Pages, 0, MEM_RELEASE);
    }

    return 0;
}

static DWORD WINAPI
QueryingThread(_In_ PVOID Parameter)
{
    PPROCESS_WS_WATCH_INFORMATION Buffer;
    ULONG Size = MAX_RECORDS * sizeof(*Buffer), Length, Count;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Parameter);
    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Buffer == NULL)
        return 1;

    while (!ReadAcquire(&StopWatching))
    {
        Length = 0;
        Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessWorkingSetWatch, Buffer, Size, &Length);
        if (!NT_SUCCESS(Status))
            continue;

        /* The list ends with an entry whose program counter is null */
        InterlockedIncrement(&Queries);
        Count = Length / sizeof(*Buffer);
        if (Count == 0 || Length % sizeof(*Buffer) != 0 || Length > Size || Buffer[Count - 1].FaultingPc != NULL)
        {
            InterlockedIncrement(&Malformed);
            continue;
        }

        InterlockedExchangeAdd(&Records, (LONG)(Count - 1));
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return 0;
}

START_TEST(WorkingSetWatch)
{
    HANDLE Threads[QUERY_THREADS + 1] = { NULL };
    NTSTATUS Status;
    ULONG Index;

    Status = NtSetInformationProcess(NtCurrentProcess(), ProcessWorkingSetWatch, NULL, 0);
    ok(Status == STATUS_SUCCESS || Status == STATUS_PORT_ALREADY_SET, "Enabling the watch returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status) && Status != STATUS_PORT_ALREADY_SET)
        return;

    Threads[0] = CreateThread(NULL, 0, FaultingThread, NULL, 0, NULL);
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    for (Index = 1; Index <= QUERY_THREADS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, QueryingThread, NULL, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    Sleep(RUN_MS);
    InterlockedExchange(&StopWatching, 1);

    for (Index = 0; Index <= QUERY_THREADS; Index++)
    {
        if (!Threads[Index])
            continue;
        ok(WaitForSingleObject(Threads[Index], 20000) == WAIT_OBJECT_0, "Thread %lu did not finish\n", Index);
        CloseHandle(Threads[Index]);
    }

    trace("WORKING_SET_WATCH queries=%ld records=%ld malformed=%ld\n", Queries, Records, Malformed);
    ok(Queries != 0, "No query succeeded\n");
    ok(Records != 0, "No fault was reported\n");
    ok(Malformed == 0, "%ld fault lists were malformed\n", Malformed);
}
