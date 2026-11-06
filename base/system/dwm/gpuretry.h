/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Paces the attempts to bring back a Direct3D compositor that failed to start
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#define DWM_GPU_RETRY_FIRST_MS 200u
#define DWM_GPU_RETRY_MAX_DOUBLINGS 6u

static __inline DWORD
DwmGpuRetryDelay(ULONG Failures)
{
    return DWM_GPU_RETRY_FIRST_MS << (Failures < DWM_GPU_RETRY_MAX_DOUBLINGS ? Failures : DWM_GPU_RETRY_MAX_DOUBLINGS);
}

static __inline BOOL
DwmGpuRetryDue(DWORD Now, DWORD Last, ULONG Failures)
{
    return Now - Last >= DwmGpuRetryDelay(Failures);
}
