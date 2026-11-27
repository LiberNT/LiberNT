/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for Wait Chain Traversal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wct.h>

static HANDLE Mutex, Owned, Release, CallbackDone;
static DWORD CallbackError = 0xdead, CallbackCount = 0xdead;
static DWORD_PTR CallbackContext;

static DWORD WINAPI OwnerThread(LPVOID Parameter)
{
    WaitForSingleObject(Mutex, INFINITE);
    SetEvent(Owned);
    WaitForSingleObject(Release, 5000);
    ReleaseMutex(Mutex);
    return 0;
}

static DWORD WINAPI WaiterThread(LPVOID Parameter)
{
    if (WaitForSingleObject(Mutex, 5000) == WAIT_OBJECT_0)
        ReleaseMutex(Mutex);
    return 0;
}

static VOID CALLBACK WaitChainCallback(HWCT Session, DWORD_PTR Context, DWORD Error, LPDWORD NodeCount,
                                       PWAITCHAIN_NODE_INFO Nodes, LPBOOL IsCycle)
{
    CallbackContext = Context;
    CallbackError = Error;
    CallbackCount = NodeCount ? *NodeCount : 0xdead;
    SetEvent(CallbackDone);
}

static BOOL IsThreadBlocked(DWORD ThreadId)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count = WCT_MAX_NODE_COUNT;
    BOOL Cycle;
    HWCT Session = OpenThreadWaitChainSession(0, NULL);
    BOOL Blocked;

    Blocked = Session && GetThreadWaitChain(Session, 0, 0, ThreadId, &Count, Nodes, &Cycle) &&
              Nodes[0].ObjectStatus == WctStatusBlocked;
    if (Session) CloseThreadWaitChainSession(Session);
    return Blocked;
}

START_TEST(WaitChain)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    HANDLE Owner = NULL, Waiter = NULL;
    DWORD Count, OwnerId = 0, WaiterId = 0, i;
    HWCT Session, Other;
    BOOL Cycle, Result;

    SetLastError(0xdeadbeef);
    Other = OpenThreadWaitChainSession(WCT_ASYNC_OPEN_FLAG, NULL);
    ok(Other == NULL && GetLastError() == ERROR_INVALID_PARAMETER, "async without callback: %p, %lu\n", Other, GetLastError());
    SetLastError(0xdeadbeef);
    Other = OpenThreadWaitChainSession(2, NULL);
    ok(Other == NULL && GetLastError() == ERROR_INVALID_PARAMETER, "flags 2: %p, %lu\n", Other, GetLastError());
    Other = OpenThreadWaitChainSession(0, WaitChainCallback);
    ok(Other != NULL, "sync with callback failed with %lu\n", GetLastError());
    if (Other) CloseThreadWaitChainSession(Other);

    Session = OpenThreadWaitChainSession(0, NULL);
    ok(Session != NULL, "OpenThreadWaitChainSession failed with %lu\n", GetLastError());
    if (Session == NULL) return;

    Count = WCT_MAX_NODE_COUNT;
    Cycle = 7;
    Result = GetThreadWaitChain(Session, 0, 0, GetCurrentThreadId(), &Count, Nodes, &Cycle);
    ok(Result, "own thread failed with %lu\n", GetLastError());
    ok_long(Count, 1);
    ok_int(Cycle, FALSE);
    ok_int(Nodes[0].ObjectType, WctThreadType);
    ok_int(Nodes[0].ObjectStatus, WctStatusRunning);
    ok_long(Nodes[0].ThreadObject.ProcessId, GetCurrentProcessId());
    ok_long(Nodes[0].ThreadObject.ThreadId, GetCurrentThreadId());

    Count = 0;
    Cycle = 7;
    SetLastError(0xdeadbeef);
    Result = GetThreadWaitChain(Session, 0, 0, GetCurrentThreadId(), &Count, Nodes, &Cycle);
    ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER, "no room: %d, %lu\n", Result, GetLastError());
    ok_long(Count, 0);
    ok_int(Cycle, 7);

    Count = WCT_MAX_NODE_COUNT;
    Cycle = 7;
    SetLastError(0xdeadbeef);
    Result = GetThreadWaitChain(Session, 0, 0x100, GetCurrentThreadId(), &Count, Nodes, &Cycle);
    ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER, "bad flags: %d, %lu\n", Result, GetLastError());
    ok_int(Cycle, 7);

    for (i = 0; i < 2; i++)
    {
        Count = WCT_MAX_NODE_COUNT;
        Cycle = 7;
        SetLastError(0xdeadbeef);
        Result = GetThreadWaitChain(Session, 0, 0, i ? 0 : 0xfffffff0, &Count, Nodes, &Cycle);
        ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER, "unknown thread %lu: %d, %lu\n", i, Result, GetLastError());
        ok_long(Count, WCT_MAX_NODE_COUNT);
        ok_int(Cycle, FALSE);
    }

    Mutex = CreateMutexW(NULL, FALSE, NULL);
    Owned = CreateEventW(NULL, TRUE, FALSE, NULL);
    Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    CallbackDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    Owner = CreateThread(NULL, 0, OwnerThread, NULL, 0, &OwnerId);
    ok(WaitForSingleObject(Owned, 5000) == WAIT_OBJECT_0, "owner did not take the mutex\n");
    Waiter = CreateThread(NULL, 0, WaiterThread, NULL, 0, &WaiterId);
    for (i = 0; i < 100 && !(IsThreadBlocked(WaiterId) && IsThreadBlocked(OwnerId)); i++)
        Sleep(10);

    Count = WCT_MAX_NODE_COUNT;
    Result = GetThreadWaitChain(Session, 0, 0, WaiterId, &Count, Nodes, &Cycle);
    ok(Result, "waiter failed with %lu\n", GetLastError());
    ok_int(Nodes[0].ObjectType, WctThreadType);
    ok_int(Nodes[0].ObjectStatus, WctStatusBlocked);
    ok_long(Nodes[0].ThreadObject.ThreadId, WaiterId);
    todo_ros ok_long(Count, 3);
    if (Count == 3)
    {
        ok_int(Nodes[1].ObjectType, WctMutexType);
        ok_int(Nodes[1].ObjectStatus, WctStatusOwned);
        ok_int(Nodes[2].ObjectType, WctThreadType);
        ok_int(Nodes[2].ObjectStatus, WctStatusBlocked);
        ok_long(Nodes[2].ThreadObject.ThreadId, OwnerId);
    }

    Count = 1;
    SetLastError(0xdeadbeef);
    Result = GetThreadWaitChain(Session, 0, 0, WaiterId, &Count, Nodes, &Cycle);
    todo_ros ok(!Result && GetLastError() == ERROR_MORE_DATA, "one node of room: %d, %lu\n", Result, GetLastError());
    todo_ros ok_long(Count, 3);

    Count = WCT_MAX_NODE_COUNT;
    Result = GetThreadWaitChain(Session, 0, 0, OwnerId, &Count, Nodes, &Cycle);
    ok(Result, "owner failed with %lu\n", GetLastError());
    ok_long(Count, 1);
    ok_int(Nodes[0].ObjectStatus, WctStatusBlocked);

    Other = OpenThreadWaitChainSession(WCT_ASYNC_OPEN_FLAG, WaitChainCallback);
    ok(Other != NULL, "async session failed with %lu\n", GetLastError());
    if (Other)
    {
        Count = WCT_MAX_NODE_COUNT;
        SetLastError(0xdeadbeef);
        Result = GetThreadWaitChain(Other, 9, 0, WaiterId, &Count, Nodes, &Cycle);
        ok(!Result && GetLastError() == ERROR_IO_PENDING, "async: %d, %lu\n", Result, GetLastError());
        ok(WaitForSingleObject(CallbackDone, 5000) == WAIT_OBJECT_0, "no callback\n");
        ok(CallbackContext == 9, "callback context %Iu\n", CallbackContext);
        ok_long(CallbackError, ERROR_SUCCESS);
        todo_ros ok_long(CallbackCount, 3);
        CloseThreadWaitChainSession(Other);
    }

    SetEvent(Release);
    WaitForSingleObject(Owner, 5000);
    WaitForSingleObject(Waiter, 5000);
    CloseHandle(Owner);
    CloseHandle(Waiter);
    CloseHandle(Mutex);
    CloseHandle(Owned);
    CloseHandle(Release);
    CloseHandle(CallbackDone);
    CloseThreadWaitChainSession(Session);
}
