/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D11CreateDeviceAndSwapChain without a swap chain description creates only the device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <wine/test.h>

static const D3D_FEATURE_LEVEL Levels[] =
{
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1,
};

START_TEST(null_swapchain_desc)
{
    IDXGISwapChain *SwapChain = (IDXGISwapChain *)(ULONG_PTR)1;
    ID3D11Device *Device = NULL;
    HRESULT hr;

    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels, ARRAYSIZE(Levels),
                                       D3D11_SDK_VERSION, NULL, &SwapChain, &Device, NULL, NULL);
    ok(hr == S_OK, "D3D11CreateDeviceAndSwapChain returned 0x%08lx\n", hr);
    ok(Device != NULL, "no device\n");
    ok(SwapChain == NULL, "swap chain output %p\n", SwapChain);
    if (Device)
        ID3D11Device_Release(Device);
}
