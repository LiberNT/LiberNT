/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TlsFree empties the released index in every thread, and only that index
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/pstypes.h>

#define CELL_VALUE ((PVOID)(ULONG_PTR)0x5a5a)
#define NEIGHBOUR_VALUE ((PVOID)(ULONG_PTR)0xa5a5)

static HANDLE CellReady;
static HANDLE CellContinue;
static DWORD CellIndex;
static PVOID CellAfter;
static PVOID NeighbourAfter;

static DWORD WINAPI CellWorker(PVOID Parameter)
{
    PVOID *Neighbour = NULL;
    PVOID Saved = NULL;

    UNREFERENCED_PARAMETER(Parameter);

    /* The inline slot numbered like an expansion cell belongs to someone else: it is put back */
    if (CellIndex >= TLS_MINIMUM_AVAILABLE)
    {
        Neighbour = &NtCurrentTeb()->TlsSlots[CellIndex - TLS_MINIMUM_AVAILABLE];
        Saved = *Neighbour;
        *Neighbour = NEIGHBOUR_VALUE;
    }

    TlsSetValue(CellIndex, CELL_VALUE);
    SetEvent(CellReady);
    WaitForSingleObject(CellContinue, 5000);
    CellAfter = TlsGetValue(CellIndex);

    if (Neighbour != NULL)
    {
        NeighbourAfter = *Neighbour;
        *Neighbour = Saved;
    }

    return 0;
}

static VOID ReleaseWhileAnotherThreadHoldsIt(DWORD Index)
{
    HANDLE Thread;

    CellIndex = Index;
    CellAfter = NeighbourAfter = NULL;

    Thread = CreateThread(NULL, 0, CellWorker, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread == NULL) return;

    ok_hex(WaitForSingleObject(CellReady, 5000), WAIT_OBJECT_0);
    ok(TlsFree(Index), "TlsFree(%lu) failed: %lu\n", Index, GetLastError());
    SetEvent(CellContinue);
    ok_hex(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
    CloseHandle(Thread);

    ok(CellAfter == NULL, "TlsFree(%lu) left %p in another thread\n", Index, CellAfter);
}

START_TEST(TlsFree)
{
    DWORD Indexes[TLS_MINIMUM_AVAILABLE + 1];
    DWORD Count = 0, Index;

    CellReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    CellContinue = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(CellReady != NULL && CellContinue != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (CellReady == NULL || CellContinue == NULL) return;

    Index = TlsAlloc();
    ok(Index != TLS_OUT_OF_INDEXES, "TlsAlloc failed: %lu\n", GetLastError());
    if (Index != TLS_OUT_OF_INDEXES) ReleaseWhileAnotherThreadHoldsIt(Index);

    do
    {
        Index = TlsAlloc();
        if (Index == TLS_OUT_OF_INDEXES) break;
        Indexes[Count++] = Index;
    } while (Index < TLS_MINIMUM_AVAILABLE && Count < RTL_NUMBER_OF(Indexes));

    ok(Index != TLS_OUT_OF_INDEXES && Index >= TLS_MINIMUM_AVAILABLE, "No expansion index, last %lu\n", Index);
    if (Index != TLS_OUT_OF_INDEXES && Index >= TLS_MINIMUM_AVAILABLE)
    {
        Count--;
        ReleaseWhileAnotherThreadHoldsIt(Index);
        ok(NeighbourAfter == NEIGHBOUR_VALUE, "TlsFree(%lu) cleared inline slot %lu: %p\n",
           Index, Index - TLS_MINIMUM_AVAILABLE, NeighbourAfter);
    }

    while (Count != 0) TlsFree(Indexes[--Count]);

    CloseHandle(CellContinue);
    CloseHandle(CellReady);
}
