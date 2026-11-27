/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process DLL search directories
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "k32_api.h"

DLL_DIRECTORY_COOKIE
WINAPI
AddDllDirectory(
    _In_ PCWSTR NewDirectory)
{
    UNICODE_STRING Directory;
    PVOID Cookie;
    NTSTATUS Status;

    if (!NewDirectory)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    RtlInitUnicodeString(&Directory, NewDirectory);
    Status = LdrAddDllDirectory(&Directory, &Cookie);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }
    return Cookie;
}

BOOL
WINAPI
RemoveDllDirectory(
    _In_ DLL_DIRECTORY_COOKIE Cookie)
{
    NTSTATUS Status = LdrRemoveDllDirectory(Cookie);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
