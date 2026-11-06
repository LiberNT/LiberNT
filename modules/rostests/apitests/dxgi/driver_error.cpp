/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the error a user-mode driver's report turns into for an application
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <dxgi.h>
#include "../../../../dll/directx/wine/d3d11/nativeerror.h"

START_TEST(driver_error)
{
    ok_hex(NativeDriverErrorToApi((HRESULT)0x88760870L), DXGI_ERROR_DEVICE_REMOVED);
    ok_hex(NativeDriverErrorToApi((HRESULT)0x887B0001L), DXGI_ERROR_WAS_STILL_DRAWING);
    ok_hex(NativeDriverErrorToApi((HRESULT)0x887B0003L), DXGI_ERROR_UNSUPPORTED);
    ok_hex(NativeDriverErrorToApi(S_OK), S_OK);
    ok_hex(NativeDriverErrorToApi(E_OUTOFMEMORY), E_OUTOFMEMORY);
    ok_hex(NativeDriverErrorToApi(DXGI_ERROR_DEVICE_HUNG), DXGI_ERROR_DEVICE_HUNG);
}
