/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the registration of event trace class callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wmistr.h>
#include <evntrace.h>

typedef ULONG (WINAPI *PFN_SET_TRACE_CALLBACK)(LPCGUID, PEVENT_CALLBACK);
typedef ULONG (WINAPI *PFN_REMOVE_TRACE_CALLBACK)(LPCGUID);

static const GUID TraceClass = { 0x5d6b2a71, 0x3c40, 0x4e2d, { 0x9b, 0x61, 0x2e, 0x7c, 0x0f, 0x45, 0xa8, 0x13 } };

static VOID WINAPI
FirstCallback(PEVENT_TRACE Event)
{
    UNREFERENCED_PARAMETER(Event);
}

static VOID WINAPI
SecondCallback(PEVENT_TRACE Event)
{
    UNREFERENCED_PARAMETER(Event);
}

static void
TestCallbacks(PCSTR Module, PFN_SET_TRACE_CALLBACK pSetTraceCallback, PFN_REMOVE_TRACE_CALLBACK pRemoveTraceCallback)
{
    ok_long(pSetTraceCallback(NULL, FirstCallback), ERROR_INVALID_PARAMETER);
    ok_long(pSetTraceCallback(&TraceClass, NULL), ERROR_INVALID_PARAMETER);
    ok_long(pRemoveTraceCallback(NULL), ERROR_INVALID_PARAMETER);
    ok_long(pRemoveTraceCallback(&TraceClass), ERROR_WMI_GUID_NOT_FOUND);

    ok_long(pSetTraceCallback(&TraceClass, FirstCallback), ERROR_SUCCESS);
    ok_long(pSetTraceCallback(&TraceClass, SecondCallback), ERROR_SUCCESS);
    ok_long(pRemoveTraceCallback(&TraceClass), ERROR_SUCCESS);
    ok(pRemoveTraceCallback(&TraceClass) == ERROR_WMI_GUID_NOT_FOUND, "%s: a replaced callback left a second registration\n", Module);
}

START_TEST(TraceCallback)
{
    static const PCSTR Modules[] = { "advapi32.dll", "sechost.dll", "api-ms-win-eventing-obsolete-l1-1-0.dll" };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Modules); Index++)
    {
        HMODULE Module = LoadLibraryA(Modules[Index]);
        PFN_SET_TRACE_CALLBACK pSetTraceCallback;
        PFN_REMOVE_TRACE_CALLBACK pRemoveTraceCallback;

        ok(Module != NULL, "LoadLibrary(%s) failed: %lu\n", Modules[Index], GetLastError());
        if (Module == NULL)
            continue;
        pSetTraceCallback = (PFN_SET_TRACE_CALLBACK)GetProcAddress(Module, "SetTraceCallback");
        pRemoveTraceCallback = (PFN_REMOVE_TRACE_CALLBACK)GetProcAddress(Module, "RemoveTraceCallback");
        ok(pSetTraceCallback != NULL && pRemoveTraceCallback != NULL, "%s does not export the trace callback functions\n", Modules[Index]);
        if (pSetTraceCallback != NULL && pRemoveTraceCallback != NULL)
            TestCallbacks(Modules[Index], pSetTraceCallback, pRemoveTraceCallback);
        FreeLibrary(Module);
    }
}
