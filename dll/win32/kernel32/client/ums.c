/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode scheduling entry points, unsupported as on Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <k32.h>

BOOL
WINAPI
CreateUmsCompletionList(
    _Outptr_ PVOID *UmsCompletionList)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
CreateUmsThreadContext(
    _Outptr_ PVOID *UmsThread)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
DeleteUmsCompletionList(
    _In_ PVOID UmsCompletionList)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
DeleteUmsThreadContext(
    _In_ PVOID UmsThread)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
DequeueUmsCompletionListItems(
    _In_ PVOID UmsCompletionList,
    _In_ DWORD WaitTimeOut,
    _Out_ PVOID *UmsThreadList)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
EnterUmsSchedulingMode(
    _In_ PVOID SchedulerStartupInfo)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
ExecuteUmsThread(
    _Inout_ PVOID UmsThread)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

PVOID
WINAPI
GetCurrentUmsThread(VOID)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

PVOID
WINAPI
GetNextUmsListItem(
    _Inout_ PVOID UmsContext)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

BOOL
WINAPI
GetUmsCompletionListEvent(
    _In_ PVOID UmsCompletionList,
    _Inout_ PHANDLE UmsCompletionEvent)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
QueryUmsThreadInformation(
    _In_ PVOID UmsThread,
    _In_ ULONG UmsThreadInfoClass,
    _Out_writes_bytes_to_(UmsThreadInformationLength, *ReturnLength) PVOID UmsThreadInformation,
    _In_ ULONG UmsThreadInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
SetUmsThreadInformation(
    _In_ PVOID UmsThread,
    _In_ ULONG UmsThreadInfoClass,
    _In_ PVOID UmsThreadInformation,
    _In_ ULONG UmsThreadInformationLength)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
UmsThreadYield(
    _In_ PVOID SchedulerParam)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
