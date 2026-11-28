/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device management registration queries for modern applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

HRESULT
WINAPI
IsDeviceRegisteredWithManagement(
    _Out_ BOOL *IsManaged,
    _In_ DWORD UPNSize,
    _In_reads_opt_(UPNSize) PWSTR UPN)
{
    UNREFERENCED_PARAMETER(UPNSize);
    UNREFERENCED_PARAMETER(UPN);

    if (IsManaged != NULL)
        *IsManaged = FALSE;
    return S_OK;
}
