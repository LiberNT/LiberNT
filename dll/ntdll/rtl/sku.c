/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Product SKU queries backed by the shared user data flags
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

BOOLEAN
NTAPI
RtlIsMultiSessionSku(VOID)
{
    return SharedUserData->DbgMultiSessionSku != 0;
}

BOOLEAN
NTAPI
RtlIsMultiUsersInSessionSku(VOID)
{
    return SharedUserData->DbgMultiUsersInSessionSku != 0;
}

BOOLEAN
NTAPI
RtlIsStateSeparationEnabled(VOID)
{
    return SharedUserData->DbgStateSeparationEnabled != 0;
}
