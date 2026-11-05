/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared and keyed mutex resources of every texture dimension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

enum DIMENSION { DIM_1D, DIM_2D, DIM_3D, DIM_BUFFER };

struct SHARE_CASE
{
    UINT Misc;
    UINT Mips;
    UINT Array;
};

static const SHARE_CASE Cases[] =
{
    { D3D11_RESOURCE_MISC_SHARED, 1, 1 },
    { D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, 1, 1 },
    { D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, 1, 1 },
    { D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, 1, 1 },
    { D3D11_RESOURCE_MISC_SHARED, 2, 1 },
    { D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, 2, 1 },
    { D3D11_RESOURCE_MISC_SHARED, 1, 2 },
    { D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, 1, 2 },
};

static HRESULT CreateResource(ID3D11Device *Device, DIMENSION Dimension, const SHARE_CASE &Case, ID3D11Resource **Out)
{
    HRESULT Hr = E_FAIL;

    *Out = NULL;
    switch (Dimension)
    {
        case DIM_1D:
        {
            D3D11_TEXTURE1D_DESC Desc = {64, Case.Mips, Case.Array, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                         D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, Case.Misc};
            ID3D11Texture1D *Texture = NULL;
            Hr = Device->CreateTexture1D(&Desc, NULL, &Texture);
            *Out = Texture;
            break;
        }
        case DIM_2D:
        {
            D3D11_TEXTURE2D_DESC Desc = {64, 64, Case.Mips, Case.Array, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                                         D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0,
                                         Case.Misc};
            ID3D11Texture2D *Texture = NULL;
            Hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
            *Out = Texture;
            break;
        }
        case DIM_3D:
        {
            D3D11_TEXTURE3D_DESC Desc = {16, 16, 4 * Case.Array, Case.Mips, DXGI_FORMAT_R8G8B8A8_UNORM,
                                         D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0,
                                         Case.Misc};
            ID3D11Texture3D *Texture = NULL;
            Hr = Device->CreateTexture3D(&Desc, NULL, &Texture);
            *Out = Texture;
            break;
        }
        case DIM_BUFFER:
        {
            D3D11_BUFFER_DESC Desc = {256 * Case.Mips * Case.Array, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                      Case.Misc, 0};
            ID3D11Buffer *Buffer = NULL;
            Hr = Device->CreateBuffer(&Desc, NULL, &Buffer);
            *Out = Buffer;
            break;
        }
    }
    return Hr;
}

static UINT ResourceMisc(ID3D11Resource *Resource, DIMENSION Dimension)
{
    switch (Dimension)
    {
        case DIM_1D: { D3D11_TEXTURE1D_DESC Desc; static_cast<ID3D11Texture1D *>(Resource)->GetDesc(&Desc); return Desc.MiscFlags; }
        case DIM_2D: { D3D11_TEXTURE2D_DESC Desc; static_cast<ID3D11Texture2D *>(Resource)->GetDesc(&Desc); return Desc.MiscFlags; }
        case DIM_3D: { D3D11_TEXTURE3D_DESC Desc; static_cast<ID3D11Texture3D *>(Resource)->GetDesc(&Desc); return Desc.MiscFlags; }
        default: { D3D11_BUFFER_DESC Desc; static_cast<ID3D11Buffer *>(Resource)->GetDesc(&Desc); return Desc.MiscFlags; }
    }
}

static REFIID DimensionIid(DIMENSION Dimension)
{
    switch (Dimension)
    {
        case DIM_1D: return IID_ID3D11Texture1D;
        case DIM_2D: return IID_ID3D11Texture2D;
        case DIM_3D: return IID_ID3D11Texture3D;
        default: return IID_ID3D11Buffer;
    }
}

static void Probe(ID3D11Device *Device, DIMENSION Dimension, UINT Index)
{
    const SHARE_CASE &Case = Cases[Index];
    BOOL NtHandle = !!(Case.Misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    BOOL Keyed = !!(Case.Misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX);
    ID3D11Resource *Resource = NULL;
    IDXGIKeyedMutex *Mutex = NULL;
    IDXGIResource *Dxgi = NULL;
    IDXGIResource1 *Dxgi1 = NULL;
    HANDLE Shared = (HANDLE)0xdeadbeef, NtShared = NULL;
    HRESULT Hr;

    Hr = CreateResource(Device, Dimension, Case, &Resource);
    ok(Hr == (NtHandle && Dimension != DIM_2D ? E_INVALIDARG : S_OK), "dim %u case %u: create %#lx\n", Dimension, Index, Hr);
    if (FAILED(Hr) || !Resource)
        return;
    ok(ResourceMisc(Resource, Dimension) == Case.Misc, "dim %u case %u: misc %#x\n", Dimension, Index,
       ResourceMisc(Resource, Dimension));
    Hr = Resource->QueryInterface(IID_IDXGIKeyedMutex, reinterpret_cast<void **>(&Mutex));
    ok(Hr == (Keyed ? S_OK : E_NOINTERFACE), "dim %u case %u: keyed mutex %#lx\n", Dimension, Index, Hr);
    if (Mutex)
    {
        Hr = Mutex->AcquireSync(0, 0);
        ok(Hr == S_OK, "dim %u case %u: AcquireSync %#lx\n", Dimension, Index, Hr);
        if (Hr == S_OK)
            Mutex->ReleaseSync(0);
        Mutex->Release();
    }
    if (SUCCEEDED(Resource->QueryInterface(IID_IDXGIResource, reinterpret_cast<void **>(&Dxgi))))
    {
        Hr = Dxgi->GetSharedHandle(&Shared);
        ok(NtHandle ? Hr == E_INVALIDARG : (Hr == S_OK && Shared != NULL), "dim %u case %u: GetSharedHandle %#lx %p\n",
           Dimension, Index, Hr, Shared);
        Dxgi->Release();
    }
    if (SUCCEEDED(Resource->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Dxgi1))))
    {
        Hr = Dxgi1->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &NtShared);
        ok(Hr == (NtHandle ? S_OK : E_INVALIDARG), "dim %u case %u: CreateSharedHandle %#lx\n", Dimension, Index, Hr);
        Dxgi1->Release();
    }
    if (!NtHandle && Shared && Shared != (HANDLE)0xdeadbeef)
    {
        void *Opened = NULL;
        D3D11_RESOURCE_DIMENSION Type = D3D11_RESOURCE_DIMENSION_UNKNOWN;

        Hr = Device->OpenSharedResource(Shared, DimensionIid(Dimension), &Opened);
        ok(Hr == S_OK && Opened, "dim %u case %u: OpenSharedResource %#lx\n", Dimension, Index, Hr);
        if (Opened)
        {
            static_cast<ID3D11Resource *>(Opened)->GetType(&Type);
            ok(Type == (Dimension == DIM_BUFFER ? D3D11_RESOURCE_DIMENSION_BUFFER
                        : static_cast<D3D11_RESOURCE_DIMENSION>(D3D11_RESOURCE_DIMENSION_TEXTURE1D + Dimension)),
               "dim %u case %u: opened type %u\n", Dimension, Index, Type);
            static_cast<IUnknown *>(Opened)->Release();
        }
    }
    if (NtShared)
        CloseHandle(NtShared);
    Resource->Release();
}

START_TEST(shared_dimensions)
{
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    ID3D11Device *Device = NULL;
    D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_11_0;

    if (!Create)
    {
        skip("d3d11.dll is unavailable\n");
        return;
    }
    if (FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Device, NULL, NULL)))
    {
        skip("no hardware device\n");
        FreeLibrary(Module);
        return;
    }
    for (UINT Dimension = DIM_1D; Dimension <= DIM_BUFFER; ++Dimension)
        for (UINT Index = 0; Index < ARRAYSIZE(Cases); ++Index)
            Probe(Device, static_cast<DIMENSION>(Dimension), Index);
    Device->Release();
    FreeLibrary(Module);
}
