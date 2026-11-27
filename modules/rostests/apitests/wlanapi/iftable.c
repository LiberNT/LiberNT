/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface table queries while an adapter is installed and removed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winsock2.h>
#include <iphlpapi.h>
#include <setupapi.h>

#include "vwifi.h"

#define CHURN_CYCLES 12

static volatile LONG StopQueries;
static LONG Queries;
static LONG UnexpectedErrors;

static DWORD WINAPI QueryThread(LPVOID Parameter)
{
    PMIB_IFTABLE Table;
    ULONG Size, i;
    DWORD Error;

    while (!StopQueries)
    {
        Size = 0;
        GetIfTable(NULL, &Size, FALSE);
        Size += 16 * sizeof(MIB_IFROW);
        Table = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Table)
            continue;

        Error = GetIfTable(Table, &Size, FALSE);
        if (Error == NO_ERROR)
        {
            for (i = 0; i < Table->dwNumEntries; i++)
            {
                MIB_IFROW Row = { 0 };

                Row.dwIndex = Table->table[i].dwIndex;
                Error = GetIfEntry(&Row);
                if (Error != NO_ERROR && Error != ERROR_FILE_NOT_FOUND)
                {
                    InterlockedIncrement(&UnexpectedErrors);
                }
                InterlockedIncrement(&Queries);
            }
        }
        else if (Error != ERROR_INSUFFICIENT_BUFFER)
        {
            InterlockedIncrement(&UnexpectedErrors);
        }
        HeapFree(GetProcessHeap(), 0, Table);
    }

    return 0;
}

START_TEST(WlanIfTableChurn)
{
    HDEVINFO Devices;
    SP_DEVINFO_DATA Device;
    HANDLE Thread;
    ULONG Cycle;

    Thread = CreateThread(NULL, 0, QueryThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;

    for (Cycle = 0; Cycle < CHURN_CYCLES; Cycle++)
    {
        if (!InstallVwifi(&Devices, &Device))
            break;
        Sleep(1500);
        RemoveVwifi(Devices, &Device);
        Sleep(500);
    }

    InterlockedExchange(&StopQueries, 1);
    ok(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0, "The query thread did not stop\n");
    CloseHandle(Thread);

    ok(Cycle == CHURN_CYCLES, "Only %lu of %u install/remove cycles ran\n", Cycle, CHURN_CYCLES);
    ok(Queries > 0, "No interface was queried\n");
    ok(UnexpectedErrors == 0, "%ld interface queries failed with an unexpected error\n", UnexpectedErrors);
    trace("%ld interface queries over %lu cycles\n", Queries, Cycle);
}
