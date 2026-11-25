/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable check of the members a full display miniport must supply
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

FORCEINLINE
BOOLEAN
DxgkInitDataCoreHasRequiredMembers(
    _In_ const DRIVER_INITIALIZATION_DATA *Data,
    _In_ ULONG Size)
{
    BOOLEAN HasConnectionChange;

    HasConnectionChange =
        Size >= RTL_SIZEOF_THROUGH_FIELD(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryConnectionChange) &&
        Data->DxgkDdiQueryConnectionChange != NULL;

    return Data->DxgkDdiAddDevice != NULL &&
           Data->DxgkDdiStartDevice != NULL &&
           Data->DxgkDdiStopDevice != NULL &&
           Data->DxgkDdiRemoveDevice != NULL &&
           Data->DxgkDdiDispatchIoRequest != NULL &&
           Data->DxgkDdiInterruptRoutine != NULL &&
           Data->DxgkDdiDpcRoutine != NULL &&
           Data->DxgkDdiQueryChildRelations != NULL &&
           (Data->DxgkDdiQueryChildStatus != NULL || HasConnectionChange) &&
           Data->DxgkDdiQueryDeviceDescriptor != NULL &&
           Data->DxgkDdiSetPowerState != NULL &&
           Data->DxgkDdiResetDevice != NULL &&
           Data->DxgkDdiUnload != NULL;
}
