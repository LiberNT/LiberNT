/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VC++ 2019+ exception handler module
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

BOOL
WINAPI
DllMainCRTStartup(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}

/* FIXME: decode the v4 funclet tables; until then let the unwind continue */
EXCEPTION_DISPOSITION
WINAPI
__CxxFrameHandler4(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ ULONG64 EstablisherFrame,
    _In_ PCONTEXT ContextRecord,
    _In_ PDISPATCHER_CONTEXT DispatcherContext)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(DispatcherContext);
    return ExceptionContinueSearch;
}
