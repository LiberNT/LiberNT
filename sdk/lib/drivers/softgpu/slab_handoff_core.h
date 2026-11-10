/*
 * PROJECT:     LiberNT WDDM Software GPU Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hand a stopped adapter's contiguous slab to an adapter that is starting
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#ifndef _SOFTGPU_SLAB_HANDOFF_CORE_H_
#define _SOFTGPU_SLAB_HANDOFF_CORE_H_

#include <ntddk.h>

typedef struct _SOFTGPU_SLAB
{
    PVOID Base;
    SIZE_T Size;
    PHYSICAL_ADDRESS Physical;
    MEMORY_CACHING_TYPE CacheType;
} SOFTGPU_SLAB, *PSOFTGPU_SLAB;

typedef struct _SOFTGPU_SLAB_HANDOFF
{
    LONG StartsInProgress;
    SOFTGPU_SLAB Parked;
} SOFTGPU_SLAB_HANDOFF, *PSOFTGPU_SLAB_HANDOFF;

FORCEINLINE
BOOLEAN
SoftGpuSlabIsEmpty(
    _In_ const SOFTGPU_SLAB *Slab)
{
    ASSERT((Slab->Base == NULL) == (Slab->Size == 0));
    return Slab->Base == NULL;
}

FORCEINLINE
VOID
SoftGpuSlabHandoffBeginStart(
    _Inout_ PSOFTGPU_SLAB_HANDOFF Handoff)
{
    ASSERT(Handoff->StartsInProgress >= 0);
    Handoff->StartsInProgress++;
}

FORCEINLINE
BOOLEAN
SoftGpuSlabHandoffEndStart(
    _Inout_ PSOFTGPU_SLAB_HANDOFF Handoff,
    _Out_ PSOFTGPU_SLAB Release)
{
    ASSERT(Handoff->StartsInProgress > 0);
    RtlZeroMemory(Release, sizeof(*Release));
    if (--Handoff->StartsInProgress != 0 || SoftGpuSlabIsEmpty(&Handoff->Parked))
        return FALSE;

    *Release = Handoff->Parked;
    RtlZeroMemory(&Handoff->Parked, sizeof(Handoff->Parked));
    return TRUE;
}

FORCEINLINE
BOOLEAN
SoftGpuSlabHandoffPark(
    _Inout_ PSOFTGPU_SLAB_HANDOFF Handoff,
    _In_ const SOFTGPU_SLAB *Slab)
{
    ASSERT(!SoftGpuSlabIsEmpty(Slab));
    ASSERT(Handoff->StartsInProgress >= 0);
    if (Handoff->StartsInProgress == 0 || !SoftGpuSlabIsEmpty(&Handoff->Parked))
        return FALSE;

    Handoff->Parked = *Slab;
    return TRUE;
}

FORCEINLINE
BOOLEAN
SoftGpuSlabFits(
    _In_ const SOFTGPU_SLAB *Slab,
    _In_ SIZE_T Size,
    _In_ ULONGLONG HighestAddress,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    ULONGLONG First = (ULONGLONG)Slab->Physical.QuadPart;

    return Slab->Size >= Size &&
           Slab->CacheType == CacheType &&
           (HighestAddress == 0 ||
            (First <= HighestAddress && Slab->Size - 1 <= HighestAddress - First));
}

FORCEINLINE
BOOLEAN
SoftGpuSlabHandoffTake(
    _Inout_ PSOFTGPU_SLAB_HANDOFF Handoff,
    _In_ SIZE_T Size,
    _In_ ULONGLONG HighestAddress,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _Out_ PSOFTGPU_SLAB Adopted)
{
    ASSERT(Size != 0);
    RtlZeroMemory(Adopted, sizeof(*Adopted));
    if (SoftGpuSlabIsEmpty(&Handoff->Parked) ||
        !SoftGpuSlabFits(&Handoff->Parked, Size, HighestAddress, CacheType))
    {
        return FALSE;
    }

    *Adopted = Handoff->Parked;
    RtlZeroMemory(&Handoff->Parked, sizeof(Handoff->Parked));
    return TRUE;
}

#endif /* _SOFTGPU_SLAB_HANDOFF_CORE_H_ */
