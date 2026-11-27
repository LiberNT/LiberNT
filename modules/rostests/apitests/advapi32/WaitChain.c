/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the thread wait chain traversal against Windows 11 results
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wct.h>
#include <winuser.h>
#include <ndk/psfuncs.h>

typedef HWCT (WINAPI *PFN_OPEN)(DWORD, PWAITCHAINCALLBACK);
typedef BOOL (WINAPI *PFN_GET)(HWCT, DWORD_PTR, DWORD, DWORD, LPDWORD, PWAITCHAIN_NODE_INFO, LPBOOL);
typedef VOID (WINAPI *PFN_CLOSE)(HWCT);

static PFN_OPEN pOpen;
static PFN_GET pGet;
static PFN_CLOSE pClose;
static HANDLE StartedEvent, Release, Mutex, Mutex2, Done, Go;
static HWND Window;
static DWORD AsyncError, AsyncCount;
static WAITCHAIN_NODE_INFO AsyncNodes[WCT_MAX_NODE_COUNT];

static DWORD WINAPI WaitEvent(PVOID P) { SetEvent(StartedEvent); WaitForSingleObject(Release, INFINITE); return 0; }
static DWORD WINAPI SleepForever(PVOID P) { SetEvent(StartedEvent); SleepEx(INFINITE, FALSE); return 0; }
static DWORD WINAPI WaitMutex(PVOID P) { SetEvent(StartedEvent); WaitForSingleObject(Mutex, INFINITE); ReleaseMutex(Mutex); return 0; }
static DWORD WINAPI OwnMutex(PVOID P) { WaitForSingleObject(Mutex, INFINITE); SetEvent(StartedEvent); WaitForSingleObject(Release, INFINITE); ReleaseMutex(Mutex); return 0; }
static DWORD WINAPI Spin(PVOID P) { SetEvent(StartedEvent); while (WaitForSingleObject(Release, 0) != WAIT_OBJECT_0) YieldProcessor(); return 0; }

static
DWORD
WINAPI
WaitMultiple(
    _In_ PVOID Parameter)
{
    HANDLE Handles[2] = { Release, Mutex };

    SetEvent(StartedEvent);
    WaitForMultipleObjects(2, Handles, FALSE, INFINITE);
    return 0;
}

static
DWORD
WINAPI
DeadlockWait(
    _In_ PVOID Parameter)
{
    HANDLE First = Parameter ? Mutex2 : Mutex, Second = Parameter ? Mutex : Mutex2;

    WaitForSingleObject(First, INFINITE);
    SetEvent(StartedEvent);
    WaitForSingleObject(Go, INFINITE);
    WaitForSingleObject(Second, INFINITE);
    ReleaseMutex(Second);
    ReleaseMutex(First);
    return 0;
}

static
DWORD
WINAPI
WindowThread(
    _In_ PVOID Parameter)
{
    MSG Message;

    Window = CreateWindowW(L"STATIC", L"wct", 0, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    SetEvent(StartedEvent);
    WaitForSingleObject(Release, INFINITE);
    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&Message);
    DestroyWindow(Window);
    return 0;
}

static DWORD WINAPI Sender(PVOID P) { SetEvent(StartedEvent); SendMessageW(Window, WM_NULL, 0, 0); return 0; }

static
VOID
WaitBlocked(
    _In_ HANDLE Thread,
    _In_ PVOID FirstArgument)
{
    THREAD_LAST_SYSCALL_INFORMATION LastCall;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 2000; Attempt++)
    {
        if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadLastSystemCall, &LastCall, sizeof(LastCall), NULL)) &&
            (!FirstArgument || LastCall.FirstArgument == FirstArgument))
        {
            return;
        }
        Sleep(1);
    }
    ok(0, "the thread never blocked on %p\n", FirstArgument);
}

static
HANDLE
Start(
    _In_ LPTHREAD_START_ROUTINE Routine,
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID BlockedOn,
    _Out_ PDWORD ThreadId)
{
    HANDLE Thread;

    ResetEvent(StartedEvent);
    Thread = CreateThread(NULL, 0, Routine, Parameter, 0, ThreadId);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    WaitForSingleObject(StartedEvent, INFINITE);
    if (BlockedOn != (PVOID)-1)
        WaitBlocked(Thread, BlockedOn);
    return Thread;
}

static
VOID
Finish(
    _In_ HANDLE Thread)
{
    SetEvent(Release);
    WaitForSingleObject(Thread, INFINITE);
    ResetEvent(Release);
    CloseHandle(Thread);
}

static
BOOL
Chain(
    _In_ HWCT Wct,
    _In_ DWORD ThreadId,
    _In_ DWORD Max,
    _Out_writes_(WCT_MAX_NODE_COUNT) PWAITCHAIN_NODE_INFO Nodes,
    _Out_ PDWORD Count,
    _Out_ PBOOL Cycle)
{
    *Count = Max;
    *Cycle = 7;
    FillMemory(Nodes, WCT_MAX_NODE_COUNT * sizeof(*Nodes), 0xcc);
    SetLastError(0xdeadbeef);
    return pGet(Wct, 0, 0, ThreadId, Count, Nodes, Cycle);
}

static
VOID
CheckThread(
    _In_ const WAITCHAIN_NODE_INFO *Node,
    _In_ WCT_OBJECT_STATUS Status,
    _In_ DWORD ThreadId,
    _In_ PCSTR What)
{
    ok(Node->ObjectType == WctThreadType, "%s: type %d\n", What, Node->ObjectType);
    ok(Node->ObjectStatus == Status, "%s: status %d, expected %d\n", What, Node->ObjectStatus, Status);
    ok(Node->ThreadObject.ProcessId == GetCurrentProcessId(), "%s: process %lu\n", What, Node->ThreadObject.ProcessId);
    ok(Node->ThreadObject.ThreadId == ThreadId, "%s: thread %lu, expected %lu\n", What, Node->ThreadObject.ThreadId, ThreadId);
}

static
VOID
CALLBACK
AsyncCallback(
    _In_ HWCT Wct,
    _In_ DWORD_PTR Context,
    _In_ DWORD Status,
    _In_ LPDWORD NodeCount,
    _In_ PWAITCHAIN_NODE_INFO Nodes,
    _In_ LPBOOL Cycle)
{
    ok(Context == 0x1234, "context %Ix\n", Context);
    AsyncError = Status;
    AsyncCount = *NodeCount;
    if (Status == ERROR_SUCCESS)
        CopyMemory(AsyncNodes, Nodes, *NodeCount * sizeof(*Nodes));
    SetEvent(Done);
}

static
VOID
TestArguments(
    _In_ HWCT Wct)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count;
    BOOL Cycle;
    HWCT Other;

    SetLastError(0xdeadbeef);
    Other = pOpen(2, NULL);
    ok(Other == NULL, "flags 2 opened a session\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Other = pOpen(WCT_ASYNC_OPEN_FLAG, NULL);
    ok(Other == NULL, "an asynchronous session without a callback opened\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);

    ok(!Chain(Wct, 0xfffffff0, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "bad thread id succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_int(Count, WCT_MAX_NODE_COUNT);
    ok_int(Cycle, FALSE);
    ok(!Chain(Wct, 0, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "thread id 0 succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    ok(!Chain(Wct, GetCurrentThreadId(), 0, Nodes, &Count, &Cycle), "count 0 succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_int(Count, 0);
    ok_int(Cycle, 7);
    ok(!Chain(Wct, GetCurrentThreadId(), WCT_MAX_NODE_COUNT + 1, Nodes, &Count, &Cycle), "count 17 succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_int(Count, WCT_MAX_NODE_COUNT + 1);
    ok_int(Cycle, 7);

    Count = 4;
    SetLastError(0xdeadbeef);
    ok(!pGet(NULL, 0, 0, GetCurrentThreadId(), &Count, Nodes, &Cycle), "NULL session succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!pGet(Wct, 0, 0x10, GetCurrentThreadId(), &Count, Nodes, &Cycle), "flag 0x10 succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!pGet(Wct, 0, 0, GetCurrentThreadId(), NULL, Nodes, &Cycle), "NULL count succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!pGet(Wct, 0, 0, GetCurrentThreadId(), &Count, NULL, &Cycle), "NULL nodes succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!pGet(Wct, 0, 0, GetCurrentThreadId(), &Count, Nodes, NULL), "NULL cycle succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    Count = 4;
    SetLastError(0xdeadbeef);
    ok(pGet(Wct, 0, WCTP_GETINFO_ALL_FLAGS, GetCurrentThreadId(), &Count, Nodes, &Cycle), "all flags failed: %lu\n", GetLastError());
    ok_int(Count, 1);

    SetLastError(0xdeadbeef);
    pClose(NULL);
    ok_int(GetLastError(), 0xdeadbeef);
}

static
VOID
TestSingleThread(
    _In_ HWCT Wct)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count, ThreadId;
    HANDLE Thread;
    BOOL Cycle;

    ok(Chain(Wct, GetCurrentThreadId(), WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "self failed: %lu\n", GetLastError());
    ok_int(GetLastError(), 0xdeadbeef);
    ok_int(Count, 1);
    ok_int(Cycle, FALSE);
    CheckThread(&Nodes[0], WctStatusRunning, GetCurrentThreadId(), "self");
    ok(Nodes[0].ThreadObject.ContextSwitches != 0, "self has no context switch\n");

    Thread = Start(WaitEvent, NULL, Release, &ThreadId);
    ok(Chain(Wct, ThreadId, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "event wait failed: %lu\n", GetLastError());
    ok_int(GetLastError(), 0xdeadbeef);
    ok_int(Count, 1);
    CheckThread(&Nodes[0], WctStatusBlocked, ThreadId, "event wait");
    Finish(Thread);

    Thread = Start(SleepForever, NULL, NULL, &ThreadId);
    ok(Chain(Wct, ThreadId, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "sleep failed: %lu\n", GetLastError());
    ok_int(Count, 1);
    CheckThread(&Nodes[0], WctStatusBlocked, ThreadId, "sleep");
    TerminateThread(Thread, 0);
    WaitForSingleObject(Thread, INFINITE);

    ok(!Chain(Wct, ThreadId, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "exited thread succeeded\n");
    ok_int(GetLastError(), ERROR_INVALID_THREAD_ID);
    ok_int(Count, 0);
    ok_int(Cycle, FALSE);
    CloseHandle(Thread);

    Thread = Start(Spin, NULL, (PVOID)-1, &ThreadId);
    ok(Chain(Wct, ThreadId, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "spin failed: %lu\n", GetLastError());
    ok_int(Count, 1);
    CheckThread(&Nodes[0], WctStatusRunning, ThreadId, "spin");
    Finish(Thread);

    Thread = Start(OwnMutex, NULL, Release, &ThreadId);
    {
        DWORD Waiter;
        HANDLE Multi = Start(WaitMultiple, NULL, (PVOID)2, &Waiter);

        ok(Chain(Wct, Waiter, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "multiple wait failed: %lu\n", GetLastError());
        ok_int(Count, 1);
        CheckThread(&Nodes[0], WctStatusBlocked, Waiter, "multiple wait");
        Finish(Thread);
        WaitForSingleObject(Multi, INFINITE);
        CloseHandle(Multi);
    }
}

static
VOID
TestMutexChain(
    _In_ HWCT Wct)
{
    static const WCHAR Suffix[] = L"\\BaseNamedObjects\\LiberNTWaitChainMutex";
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count, Owner, Waiter;
    HANDLE OwnerThread, WaiterThread;
    SIZE_T Length;
    BOOL Cycle;

    OwnerThread = Start(OwnMutex, NULL, Release, &Owner);
    WaiterThread = Start(WaitMutex, NULL, Mutex, &Waiter);

    ok(Chain(Wct, Waiter, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "mutex chain failed: %lu\n", GetLastError());
    ok_int(GetLastError(), 0xdeadbeef);
    ok_int(Count, 3);
    ok_int(Cycle, FALSE);
    CheckThread(&Nodes[0], WctStatusBlocked, Waiter, "mutex waiter");
    ok_int(Nodes[1].ObjectType, WctMutexType);
    ok_int(Nodes[1].ObjectStatus, WctStatusOwned);
    Length = wcslen(Nodes[1].LockObject.ObjectName);
    ok(Length > ARRAYSIZE(Suffix) && !wcscmp(Nodes[1].LockObject.ObjectName + Length - (ARRAYSIZE(Suffix) - 1), Suffix) &&
       Nodes[1].LockObject.ObjectName[0] == L'\\', "mutex name '%ls'\n", Nodes[1].LockObject.ObjectName);
    ok(Nodes[1].LockObject.Timeout.QuadPart == 0, "timeout %I64d\n", Nodes[1].LockObject.Timeout.QuadPart);
    ok_int(Nodes[1].LockObject.Alertable, FALSE);
    CheckThread(&Nodes[2], WctStatusBlocked, Owner, "mutex owner");

    ok(!Chain(Wct, Waiter, 1, Nodes, &Count, &Cycle), "count 1 succeeded\n");
    ok_int(GetLastError(), ERROR_MORE_DATA);
    ok_int(Count, 3);
    ok_int(Cycle, FALSE);
    ok(!Chain(Wct, Waiter, 2, Nodes, &Count, &Cycle), "count 2 succeeded\n");
    ok_int(GetLastError(), ERROR_MORE_DATA);
    ok_int(Count, 3);

    ok(Chain(Wct, Owner, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "owner chain failed: %lu\n", GetLastError());
    ok_int(Count, 1);
    CheckThread(&Nodes[0], WctStatusBlocked, Owner, "owner alone");

    Finish(OwnerThread);
    WaitForSingleObject(WaiterThread, INFINITE);
    CloseHandle(WaiterThread);

    WaitForSingleObject(Mutex, INFINITE);
    WaiterThread = Start(WaitMutex, NULL, Mutex, &Waiter);
    ok(Chain(Wct, Waiter, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "chain to main failed: %lu\n", GetLastError());
    ok_int(Count, 3);
    CheckThread(&Nodes[2], WctStatusRunning, GetCurrentThreadId(), "running owner");
    ReleaseMutex(Mutex);
    WaitForSingleObject(WaiterThread, INFINITE);
    CloseHandle(WaiterThread);
}

static
VOID
TestDeadlock(
    _In_ HWCT Wct)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count, First, Second;
    HANDLE FirstThread, SecondThread;
    BOOL Cycle;

    FirstThread = Start(DeadlockWait, NULL, (PVOID)-1, &First);
    SecondThread = Start(DeadlockWait, (PVOID)1, (PVOID)-1, &Second);
    SetEvent(Go);
    WaitBlocked(FirstThread, Mutex2);
    WaitBlocked(SecondThread, Mutex);

    ok(Chain(Wct, First, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "deadlock chain failed: %lu\n", GetLastError());
    ok_int(Cycle, TRUE);
    ok_int(Count, 5);
    CheckThread(&Nodes[0], WctStatusBlocked, First, "deadlock first");
    ok_int(Nodes[1].ObjectType, WctMutexType);
    ok_int(Nodes[1].ObjectStatus, WctStatusOwned);
    CheckThread(&Nodes[2], WctStatusBlocked, Second, "deadlock second");
    ok_int(Nodes[3].ObjectType, WctMutexType);
    ok_int(Nodes[3].ObjectStatus, WctStatusOwned);
    CheckThread(&Nodes[4], WctStatusBlocked, First, "deadlock repeat");

    TerminateThread(FirstThread, 0);
    TerminateThread(SecondThread, 0);
    WaitForSingleObject(FirstThread, INFINITE);
    WaitForSingleObject(SecondThread, INFINITE);
    CloseHandle(FirstThread);
    CloseHandle(SecondThread);
    ResetEvent(Go);
    if (WaitForSingleObject(Mutex, 0) == WAIT_ABANDONED)
        ReleaseMutex(Mutex);
    if (WaitForSingleObject(Mutex2, 0) == WAIT_ABANDONED)
        ReleaseMutex(Mutex2);
}

static
VOID
TestSendMessage(
    _In_ HWCT Wct)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count, Receiver, Sending;
    HANDLE ReceiverThread, SenderThread;
    BOOL Cycle;

    ReceiverThread = Start(WindowThread, NULL, Release, &Receiver);
    SenderThread = Start(Sender, NULL, Window, &Sending);

    ok(Chain(Wct, Sending, WCT_MAX_NODE_COUNT, Nodes, &Count, &Cycle), "send chain failed: %lu\n", GetLastError());
    ok_int(Count, 3);
    CheckThread(&Nodes[0], WctStatusBlocked, Sending, "sender");
    ok_int(Nodes[1].ObjectType, WctSendMessageType);
    ok_int(Nodes[1].ObjectStatus, WctStatusOwned);
    ok(Nodes[1].LockObject.ObjectName[0] == 0, "send name '%ls'\n", Nodes[1].LockObject.ObjectName);
    CheckThread(&Nodes[2], WctStatusBlocked, Receiver, "receiver");

    SetEvent(Release);
    WaitForSingleObject(SenderThread, INFINITE);
    WaitForSingleObject(ReceiverThread, INFINITE);
    ResetEvent(Release);
    CloseHandle(SenderThread);
    CloseHandle(ReceiverThread);
}

static
VOID
TestAsync(VOID)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count = WCT_MAX_NODE_COUNT, ThreadId;
    BOOL Cycle = 7;
    HANDLE Thread;
    HWCT Wct;

    Wct = pOpen(WCT_ASYNC_OPEN_FLAG, AsyncCallback);
    ok(Wct != NULL, "asynchronous session failed: %lu\n", GetLastError());
    if (!Wct)
        return;

    Thread = Start(WaitEvent, NULL, Release, &ThreadId);

    AsyncError = 0xdead;
    SetLastError(0xdeadbeef);
    ok(!pGet(Wct, 0x1234, 0, ThreadId, &Count, Nodes, &Cycle), "asynchronous query completed inline\n");
    ok_int(GetLastError(), ERROR_IO_PENDING);
    ok_int(WaitForSingleObject(Done, 5000), WAIT_OBJECT_0);
    ok_int(AsyncError, ERROR_SUCCESS);
    ok_int(AsyncCount, 1);
    CheckThread(&AsyncNodes[0], WctStatusBlocked, ThreadId, "asynchronous wait");
    pClose(Wct);
    Finish(Thread);
}

START_TEST(WaitChain)
{
    HMODULE Advapi32 = LoadLibraryW(L"advapi32.dll");
    HWCT Wct;

    pOpen = (PFN_OPEN)GetProcAddress(Advapi32, "OpenThreadWaitChainSession");
    pGet = (PFN_GET)GetProcAddress(Advapi32, "GetThreadWaitChain");
    pClose = (PFN_CLOSE)GetProcAddress(Advapi32, "CloseThreadWaitChainSession");
    ok(pOpen && pGet && pClose && GetProcAddress(Advapi32, "RegisterWaitChainCOMCallback"), "advapi32 lacks a wait chain export\n");
    if (!pOpen || !pGet || !pClose)
        return;

    StartedEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    Done = CreateEventW(NULL, FALSE, FALSE, NULL);
    Mutex = CreateMutexW(NULL, FALSE, L"LiberNTWaitChainMutex");
    Mutex2 = CreateMutexW(NULL, FALSE, NULL);
    Go = CreateEventW(NULL, TRUE, FALSE, NULL);

    SetLastError(0xdeadbeef);
    Wct = pOpen(0, NULL);
    ok(Wct != NULL, "OpenThreadWaitChainSession failed: %lu\n", GetLastError());
    ok_int(GetLastError(), ERROR_SUCCESS);
    if (Wct)
    {
        TestArguments(Wct);
        TestSingleThread(Wct);
        TestMutexChain(Wct);
        TestDeadlock(Wct);
        TestSendMessage(Wct);
        pClose(Wct);
    }
    TestAsync();

    CloseHandle(Go);
    CloseHandle(Mutex2);
    CloseHandle(Mutex);
    CloseHandle(Done);
    CloseHandle(Release);
    CloseHandle(StartedEvent);
}
