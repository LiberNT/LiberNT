/*
 * PROJECT:     LiberNT Security and Host Services
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Event trace class callbacks registered by trace consumers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wmistr.h"
#define _WMI_SOURCE_
#include "evntrace.h"

typedef struct _TRACE_CLASS_CALLBACK
{
    struct _TRACE_CLASS_CALLBACK *Next;
    GUID ClassGuid;
    PEVENT_CALLBACK Callback;
} TRACE_CLASS_CALLBACK, *PTRACE_CLASS_CALLBACK;

/* TODO: ProcessTrace does not deliver events yet, so no registered class callback is called */
static SRWLOCK TraceClassLock = SRWLOCK_INIT;
static PTRACE_CLASS_CALLBACK TraceClassCallbacks;

static PTRACE_CLASS_CALLBACK *
FindTraceClassLocked(const GUID *ClassGuid)
{
    PTRACE_CLASS_CALLBACK *Link;

    for (Link = &TraceClassCallbacks; *Link != NULL; Link = &(*Link)->Next)
    {
        if (IsEqualGUID(&(*Link)->ClassGuid, ClassGuid))
            return Link;
    }
    return Link;
}

ULONG
WINAPI
SetTraceCallback(
    _In_ LPCGUID pGuid,
    _In_ PEVENT_CALLBACK EventCallback)
{
    PTRACE_CLASS_CALLBACK New, *Link;

    if (pGuid == NULL || EventCallback == NULL)
        return ERROR_INVALID_PARAMETER;

    New = HeapAlloc(GetProcessHeap(), 0, sizeof(*New));
    if (New == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    New->ClassGuid = *pGuid;
    New->Callback = EventCallback;

    /* One callback per class: the last one registered receives the events */
    AcquireSRWLockExclusive(&TraceClassLock);
    Link = FindTraceClassLocked(pGuid);
    if (*Link != NULL)
    {
        (*Link)->Callback = EventCallback;
        ReleaseSRWLockExclusive(&TraceClassLock);
        HeapFree(GetProcessHeap(), 0, New);
        return ERROR_SUCCESS;
    }
    New->Next = NULL;
    *Link = New;
    ReleaseSRWLockExclusive(&TraceClassLock);
    return ERROR_SUCCESS;
}

ULONG
WINAPI
RemoveTraceCallback(
    _In_ LPCGUID pGuid)
{
    PTRACE_CLASS_CALLBACK Entry, *Link;

    if (pGuid == NULL)
        return ERROR_INVALID_PARAMETER;

    AcquireSRWLockExclusive(&TraceClassLock);
    Link = FindTraceClassLocked(pGuid);
    Entry = *Link;
    if (Entry != NULL)
        *Link = Entry->Next;
    ReleaseSRWLockExclusive(&TraceClassLock);

    if (Entry == NULL)
        return ERROR_WMI_GUID_NOT_FOUND;
    HeapFree(GetProcessHeap(), 0, Entry);
    return ERROR_SUCCESS;
}
