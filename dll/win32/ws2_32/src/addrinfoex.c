/*
 * PROJECT:     ReactOS WinSock 2 API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Extended Protocol-Independent Address Resolution
 * COPYRIGHT:   Adapted from Wine dlls/ws2_32/protocol.c
 */

#include <ws2_32.h>

#include <ws2tcpip.h>

#define NDEBUG
#include <debug.h>

typedef struct _WS_ADDRINFOEX_QUERY
{
    LIST_ENTRY Links;
    HANDLE Handle;
    LONG RefCount;
    BOOLEAN Pending;
    LPOVERLAPPED Overlapped;
    HANDLE Event;
    LPLOOKUPSERVICE_COMPLETION_ROUTINE CompletionRoutine;
    PADDRINFOEXW *ppResult;
    PTP_TIMER Timer;
    PWSTR Name;
    PWSTR ServiceName;
    PADDRINFOW Hints;
    ADDRINFOW HintsBuffer;
    WCHAR Strings[ANYSIZE_ARRAY];
} WS_ADDRINFOEX_QUERY, *PWS_ADDRINFOEX_QUERY;

typedef struct _WS_ADDRINFOEX_COMPLETION
{
    LPOVERLAPPED Overlapped;
    HANDLE Event;
    LPLOOKUPSERVICE_COMPLETION_ROUTINE CompletionRoutine;
    PADDRINFOEXW *ppResult;
} WS_ADDRINFOEX_COMPLETION, *PWS_ADDRINFOEX_COMPLETION;

static SRWLOCK WsAddrInfoExLock = SRWLOCK_INIT;
static LIST_ENTRY WsAddrInfoExQueries = { &WsAddrInfoExQueries, &WsAddrInfoExQueries };
static LONG WsAddrInfoExNextHandle;

static
INT
WSAAPI
ConvertAddrInfoToAddrInfoEx(IN PADDRINFOW AddrInfo,
                            OUT PADDRINFOEXW *ppResult)
{
    PADDRINFOEXW New;
    PADDRINFOEXW *Next = ppResult;
    SIZE_T NameSize;

    *ppResult = NULL;

    for (; AddrInfo; AddrInfo = AddrInfo->ai_next)
    {
        New = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, sizeof(ADDRINFOEXW));
        if (!New) goto Failure;

        *Next = New;

        New->ai_flags = AddrInfo->ai_flags;
        New->ai_family = AddrInfo->ai_family;
        New->ai_socktype = AddrInfo->ai_socktype;
        New->ai_protocol = AddrInfo->ai_protocol;

        if (AddrInfo->ai_addr)
        {
            New->ai_addr = HeapAlloc(WsSockHeap, 0, AddrInfo->ai_addrlen);
            if (!New->ai_addr) goto Failure;

            New->ai_addrlen = AddrInfo->ai_addrlen;
            RtlMoveMemory(New->ai_addr, AddrInfo->ai_addr, AddrInfo->ai_addrlen);
        }

        if (AddrInfo->ai_canonname)
        {
            NameSize = (wcslen(AddrInfo->ai_canonname) + 1) * sizeof(WCHAR);
            New->ai_canonname = HeapAlloc(WsSockHeap, 0, NameSize);
            if (!New->ai_canonname) goto Failure;

            RtlMoveMemory(New->ai_canonname, AddrInfo->ai_canonname, NameSize);
        }

        Next = &New->ai_next;
    }

    return ERROR_SUCCESS;

Failure:
    FreeAddrInfoExW(*ppResult);
    *ppResult = NULL;
    return WSA_NOT_ENOUGH_MEMORY;
}

static
INT
WsGetAddrInfoEx(IN PCWSTR pName,
                IN PCWSTR pServiceName,
                IN PADDRINFOW Hints,
                OUT PADDRINFOEXW *ppResult)
{
    INT ErrorCode;
    PADDRINFOW Result = NULL;

    *ppResult = NULL;

    ErrorCode = GetAddrInfoW(pName, pServiceName, Hints, &Result);
    if (ErrorCode != ERROR_SUCCESS) return ErrorCode;

    ErrorCode = ConvertAddrInfoToAddrInfoEx(Result, ppResult);

    freeaddrinfo((LPADDRINFO)Result);

    ASSERT((ErrorCode == ERROR_SUCCESS) || (*ppResult == NULL));
    return ErrorCode;
}

static
BOOLEAN
WsAddrInfoExDetachLocked(IN PWS_ADDRINFOEX_QUERY Query,
                         OUT PWS_ADDRINFOEX_COMPLETION Completion)
{
    if (!Query->Pending) return FALSE;

    ASSERT(Query->Overlapped != NULL);
    ASSERT(!IsListEmpty(&Query->Links));

    RemoveEntryList(&Query->Links);
    InitializeListHead(&Query->Links);
    Query->Pending = FALSE;

    Completion->Overlapped = Query->Overlapped;
    Completion->Event = Query->Event;
    Completion->CompletionRoutine = Query->CompletionRoutine;
    Completion->ppResult = Query->ppResult;
    return TRUE;
}

static
BOOLEAN
WsAddrInfoExDetach(IN PWS_ADDRINFOEX_QUERY Query,
                   OUT PWS_ADDRINFOEX_COMPLETION Completion)
{
    BOOLEAN Detached;

    AcquireSRWLockExclusive(&WsAddrInfoExLock);
    Detached = WsAddrInfoExDetachLocked(Query, Completion);
    ReleaseSRWLockExclusive(&WsAddrInfoExLock);

    return Detached;
}

static
VOID
WsAddrInfoExDeliver(IN PWS_ADDRINFOEX_COMPLETION Completion,
                    IN INT ErrorCode,
                    IN PADDRINFOEXW Result)
{
    ASSERT(ErrorCode != WSA_IO_PENDING);
    ASSERT(ErrorCode != WSAEINPROGRESS);
    ASSERT((ErrorCode == ERROR_SUCCESS) || (Result == NULL));
    ASSERT((Completion->Event == NULL) || (Completion->CompletionRoutine == NULL));

    *Completion->ppResult = Result;
    Completion->Overlapped->Pointer = Completion->ppResult;
    WriteULongPtrRelease(&Completion->Overlapped->Internal, (ULONG_PTR)ErrorCode);

    if (Completion->CompletionRoutine)
    {
        Completion->CompletionRoutine(ErrorCode, 0, Completion->Overlapped);
    }
    else if (Completion->Event)
    {
        SetEvent(Completion->Event);
    }
}

static
VOID
WsAddrInfoExRelease(IN PWS_ADDRINFOEX_QUERY Query)
{
    LONG RefCount;

    RefCount = InterlockedDecrement(&Query->RefCount);
    ASSERT(RefCount >= 0);
    if (RefCount) return;

    ASSERT(!Query->Pending);
    ASSERT(IsListEmpty(&Query->Links));

    if (Query->Timer)
    {
        SetThreadpoolTimer(Query->Timer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(Query->Timer, TRUE);
        CloseThreadpoolTimer(Query->Timer);
    }

    HeapFree(WsSockHeap, 0, Query);
}

static
VOID
CALLBACK
WsAddrInfoExTimeout(IN OUT PTP_CALLBACK_INSTANCE Instance,
                    IN OUT PVOID Context,
                    IN OUT PTP_TIMER Timer)
{
    PWS_ADDRINFOEX_QUERY Query = Context;
    WS_ADDRINFOEX_COMPLETION Completion;

    ASSERT(Query->Timer == Timer);

    if (WsAddrInfoExDetach(Query, &Completion))
    {
        /* FIXME: the status Windows 11 reports for an expired timeout is not confirmed */
        WsAddrInfoExDeliver(&Completion, WSAETIMEDOUT, NULL);
    }
}

static
VOID
CALLBACK
WsAddrInfoExWorker(IN OUT PTP_CALLBACK_INSTANCE Instance,
                   IN OUT PVOID Context)
{
    PWS_ADDRINFOEX_QUERY Query = Context;
    WS_ADDRINFOEX_COMPLETION Completion;
    PADDRINFOEXW Result;
    INT ErrorCode;

    ASSERT(Query->RefCount > 0);

    ErrorCode = WsGetAddrInfoEx(Query->Name, Query->ServiceName, Query->Hints, &Result);

    if (WsAddrInfoExDetach(Query, &Completion))
    {
        WsAddrInfoExDeliver(&Completion, ErrorCode, Result);
    }
    else if (Result)
    {
        FreeAddrInfoExW(Result);
    }

    WsAddrInfoExRelease(Query);

    FreeLibraryWhenCallbackReturns(Instance, WsDllHandle);
}

static
INT
WsAddrInfoExStart(IN PCWSTR pName,
                  IN PCWSTR pServiceName,
                  IN PADDRINFOW Hints,
                  OUT PADDRINFOEXW *ppResult,
                  IN PLARGE_INTEGER Timeout,
                  IN LPOVERLAPPED lpOverlapped,
                  IN LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
                  OUT LPHANDLE lpHandle)
{
    PWS_ADDRINFOEX_QUERY Query;
    SIZE_T NameLength, ServiceLength;
    WS_ADDRINFOEX_COMPLETION Completion;
    HMODULE Module;
    FILETIME DueTime;
    INT ErrorCode;

    NameLength = pName ? wcslen(pName) + 1 : 0;
    ServiceLength = pServiceName ? wcslen(pServiceName) + 1 : 0;

    Query = HeapAlloc(WsSockHeap,
                      HEAP_ZERO_MEMORY,
                      FIELD_OFFSET(WS_ADDRINFOEX_QUERY, Strings[NameLength + ServiceLength]));
    if (!Query) return WSA_NOT_ENOUGH_MEMORY;

    if (pName)
    {
        Query->Name = Query->Strings;
        RtlCopyMemory(Query->Name, pName, NameLength * sizeof(WCHAR));
    }
    if (pServiceName)
    {
        Query->ServiceName = &Query->Strings[NameLength];
        RtlCopyMemory(Query->ServiceName, pServiceName, ServiceLength * sizeof(WCHAR));
    }
    if (Hints)
    {
        Query->HintsBuffer = *Hints;
        Query->Hints = &Query->HintsBuffer;
    }

    do
    {
        Query->Handle = (HANDLE)(ULONG_PTR)(ULONG)InterlockedIncrement(&WsAddrInfoExNextHandle);
    } while (Query->Handle == NULL);

    Query->RefCount = 2;
    Query->Pending = TRUE;
    Query->Overlapped = lpOverlapped;
    Query->Event = lpOverlapped->hEvent;
    Query->CompletionRoutine = lpCompletionRoutine;
    Query->ppResult = ppResult;

    if (Timeout)
    {
        Query->Timer = CreateThreadpoolTimer(WsAddrInfoExTimeout, Query, NULL);
        if (!Query->Timer)
        {
            HeapFree(WsSockHeap, 0, Query);
            return WSA_NOT_ENOUGH_MEMORY;
        }
    }

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR)WsAddrInfoExWorker,
                            &Module))
    {
        if (Query->Timer) CloseThreadpoolTimer(Query->Timer);
        HeapFree(WsSockHeap, 0, Query);
        return WSA_NOT_ENOUGH_MEMORY;
    }
    ASSERT(Module == WsDllHandle);

    AcquireSRWLockExclusive(&WsAddrInfoExLock);
    InsertTailList(&WsAddrInfoExQueries, &Query->Links);
    ReleaseSRWLockExclusive(&WsAddrInfoExLock);

    if (lpHandle) *lpHandle = Query->Handle;

    if (!TrySubmitThreadpoolCallback(WsAddrInfoExWorker, Query, NULL))
    {
        ErrorCode = WSA_IO_PENDING;
        if (WsAddrInfoExDetach(Query, &Completion))
        {
            if (lpHandle) *lpHandle = NULL;
            ErrorCode = WSA_NOT_ENOUGH_MEMORY;
        }
        WsAddrInfoExRelease(Query);
        WsAddrInfoExRelease(Query);
        FreeLibrary(Module);
        return ErrorCode;
    }

    if (Timeout)
    {
        DueTime.dwLowDateTime = Timeout->LowPart;
        DueTime.dwHighDateTime = (DWORD)Timeout->HighPart;
        SetThreadpoolTimer(Query->Timer, &DueTime, 0, 0);
    }

    WsAddrInfoExRelease(Query);
    return WSA_IO_PENDING;
}

INT
WSAAPI
GetAddrInfoExW(IN PCWSTR pName,
               IN PCWSTR pServiceName,
               IN DWORD dwNameSpace,
               IN LPGUID lpNspId,
               IN const ADDRINFOEXW *hints,
               OUT PADDRINFOEXW *ppResult,
               IN struct timeval *timeout,
               IN LPOVERLAPPED lpOverlapped,
               IN LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
               OUT LPHANDLE lpHandle)
{
    INT ErrorCode;
    ADDRINFOW Hints, NumericHints;
    PADDRINFOW HintsW = NULL;
    LARGE_INTEGER Timeout;

    DPRINT("GetAddrInfoExW: %S, %S, %lu, %p\n", pName, pServiceName, dwNameSpace, ppResult);

    if (lpHandle) *lpHandle = NULL;

    if ((ErrorCode = WsQuickProlog()) != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return ErrorCode;
    }

    if (!ppResult)
    {
        SetLastError(WSAEFAULT);
        return WSAEFAULT;
    }

    *ppResult = NULL;

    if (!(lpOverlapped) && ((lpCompletionRoutine) || (lpHandle) || (timeout)))
    {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }

    if ((lpOverlapped) && (lpOverlapped->hEvent) && (lpCompletionRoutine))
    {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }

    if (timeout)
    {
        Timeout.QuadPart = (LONGLONG)timeout->tv_sec * 10000000 + (LONGLONG)timeout->tv_usec * 10;
        if (Timeout.QuadPart <= 0)
        {
            SetLastError(WSAEINVAL);
            return WSAEINVAL;
        }
        Timeout.QuadPart = -Timeout.QuadPart;
    }

    if (((dwNameSpace != NS_ALL) && (dwNameSpace != NS_DNS)) || (lpNspId))
    {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }

    if (!(pName) && !(pServiceName))
    {
        SetLastError(WSAHOST_NOT_FOUND);
        return WSAHOST_NOT_FOUND;
    }

    if (hints)
    {
        if ((hints->ai_addrlen) || (hints->ai_canonname) || (hints->ai_addr) || (hints->ai_next))
        {
            SetLastError(WSANO_RECOVERY);
            return WSANO_RECOVERY;
        }

        RtlZeroMemory(&Hints, sizeof(Hints));
        Hints.ai_flags = hints->ai_flags;
        Hints.ai_family = hints->ai_family;
        Hints.ai_socktype = hints->ai_socktype;
        Hints.ai_protocol = hints->ai_protocol;
        HintsW = &Hints;
    }

    if (!lpOverlapped)
    {
        ErrorCode = WsGetAddrInfoEx(pName, pServiceName, HintsW, ppResult);
        SetLastError(ErrorCode);
        return ErrorCode;
    }

    lpOverlapped->Internal = WSAEINPROGRESS;

    if ((HintsW) && (HintsW->ai_flags & AI_NUMERICHOST))
    {
        ErrorCode = WsGetAddrInfoEx(pName, pServiceName, HintsW, ppResult);
    }
    else
    {
        if (HintsW)
        {
            NumericHints = *HintsW;
        }
        else
        {
            RtlZeroMemory(&NumericHints, sizeof(NumericHints));
        }
        NumericHints.ai_flags |= AI_NUMERICHOST;

        ErrorCode = WsGetAddrInfoEx(pName, pServiceName, &NumericHints, ppResult);
        if (ErrorCode != ERROR_SUCCESS)
        {
            ErrorCode = WsAddrInfoExStart(pName,
                                          pServiceName,
                                          HintsW,
                                          ppResult,
                                          timeout ? &Timeout : NULL,
                                          lpOverlapped,
                                          lpCompletionRoutine,
                                          lpHandle);
        }
    }

    SetLastError(ErrorCode);
    return ErrorCode;
}

VOID
WSAAPI
FreeAddrInfoExW(IN PADDRINFOEXW pAddrInfoEx)
{
    PADDRINFOEXW NextInfo;

    for (NextInfo = pAddrInfoEx; NextInfo; NextInfo = pAddrInfoEx)
    {
        if (NextInfo->ai_canonname)
        {
            HeapFree(WsSockHeap, 0, NextInfo->ai_canonname);
        }

        if (NextInfo->ai_addr)
        {
            HeapFree(WsSockHeap, 0, NextInfo->ai_addr);
        }

        pAddrInfoEx = NextInfo->ai_next;

        HeapFree(WsSockHeap, 0, NextInfo);
    }
}

INT
WSAAPI
GetAddrInfoExCancel(IN LPHANDLE lpHandle)
{
    PLIST_ENTRY Entry;
    PWS_ADDRINFOEX_QUERY Query;
    WS_ADDRINFOEX_COMPLETION Completion;
    BOOLEAN Detached = FALSE;

    DPRINT("GetAddrInfoExCancel: %p\n", lpHandle);

    if (lpHandle)
    {
        AcquireSRWLockExclusive(&WsAddrInfoExLock);
        for (Entry = WsAddrInfoExQueries.Flink; Entry != &WsAddrInfoExQueries; Entry = Entry->Flink)
        {
            Query = CONTAINING_RECORD(Entry, WS_ADDRINFOEX_QUERY, Links);
            if (Query->Handle == *lpHandle)
            {
                Detached = WsAddrInfoExDetachLocked(Query, &Completion);
                ASSERT(Detached);
                break;
            }
        }
        ReleaseSRWLockExclusive(&WsAddrInfoExLock);
    }

    if (!Detached)
    {
        SetLastError(WSA_INVALID_HANDLE);
        return WSA_INVALID_HANDLE;
    }

    WsAddrInfoExDeliver(&Completion, WSA_E_CANCELLED, NULL);

    SetLastError(ERROR_SUCCESS);
    return ERROR_SUCCESS;
}

INT
WSAAPI
GetAddrInfoExOverlappedResult(IN LPOVERLAPPED lpOverlapped)
{
    DPRINT("GetAddrInfoExOverlappedResult: %p\n", lpOverlapped);

    if (!lpOverlapped) return WSAEINVAL;

    return (INT)ReadULongPtrAcquire(&lpOverlapped->Internal);
}
