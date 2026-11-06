/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A DXGI factory on adapters with a native D3D11 driver does not start an OpenGL driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include <apitest.h>
#include <windows.h>
#include <dxgi.h>
#include <d3dkmthk.h>

typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);

static const GUID IID_FactoryLocal = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};

#define LOAD(Type, Name) reinterpret_cast<Type>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), Name))

static BOOL
AdapterHasD3d11Driver(D3DKMT_HANDLE Adapter)
{
    PFND3DKMT_QUERYADAPTERINFO QueryInfo = LOAD(PFND3DKMT_QUERYADAPTERINFO, "D3DKMTQueryAdapterInfo");
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_UMDFILENAMEINFO Name = {};

    if (!QueryInfo)
        return FALSE;
    Name.Version = KMTUMDVERSION_DX11;
    Query.hAdapter = Adapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Name;
    Query.PrivateDriverDataSize = sizeof(Name);
    return QueryInfo(&Query) >= 0 && Name.UmdFileName[0] != 0;
}

static BOOL
EveryAdapterHasD3d11Driver(void)
{
    PFND3DKMT_ENUMADAPTERS2 EnumAdapters = LOAD(PFND3DKMT_ENUMADAPTERS2, "D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER CloseAdapter = LOAD(PFND3DKMT_CLOSEADAPTER, "D3DKMTCloseAdapter");
    D3DKMT_ADAPTERINFO Adapters[16] = {};
    D3DKMT_ENUMADAPTERS2 Enum = {};
    D3DKMT_CLOSEADAPTER Close = {};
    BOOL Result;
    ULONG Index;

    if (!EnumAdapters || !CloseAdapter)
        return FALSE;
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (EnumAdapters(&Enum) < 0 || Enum.NumAdapters == 0)
        return FALSE;
    Result = TRUE;
    for (Index = 0; Index < Enum.NumAdapters; ++Index)
    {
        if (!AdapterHasD3d11Driver(Adapters[Index].hAdapter))
            Result = FALSE;
        Close.hAdapter = Adapters[Index].hAdapter;
        CloseAdapter(&Close);
    }
    return Result;
}

START_TEST(factory_opengl)
{
    HMODULE Module = LoadLibraryW(L"dxgi.dll");
    PFN_CREATE_FACTORY CreateFactory;
    IDXGIFactory1 *Factory = NULL;

    if (!Module)
    {
        skip("dxgi.dll is not available\n");
        return;
    }
    if (GetModuleHandleW(L"mesa_gallium.dll") != NULL)
    {
        skip("This process has already loaded the OpenGL driver\n");
        return;
    }
    CreateFactory = (PFN_CREATE_FACTORY)GetProcAddress(Module, "CreateDXGIFactory1");
    ok(CreateFactory != NULL, "CreateDXGIFactory1 is not exported\n");
    if (!CreateFactory)
        return;
    ok_hex(CreateFactory(IID_FactoryLocal, (void **)&Factory), S_OK);
    if (!Factory)
        return;
    if (EveryAdapterHasD3d11Driver())
        ok(GetModuleHandleW(L"mesa_gallium.dll") == NULL, "The factory loaded an OpenGL driver\n");
    else
        skip("An adapter has no native D3D11 driver\n");
    Factory->Release();
}
