/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXGI identifies software rendering as the Microsoft Basic Render Driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d11.h>

typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);

static void
CheckWarpDevice(void)
{
    static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    HMODULE D3D11 = LoadLibraryW(L"d3d11.dll");
    PFN_D3D11_CREATE_DEVICE CreateDevice;
    ID3D11Device *Device = NULL;
    IDXGIDevice *DxgiDevice;
    IDXGIAdapter *Adapter;
    IDXGIOutput *Output;
    DXGI_ADAPTER_DESC Desc;
    D3D_FEATURE_LEVEL Level = (D3D_FEATURE_LEVEL)0;
    HRESULT hr;

    ok(D3D11 != NULL, "LoadLibrary(d3d11.dll) failed: %lu\n", GetLastError());
    if (!D3D11)
        return;
    CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(D3D11, "D3D11CreateDevice"));
    ok(CreateDevice != NULL, "D3D11CreateDevice is not exported\n");
    if (!CreateDevice)
        return;
    hr = CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, Levels, ARRAYSIZE(Levels), D3D11_SDK_VERSION,
                      &Device, &Level, NULL);
    ok(hr == S_OK, "WARP device creation returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    ok(Level >= D3D_FEATURE_LEVEL_10_0, "WARP device has feature level %#x\n", Level);

    hr = Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
    ok(hr == S_OK, "QueryInterface(IDXGIDevice) returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = DxgiDevice->GetAdapter(&Adapter);
        ok(hr == S_OK, "GetAdapter returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = Adapter->GetDesc(&Desc);
            ok(hr == S_OK, "GetDesc returned %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                ok(Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8c, "WARP adapter is %04x:%04x\n",
                   Desc.VendorId, Desc.DeviceId);
                ok(wcscmp(Desc.Description, L"Microsoft Basic Render Driver") == 0, "WARP adapter is \"%ls\"\n",
                   Desc.Description);
            }
            hr = Adapter->EnumOutputs(0, &Output);
            ok(hr == DXGI_ERROR_NOT_FOUND, "WARP adapter EnumOutputs(0) returned %#lx\n", hr);
            if (SUCCEEDED(hr))
                Output->Release();
            Adapter->Release();
        }
        DxgiDevice->Release();
    }
    Device->Release();
}

static BOOL
BasicDisplayOnly(void)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;

    for (Index = 0;; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            return FALSE;
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            return wcsstr(Device.DeviceString, L"Basic Display Adapter") != NULL;
    }
}

START_TEST(software_adapter)
{
    HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");
    BOOL BasicOnly = BasicDisplayOnly();
    PFN_CREATE_FACTORY CreateFactory;
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter;
    IDXGIOutput *Output;
    DXGI_ADAPTER_DESC1 Desc;
    UINT Index, Outputs;
    HRESULT hr;

    ok(Dxgi != NULL, "LoadLibrary(dxgi.dll) failed: %lu\n", GetLastError());
    if (!Dxgi)
        return;
    CreateFactory = reinterpret_cast<PFN_CREATE_FACTORY>(GetProcAddress(Dxgi, "CreateDXGIFactory1"));
    ok(CreateFactory != NULL, "CreateDXGIFactory1 is not exported\n");
    if (!CreateFactory)
        return;
    hr = CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&Factory));
    ok(hr == S_OK, "CreateDXGIFactory1 returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    for (Index = 0; Factory->EnumAdapters1(Index, &Adapter) == S_OK; Index++)
    {
        hr = Adapter->GetDesc1(&Desc);
        ok(hr == S_OK, "adapter %u: GetDesc1 returned %#lx\n", Index, hr);
        for (Outputs = 0; Adapter->EnumOutputs(Outputs, &Output) == S_OK; Outputs++)
            Output->Release();
        Adapter->Release();
        if (FAILED(hr))
            continue;

        if (Desc.VendorId == 0x1414 || (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        {
            ok(Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8c, "adapter %u: %04x:%04x\n",
               Index, Desc.VendorId, Desc.DeviceId);
            ok(wcscmp(Desc.Description, L"Microsoft Basic Render Driver") == 0, "adapter %u: \"%ls\"\n",
               Index, Desc.Description);
            ok(Desc.SubSysId == 0 && Desc.Revision == 0, "adapter %u: subsystem %#x revision %#x\n",
               Index, Desc.SubSysId, Desc.Revision);
            ok(Desc.DedicatedVideoMemory == 0, "adapter %u: %Iu bytes of dedicated video memory\n",
               Index, Desc.DedicatedVideoMemory);
        }
        if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            ok(Outputs == 0, "adapter %u: software adapter has %u outputs\n", Index, Outputs);
        if (Index == 0 && BasicOnly)
        {
            ok(Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8c, "basic display: adapter 0 is \"%ls\" %04x:%04x\n",
               Desc.Description, Desc.VendorId, Desc.DeviceId);
            ok(Desc.Flags == 0, "basic display: adapter 0 has flags %#x\n", Desc.Flags);
            ok(Outputs != 0, "basic display: adapter 0 has no output\n");
        }
    }
    ok(Index != 0, "no adapter\n");
    Factory->Release();

    CheckWarpDevice();
}
