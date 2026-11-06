/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 10 device creation: level 9, debug devices without SDK layers, invalid driver types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <d3d10_1.h>
#include <d3d11.h>

typedef HRESULT (WINAPI *PFN_CREATE_DEVICE1)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT,
                                             ID3D10Device1 **);
typedef HRESULT (WINAPI *PFN_CREATE_DEVICE)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device **);
typedef HRESULT (WINAPI *PFN_CREATE_DEVICE11)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                              UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static void
CheckLevel(PFN_CREATE_DEVICE1 Create, D3D10_FEATURE_LEVEL1 Level)
{
    ID3D10Device1 *Device = NULL;

    ok_hex(Create(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, Level, D3D10_1_SDK_VERSION, &Device), S_OK);
    if (Device == NULL)
        return;
    ok(Device->GetFeatureLevel() == Level, "Device reports level %#x, expected %#x\n", Device->GetFeatureLevel(), Level);
    Device->Release();
}

static void
CheckRefused1(PFN_CREATE_DEVICE1 Create, D3D10_DRIVER_TYPE Type, UINT Flags, D3D10_FEATURE_LEVEL1 Level, HRESULT Expected)
{
    ID3D10Device1 *Device = (ID3D10Device1 *)(ULONG_PTR)0xdeadbeef;

    ok_hex(Create(NULL, Type, NULL, Flags, Level, D3D10_1_SDK_VERSION, &Device), Expected);
    ok(Device == NULL, "Type %#x flags %#x level %#x left device %p\n", Type, Flags, Level, Device);
}

static void
CheckRefused(PFN_CREATE_DEVICE Create, D3D10_DRIVER_TYPE Type, UINT Flags, HRESULT Expected)
{
    ID3D10Device *Device = (ID3D10Device *)(ULONG_PTR)0xdeadbeef;

    ok_hex(Create(NULL, Type, NULL, Flags, D3D10_SDK_VERSION, &Device), Expected);
    ok(Device == NULL, "Type %#x flags %#x left device %p\n", Type, Flags, Device);
}

START_TEST(d3d10_create)
{
    HMODULE D3d10_1 = LoadLibraryW(L"d3d10_1.dll");
    HMODULE D3d10 = LoadLibraryW(L"d3d10.dll");
    HMODULE D3d11 = LoadLibraryW(L"d3d11.dll");
    PFN_CREATE_DEVICE1 Create1 = D3d10_1 ? (PFN_CREATE_DEVICE1)GetProcAddress(D3d10_1, "D3D10CreateDevice1") : NULL;
    PFN_CREATE_DEVICE Create = D3d10 ? (PFN_CREATE_DEVICE)GetProcAddress(D3d10, "D3D10CreateDevice") : NULL;
    PFN_CREATE_DEVICE11 Create11 = D3d11 ? (PFN_CREATE_DEVICE11)GetProcAddress(D3d11, "D3D11CreateDevice") : NULL;
    D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_10_0;
    ID3D10Device1 *Probe = NULL;
    ID3D11Device *Device11 = NULL;

    if (Create1 == NULL || Create == NULL || Create11 == NULL)
    {
        skip("Direct3D 10/11 entry points are missing\n");
        return;
    }
    if (FAILED(Create1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &Probe)))
    {
        skip("No Direct3D 10.1 hardware device\n");
        return;
    }
    Probe->Release();

    CheckLevel(Create1, D3D10_FEATURE_LEVEL_9_1);
    CheckLevel(Create1, D3D10_FEATURE_LEVEL_9_3);
    CheckLevel(Create1, D3D10_FEATURE_LEVEL_10_1);

    CheckRefused1(Create1, D3D10_DRIVER_TYPE_HARDWARE, D3D10_CREATE_DEVICE_DEBUG, D3D10_FEATURE_LEVEL_10_1, DXGI_ERROR_SDK_COMPONENT_MISSING);
    CheckRefused1(Create1, D3D10_DRIVER_TYPE_HARDWARE, D3D10_CREATE_DEVICE_DEBUG, D3D10_FEATURE_LEVEL_10_0, DXGI_ERROR_SDK_COMPONENT_MISSING);
    CheckRefused1(Create1, D3D10_DRIVER_TYPE_HARDWARE, D3D10_CREATE_DEVICE_DEBUG, D3D10_FEATURE_LEVEL_9_1, DXGI_ERROR_SDK_COMPONENT_MISSING);
    CheckRefused1(Create1, (D3D10_DRIVER_TYPE)0xffffffff, 0, D3D10_FEATURE_LEVEL_10_0, E_INVALIDARG);
    CheckRefused1(Create1, (D3D10_DRIVER_TYPE)7, 0, D3D10_FEATURE_LEVEL_10_0, E_INVALIDARG);

    CheckRefused(Create, D3D10_DRIVER_TYPE_HARDWARE, D3D10_CREATE_DEVICE_DEBUG, DXGI_ERROR_SDK_COMPONENT_MISSING);
    CheckRefused(Create, (D3D10_DRIVER_TYPE)0xffffffff, 0, E_INVALIDARG);

    ok_hex(Create11(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &Level, 1, D3D11_SDK_VERSION,
                    &Device11, NULL, NULL), DXGI_ERROR_SDK_COMPONENT_MISSING);
    ok(Device11 == NULL, "Debug D3D11 device %p\n", Device11);
}
