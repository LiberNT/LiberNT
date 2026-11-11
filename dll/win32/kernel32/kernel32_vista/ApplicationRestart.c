/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Application restart registration support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <k32_vista.h>
#include <basewer.h>

#ifndef RESTART_MAX_CMD_LINE
#define RESTART_MAX_CMD_LINE 1024
#endif
#ifndef RESTART_NO_CRASH
#define RESTART_NO_CRASH  0x1
#define RESTART_NO_HANG   0x2
#define RESTART_NO_PATCH  0x4
#define RESTART_NO_REBOOT 0x8
#endif

HRESULT
WINAPI
RegisterApplicationRestart(
    _In_opt_ PCWSTR pwzCommandline,
    _In_ DWORD dwFlags)
{
    PBASE_WER_REGISTRATION Registration;
    PWSTR CommandLine;
    SIZE_T Length = 0;

    if (dwFlags & ~(RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_PATCH | RESTART_NO_REBOOT))
        return E_INVALIDARG;

    if (pwzCommandline != NULL)
    {
        Length = wcslen(pwzCommandline);
        if (Length >= RESTART_MAX_CMD_LINE)
            return E_INVALIDARG;
    }

    Registration = BaseWerGetRegistration(TRUE);
    if (!Registration)
        return E_FAIL;

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    CommandLine = (PWSTR)(ULONG_PTR)Registration->RestartCommandLine;
    if (!CommandLine)
    {
        CommandLine = RtlAllocateHeap(RtlGetProcessHeap(), 0, RESTART_MAX_CMD_LINE * sizeof(WCHAR));
        if (!CommandLine)
        {
            RtlReleaseSRWLockExclusive(&Registration->Lock);
            return E_FAIL;
        }
        Registration->RestartCommandLine = (ULONG_PTR)CommandLine;
    }

    InterlockedAnd(&Registration->State, ~BASE_WER_RESTART_REGISTERED);
    if (Length)
        RtlCopyMemory(CommandLine, pwzCommandline, Length * sizeof(WCHAR));
    CommandLine[Length] = UNICODE_NULL;
    Registration->RestartLength = (ULONG)Length;
    Registration->RestartFlags = dwFlags;
    InterlockedOr(&Registration->State, BASE_WER_RESTART_REGISTERED);
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    return S_OK;
}

HRESULT
WINAPI
UnregisterApplicationRestart(VOID)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);

    if (!Registration)
        return S_OK;

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    InterlockedAnd(&Registration->State, ~BASE_WER_RESTART_REGISTERED);
    Registration->RestartLength = 0;
    Registration->RestartFlags = 0;
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    return S_OK;
}

HRESULT
WINAPI
GetApplicationRestartSettings(
    _In_ HANDLE hProcess,
    _Out_writes_opt_(*pcchSize) PWSTR pwzCommandline,
    _Inout_ PDWORD pcchSize,
    _Out_opt_ PDWORD pdwFlags)
{
    BASE_WER_REGISTRATION Registration;
    DWORD Needed;
    HRESULT Result;

    if (pcchSize == NULL || (pwzCommandline == NULL && *pcchSize != 0))
        return E_INVALIDARG;

    Result = BaseWerQueryRegistration(hProcess, &Registration);
    if (FAILED(Result))
        return Result;

    if (!(Registration.State & BASE_WER_RESTART_REGISTERED) ||
        !Registration.RestartCommandLine ||
        Registration.RestartLength >= RESTART_MAX_CMD_LINE)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    Needed = Registration.RestartLength + 1;
    if (pwzCommandline != NULL)
    {
        if (*pcchSize < Needed)
        {
            *pcchSize = Needed;
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }

        Result = BaseWerReadRegistrationMemory(hProcess,
                                               Registration.RestartCommandLine,
                                               pwzCommandline,
                                               Needed * sizeof(WCHAR));
        if (FAILED(Result))
            return Result;
        pwzCommandline[Needed - 1] = UNICODE_NULL;
    }

    *pcchSize = Needed;
    if (pdwFlags != NULL)
        *pdwFlags = Registration.RestartFlags;
    return S_OK;
}
