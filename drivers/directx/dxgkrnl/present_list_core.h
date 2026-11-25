/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable binding of a GPU-VA Present's allocation list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* A GPU-VA Present still hands the miniport the DMA buffer's DXGK_ALLOCATIONLIST; DXGK_PRESENTALLOCATIONINFO is reserved for the system */
FORCEINLINE
VOID
DxgkPresentListCoreBindVirtualAddresses(
    _Inout_updates_(DXGK_PRESENT_MAX_INDEX + 1) DXGK_ALLOCATIONLIST *AllocationList,
    _In_ BOOLEAN Flip,
    _In_ BOOLEAN SourceMapped,
    _In_ D3DGPU_VIRTUAL_ADDRESS SourceVirtualAddress,
    _In_ BOOLEAN DestinationMapped,
    _In_ D3DGPU_VIRTUAL_ADDRESS DestinationVirtualAddress)
{
    if (Flip)
        return;
    if (SourceMapped)
        AllocationList[DXGK_PRESENT_SOURCE_INDEX].VirtualAddress = SourceVirtualAddress;
    if (DestinationMapped)
        AllocationList[DXGK_PRESENT_DESTINATION_INDEX].VirtualAddress = DestinationVirtualAddress;
}
