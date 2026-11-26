/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXGI adapter order and identity for display-only and render-only kernel adapters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <initguid.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dkmthk.h>

typedef HRESULT (WINAPI *CREATE_FACTORY1)(REFIID, void **);
typedef HRESULT (WINAPI *CREATE_DEVICE12)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

#define MAX_ADAPTERS 16

typedef struct _KMT_ADAPTER
{
    LUID Luid;
    D3DKMT_ADAPTERTYPE Type;
} KMT_ADAPTER;

static BOOL LuidEqual(const LUID *A, const LUID *B)
{
    return A->LowPart == B->LowPart && A->HighPart == B->HighPart;
}

static UINT GetKmtAdapters(KMT_ADAPTER *Kmt)
{
    D3DKMT_ADAPTERINFO Info[MAX_ADAPTERS] = {};
    D3DKMT_ENUMADAPTERS2 Enum = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_CLOSEADAPTER Close = {};
    UINT i, Count = 0;

    Enum.NumAdapters = MAX_ADAPTERS;
    Enum.pAdapters = Info;
    if (D3DKMTEnumAdapters2(&Enum) < 0)
        return 0;
    for (i = 0; i < Enum.NumAdapters && i < MAX_ADAPTERS; ++i)
    {
        Query.hAdapter = Info[i].hAdapter;
        Query.Type = KMTQAITYPE_ADAPTERTYPE;
        Query.pPrivateDriverData = &Kmt[Count].Type;
        Query.PrivateDriverDataSize = sizeof(Kmt[Count].Type);
        if (D3DKMTQueryAdapterInfo(&Query) >= 0)
            Kmt[Count++].Luid = Info[i].AdapterLuid;
        Close.hAdapter = Info[i].hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    return Count;
}

static BOOL GetPrimaryOwner(LUID *Owner)
{
    DISPLAY_DEVICEW Device = {sizeof(Device)};
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open = {};
    D3DKMT_CLOSEADAPTER Close = {};

    for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &Device, 0); ++i)
    {
        if (!(Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            continue;
        lstrcpynW(Open.DeviceName, Device.DeviceName, ARRAYSIZE(Open.DeviceName));
        if (D3DKMTOpenAdapterFromGdiDisplayName(&Open) < 0)
            return FALSE;
        Close.hAdapter = Open.hAdapter;
        D3DKMTCloseAdapter(&Close);
        *Owner = Open.AdapterLuid;
        return TRUE;
    }
    return FALSE;
}

static UINT CountOutputs(IDXGIAdapter1 *Adapter)
{
    IDXGIOutput *Output;
    UINT Count;

    for (Count = 0; Adapter->EnumOutputs(Count, &Output) == S_OK; ++Count)
        Output->Release();
    return Count;
}

static void CheckOutputOrder(IDXGIFactory1 *Factory)
{
    IDXGIAdapter1 *Adapter;
    BOOL SeenWithout = FALSE;
    UINT Outputs;

    for (UINT i = 0; Factory->EnumAdapters1(i, &Adapter) == S_OK; ++i)
    {
        Outputs = CountOutputs(Adapter);
        Adapter->Release();
        ok(!Outputs || !SeenWithout, "adapter %u has %u outputs after an adapter without outputs\n", i, Outputs);
        SeenWithout |= !Outputs;
    }
}

static void CheckBasicAdapter(IDXGIAdapter1 *Adapter, UINT Index, const LUID *Luid, UINT Flags, UINT Outputs)
{
    DXGI_ADAPTER_DESC1 Desc = {};
    UINT Count = CountOutputs(Adapter);
    HRESULT hr;

    hr = Adapter->GetDesc1(&Desc);
    ok(hr == S_OK, "adapter %u: GetDesc1 returned %#lx\n", Index, hr);
    ok(LuidEqual(&Desc.AdapterLuid, Luid), "adapter %u: LUID %08lx:%08lx, expected %08lx:%08lx\n", Index,
       Desc.AdapterLuid.HighPart, Desc.AdapterLuid.LowPart, Luid->HighPart, Luid->LowPart);
    ok(!wcscmp(Desc.Description, L"Microsoft Basic Render Driver"), "adapter %u: \"%ls\"\n", Index, Desc.Description);
    ok(Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8c, "adapter %u: %04x:%04x\n", Index, Desc.VendorId,
       Desc.DeviceId);
    ok(Desc.Flags == Flags, "adapter %u: flags %#x, expected %#x\n", Index, Desc.Flags, Flags);
    ok(Count == Outputs, "adapter %u: %u outputs, expected %u\n", Index, Count, Outputs);
}

static void CheckDevices(IDXGIAdapter1 *Adapter, const LUID *Luid)
{
    HMODULE D3D11 = LoadLibraryW(L"d3d11.dll"), D3D12 = LoadLibraryW(L"d3d12.dll");
    PFN_D3D11_CREATE_DEVICE Create11 = D3D11 ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(D3D11, "D3D11CreateDevice") : NULL;
    CREATE_DEVICE12 Create12 = D3D12 ? (CREATE_DEVICE12)GetProcAddress(D3D12, "D3D12CreateDevice") : NULL;
    ID3D11Device *Device11 = NULL;
    ID3D12Device *Device12 = NULL;
    IDXGIDevice *DxgiDevice;
    IDXGIAdapter *DeviceAdapter;
    DXGI_ADAPTER_DESC Desc;
    D3D_FEATURE_LEVEL Level = (D3D_FEATURE_LEVEL)0;
    HRESULT hr;

    ok(Create11 != NULL, "no D3D11CreateDevice\n");
    if (Create11)
    {
        hr = Create11(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device11, &Level, NULL);
        ok(hr == S_OK, "D3D11 device on the display adapter: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(Level >= D3D_FEATURE_LEVEL_11_0, "feature level %#x\n", Level);
            if (SUCCEEDED(Device11->QueryInterface(__uuidof(IDXGIDevice), (void **)&DxgiDevice)))
            {
                if (SUCCEEDED(DxgiDevice->GetAdapter(&DeviceAdapter)))
                {
                    ok(SUCCEEDED(DeviceAdapter->GetDesc(&Desc)) && LuidEqual(&Desc.AdapterLuid, Luid),
                       "D3D11 device adapter %08lx:%08lx\n", Desc.AdapterLuid.HighPart, Desc.AdapterLuid.LowPart);
                    DeviceAdapter->Release();
                }
                DxgiDevice->Release();
            }
            Device11->Release();
        }
    }
    ok(Create12 != NULL, "no D3D12CreateDevice\n");
    if (Create12)
    {
        hr = Create12(Adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&Device12);
        ok(hr == S_OK, "D3D12 device on the display adapter: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            LUID DeviceLuid = Device12->GetAdapterLuid();
            ok(LuidEqual(&DeviceLuid, Luid), "D3D12 device adapter %08lx:%08lx\n", DeviceLuid.HighPart,
               DeviceLuid.LowPart);
            Device12->Release();
        }
        Device12 = NULL;
        hr = Create12(NULL, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&Device12);
        ok(hr == S_OK, "D3D12 default device: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            LUID DeviceLuid = Device12->GetAdapterLuid();
            ok(LuidEqual(&DeviceLuid, Luid), "D3D12 default device adapter %08lx:%08lx\n", DeviceLuid.HighPart,
               DeviceLuid.LowPart);
            Device12->Release();
        }
    }
}

static void CheckPreference(IDXGIFactory1 *Factory1, DXGI_GPU_PREFERENCE Preference, const LUID *Display,
        const LUID *Render)
{
    IDXGIFactory6 *Factory;
    IDXGIAdapter1 *Adapter;
    UINT i;

    if (FAILED(Factory1->QueryInterface(__uuidof(IDXGIFactory6), (void **)&Factory)))
    {
        skip("no IDXGIFactory6\n");
        return;
    }
    for (i = 0; Factory->EnumAdapterByGpuPreference(i, Preference, __uuidof(IDXGIAdapter1),
            (void **)&Adapter) == S_OK; ++i)
    {
        winetest_push_context("preference %u", Preference);
        if (i == 0)
            CheckBasicAdapter(Adapter, i, Display, 0, 1);
        else if (i == 1)
            CheckBasicAdapter(Adapter, i, Render, DXGI_ADAPTER_FLAG_SOFTWARE, 0);
        winetest_pop_context();
        Adapter->Release();
    }
    ok(i == 2, "preference %u: %u adapters\n", Preference, i);
    Factory->Release();
}

START_TEST(adapter_topology)
{
    HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");
    CREATE_FACTORY1 Create = Dxgi ? (CREATE_FACTORY1)GetProcAddress(Dxgi, "CreateDXGIFactory1") : NULL;
    KMT_ADAPTER Kmt[MAX_ADAPTERS];
    IDXGIFactory1 *Factory = NULL;
    IDXGIFactory4 *Factory4;
    IDXGIAdapter1 *Adapter;
    DXGI_ADAPTER_DESC1 Desc;
    LUID Owner, Display = {}, Render = {};
    UINT Count, DisplayOnly = 0, RenderOnly = 0, i;
    HRESULT hr;

    if (!Create || FAILED(Create(__uuidof(IDXGIFactory1), (void **)&Factory)))
    {
        skip("no DXGI factory\n");
        return;
    }
    CheckOutputOrder(Factory);

    Count = GetKmtAdapters(Kmt);
    for (i = 0; i < Count; ++i)
    {
        if (Kmt[i].Type.DisplaySupported && !Kmt[i].Type.RenderSupported && Kmt[i].Type.SoftwareDevice)
        {
            ++DisplayOnly;
            Display = Kmt[i].Luid;
        }
        if (Kmt[i].Type.RenderSupported && !Kmt[i].Type.DisplaySupported && Kmt[i].Type.SoftwareDevice)
        {
            ++RenderOnly;
            Render = Kmt[i].Luid;
        }
    }
    if (Count != 2 || DisplayOnly != 1 || RenderOnly != 1 || !GetPrimaryOwner(&Owner) || !LuidEqual(&Owner, &Display))
    {
        skip("%u kernel adapters, %u software display-only, %u software render-only\n", Count, DisplayOnly, RenderOnly);
        Factory->Release();
        return;
    }

    for (i = 0; Factory->EnumAdapters1(i, &Adapter) == S_OK; ++i)
    {
        if (i == 0)
        {
            CheckBasicAdapter(Adapter, i, &Display, 0, 1);
            CheckDevices(Adapter, &Display);
        }
        else if (i == 1)
        {
            CheckBasicAdapter(Adapter, i, &Render, DXGI_ADAPTER_FLAG_SOFTWARE, 0);
        }
        Adapter->Release();
    }
    ok(i == 2, "%u adapters\n", i);

    if (SUCCEEDED(Factory->QueryInterface(__uuidof(IDXGIFactory4), (void **)&Factory4)))
    {
        hr = Factory4->EnumWarpAdapter(__uuidof(IDXGIAdapter1), (void **)&Adapter);
        ok(hr == S_OK, "EnumWarpAdapter returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            Desc = {};
            Adapter->GetDesc1(&Desc);
            ok(LuidEqual(&Desc.AdapterLuid, &Render), "WARP adapter %08lx:%08lx\n", Desc.AdapterLuid.HighPart,
               Desc.AdapterLuid.LowPart);
            ok(Desc.Flags == DXGI_ADAPTER_FLAG_SOFTWARE, "WARP adapter flags %#x\n", Desc.Flags);
            Adapter->Release();
        }
        Factory4->Release();
    }
    CheckPreference(Factory, DXGI_GPU_PREFERENCE_MINIMUM_POWER, &Display, &Render);
    CheckPreference(Factory, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &Display, &Render);
    Factory->Release();
    FreeLibrary(Dxgi);
}
