/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6.30 selective suspend idle notification completions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

VOID
EXPORT
NdisMIdleNotificationComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

VOID
EXPORT
NdisMIdleNotificationConfirm(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_DEVICE_POWER_STATE IdlePowerState)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(IdlePowerState);
}

/* EOF */
