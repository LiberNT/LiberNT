/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The first DXGI adapter creates a Direct3D device whenever some adapter renders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>

static BOOL
AdapterRenders(LUID Luid, BOOL *Renders)
{
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_CLOSEADAPTER Close = {};
    D3DKMT_ADAPTERTYPE Type = {};
    NTSTATUS Status;

    Open.AdapterLuid = Luid;
    if (D3DKMTOpenAdapterFromLuid(&Open) < 0)
        return FALSE;
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERTYPE;
    Query.pPrivateDriverData = &Type;
    Query.PrivateDriverDataSize = sizeof(Type);
    Status = D3DKMTQueryAdapterInfo(&Query);
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    *Renders = Type.RenderSupported;
    return Status >= 0;
}

typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);

START_TEST(first_adapter)
{
    PFN_CREATE_FACTORY CreateFactory = (PFN_CREATE_FACTORY)GetProcAddress(LoadLibraryW(L"dxgi.dll"), "CreateDXGIFactory1");
    HMODULE D3D11 = LoadLibraryW(L"d3d11.dll");
    PFN_D3D11_CREATE_DEVICE CreateDevice = D3D11 ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(D3D11, "D3D11CreateDevice") : NULL;
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter, *First = NULL;
    ID3D11Device *Device = NULL;
    DXGI_ADAPTER_DESC1 Desc;
    BOOL AnyRenders = FALSE, Renders;
    D3D_FEATURE_LEVEL Level;
    UINT Index;
    HRESULT hr;

    if (CreateFactory == NULL || FAILED(CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&Factory))))
    {
        skip("no DXGI factory\n");
        return;
    }
    for (Index = 0; Factory->EnumAdapters1(Index, &Adapter) == S_OK; ++Index)
    {
        if (SUCCEEDED(Adapter->GetDesc1(&Desc)) && AdapterRenders(Desc.AdapterLuid, &Renders))
            AnyRenders |= Renders;
        if (Index == 0)
            First = Adapter;
        else
            Adapter->Release();
    }
    Factory->Release();
    if (!AnyRenders || !First)
    {
        skip("no adapter renders\n");
        if (First)
            First->Release();
        return;
    }
    ok(CreateDevice != NULL, "no D3D11CreateDevice\n");
    if (CreateDevice)
    {
        hr = CreateDevice(First, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, &Level, NULL);
        ok(hr == S_OK, "the first adapter creates no device while another one renders: %#lx\n", hr);
        if (Device)
            Device->Release();
    }
    First->Release();
}
