/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     CheckElevationEnabled
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "k32_api.h"

DWORD
WINAPI
CheckElevationEnabled(
    _Out_ PBOOL ElevationEnabled)
{
    *ElevationEnabled = SharedUserData->DbgElevationEnabled ? TRUE : FALSE;
    return ERROR_SUCCESS;
}
