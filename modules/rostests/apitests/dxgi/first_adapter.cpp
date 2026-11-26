/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The first DXGI adapter is one that renders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <dxgi.h>
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
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter;
    DXGI_ADAPTER_DESC1 Desc;
    BOOL FirstRenders = FALSE, AnyRenders = FALSE, Renders;
    UINT Index;

    if (CreateFactory == NULL || FAILED(CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&Factory))))
    {
        skip("no DXGI factory\n");
        return;
    }
    for (Index = 0; Factory->EnumAdapters1(Index, &Adapter) == S_OK; ++Index)
    {
        if (SUCCEEDED(Adapter->GetDesc1(&Desc)) && AdapterRenders(Desc.AdapterLuid, &Renders))
        {
            if (Index == 0)
                FirstRenders = Renders;
            AnyRenders |= Renders;
        }
        Adapter->Release();
    }
    Factory->Release();
    if (!AnyRenders)
    {
        skip("no adapter renders\n");
        return;
    }
    ok(FirstRenders, "the first adapter does not render while another one does\n");
}
