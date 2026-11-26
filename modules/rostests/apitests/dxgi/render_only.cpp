/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXGI lists a render-only WDDM adapter, with no outputs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3dkmthk.h>

typedef HRESULT (WINAPI *CREATE_FACTORY1)(REFIID, void **);

static BOOL IsRenderOnly(const LUID *Luid)
{
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_ADAPTERTYPE Type = {};
    D3DKMT_CLOSEADAPTER Close = {};
    NTSTATUS Status;

    Open.AdapterLuid = *Luid;
    if (D3DKMTOpenAdapterFromLuid(&Open) < 0)
        return FALSE;
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERTYPE;
    Query.pPrivateDriverData = &Type;
    Query.PrivateDriverDataSize = sizeof(Type);
    Status = D3DKMTQueryAdapterInfo(&Query);
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return Status >= 0 && Type.RenderSupported && !Type.DisplaySupported;
}

START_TEST(render_only)
{
    HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");
    CREATE_FACTORY1 Create = Dxgi ? reinterpret_cast<CREATE_FACTORY1>(GetProcAddress(Dxgi, "CreateDXGIFactory1")) : NULL;
    D3DKMT_ENUMADAPTERS2 Enum = {};
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter;
    UINT RenderOnly = 0;
    HRESULT Hr;

    if (D3DKMTEnumAdapters2(&Enum) < 0 || Enum.NumAdapters == 0)
    {
        skip("no WDDM adapter\n");
        return;
    }
    ok(Create != NULL, "CreateDXGIFactory1 is not exported\n");
    if (!Create)
        return;

    Hr = Create(IID_IDXGIFactory1, reinterpret_cast<void **>(&Factory));
    ok(SUCCEEDED(Hr), "CreateDXGIFactory1 returned %#lx with %lu WDDM adapters\n", Hr, Enum.NumAdapters);
    if (FAILED(Hr))
        return;

    for (UINT i = 0; SUCCEEDED(Factory->EnumAdapters1(i, &Adapter)); ++i)
    {
        DXGI_ADAPTER_DESC1 Desc = {};
        IDXGIOutput *Output = NULL;

        Adapter->GetDesc1(&Desc);
        if (IsRenderOnly(&Desc.AdapterLuid))
        {
            ++RenderOnly;
            Hr = Adapter->EnumOutputs(0, &Output);
            ok(Hr == DXGI_ERROR_NOT_FOUND, "render-only adapter %u enumerated an output: %#lx\n", i, Hr);
            if (Output)
                Output->Release();
        }
        Adapter->Release();
    }
    trace("%u render-only adapters\n", RenderOnly);
    Factory->Release();
    FreeLibrary(Dxgi);
}
