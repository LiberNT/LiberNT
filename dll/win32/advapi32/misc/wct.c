/*
 * PROJECT:     LiberNT Advanced API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Thread wait chain traversal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>
#include <ndk/exfuncs.h>
#include <wct.h>

#define WCT_SESSION_MAGIC 0x53544357
#define WCT_WIN32K_SERVICE 0x1000
#define WCT_SERVICE_UNKNOWN 0xFFFFFFFF

typedef struct _WCT_SESSION
{
    ULONG Magic;
    DWORD Flags;
    PWAITCHAINCALLBACK Callback;
    ULONG Pending;
} WCT_SESSION, *PWCT_SESSION;

typedef struct _WCT_REQUEST
{
    PWCT_SESSION Session;
    DWORD_PTR Context;
    DWORD Flags;
    DWORD ThreadId;
    LPDWORD NodeCount;
    PWAITCHAIN_NODE_INFO NodeInfoArray;
    LPBOOL IsCycle;
} WCT_REQUEST, *PWCT_REQUEST;

typedef BOOL (WINAPI *PFN_IS_WINDOW)(HWND);
typedef DWORD (WINAPI *PFN_GET_WINDOW_THREAD_PROCESS_ID)(HWND, LPDWORD);

static PVOID WctCallStateCallback;
static PVOID WctActivationStateCallback;
static volatile LONG WctWaitSingleService = (LONG)WCT_SERVICE_UNKNOWN;
static SRWLOCK WctPendingLock = SRWLOCK_INIT;
static CONDITION_VARIABLE WctPendingDone = CONDITION_VARIABLE_INIT;

static
DWORD
WINAPI
WctCalibrationThread(
    _In_ PVOID Parameter)
{
    NtWaitForSingleObject((HANDLE)Parameter, FALSE, NULL);
    return 0;
}

static
ULONG
WctGetWaitSingleService(VOID)
{
    THREAD_LAST_SYSCALL_INFORMATION LastCall;
    ULONG Service = (ULONG)ReadAcquire(&WctWaitSingleService);
    HANDLE Event, Thread;
    ULONG Attempt;

    if (Service != WCT_SERVICE_UNKNOWN)
        return Service;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Event)
        return WCT_SERVICE_UNKNOWN;
    Thread = CreateThread(NULL, 0, WctCalibrationThread, Event, 0, NULL);
    if (!Thread)
    {
        CloseHandle(Event);
        return WCT_SERVICE_UNKNOWN;
    }

    for (Attempt = 0; Attempt < 1000; Attempt++)
    {
        if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadLastSystemCall, &LastCall, sizeof(LastCall), NULL)) &&
            LastCall.FirstArgument == Event)
        {
            Service = LastCall.SystemCallNumber;
            break;
        }
        Sleep(Attempt < 10 ? 0 : 1);
    }

    SetEvent(Event);
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
    CloseHandle(Event);

    if (Service != WCT_SERVICE_UNKNOWN)
        InterlockedCompareExchange(&WctWaitSingleService, (LONG)Service, (LONG)WCT_SERVICE_UNKNOWN);
    return Service;
}

static
BOOL
WctQueryThread(
    _In_ HANDLE Thread,
    _Out_ PSYSTEM_THREAD_INFORMATION Information)
{
    return NT_SUCCESS(NtQueryInformationThread(Thread,
                                               ThreadSystemThreadInformation,
                                               Information,
                                               sizeof(*Information),
                                               NULL));
}

static
VOID
WctFillThreadNode(
    _Out_ PWAITCHAIN_NODE_INFO Node,
    _In_ const SYSTEM_THREAD_INFORMATION *Information)
{
    ZeroMemory(Node, sizeof(*Node));
    Node->ObjectType = WctThreadType;
    Node->ObjectStatus = (Information->ThreadState == Waiting) ? WctStatusBlocked : WctStatusRunning;
    Node->ThreadObject.ProcessId = HandleToUlong(Information->ClientId.UniqueProcess);
    Node->ThreadObject.ThreadId = HandleToUlong(Information->ClientId.UniqueThread);
    Node->ThreadObject.WaitTime = Information->WaitTime;
    Node->ThreadObject.ContextSwitches = Information->ContextSwitches;
}

static
HANDLE
WctDuplicateWaitHandle(
    _In_ DWORD ProcessId,
    _In_ HANDLE Handle)
{
    HANDLE Process, Copy = NULL;

    if (ProcessId == GetCurrentProcessId())
    {
        if (!DuplicateHandle(GetCurrentProcess(), Handle, GetCurrentProcess(), &Copy,
                             MUTANT_QUERY_STATE, FALSE, 0))
        {
            return NULL;
        }
        return Copy;
    }

    Process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, ProcessId);
    if (!Process)
        return NULL;
    if (!DuplicateHandle(Process, Handle, GetCurrentProcess(), &Copy, MUTANT_QUERY_STATE, FALSE, 0))
        Copy = NULL;
    CloseHandle(Process);
    return Copy;
}

static
BOOL
WctIsMutant(
    _In_ HANDLE Handle)
{
    static const WCHAR Mutant[] = L"Mutant";
    UCHAR Buffer[sizeof(OBJECT_TYPE_INFORMATION) + 64 * sizeof(WCHAR)];
    POBJECT_TYPE_INFORMATION Type = (POBJECT_TYPE_INFORMATION)Buffer;

    if (!NT_SUCCESS(NtQueryObject(Handle, ObjectTypeInformation, Buffer, sizeof(Buffer), NULL)))
        return FALSE;
    return (Type->TypeName.Length == sizeof(Mutant) - sizeof(WCHAR)) &&
           !_wcsnicmp(Type->TypeName.Buffer, Mutant, ARRAYSIZE(Mutant) - 1);
}

static
VOID
WctFillMutantNode(
    _Out_ PWAITCHAIN_NODE_INFO Node,
    _In_ HANDLE Mutant,
    _Out_ PCLIENT_ID Owner)
{
    UCHAR Buffer[sizeof(OBJECT_NAME_INFORMATION) + WCT_OBJNAME_LENGTH * sizeof(WCHAR) * 2];
    POBJECT_NAME_INFORMATION Name = (POBJECT_NAME_INFORMATION)Buffer;
    MUTANT_OWNER_INFORMATION OwnerInformation;
    MUTANT_BASIC_INFORMATION Basic;

    ZeroMemory(Node, sizeof(*Node));
    ZeroMemory(Owner, sizeof(*Owner));
    Node->ObjectType = WctMutexType;
    Node->ObjectStatus = WctStatusUnknown;

    if (NT_SUCCESS(NtQueryObject(Mutant, ObjectNameInformation, Buffer, sizeof(Buffer), NULL)) &&
        Name->Name.Buffer)
    {
        SIZE_T Length = min(Name->Name.Length / sizeof(WCHAR), WCT_OBJNAME_LENGTH - 1);

        CopyMemory(Node->LockObject.ObjectName, Name->Name.Buffer, Length * sizeof(WCHAR));
    }

    if (NT_SUCCESS(NtQueryMutant(Mutant, MutantOwnerInformation, &OwnerInformation, sizeof(OwnerInformation), NULL)))
    {
        *Owner = OwnerInformation.ClientId;
        if (Owner->UniqueThread)
        {
            Node->ObjectStatus = WctStatusOwned;
        }
        else if (NT_SUCCESS(NtQueryMutant(Mutant, MutantBasicInformation, &Basic, sizeof(Basic), NULL)))
        {
            Node->ObjectStatus = Basic.AbandonedState ? WctStatusAbandoned : WctStatusNotOwned;
        }
    }
}

static
DWORD
WctFindSendMessageTarget(
    _In_ const THREAD_LAST_SYSCALL_INFORMATION *LastCall,
    _In_ DWORD ThreadId)
{
    PFN_GET_WINDOW_THREAD_PROCESS_ID GetOwner;
    PFN_IS_WINDOW IsWindowProc;
    HMODULE User32;
    DWORD Target;

    if (!(LastCall->SystemCallNumber & WCT_WIN32K_SERVICE))
        return 0;
    User32 = GetModuleHandleW(L"user32.dll");
    if (!User32)
        return 0;
    IsWindowProc = (PFN_IS_WINDOW)GetProcAddress(User32, "IsWindow");
    GetOwner = (PFN_GET_WINDOW_THREAD_PROCESS_ID)GetProcAddress(User32, "GetWindowThreadProcessId");
    if (!IsWindowProc || !GetOwner || !IsWindowProc((HWND)LastCall->FirstArgument))
        return 0;

    Target = GetOwner((HWND)LastCall->FirstArgument, NULL);
    return (Target != ThreadId) ? Target : 0;
}

static
BOOL
WctInChain(
    _In_reads_(Count) const WAITCHAIN_NODE_INFO *Nodes,
    _In_ DWORD Count,
    _In_ DWORD ThreadId)
{
    DWORD Index;

    for (Index = 0; Index < Count; Index++)
    {
        if ((Nodes[Index].ObjectType == WctThreadType) && (Nodes[Index].ThreadObject.ThreadId == ThreadId))
            return TRUE;
    }
    return FALSE;
}

static
DWORD
WctBuildChain(
    _In_ DWORD Flags,
    _In_ DWORD ThreadId,
    _Out_writes_(WCT_MAX_NODE_COUNT) PWAITCHAIN_NODE_INFO Nodes,
    _Out_ PDWORD Count,
    _Out_ PBOOL IsCycle)
{
    SYSTEM_THREAD_INFORMATION Information;
    HANDLE Thread;

    *Count = 0;
    *IsCycle = FALSE;

    Thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, ThreadId);
    if (!Thread)
        return ERROR_INVALID_PARAMETER;
    if (!WctQueryThread(Thread, &Information))
    {
        CloseHandle(Thread);
        return ERROR_INVALID_PARAMETER;
    }
    if ((HandleToUlong(Information.ClientId.UniqueProcess) != GetCurrentProcessId()) &&
        !(Flags & WCT_OUT_OF_PROC_FLAG))
    {
        CloseHandle(Thread);
        return ERROR_INVALID_PARAMETER;
    }
    if (Information.ThreadState == Terminated)
    {
        CloseHandle(Thread);
        return ERROR_INVALID_THREAD_ID;
    }

    for (;;)
    {
        THREAD_LAST_SYSCALL_INFORMATION LastCall;
        DWORD ProcessId = HandleToUlong(Information.ClientId.UniqueProcess);
        DWORD NextThreadId = 0;
        CLIENT_ID Owner;
        HANDLE Mutant;

        ASSERT(*Count < WCT_MAX_NODE_COUNT);
        WctFillThreadNode(&Nodes[(*Count)++], &Information);

        if ((Information.ThreadState != Waiting) || (*Count + 2 > WCT_MAX_NODE_COUNT) ||
            !NT_SUCCESS(NtQueryInformationThread(Thread, ThreadLastSystemCall, &LastCall, sizeof(LastCall), NULL)))
        {
            break;
        }
        CloseHandle(Thread);
        Thread = NULL;

        if (LastCall.SystemCallNumber == WctGetWaitSingleService())
        {
            Mutant = WctDuplicateWaitHandle(ProcessId, (HANDLE)LastCall.FirstArgument);
            if (!Mutant)
                break;
            if (!WctIsMutant(Mutant))
            {
                CloseHandle(Mutant);
                break;
            }
            WctFillMutantNode(&Nodes[(*Count)++], Mutant, &Owner);
            CloseHandle(Mutant);
            NextThreadId = HandleToUlong(Owner.UniqueThread);
        }
        else
        {
            NextThreadId = WctFindSendMessageTarget(&LastCall, HandleToUlong(Information.ClientId.UniqueThread));
            if (!NextThreadId)
                break;
            ZeroMemory(&Nodes[*Count], sizeof(Nodes[*Count]));
            Nodes[*Count].ObjectType = WctSendMessageType;
            Nodes[*Count].ObjectStatus = WctStatusOwned;
            (*Count)++;
        }

        if (!NextThreadId)
            break;

        Thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, NextThreadId);
        if (!Thread || !WctQueryThread(Thread, &Information))
        {
            ZeroMemory(&Nodes[*Count], sizeof(Nodes[*Count]));
            Nodes[*Count].ObjectType = WctThreadType;
            Nodes[*Count].ObjectStatus = WctStatusNoAccess;
            Nodes[*Count].ThreadObject.ThreadId = NextThreadId;
            (*Count)++;
            break;
        }
        if (WctInChain(Nodes, *Count, NextThreadId))
        {
            WctFillThreadNode(&Nodes[(*Count)++], &Information);
            *IsCycle = TRUE;
            break;
        }
        if ((HandleToUlong(Information.ClientId.UniqueProcess) != GetCurrentProcessId()) &&
            !(Flags & WCT_OUT_OF_PROC_FLAG))
        {
            WctFillThreadNode(&Nodes[*Count], &Information);
            Nodes[(*Count)++].ObjectStatus = WctStatusPidOnly;
            break;
        }
    }

    if (Thread)
        CloseHandle(Thread);
    ASSERT(*Count >= 1 && *Count <= WCT_MAX_NODE_COUNT);
    return ERROR_SUCCESS;
}

static
DWORD
WctRunRequest(
    _In_ PWCT_REQUEST Request)
{
    WAITCHAIN_NODE_INFO Nodes[WCT_MAX_NODE_COUNT];
    DWORD Count, Error;
    BOOL Cycle;

    Error = WctBuildChain(Request->Flags, Request->ThreadId, Nodes, &Count, &Cycle);
    *Request->IsCycle = Cycle;
    if (Error == ERROR_INVALID_THREAD_ID)
    {
        *Request->NodeCount = 0;
        return Error;
    }
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Count > *Request->NodeCount)
    {
        *Request->NodeCount = Count;
        return ERROR_MORE_DATA;
    }

    CopyMemory(Request->NodeInfoArray, Nodes, Count * sizeof(Nodes[0]));
    *Request->NodeCount = Count;
    return ERROR_SUCCESS;
}

static
DWORD
WINAPI
WctAsyncWorker(
    _In_ PVOID Parameter)
{
    PWCT_REQUEST Request = Parameter;
    PWCT_SESSION Session = Request->Session;
    DWORD Error;

    Error = WctRunRequest(Request);
    Session->Callback(Session, Request->Context, Error, Request->NodeCount, Request->NodeInfoArray, Request->IsCycle);
    HeapFree(GetProcessHeap(), 0, Request);

    AcquireSRWLockExclusive(&WctPendingLock);
    ASSERT(Session->Pending != 0);
    if (--Session->Pending == 0)
        WakeAllConditionVariable(&WctPendingDone);
    ReleaseSRWLockExclusive(&WctPendingLock);
    return 0;
}

HWCT
WINAPI
OpenThreadWaitChainSession(
    _In_ DWORD Flags,
    _In_opt_ PWAITCHAINCALLBACK Callback)
{
    PWCT_SESSION Session;

    if ((Flags & ~WCTP_OPEN_ALL_FLAGS) || ((Flags & WCT_ASYNC_OPEN_FLAG) && !Callback))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    Session = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Session));
    if (!Session)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    Session->Magic = WCT_SESSION_MAGIC;
    Session->Flags = Flags;
    Session->Callback = Callback;
    SetLastError(ERROR_SUCCESS);
    return Session;
}

VOID
WINAPI
CloseThreadWaitChainSession(
    _In_ HWCT WctHandle)
{
    PWCT_SESSION Session = WctHandle;

    if (!Session || Session->Magic != WCT_SESSION_MAGIC)
        return;

    AcquireSRWLockExclusive(&WctPendingLock);
    while (Session->Pending != 0)
        SleepConditionVariableSRW(&WctPendingDone, &WctPendingLock, INFINITE, 0);
    ReleaseSRWLockExclusive(&WctPendingLock);
    Session->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Session);
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
    WCT_REQUEST Request;
    PWCT_REQUEST Copy;
    DWORD Error, Saved;

    if (!Session || Session->Magic != WCT_SESSION_MAGIC || (Flags & ~WCTP_GETINFO_ALL_FLAGS) ||
        !NodeCount || !NodeInfoArray || !IsCycle || !*NodeCount || *NodeCount > WCT_MAX_NODE_COUNT)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Request.Session = Session;
    Request.Context = Context;
    Request.Flags = Flags;
    Request.ThreadId = ThreadId;
    Request.NodeCount = NodeCount;
    Request.NodeInfoArray = NodeInfoArray;
    Request.IsCycle = IsCycle;

    if (Session->Flags & WCT_ASYNC_OPEN_FLAG)
    {
        Copy = HeapAlloc(GetProcessHeap(), 0, sizeof(*Copy));
        if (!Copy)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        *Copy = Request;
        AcquireSRWLockExclusive(&WctPendingLock);
        Session->Pending++;
        ReleaseSRWLockExclusive(&WctPendingLock);
        if (!QueueUserWorkItem(WctAsyncWorker, Copy, WT_EXECUTEDEFAULT))
        {
            Error = GetLastError();
            HeapFree(GetProcessHeap(), 0, Copy);
            AcquireSRWLockExclusive(&WctPendingLock);
            if (--Session->Pending == 0)
                WakeAllConditionVariable(&WctPendingDone);
            ReleaseSRWLockExclusive(&WctPendingLock);
            SetLastError(Error);
            return FALSE;
        }
        SetLastError(ERROR_IO_PENDING);
        return FALSE;
    }

    Saved = GetLastError();
    *IsCycle = FALSE;
    Error = WctRunRequest(&Request);
    if (Error != ERROR_SUCCESS)
    {
        SetLastError(Error);
        return FALSE;
    }
    SetLastError(Saved);
    return TRUE;
}

VOID
WINAPI
RegisterWaitChainCOMCallback(
    _In_ PCOGETCALLSTATE CallStateCallback,
    _In_ PCOGETACTIVATIONSTATE ActivationStateCallback)
{
    /* TODO: COM call and activation nodes are not traversed; the callbacks are kept for them */
    InterlockedExchangePointer(&WctCallStateCallback, (PVOID)CallStateCallback);
    InterlockedExchangePointer(&WctActivationStateCallback, (PVOID)ActivationStateCallback);
}
