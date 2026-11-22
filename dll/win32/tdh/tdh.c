/*
 * PROJECT:     LiberNT Trace Data Helper
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Event schema lookup for ETW consumers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>

typedef struct _EVENT_RECORD *PEVENT_RECORD;
typedef struct _TDH_CONTEXT *PTDH_CONTEXT;
typedef struct _TRACE_EVENT_INFO *PTRACE_EVENT_INFO;
typedef struct _PROPERTY_DATA_DESCRIPTOR *PPROPERTY_DATA_DESCRIPTOR;

ULONG
WINAPI
TdhGetEventInformation(
    _In_ PEVENT_RECORD Event,
    _In_ ULONG TdhContextCount,
    _In_reads_opt_(TdhContextCount) PTDH_CONTEXT TdhContext,
    _Out_writes_bytes_opt_(*BufferSize) PTRACE_EVENT_INFO Buffer,
    _Inout_ PULONG BufferSize)
{
    if (!Event || !BufferSize || (TdhContextCount && !TdhContext))
        return ERROR_INVALID_PARAMETER;

    return ERROR_NOT_FOUND;
}

static
ULONG
TdhpFindProperty(
    _In_opt_ PEVENT_RECORD Event,
    _In_ ULONG TdhContextCount,
    _In_reads_opt_(TdhContextCount) PTDH_CONTEXT TdhContext,
    _In_ ULONG PropertyDataCount,
    _In_reads_opt_(PropertyDataCount) PPROPERTY_DATA_DESCRIPTOR PropertyData)
{
    if (!Event || (TdhContextCount && !TdhContext) || !PropertyDataCount || !PropertyData)
        return ERROR_INVALID_PARAMETER;

    /* TODO: decode the event with the schema of its provider (manifest, MOF or TraceLogging); none is found yet */
    return ERROR_NOT_FOUND;
}

ULONG
WINAPI
TdhGetPropertySize(
    _In_ PEVENT_RECORD Event,
    _In_ ULONG TdhContextCount,
    _In_reads_opt_(TdhContextCount) PTDH_CONTEXT TdhContext,
    _In_ ULONG PropertyDataCount,
    _In_reads_(PropertyDataCount) PPROPERTY_DATA_DESCRIPTOR PropertyData,
    _Out_ ULONG *PropertySize)
{
    if (!PropertySize)
        return ERROR_INVALID_PARAMETER;

    return TdhpFindProperty(Event, TdhContextCount, TdhContext, PropertyDataCount, PropertyData);
}

ULONG
WINAPI
TdhGetProperty(
    _In_ PEVENT_RECORD Event,
    _In_ ULONG TdhContextCount,
    _In_reads_opt_(TdhContextCount) PTDH_CONTEXT TdhContext,
    _In_ ULONG PropertyDataCount,
    _In_reads_(PropertyDataCount) PPROPERTY_DATA_DESCRIPTOR PropertyData,
    _In_ ULONG BufferSize,
    _Out_writes_bytes_(BufferSize) PBYTE Buffer)
{
    if (!BufferSize || !Buffer)
        return ERROR_INVALID_PARAMETER;

    return TdhpFindProperty(Event, TdhContextCount, TdhContext, PropertyDataCount, PropertyData);
}
