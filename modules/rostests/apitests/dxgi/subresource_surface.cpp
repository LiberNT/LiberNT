/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDXGIResource1::CreateSubresourceSurface and IDXGISurface2 of Direct3D 11 resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

typedef struct _SUBRESOURCE_CASE
{
    UINT Type; /* 0 buffer, 1..3 texture 1D/2D/3D */
    UINT Width;
    UINT Height;
    UINT Pitch;
    UINT Mips;
    UINT Layers; /* array size, or depth of a volume */
    HRESULT Hr;
} SUBRESOURCE_CASE;

static ULONG RefCount(IUnknown *Object)
{
    Object->AddRef();
    return Object->Release();
}

static BOOL Exposes(IUnknown *Object, REFIID Iid)
{
    IUnknown *Out = NULL;
    HRESULT Hr = Object->QueryInterface(Iid, reinterpret_cast<void **>(&Out));

    if (Out) Out->Release();
    return SUCCEEDED(Hr);
}

static IDXGIResource1 *CreateResource(ID3D11Device *Device, const SUBRESOURCE_CASE *Case)
{
    IDXGIResource1 *Resource = NULL;
    ID3D11Resource *Object = NULL;
    HRESULT Hr = E_FAIL;

    if (Case->Type == 0)
    {
        D3D11_BUFFER_DESC Desc = { Case->Width, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
        Hr = Device->CreateBuffer(&Desc, NULL, reinterpret_cast<ID3D11Buffer **>(&Object));
    }
    else if (Case->Type == 1)
    {
        D3D11_TEXTURE1D_DESC Desc = { Case->Width, Case->Mips, Case->Layers, DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
        Hr = Device->CreateTexture1D(&Desc, NULL, reinterpret_cast<ID3D11Texture1D **>(&Object));
    }
    else if (Case->Type == 2)
    {
        D3D11_TEXTURE2D_DESC Desc = { Case->Width, Case->Height, Case->Mips, Case->Layers, DXGI_FORMAT_B8G8R8A8_UNORM,
                                      { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
        Hr = Device->CreateTexture2D(&Desc, NULL, reinterpret_cast<ID3D11Texture2D **>(&Object));
    }
    else
    {
        D3D11_TEXTURE3D_DESC Desc = { Case->Width, Case->Height, Case->Layers, Case->Mips, DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
        Hr = Device->CreateTexture3D(&Desc, NULL, reinterpret_cast<ID3D11Texture3D **>(&Object));
    }
    ok(Hr == S_OK, "create type %u: %#lx\n", Case->Type, Hr);
    if (!Object) return NULL;
    Hr = Object->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Resource));
    ok(Hr == S_OK, "type %u IDXGIResource1: %#lx\n", Case->Type, Hr);
    Object->Release();
    return Resource;
}

static void CheckWholeSurface(const SUBRESOURCE_CASE *Case, IDXGIResource1 *Resource)
{
    IDXGISurface2 *Surface = NULL;
    IDXGIResource1 *Parent;
    UINT Index;
    HRESULT Hr;

    /* Only a resource with a single subresource, a buffer included, is itself a surface. */
    Hr = Resource->QueryInterface(IID_IDXGISurface2, reinterpret_cast<void **>(&Surface));
    ok(Hr == (Case->Mips * Case->Layers == 1 ? S_OK : E_NOINTERFACE), "type %u mips %u layers %u whole surface: %#lx\n",
       Case->Type, Case->Mips, Case->Layers, Hr);
    if (FAILED(Hr))
        return;
    Parent = reinterpret_cast<IDXGIResource1 *>(0xdeadbeef);
    Index = 1234;
    Hr = Surface->GetResource(IID_IDXGIFactory, reinterpret_cast<void **>(&Parent), &Index);
    ok(Hr == E_NOINTERFACE && Parent == NULL && Index == 1234, "GetResource(IDXGIFactory): %#lx %p %u\n", Hr, Parent, Index);
    Hr = Surface->GetResource(IID_IDXGIResource1, reinterpret_cast<void **>(&Parent), &Index);
    ok(Hr == S_OK && Parent == Resource && Index == 0, "GetResource: %#lx %p %u\n", Hr, Parent, Index);
    if (SUCCEEDED(Hr)) Parent->Release();
    Surface->Release();
}

static void CheckSubresource(const SUBRESOURCE_CASE *Case, IDXGIResource1 *Resource, UINT Index)
{
    static const IID *Hidden[] =
    {
        &IID_ID3D11Buffer, &IID_ID3D11Texture1D, &IID_ID3D11Texture2D, &IID_ID3D11Texture3D, &IID_ID3D11Resource,
        &IID_IDXGIResource, &IID_IDXGIResource1,
    };
    static const IID *Shown[] =
    {
        &IID_IDXGISurface, &IID_IDXGISurface1, &IID_IDXGIDeviceSubObject, &IID_IDXGIObject, &IID_IUnknown,
    };
    IDXGISurface2 *Surface = NULL;
    IDXGIResource1 *Parent = NULL;
    DXGI_SURFACE_DESC Desc = {};
    DXGI_MAPPED_RECT Mapped = {};
    UINT Mip = Index % Case->Mips, Subresource = 0x55, Width, Height;
    ULONG Count;
    HRESULT Hr;

    Count = RefCount(Resource);
    ok(Count == 1, "type %u subresource %u: refcount before %lu\n", Case->Type, Index, Count);
    Hr = Resource->CreateSubresourceSurface(Index, &Surface);
    ok(Hr == Case->Hr, "type %u subresource %u: %#lx\n", Case->Type, Index, Hr);
    if (FAILED(Hr)) return;
    Count = RefCount(Resource);
    ok(Count == 2, "type %u subresource %u: refcount with a surface %lu\n", Case->Type, Index, Count);
    for (UINT Item = 0; Item < ARRAYSIZE(Shown); ++Item)
        ok(Exposes(Surface, *Shown[Item]), "type %u subresource %u: interface %u missing\n", Case->Type, Index, Item);
    for (UINT Item = 0; Item < ARRAYSIZE(Hidden); ++Item)
        ok(!Exposes(Surface, *Hidden[Item]), "type %u subresource %u: resource interface %u exposed\n", Case->Type, Index, Item);

    Width = Case->Width >> Mip;
    Height = Case->Type <= 1 ? 1 : Case->Height >> Mip;
    Hr = Surface->GetDesc(&Desc);
    ok(Hr == S_OK && Desc.Width == Width && Desc.Height == Height, "type %u subresource %u: desc %#lx %ux%u, expected %ux%u\n",
       Case->Type, Index, Hr, Desc.Width, Desc.Height, Width, Height);
    ok(Desc.Format == (Case->Type ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_UNKNOWN), "type %u subresource %u: format %u\n",
       Case->Type, Index, Desc.Format);
    ok(Desc.SampleDesc.Count == 1 && Desc.SampleDesc.Quality == 0, "type %u subresource %u: samples %u/%u\n",
       Case->Type, Index, Desc.SampleDesc.Count, Desc.SampleDesc.Quality);

    Hr = Surface->Map(&Mapped, DXGI_MAP_READ);
    ok(Hr == S_OK && Mapped.Pitch == static_cast<INT>(Case->Pitch >> Mip), "type %u subresource %u: map %#lx pitch %d, expected %u\n",
       Case->Type, Index, Hr, Mapped.Pitch, Case->Pitch >> Mip);
    if (SUCCEEDED(Hr))
    {
        Hr = Surface->Unmap();
        ok(Hr == S_OK, "type %u subresource %u: unmap %#lx\n", Case->Type, Index, Hr);
    }

    Hr = Surface->GetResource(IID_IDXGIResource1, reinterpret_cast<void **>(&Parent), &Subresource);
    ok(Hr == S_OK && Parent == Resource && Subresource == Index, "type %u subresource %u: GetResource %#lx %p %u\n",
       Case->Type, Index, Hr, Parent, Subresource);
    if (Parent) Parent->Release();
    Surface->Release();
}

/* Not pinned yet: the local Windows 11 image answers these. */
static void TraceOpenCases(ID3D11Device *Device)
{
    D3D11_TEXTURE2D_DESC Desc = { 16, 8, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_GDI_COMPATIBLE };
    ID3D11Texture2D *Texture = NULL;
    IDXGIResource1 *Resource = NULL;
    IDXGISurface2 *Surface = NULL;
    HRESULT Hr;

    Device->CreateTexture2D(&Desc, NULL, &Texture);
    if (Texture) Texture->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Resource));
    if (Resource)
    {
        Hr = Resource->CreateSubresourceSurface(0, &Surface);
        if (Surface)
        {
            HDC Dc = NULL;

            Hr = Surface->GetDC(FALSE, &Dc);
            trace("PIN gdi subresource GetDC: %#lx\n", Hr);
            if (SUCCEEDED(Hr)) trace("PIN gdi subresource ReleaseDC: %#lx\n", Surface->ReleaseDC(NULL));
            Surface->Release();
        }
        Resource->Release();
    }
    if (Texture) Texture->Release();
}

START_TEST(subresource_surface)
{
    static const SUBRESOURCE_CASE Cases[] =
    {
        { 0, 512, 1, 512, 1, 1, S_OK },
        { 1, 512, 1, 2048, 1, 1, S_OK },
        { 1, 512, 1, 2048, 4, 4, S_OK },
        { 2, 512, 512, 2048, 1, 1, S_OK },
        { 2, 512, 512, 2048, 4, 4, S_OK },
        { 3, 512, 512, 2048, 1, 1, S_OK },
        { 3, 512, 512, 2048, 2, 1, S_OK },
        { 3, 512, 512, 2048, 1, 2, E_INVALIDARG },
    };
    ID3D11Device *Device = NULL;

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create || FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                 D3D11_SDK_VERSION, &Device, NULL, NULL)))
    {
        skip("no D3D11 device\n");
        FreeLibrary(Module);
        return;
    }
    for (UINT Item = 0; Item < ARRAYSIZE(Cases); ++Item)
    {
        const SUBRESOURCE_CASE *Case = &Cases[Item];
        IDXGIResource1 *Resource = CreateResource(Device, Case);
        IDXGISurface2 *Surface = reinterpret_cast<IDXGISurface2 *>(0xdeadbeef);
        UINT Count = Case->Mips * Case->Layers, Index;
        HRESULT Hr;

        if (!Resource) continue;
        CheckWholeSurface(Case, Resource);
        for (Index = 0; Index < Count; ++Index)
            CheckSubresource(Case, Resource, Index);
        Hr = Resource->CreateSubresourceSurface(Index, &Surface);
        ok(Hr == E_INVALIDARG && Surface == reinterpret_cast<IDXGISurface2 *>(0xdeadbeef),
           "type %u subresource %u out of range: %#lx %p\n", Case->Type, Index, Hr, Surface);
        Resource->Release();
    }
    TraceOpenCases(Device);
    Device->Release();
    FreeLibrary(Module);
}
