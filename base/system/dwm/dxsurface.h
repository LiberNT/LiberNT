/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMT shared-surface consumer declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <windows.h>
#include <reactos/dwmframe.h>

#ifdef __cplusplus
extern "C" {
#endif

const BYTE *DwmDxGetSurfaceSnapshot(const DWM_WIN *Window);
const BYTE *DwmDxGetSurfacePixels(const DWM_WIN *Window, ULONG *Pitch);
BOOL DwmDxSurfaceReadBack(const DWM_WIN *Window);
const BYTE *DwmDxReadbackSurface(const DWM_WIN *Window, ULONG *Pitch);
BOOL DwmDxReadbackCurrent(const DWM_WIN *Window);
void DwmDxReadbackCleanup(void);
const BYTE *DwmDxGetRedirectionSnapshot(const DWM_WIN *Window);
void DwmDxAcknowledgeSurface(const DWM_WIN *Window);
void DwmDxHoldFrames(const DWM_WIN *Windows, ULONG Count);
void DwmDxReleaseRetiredFrames(void);
void DwmDxSetScanoutSurfaces(const ULONG *SurfaceIds, ULONG Count);
void DwmDxSweepSurfaces(const DWM_WIN *Windows, ULONG Count);
void DwmDxCleanupSurfaces(void);
void DwmDxReleaseRedirectionMappings(void);
BOOL DwmDxWaitForVBlank(void);

#ifdef __cplusplus
}
#endif
