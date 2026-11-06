/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interprets the answer of a D3D11 event query poll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

typedef enum _DWM_GPU_EVENT_POLL
{
    DWM_GPU_EVENT_PENDING,
    DWM_GPU_EVENT_COMPLETE,
    DWM_GPU_EVENT_REISSUE
} DWM_GPU_EVENT_POLL;

/* UTM's virtio-gpu driver latches S_OK with FALSE: only a new End can report. */
static __inline DWM_GPU_EVENT_POLL
DwmGpuEventPoll(HRESULT Status, BOOL Complete)
{
    if (Status != S_OK)
        return DWM_GPU_EVENT_PENDING;
    return Complete ? DWM_GPU_EVENT_COMPLETE : DWM_GPU_EVENT_REISSUE;
}
