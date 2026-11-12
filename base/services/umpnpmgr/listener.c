/*
 * PROJECT:     LiberNT Plug and Play Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device event listeners behind CM_Register_Notification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

#define LISTENER_MAX_EVENTS 512
/* FIXME: how long Windows waits for an application to answer a query is INFO_NEEDED */
#define LISTENER_QUERY_TIMEOUT 30000

typedef struct _LISTENER_EVENT
{
    LIST_ENTRY ListEntry;
    DWORD Code;
    PWSTR Path;
    PBYTE Data;
    UINT Size;
    ULONG Query;
} LISTENER_EVENT, *PLISTENER_EVENT;

typedef struct _LISTENER
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY Events;
    ULONG EventCount;
    HANDLE Signal;
    BOOL Registered;
    ULONG Asked;
    ULONG Delivered;
    ULONG Answered;
    DWORD Answer;
} LISTENER, *PLISTENER;

static CRITICAL_SECTION ListenerLock;
static LIST_ENTRY ListenerHead;
static CRITICAL_SECTION ListenerQueryLock;
static HANDLE ListenerAnswered;
static ULONG ListenerQuery;

static
VOID
FreeListenerEvent(
    _In_ PLISTENER_EVENT Event)
{
    midl_user_free(Event->Path);
    midl_user_free(Event->Data);
    HeapFree(GetProcessHeap(), 0, Event);
}

static
VOID
DropListenerEvents(
    _In_ PLISTENER Listener)
{
    while (!IsListEmpty(&Listener->Events))
        FreeListenerEvent(CONTAINING_RECORD(RemoveHeadList(&Listener->Events), LISTENER_EVENT, ListEntry));
    Listener->EventCount = 0;
}

static
VOID
UnlinkListener(
    _In_ PLISTENER Listener)
{
    if (Listener->Registered)
    {
        RemoveEntryList(&Listener->ListEntry);
        Listener->Registered = FALSE;
        if (ListenerAnswered != NULL)
            SetEvent(ListenerAnswered);
    }
    DropListenerEvents(Listener);
}

VOID
InitializeListeners(VOID)
{
    InitializeCriticalSection(&ListenerLock);
    InitializeListHead(&ListenerHead);
    InitializeCriticalSection(&ListenerQueryLock);
    ListenerAnswered = CreateEventW(NULL, FALSE, FALSE, NULL);
}

/* Called with the listener lock held; a query asks only the listeners it reaches */
static
VOID
QueueListenerEvents(
    _In_ DWORD Code,
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const BYTE *Data,
    _In_ UINT Size,
    _In_ ULONG Query)
{
    SIZE_T PathSize = (wcslen(Path) + 1) * sizeof(WCHAR);
    PLIST_ENTRY Current;

    for (Current = ListenerHead.Flink; Current != &ListenerHead; Current = Current->Flink)
    {
        PLISTENER Listener = CONTAINING_RECORD(Current, LISTENER, ListEntry);
        PLISTENER_EVENT Event;

        Event = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Event));
        if (Event == NULL)
            continue;
        Event->Code = Code;
        Event->Query = Query;
        Event->Size = Size;
        Event->Path = midl_user_allocate(PathSize);
        Event->Data = midl_user_allocate(Size);
        if (Event->Path == NULL || Event->Data == NULL)
        {
            FreeListenerEvent(Event);
            continue;
        }
        CopyMemory(Event->Path, Path, PathSize);
        CopyMemory(Event->Data, Data, Size);

        if (Listener->EventCount >= LISTENER_MAX_EVENTS)
        {
            FreeListenerEvent(CONTAINING_RECORD(RemoveHeadList(&Listener->Events), LISTENER_EVENT, ListEntry));
            Listener->EventCount--;
        }
        InsertTailList(&Listener->Events, &Event->ListEntry);
        Listener->EventCount++;
        if (Query != 0)
            Listener->Asked = Query;
        SetEvent(Listener->Signal);
    }
}

VOID
BroadcastListenerEvent(
    _In_ DWORD Code,
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const BYTE *Data,
    _In_ UINT Size)
{
    EnterCriticalSection(&ListenerLock);
    QueueListenerEvents(Code, Path, Data, Size, 0);
    LeaveCriticalSection(&ListenerLock);
}

/* Every listener answers for the registrations of its process; one that denies keeps the device */
BOOL
QueryListeners(
    _In_ DWORD Code,
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const BYTE *Data,
    _In_ UINT Size)
{
    PLIST_ENTRY Current;
    BOOL Waiting, Allowed = TRUE;
    DWORD Start, Elapsed;
    ULONG Query;

    EnterCriticalSection(&ListenerQueryLock);

    EnterCriticalSection(&ListenerLock);
    Query = ++ListenerQuery;
    if (Query == 0)
        Query = ++ListenerQuery;
    QueueListenerEvents(Code, Path, Data, Size, Query);
    LeaveCriticalSection(&ListenerLock);

    Start = GetTickCount();
    for (;;)
    {
        Waiting = FALSE;
        EnterCriticalSection(&ListenerLock);
        for (Current = ListenerHead.Flink; Current != &ListenerHead; Current = Current->Flink)
        {
            PLISTENER Listener = CONTAINING_RECORD(Current, LISTENER, ListEntry);

            if (Listener->Asked != Query)
                continue;
            if (Listener->Answered != Query)
                Waiting = TRUE;
            else if (Listener->Answer == BROADCAST_QUERY_DENY)
                Allowed = FALSE;
        }
        LeaveCriticalSection(&ListenerLock);

        Elapsed = GetTickCount() - Start;
        if (!Waiting || !Allowed || Elapsed >= LISTENER_QUERY_TIMEOUT)
            break;
        WaitForSingleObject(ListenerAnswered, LISTENER_QUERY_TIMEOUT - Elapsed);
    }

    if (Waiting && Allowed)
        DPRINT1("A listener did not answer query 0x%lx for %S\n", Code, Path);

    LeaveCriticalSection(&ListenerQueryLock);
    return Allowed;
}

plugplay_rpc_handle
__cdecl
plugplay_register_listener(VOID)
{
    PLISTENER Listener;

    Listener = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Listener));
    if (Listener == NULL)
        return NULL;
    Listener->Signal = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Listener->Signal == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Listener);
        return NULL;
    }
    InitializeListHead(&Listener->Events);

    EnterCriticalSection(&ListenerLock);
    InsertTailList(&ListenerHead, &Listener->ListEntry);
    Listener->Registered = TRUE;
    LeaveCriticalSection(&ListenerLock);
    return Listener;
}

DWORD
__cdecl
plugplay_get_event(
    plugplay_rpc_handle Handle,
    WCHAR **Path,
    BYTE **Data,
    unsigned int *Size)
{
    PLISTENER Listener = Handle;
    PLISTENER_EVENT Event;
    DWORD Code;

    *Path = NULL;
    *Data = NULL;
    *Size = 0;
    for (;;)
    {
        EnterCriticalSection(&ListenerLock);
        if (!Listener->Registered)
        {
            LeaveCriticalSection(&ListenerLock);
            RpcRaiseException(ERROR_INVALID_HANDLE);
        }
        if (!IsListEmpty(&Listener->Events))
            break;
        LeaveCriticalSection(&ListenerLock);
        WaitForSingleObject(Listener->Signal, INFINITE);
    }
    Event = CONTAINING_RECORD(RemoveHeadList(&Listener->Events), LISTENER_EVENT, ListEntry);
    Listener->EventCount--;
    if (Event->Query != 0)
        Listener->Delivered = Event->Query;
    LeaveCriticalSection(&ListenerLock);

    Code = Event->Code;
    *Path = Event->Path;
    *Data = Event->Data;
    *Size = Event->Size;
    HeapFree(GetProcessHeap(), 0, Event);
    return Code;
}

void
__cdecl
plugplay_unregister_listener(
    plugplay_rpc_handle Handle)
{
    PLISTENER Listener = Handle;

    EnterCriticalSection(&ListenerLock);
    UnlinkListener(Listener);
    SetEvent(Listener->Signal);
    LeaveCriticalSection(&ListenerLock);
}

void
__cdecl
plugplay_send_event(
    const WCHAR *Path,
    DWORD EventCode,
    const BYTE *Data,
    unsigned int Size)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(EventCode);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Size);
    RpcRaiseException(ERROR_ACCESS_DENIED);
}

void
__cdecl
plugplay_answer_event(
    plugplay_rpc_handle Handle,
    DWORD Result)
{
    PLISTENER Listener = Handle;

    EnterCriticalSection(&ListenerLock);
    if (Listener->Registered && Listener->Delivered != 0)
    {
        Listener->Answered = Listener->Delivered;
        Listener->Answer = Result;
        Listener->Delivered = 0;
        SetEvent(ListenerAnswered);
    }
    LeaveCriticalSection(&ListenerLock);
}

void
__RPC_USER
plugplay_rpc_handle_rundown(
    plugplay_rpc_handle Handle)
{
    PLISTENER Listener = Handle;

    EnterCriticalSection(&ListenerLock);
    UnlinkListener(Listener);
    LeaveCriticalSection(&ListenerLock);
    CloseHandle(Listener->Signal);
    HeapFree(GetProcessHeap(), 0, Listener);
}
