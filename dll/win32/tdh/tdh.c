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
