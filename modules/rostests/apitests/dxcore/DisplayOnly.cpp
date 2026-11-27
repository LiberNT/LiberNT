/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXCore enumerates and finds render-capable adapters only, never a display-only one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <dxgi.h>
#include <d3dkmthk.h>
#include <apitest.h>
#include <initguid.h>
#include <dxcore.h>

static BOOL ListHasLuid(IDXCoreAdapterFactory *factory, const GUID &attribute, const LUID &luid)
{
    IDXCoreAdapterList *list = NULL;
    IDXCoreAdapter *adapter;
    BOOL found = FALSE;
    LUID other;
    UINT i;

    if (FAILED(factory->CreateAdapterList(1, &attribute, IID_IDXCoreAdapterList, (void **)&list)) || !list)
        return FALSE;
    for (i = 0; i < list->GetAdapterCount() && !found; i++)
    {
        adapter = NULL;
        if (FAILED(list->GetAdapter(i, IID_IDXCoreAdapter, (void **)&adapter)) || !adapter)
            continue;
        if (SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(other), &other)))
            found = other.LowPart == luid.LowPart && other.HighPart == luid.HighPart;
        adapter->Release();
    }
    list->Release();
    return found;
}

START_TEST(DisplayOnly)
{
    D3DKMT_ADAPTERINFO infos[16];
    D3DKMT_ENUMADAPTERS2 enumerate = {};
    IDXCoreAdapterFactory *factory = NULL;
    D3DKMT_QUERYADAPTERINFO query;
    D3DKMT_CLOSEADAPTER close;
    D3DKMT_ADAPTERTYPE type;
    IDXCoreAdapter *adapter;
    UINT display_only = 0, render = 0;
    HRESULT hr;
    UINT i;

    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory, (void **)&factory);
    ok(hr == S_OK, "DXCoreCreateAdapterFactory returned %#lx\n", hr);
    if (!factory) return;

    enumerate.NumAdapters = ARRAYSIZE(infos);
    enumerate.pAdapters = infos;
    ok(NT_SUCCESS(D3DKMTEnumAdapters2(&enumerate)), "D3DKMTEnumAdapters2 failed\n");

    for (i = 0; i < enumerate.NumAdapters; i++)
    {
        memset(&type, 0, sizeof(type));
        memset(&query, 0, sizeof(query));
        query.hAdapter = infos[i].hAdapter;
        query.Type = KMTQAITYPE_ADAPTERTYPE;
        query.pPrivateDriverData = &type;
        query.PrivateDriverDataSize = sizeof(type);
        if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        {
            adapter = NULL;
            hr = factory->GetAdapterByLuid(infos[i].AdapterLuid, IID_IDXCoreAdapter, (void **)&adapter);
            if (type.RenderSupported)
            {
                render++;
                ok(hr == S_OK && adapter, "render adapter %u: GetAdapterByLuid returned %#lx\n", i, hr);
                ok(ListHasLuid(factory, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, infos[i].AdapterLuid),
                   "render adapter %u is not in the D3D11 list\n", i);
            }
            else if (type.DisplaySupported && !type.ComputeOnly)
            {
                display_only++;
                ok(hr == E_INVALIDARG && !adapter, "display-only adapter %u: GetAdapterByLuid returned %#lx %p\n",
                   i, hr, adapter);
                ok(!ListHasLuid(factory, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, infos[i].AdapterLuid),
                   "display-only adapter %u is in the D3D11 list\n", i);
                ok(!ListHasLuid(factory, DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE, infos[i].AdapterLuid),
                   "display-only adapter %u is in the compute list\n", i);
            }
            if (adapter) adapter->Release();
        }
        close.hAdapter = infos[i].hAdapter;
        D3DKMTCloseAdapter(&close);
    }
    if (!display_only) skip("no display-only adapter\n");
    trace("%u render and %u display-only adapters\n", render, display_only);
    factory->Release();
}
