/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     advapi32.dll Event tracing stubs
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 */

#include <advapi32.h>
#include <wmistr.h>
#include <evntrace.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);


TRACEHANDLE
WINAPI
OpenTraceA(IN PEVENT_TRACE_LOGFILEA Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

TRACEHANDLE
WINAPI
OpenTraceW(IN PEVENT_TRACE_LOGFILEW Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

ULONG
WINAPI
ProcessTrace(IN PTRACEHANDLE HandleArray,
             IN ULONG HandleCount,
             IN LPFILETIME StartTime,
             IN LPFILETIME EndTime)
{
    UNIMPLEMENTED;
    return ERROR_NOACCESS;
}

ULONG
WINAPI
CloseTrace(IN TRACEHANDLE TraceHandle)
{
    return ERROR_INVALID_HANDLE;
}

ULONG
WINAPI
EnableTraceEx2(IN TRACEHANDLE TraceHandle,
               IN LPCGUID ProviderId,
               IN ULONG ControlCode,
               IN UCHAR Level,
               IN ULONGLONG MatchAnyKeyword,
               IN ULONGLONG MatchAllKeyword,
               IN ULONG Timeout,
               IN PENABLE_TRACE_PARAMETERS EnableParameters OPTIONAL)
{
    if (!TraceHandle || !ProviderId)
        return ERROR_INVALID_PARAMETER;

    if (ControlCode != EVENT_CONTROL_CODE_DISABLE_PROVIDER &&
        ControlCode != EVENT_CONTROL_CODE_ENABLE_PROVIDER &&
        ControlCode != EVENT_CONTROL_CODE_CAPTURE_STATE)
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (EnableParameters && EnableParameters->Version != ENABLE_TRACE_PARAMETERS_VERSION &&
        EnableParameters->Version != ENABLE_TRACE_PARAMETERS_VERSION_2)
    {
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}
