/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SetDefaultDllDirectories
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "k32_api.h"

BOOL
WINAPI
SetDefaultDllDirectories(
    _In_ DWORD DirectoryFlags)
{
    NTSTATUS Status = LdrSetDefaultDllDirectories(DirectoryFlags);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
