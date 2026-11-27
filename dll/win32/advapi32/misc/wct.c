/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wait Chain Traversal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>
#include <ndk/exfuncs.h>
#include <ndk/ketypes.h>
#include <wct.h>

#define WCT_SESSION_MAGIC 0x54435748
#define WCT_GETINFO_VALID_FLAGS (WCT_OUT_OF_PROC_FLAG | WCT_OUT_OF_PROC_COM_FLAG | WCT_OUT_OF_PROC_CS_FLAG | WCT_NETWORK_IO_FLAG)

typedef struct _WCT_SESSION
{
    ULONG Magic;
    LONG References;
    LONG Closed;
    BOOL Async;
    PWAITCHAINCALLBACK Callback;
} WCT_SESSION, *PWCT_SESSION;

typedef struct _WCT_REQUEST
{
    PWCT_SESSION Session;
    DWORD_PTR Context;
    DWORD ThreadId;
    LPDWORD NodeCount;
    PWAITCHAIN_NODE_INFO NodeInfoArray;
    LPBOOL IsCycle;
} WCT_REQUEST, *PWCT_REQUEST;

static PCOGETCALLSTATE WctpCallStateCallback;
static PCOGETACTIVATIONSTATE WctpActivationStateCallback;

static
VOID
WctpReleaseSession(
    _In_ PWCT_SESSION Session)
{
    LONG References = InterlockedDecrement(&Session->References);

    ASSERT(References >= 0);
    if (References != 0) return;

    Session->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Session);
}

static
DWORD
WctpBuildChain(
    _In_ DWORD ThreadId,
    _Inout_ LPDWORD NodeCount,
    _Out_writes_(*NodeCount) PWAITCHAIN_NODE_INFO NodeInfoArray)
{
    PSYSTEM_PROCESS_INFORMATION Snapshot = NULL, Process;
    PSYSTEM_THREAD_INFORMATION Thread = NULL;
    ULONG Size = 0x10000, Needed = 0, i;
    NTSTATUS Status;

    ASSERT(*NodeCount != 0);

    for (;;)
    {
        Snapshot = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Snapshot == NULL) return ERROR_NOT_ENOUGH_MEMORY;
        Status = NtQuerySystemInformation(SystemProcessInformation, Snapshot, Size, &Needed);
        if (Status != STATUS_INFO_LENGTH_MISMATCH) break;
        HeapFree(GetProcessHeap(), 0, Snapshot);
        Size = max(Size * 2, Needed + 0x1000);
    }
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Snapshot);
        return RtlNtStatusToDosError(Status);
    }

    Process = Snapshot;
    for (;;)
    {
        PSYSTEM_THREAD_INFORMATION Threads = (PSYSTEM_THREAD_INFORMATION)(Process + 1);

        for (i = 0; i < Process->NumberOfThreads; i++)
        {
            if (HandleToUlong(Threads[i].ClientId.UniqueThread) == ThreadId)
            {
                Thread = &Threads[i];
                break;
            }
        }
        if (Thread != NULL || Process->NextEntryOffset == 0) break;
        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }

    if (Thread == NULL || ThreadId == 0)
    {
        HeapFree(GetProcessHeap(), 0, Snapshot);
        return ERROR_INVALID_PARAMETER;
    }

    /* FIXME: the object a blocked thread waits on and the thread owning it are not followed, so the chain ends at the thread */
    ZeroMemory(&NodeInfoArray[0], sizeof(NodeInfoArray[0]));
    NodeInfoArray[0].ObjectType = WctThreadType;
    NodeInfoArray[0].ObjectStatus = (Thread->ThreadState == Waiting) ? WctStatusBlocked : WctStatusRunning;
    NodeInfoArray[0].ThreadObject.ProcessId = HandleToUlong(Thread->ClientId.UniqueProcess);
    NodeInfoArray[0].ThreadObject.ThreadId = ThreadId;
    NodeInfoArray[0].ThreadObject.WaitTime = Thread->WaitTime;
    NodeInfoArray[0].ThreadObject.ContextSwitches = Thread->ContextSwitches;
    *NodeCount = 1;

    HeapFree(GetProcessHeap(), 0, Snapshot);
    return ERROR_SUCCESS;
}

static
DWORD
WINAPI
WctpAsyncWorker(
    _In_ PVOID Parameter)
{
    PWCT_REQUEST Request = Parameter;
    PWCT_SESSION Session = Request->Session;
    DWORD Error;

    ASSERT(Session->Magic == WCT_SESSION_MAGIC);
    ASSERT(Session->Async && Session->Callback != NULL);

    if (InterlockedCompareExchange(&Session->Closed, 0, 0) != 0)
        Error = ERROR_CANCELLED;
    else
        Error = WctpBuildChain(Request->ThreadId, Request->NodeCount, Request->NodeInfoArray);

    Session->Callback(Session, Request->Context, Error, Request->NodeCount, Request->NodeInfoArray, Request->IsCycle);

    WctpReleaseSession(Session);
    HeapFree(GetProcessHeap(), 0, Request);
    return 0;
}

HWCT
WINAPI
OpenThreadWaitChainSession(
    _In_ DWORD Flags,
    _In_opt_ PWAITCHAINCALLBACK Callback)
{
    PWCT_SESSION Session;

    if ((Flags & ~WCT_ASYNC_OPEN_FLAG) != 0 || ((Flags & WCT_ASYNC_OPEN_FLAG) && Callback == NULL))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    Session = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Session));
    if (Session == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    Session->Magic = WCT_SESSION_MAGIC;
    Session->References = 1;
    Session->Async = (Flags & WCT_ASYNC_OPEN_FLAG) != 0;
    Session->Callback = Callback;
    return Session;
}

VOID
WINAPI
CloseThreadWaitChainSession(
    _In_ HWCT WctHandle)
{
    PWCT_SESSION Session = WctHandle;
    LONG WasClosed;

    if (Session == NULL || Session->Magic != WCT_SESSION_MAGIC) return;

    WasClosed = InterlockedExchange(&Session->Closed, 1);
    ASSERT(WasClosed == 0);
    WctpReleaseSession(Session);
}

BOOL
WINAPI
GetThreadWaitChain(
    _In_ HWCT WctHandle,
    _In_opt_ DWORD_PTR Context,
    _In_ DWORD Flags,
    _In_ DWORD ThreadId,
    _Inout_ LPDWORD NodeCount,
    _Out_writes_(*NodeCount) PWAITCHAIN_NODE_INFO NodeInfoArray,
    _Out_ LPBOOL IsCycle)
{
    PWCT_SESSION Session = WctHandle;
    PWCT_REQUEST Request;
    LONG References;
    DWORD Error;

    if (Session == NULL || Session->Magic != WCT_SESSION_MAGIC ||
        NodeCount == NULL || NodeInfoArray == NULL || IsCycle == NULL ||
        (Flags & ~WCT_GETINFO_VALID_FLAGS) != 0 || *NodeCount == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* TODO: chains are not followed into other processes, COM, critical sections or network I/O, whatever the flags ask */
    *IsCycle = FALSE;

    if (!Session->Async)
    {
        Error = WctpBuildChain(ThreadId, NodeCount, NodeInfoArray);
        if (Error != ERROR_SUCCESS)
        {
            SetLastError(Error);
            return FALSE;
        }
        return TRUE;
    }

    Request = HeapAlloc(GetProcessHeap(), 0, sizeof(*Request));
    if (Request == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Request->Session = Session;
    Request->Context = Context;
    Request->ThreadId = ThreadId;
    Request->NodeCount = NodeCount;
    Request->NodeInfoArray = NodeInfoArray;
    Request->IsCycle = IsCycle;

    References = InterlockedIncrement(&Session->References);
    ASSERT(References > 1);
    if (!QueueUserWorkItem(WctpAsyncWorker, Request, WT_EXECUTEDEFAULT))
    {
        Error = GetLastError();
        WctpReleaseSession(Session);
        HeapFree(GetProcessHeap(), 0, Request);
        SetLastError(Error);
        return FALSE;
    }

    SetLastError(ERROR_IO_PENDING);
    return FALSE;
}

VOID
WINAPI
RegisterWaitChainCOMCallback(
    _In_ PCOGETCALLSTATE CallStateCallback,
    _In_ PCOGETACTIVATIONSTATE ActivationStateCallback)
{
    /* TODO: COM call and activation state are kept but never asked, chains not being followed into COM */
    InterlockedExchangePointer((PVOID *)&WctpCallStateCallback, CallStateCallback);
    InterlockedExchangePointer((PVOID *)&WctpActivationStateCallback, ActivationStateCallback);
}
