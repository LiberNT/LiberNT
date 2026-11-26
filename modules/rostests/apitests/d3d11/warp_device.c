/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The WARP driver type creates the device the WARP adapter creates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wine/test.h>

static const D3D_FEATURE_LEVEL Levels[] =
{
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1,
};

static BOOL GetDeviceLuid(ID3D11Device *Device, LUID *Luid)
{
    IDXGIDevice *DxgiDevice;
    IDXGIAdapter *Adapter;
    DXGI_ADAPTER_DESC Desc;
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&DxgiDevice);
    ok(hr == S_OK, "QueryInterface(IDXGIDevice) returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = IDXGIDevice_GetAdapter(DxgiDevice, &Adapter);
    IDXGIDevice_Release(DxgiDevice);
    ok(hr == S_OK, "GetAdapter returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = IDXGIAdapter_GetDesc(Adapter, &Desc);
    IDXGIAdapter_Release(Adapter);
    ok(hr == S_OK, "GetDesc returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    *Luid = Desc.AdapterLuid;
    return TRUE;
}

START_TEST(warp_device)
{
    IDXGIFactory4 *Factory;
    IDXGIAdapter *WarpAdapter;
    ID3D11Device *AdapterDevice = NULL, *TypeDevice = NULL;
    D3D_FEATURE_LEVEL AdapterLevel = 0, TypeLevel = 0;
    DXGI_ADAPTER_DESC WarpDesc;
    HRESULT hr, AdapterHr, TypeHr;
    LUID Luid;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&Factory);
    if (FAILED(hr))
    {
        skip("no IDXGIFactory4: 0x%08lx\n", hr);
        return;
    }
    hr = IDXGIFactory4_EnumWarpAdapter(Factory, &IID_IDXGIAdapter, (void **)&WarpAdapter);
    IDXGIFactory4_Release(Factory);
    ok(hr == S_OK, "EnumWarpAdapter returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDXGIAdapter_GetDesc(WarpAdapter, &WarpDesc);
    ok(hr == S_OK, "GetDesc returned 0x%08lx\n", hr);

    AdapterHr = D3D11CreateDevice(WarpAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, Levels, ARRAYSIZE(Levels),
                                  D3D11_SDK_VERSION, &AdapterDevice, &AdapterLevel, NULL);
    IDXGIAdapter_Release(WarpAdapter);
    TypeHr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, Levels, ARRAYSIZE(Levels),
                               D3D11_SDK_VERSION, &TypeDevice, &TypeLevel, NULL);
    ok(AdapterHr == S_OK, "device on the WARP adapter: 0x%08lx\n", AdapterHr);
    ok(TypeHr == AdapterHr, "WARP driver type: 0x%08lx, WARP adapter: 0x%08lx\n", TypeHr, AdapterHr);
    ok(TypeLevel == AdapterLevel, "WARP driver type: level 0x%x, WARP adapter: level 0x%x\n", TypeLevel, AdapterLevel);

    if (TypeDevice && GetDeviceLuid(TypeDevice, &Luid))
        ok(Luid.LowPart == WarpDesc.AdapterLuid.LowPart && Luid.HighPart == WarpDesc.AdapterLuid.HighPart,
           "WARP driver type runs on adapter %08lx:%08lx, WARP adapter is %08lx:%08lx\n",
           Luid.HighPart, Luid.LowPart, WarpDesc.AdapterLuid.HighPart, WarpDesc.AdapterLuid.LowPart);

    if (TypeDevice)
        ID3D11Device_Release(TypeDevice);
    if (AdapterDevice)
        ID3D11Device_Release(AdapterDevice);
}
