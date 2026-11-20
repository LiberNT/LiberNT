/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidMm allocation destruction rules
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifndef _DXGK_VIDMM_DESTROY_CORE_H_
#define _DXGK_VIDMM_DESTROY_CORE_H_

#include <ntddk.h>
#include <windef.h>
#include <d3dkmthk.h>

/* AssumeNotInUse: the caller vouches that no queued command uses the allocations, so they go at once. */
FORCEINLINE
BOOLEAN
DxgkVidMmCoreDestroyWaitsForQueuedWork(
    _In_ D3DDDICB_DESTROYALLOCATION2FLAGS Flags)
{
    return Flags.AssumeNotInUse ? FALSE : TRUE;
}

#endif /* _DXGK_VIDMM_DESTROY_CORE_H_ */
