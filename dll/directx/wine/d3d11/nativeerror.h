/*
 * PROJECT:     LiberNT Direct3D runtimes
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Translates the error a user-mode driver reports into the one an application sees
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#define NATIVE_DDI_ERR_DEVICEREMOVED ((HRESULT)0x88760870L)
#define NATIVE_DDI_ERR_WASSTILLDRAWING ((HRESULT)0x887B0001L)
#define NATIVE_DDI_ERR_UNSUPPORTED ((HRESULT)0x887B0003L)
#define NATIVE_ERROR_DEVICE_REMOVED ((HRESULT)0x887A0005L)
#define NATIVE_ERROR_WAS_STILL_DRAWING ((HRESULT)0x887A000AL)
#define NATIVE_ERROR_UNSUPPORTED ((HRESULT)0x887A0004L)

static __inline HRESULT
NativeDriverErrorToApi(HRESULT Error)
{
    switch (Error)
    {
        case NATIVE_DDI_ERR_DEVICEREMOVED: return NATIVE_ERROR_DEVICE_REMOVED;
        case NATIVE_DDI_ERR_WASSTILLDRAWING: return NATIVE_ERROR_WAS_STILL_DRAWING;
        case NATIVE_DDI_ERR_UNSUPPORTED: return NATIVE_ERROR_UNSUPPORTED;
        default: return Error;
    }
}
