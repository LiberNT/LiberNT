/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TlsFree empties the released index in every thread, and only that index
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/pstypes.h>
#include <ndk/umfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>

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

static HANDLE ExitArrayFreed;
static HANDLE ExitContinue;
static PVOID *ExitArray;
static PVOID *ExitArrayAfterShutdown;

/* The thread runs the loader's exit work itself, so it can wait between that and its end */
static DWORD WINAPI ExitWorker(PVOID Parameter)
{
    LARGE_INTEGER Timeout;

    UNREFERENCED_PARAMETER(Parameter);

    TlsSetValue(CellIndex, CELL_VALUE);
    ExitArray = NtCurrentTeb()->TlsExpansionSlots;
    LdrShutdownThread();
    ExitArrayAfterShutdown = NtCurrentTeb()->TlsExpansionSlots;

    Timeout.QuadPart = -5000LL * 10000;
    NtSetEvent(ExitArrayFreed, NULL);
    NtWaitForSingleObject(ExitContinue, FALSE, &Timeout);
    NtTerminateThread(NtCurrentThread(), 0);
    return 0;
}

static VOID ReleaseWhileAnotherThreadExits(DWORD Index)
{
    PVOID Guards[32], *Target, Pattern;
    ULONG Count, Taken;
    BOOLEAN Covered = FALSE;
    HANDLE Thread;

    ExitArrayFreed = CreateEventW(NULL, FALSE, FALSE, NULL);
    ExitContinue = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(ExitArrayFreed != NULL && ExitContinue != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (ExitArrayFreed == NULL || ExitContinue == NULL) return;

    CellIndex = Index;
    ExitArray = ExitArrayAfterShutdown = NULL;
    Thread = CreateThread(NULL, 0, ExitWorker, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread == NULL) return;
    ok_hex(WaitForSingleObject(ExitArrayFreed, 5000), WAIT_OBJECT_0);

    ok(ExitArray != NULL, "The exiting thread had no expansion array\n");
    ok(ExitArrayAfterShutdown == NULL || HeapValidate(GetProcessHeap(), 0, ExitArrayAfterShutdown),
       "The exiting thread kept %p, a freed block, as its expansion array\n", ExitArrayAfterShutdown);

    memset(&Pattern, 0xa5, sizeof(Pattern));
    Target = ExitArray + (Index - TLS_MINIMUM_AVAILABLE);
    for (Taken = 0; Taken < RTL_NUMBER_OF(Guards) && !Covered; Taken++)
    {
        PVOID *Guard = HeapAlloc(GetProcessHeap(), 0, TLS_EXPANSION_SLOTS * sizeof(PVOID));

        Guards[Taken] = Guard;
        if (Guard == NULL) continue;
        for (Count = 0; Count < TLS_EXPANSION_SLOTS; Count++) Guard[Count] = Pattern;
        Covered = (Target >= Guard) && (Target < Guard + TLS_EXPANSION_SLOTS);
    }

    ok(TlsFree(Index), "TlsFree(%lu) failed: %lu\n", Index, GetLastError());
    if (Covered)
        ok(*Target == Pattern, "TlsFree(%lu) wrote %p into the expansion array an exiting thread had freed\n",
           Index, *Target);
    else
        trace("the freed expansion array %p was not handed out again\n", ExitArray);

    SetEvent(ExitContinue);
    ok_hex(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
    CloseHandle(Thread);

    for (Count = 0; Count < Taken; Count++) HeapFree(GetProcessHeap(), 0, Guards[Count]);
    CloseHandle(ExitContinue);
    CloseHandle(ExitArrayFreed);
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

        Index = TlsAlloc();
        ok(Index != TLS_OUT_OF_INDEXES && Index >= TLS_MINIMUM_AVAILABLE, "No expansion index, got %lu\n", Index);
        if (Index != TLS_OUT_OF_INDEXES && Index >= TLS_MINIMUM_AVAILABLE) ReleaseWhileAnotherThreadExits(Index);
    }

    while (Count != 0) TlsFree(Indexes[--Count]);

    CloseHandle(CellContinue);
    CloseHandle(CellReady);
}
