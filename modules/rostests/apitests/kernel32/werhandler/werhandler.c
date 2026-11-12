/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runtime exception handler loaded by Windows Error Reporting for the crash report test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <strsafe.h>
#include <werapi.h>
#include "werhandler.h"

static
BOOL
ReadContext(
    _In_ PVOID Context,
    _In_ const PWER_RUNTIME_EXCEPTION_INFORMATION Information,
    _Out_ PWER_HANDLER_CONTEXT Copy)
{
    return ReadProcessMemory(Information->hProcess, Context, Copy, sizeof(*Copy), NULL) &&
           Copy->Magic == WER_HANDLER_MAGIC;
}

static
PWER_HANDLER_RESULT
OpenResult(
    _In_ PWER_HANDLER_CONTEXT Copy,
    _Out_ HANDLE *Mapping)
{
    PWER_HANDLER_RESULT Result;

    Copy->MappingName[ARRAYSIZE(Copy->MappingName) - 1] = UNICODE_NULL;
    *Mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, Copy->MappingName);
    if (!*Mapping)
        return NULL;

    Result = MapViewOfFile(*Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Result));
    if (!Result)
        CloseHandle(*Mapping);
    return Result;
}

static
VOID
CloseResult(
    _In_ PWER_HANDLER_RESULT Result,
    _In_ HANDLE Mapping)
{
    UnmapViewOfFile(Result);
    CloseHandle(Mapping);
}

HRESULT
OutOfProcessExceptionEventCallback(
    _In_ PVOID pContext,
    _In_ const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
    _Out_ BOOL *pbOwnershipClaimed,
    _Out_writes_(*pchSize) PWSTR pwszEventName,
    _Inout_ PDWORD pchSize,
    _Out_ PDWORD pdwSignatureCount)
{
    WER_HANDLER_CONTEXT Copy;
    PWER_HANDLER_RESULT Result;
    HANDLE Mapping;

    *pbOwnershipClaimed = FALSE;
    if (!ReadContext(pContext, pExceptionInformation, &Copy))
        return S_OK;

    Result = OpenResult(&Copy, &Mapping);
    if (!Result)
        return S_OK;

    InterlockedIncrement(&Result->EventCalls);
    Result->ContextRead = TRUE;
    Result->Context = (ULONG_PTR)pContext;
    Result->InfoSize = pExceptionInformation->dwSize;
    Result->ProcessId = GetProcessId(pExceptionInformation->hProcess);
    Result->ThreadId = GetThreadId(pExceptionInformation->hThread);
    Result->ExceptionCode = pExceptionInformation->exceptionRecord.ExceptionCode;
    Result->ExceptionAddress = (ULONG_PTR)pExceptionInformation->exceptionRecord.ExceptionAddress;
    Result->EventNameSize = *pchSize;
    if (pExceptionInformation->pwszReportId)
        StringCchCopyW(Result->ReportId, ARRAYSIZE(Result->ReportId), pExceptionInformation->pwszReportId);

    if (Copy.Mode != WER_HANDLER_MODE_NO_CLAIM &&
        SUCCEEDED(StringCchPrintfW(pwszEventName, *pchSize, L"LntWerCrash%lu", Result->ProcessId)))
    {
        *pbOwnershipClaimed = TRUE;
        *pdwSignatureCount = 2;
    }

    CloseResult(Result, Mapping);
    return S_OK;
}

HRESULT
OutOfProcessExceptionEventSignatureCallback(
    _In_ PVOID pContext,
    _In_ const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
    _In_ DWORD dwIndex,
    _Out_writes_(*pchName) PWSTR pwszName,
    _Inout_ PDWORD pchName,
    _Out_writes_(*pchValue) PWSTR pwszValue,
    _Inout_ PDWORD pchValue)
{
    WER_HANDLER_CONTEXT Copy;
    PWER_HANDLER_RESULT Result;
    HANDLE Mapping;
    HRESULT Status;

    if (!ReadContext(pContext, pExceptionInformation, &Copy))
        return E_FAIL;

    Result = OpenResult(&Copy, &Mapping);
    if (!Result)
        return E_FAIL;

    if ((ULONG)Result->SignatureCalls != dwIndex)
        Result->SignatureOrderErrors++;
    InterlockedIncrement(&Result->SignatureCalls);
    Result->SignatureIndexMask |= 1UL << (dwIndex & 31);

    if (dwIndex == 0)
    {
        Status = StringCchCopyW(pwszName, *pchName, L"HandlerMode");
        if (SUCCEEDED(Status))
            Status = StringCchPrintfW(pwszValue, *pchValue, L"%lu", Copy.Mode);
    }
    else
    {
        Status = StringCchCopyW(pwszName, *pchName, L"HandlerProcess");
        if (SUCCEEDED(Status))
            Status = StringCchPrintfW(pwszValue, *pchValue, L"%lu", GetProcessId(pExceptionInformation->hProcess));
    }

    CloseResult(Result, Mapping);
    return Status;
}

HRESULT
OutOfProcessExceptionEventDebuggerLaunchCallback(
    _In_ PVOID pContext,
    _In_ const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
    _Out_ PBOOL pbIsCustomDebugger,
    _Out_writes_(*pchDebuggerLaunch) PWSTR pwszDebuggerLaunch,
    _Inout_ PDWORD pchDebuggerLaunch,
    _Out_ PBOOL pbIsDebuggerAutolaunch)
{
    WER_HANDLER_CONTEXT Copy;
    PWER_HANDLER_RESULT Result;
    HANDLE Mapping;

    *pbIsCustomDebugger = FALSE;
    *pbIsDebuggerAutolaunch = FALSE;
    if (!ReadContext(pContext, pExceptionInformation, &Copy))
        return S_OK;

    Result = OpenResult(&Copy, &Mapping);
    if (!Result)
        return S_OK;

    InterlockedIncrement(&Result->DebuggerCalls);
    Copy.DebuggerLaunch[ARRAYSIZE(Copy.DebuggerLaunch) - 1] = UNICODE_NULL;
    if (Copy.Mode == WER_HANDLER_MODE_DEBUGGER &&
        SUCCEEDED(StringCchCopyW(pwszDebuggerLaunch, *pchDebuggerLaunch, Copy.DebuggerLaunch)))
    {
        *pbIsCustomDebugger = TRUE;
        *pbIsDebuggerAutolaunch = TRUE;
    }

    CloseResult(Result, Mapping);
    return S_OK;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}
