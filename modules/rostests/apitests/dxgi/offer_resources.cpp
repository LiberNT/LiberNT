/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDXGIDevice residency queries, resource offers and reclaims of a Direct3D 11 device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_5.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ID3D11Texture2D *CreateTexture(ID3D11Device *Device, UINT Misc, D3D11_USAGE Usage, UINT Bind, UINT Cpu)
{
    static DWORD Pixels[16 * 16];
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Data = { Pixels, 16 * sizeof(DWORD), 0 };
    ID3D11Texture2D *Texture = NULL;

    for (UINT Index = 0; Index < ARRAYSIZE(Pixels); ++Index)
        Pixels[Index] = 0xff336699;
    Desc.Width = 16; Desc.Height = 16; Desc.MipLevels = 1; Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; Desc.SampleDesc.Count = 1;
    Desc.Usage = Usage; Desc.BindFlags = Bind; Desc.CPUAccessFlags = Cpu; Desc.MiscFlags = Misc;
    Device->CreateTexture2D(&Desc, &Data, &Texture);
    return Texture;
}

static BOOL Resident(DXGI_RESIDENCY Status)
{
    return Status == DXGI_RESIDENCY_FULLY_RESIDENT || Status == DXGI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY;
}

static IDXGIResource *DxgiResource(IUnknown *Object)
{
    IDXGIResource *Resource = NULL;

    if (Object) Object->QueryInterface(IID_IDXGIResource, reinterpret_cast<void **>(&Resource));
    return Resource;
}

static DWORD FirstPixel(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3D11Texture2D *Source)
{
    ID3D11Texture2D *Staging = CreateTexture(Device, 0, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    DWORD Pixel = 0;

    if (!Staging) return 0;
    Context->CopyResource(Staging, Source);
    if (SUCCEEDED(Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped)))
    {
        Pixel = *static_cast<DWORD *>(Mapped.pData);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return Pixel;
}

typedef struct _OFFER_OBJECTS
{
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGIDevice2 *Dxgi;
    ID3D11Texture2D *Texture;
    IDXGIResource *TextureResource;
    IDXGIResource *BufferResource;
    IDXGIResource *SharedResource;
    IDXGIResource *StagingResource;
    IDXGIResource *ForeignResource;
} OFFER_OBJECTS;

static void CheckResidency(const OFFER_OBJECTS *O)
{
    IDXGIDevice2 *Dxgi = O->Dxgi;
    IDXGIResource *TextureResource = O->TextureResource, *BufferResource = O->BufferResource;
    IDXGIResource *StagingResource = O->StagingResource, *ForeignResource = O->ForeignResource;
    ID3D11Texture2D *Texture = O->Texture;
    IUnknown *Objects[3] = { TextureResource, BufferResource, StagingResource };
    DXGI_RESIDENCY Status[3];
    HRESULT Hr;

    Status[0] = Status[1] = Status[2] = static_cast<DXGI_RESIDENCY>(0x55);
    Hr = Dxgi->QueryResourceResidency(Objects, Status, 3);
    ok(Hr == S_OK, "QueryResourceResidency: %#lx\n", Hr);
    for (UINT Index = 0; Index < 3; ++Index)
        ok(Resident(Status[Index]), "residency %u: %d\n", Index, Status[Index]);
    Status[0] = static_cast<DXGI_RESIDENCY>(0x55);
    Hr = Dxgi->QueryResourceResidency(Objects, Status, 0);
    ok(Hr == S_OK && (int)Status[0] == 0x55, "residency of no resources: %#lx, status %d\n", Hr, Status[0]);
    Objects[0] = Texture;
    Status[0] = static_cast<DXGI_RESIDENCY>(0x55);
    Hr = Dxgi->QueryResourceResidency(Objects, Status, 1);
    ok(Hr == S_OK && Resident(Status[0]), "residency through ID3D11Texture2D: %#lx, status %d\n", Hr, Status[0]);
    if (ForeignResource)
    {
        Objects[0] = ForeignResource;
        Status[0] = static_cast<DXGI_RESIDENCY>(0x55);
        Hr = Dxgi->QueryResourceResidency(Objects, Status, 1);
        ok(Hr == S_OK && Resident(Status[0]), "residency of another device's resource: %#lx, status %d\n", Hr, Status[0]);
    }
}

static void CheckOffers(const OFFER_OBJECTS *O)
{
    IDXGIDevice2 *Dxgi = O->Dxgi;
    IDXGIResource *TextureResource = O->TextureResource, *BufferResource = O->BufferResource;
    IDXGIResource *SharedResource = O->SharedResource, *StagingResource = O->StagingResource;
    IDXGIResource *ForeignResource = O->ForeignResource;
    DXGI_RESIDENCY Status[1];
    IDXGIResource *Offer[2] = { TextureResource, BufferResource };
    BOOL Discarded[2];
    HRESULT Hr;

    Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == S_OK, "OfferResources: %#lx\n", Hr);
    Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == E_INVALIDARG, "OfferResources(offered): %#lx\n", Hr);
    Status[0] = static_cast<DXGI_RESIDENCY>(0x55);
    Hr = Dxgi->QueryResourceResidency(reinterpret_cast<IUnknown **>(Offer), Status, 1);
    ok(Hr == S_OK && Resident(Status[0]), "residency of an offered resource: %#lx, status %d\n", Hr, Status[0]);
    Discarded[0] = 0x55;
    Hr = Dxgi->ReclaimResources(1, Offer, Discarded);
    ok(Hr == S_OK && Discarded[0] == FALSE, "ReclaimResources: %#lx, discarded %d\n", Hr, Discarded[0]);
    if (Hr == S_OK && !Discarded[0])
    {
        DWORD Pixel = FirstPixel(O->Device, O->Context, O->Texture);
        ok(Pixel == 0xff336699, "reclaimed content %#lx\n", Pixel);
    }
    Discarded[0] = 0x55;
    Hr = Dxgi->ReclaimResources(1, Offer, Discarded);
    ok(Hr == E_INVALIDARG && Discarded[0] == 0x55, "ReclaimResources(not offered): %#lx, discarded %d\n", Hr, Discarded[0]);

    Hr = Dxgi->OfferResources(2, Offer, DXGI_OFFER_RESOURCE_PRIORITY_LOW);
    ok(Hr == S_OK, "OfferResources(texture and buffer, low): %#lx\n", Hr);
    Discarded[0] = Discarded[1] = 0x55;
    Hr = Dxgi->ReclaimResources(2, Offer, Discarded);
    ok(Hr == S_OK && Discarded[0] == FALSE && Discarded[1] == FALSE, "ReclaimResources(texture and buffer): %#lx, discarded %d %d\n",
       Hr, Discarded[0], Discarded[1]);
    Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_HIGH);
    ok(Hr == S_OK, "OfferResources(high): %#lx\n", Hr);
    Hr = Dxgi->ReclaimResources(1, Offer, NULL);
    ok(Hr == S_OK, "ReclaimResources(no discarded array): %#lx\n", Hr);

    Hr = Dxgi->OfferResources(1, Offer, static_cast<DXGI_OFFER_RESOURCE_PRIORITY>(0));
    ok(Hr == E_INVALIDARG, "OfferResources(priority 0): %#lx\n", Hr);
    Hr = Dxgi->OfferResources(1, Offer, static_cast<DXGI_OFFER_RESOURCE_PRIORITY>(4));
    ok(Hr == E_INVALIDARG, "OfferResources(priority 4): %#lx\n", Hr);
    Hr = Dxgi->OfferResources(0, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == S_OK, "OfferResources(no resources): %#lx\n", Hr);
    Hr = Dxgi->ReclaimResources(0, Offer, Discarded);
    ok(Hr == S_OK, "ReclaimResources(no resources): %#lx\n", Hr);

    Offer[0] = SharedResource;
    Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == S_OK, "OfferResources(shared): %#lx\n", Hr);
    if (Hr == S_OK) ok_hr(Dxgi->ReclaimResources(1, Offer, NULL), S_OK);
    Offer[0] = StagingResource;
    Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == S_OK, "OfferResources(staging): %#lx\n", Hr);
    if (Hr == S_OK) ok_hr(Dxgi->ReclaimResources(1, Offer, NULL), S_OK);
    if (ForeignResource)
    {
        Offer[0] = ForeignResource;
        Hr = Dxgi->OfferResources(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
        ok(Hr == S_OK, "OfferResources(another device's resource): %#lx\n", Hr);
        if (Hr == S_OK) ok_hr(Dxgi->ReclaimResources(1, Offer, NULL), S_OK);
    }
}

static void CheckEvictionPriority(const OFFER_OBJECTS *O)
{
    IDXGIResource *TextureResource = O->TextureResource, *BufferResource = O->BufferResource;
    UINT Priority;
    HRESULT Hr;

    TextureResource->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_HIGH);
    Priority = 0x55;
    Hr = TextureResource->GetEvictionPriority(&Priority);
    ok(Hr == S_OK && Priority == DXGI_RESOURCE_PRIORITY_HIGH, "eviction priority high: %#lx %#x\n", Hr, Priority);
    Hr = TextureResource->SetEvictionPriority(5);
    Priority = 0x55;
    TextureResource->GetEvictionPriority(&Priority);
    ok(Hr == S_OK && Priority == 5, "eviction priority 5: %#lx, %#x\n", Hr, Priority);
    Priority = 0x55;
    Hr = BufferResource->GetEvictionPriority(&Priority);
    ok(Hr == S_OK && Priority == DXGI_RESOURCE_PRIORITY_NORMAL, "default eviction priority: %#lx, %#x\n", Hr, Priority);
    Priority = O->Texture->GetEvictionPriority();
    ok(Priority == 5, "ID3D11Resource eviction priority %#x\n", Priority);
}

static void CheckDevice4(const OFFER_OBJECTS *O)
{
    IDXGIResource *TextureResource = O->TextureResource;
    HRESULT Hr;
    IDXGIDevice3 *Dxgi3 = NULL;
    IDXGIDevice4 *Dxgi4 = NULL;

    Hr = O->Device->QueryInterface(IID_IDXGIDevice3, reinterpret_cast<void **>(&Dxgi3));
    ok(Hr == S_OK, "IDXGIDevice3: %#lx\n", Hr);
    if (Dxgi3)
    {
        Dxgi3->Trim();
        Dxgi3->Release();
    }
    Hr = O->Device->QueryInterface(IID_IDXGIDevice4, reinterpret_cast<void **>(&Dxgi4));
    ok(Hr == S_OK, "IDXGIDevice4: %#lx\n", Hr);
    if (Dxgi4)
    {
        IDXGIResource *Offer[1] = { TextureResource };
        DXGI_RECLAIM_RESOURCE_RESULTS Results[1] = { static_cast<DXGI_RECLAIM_RESOURCE_RESULTS>(0x55) };

        Hr = Dxgi4->OfferResources1(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL, 0);
        ok(Hr == S_OK, "OfferResources1: %#lx\n", Hr);
        Hr = Dxgi4->ReclaimResources1(1, Offer, Results);
        ok(Hr == S_OK && Results[0] == DXGI_RECLAIM_RESOURCE_RESULT_OK, "ReclaimResources1: %#lx, result %d\n", Hr, Results[0]);
        Hr = Dxgi4->OfferResources1(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL, DXGI_OFFER_RESOURCE_FLAG_ALLOW_DECOMMIT);
        ok(Hr == S_OK, "OfferResources1(allow decommit): %#lx\n", Hr);
        Results[0] = static_cast<DXGI_RECLAIM_RESOURCE_RESULTS>(0x55);
        Hr = Dxgi4->ReclaimResources1(1, Offer, Results);
        ok(Hr == S_OK && Results[0] == DXGI_RECLAIM_RESOURCE_RESULT_OK, "ReclaimResources1(allow decommit): %#lx, result %d\n",
           Hr, Results[0]);
        Hr = Dxgi4->OfferResources1(1, Offer, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL, 2);
        ok(Hr == S_OK, "OfferResources1(flags 2): %#lx\n", Hr);
        if (Hr == S_OK) Dxgi4->ReclaimResources1(1, Offer, Results);
        Dxgi4->Release();
    }
}

static void CheckNullArguments(const OFFER_OBJECTS *O)
{
    IUnknown *Objects[2] = { O->TextureResource, NULL };
    DXGI_RESIDENCY Status[2];
    BOOL Discarded[1];
    HRESULT Hr;

    Hr = O->Dxgi->QueryResourceResidency(NULL, Status, 1);
    ok(Hr == E_INVALIDARG, "QueryResourceResidency(no array): %#lx\n", Hr);
    Hr = O->Dxgi->QueryResourceResidency(Objects, NULL, 1);
    ok(Hr == E_INVALIDARG, "QueryResourceResidency(no status): %#lx\n", Hr);
    Status[0] = Status[1] = static_cast<DXGI_RESIDENCY>(0x55);
    Hr = O->Dxgi->QueryResourceResidency(Objects, Status, 2);
    ok(Hr == E_INVALIDARG && (int)Status[0] == 0x55 && (int)Status[1] == 0x55, "QueryResourceResidency(NULL element): %#lx, status %d %d\n",
       Hr, Status[0], Status[1]);
    Hr = O->Dxgi->OfferResources(1, NULL, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
    ok(Hr == E_INVALIDARG, "OfferResources(no array): %#lx\n", Hr);
    Hr = O->Dxgi->ReclaimResources(1, NULL, Discarded);
    ok(Hr == E_INVALIDARG, "ReclaimResources(no array): %#lx\n", Hr);
}

START_TEST(offer_resources)
{
    ID3D11Device *Device = NULL, *Other = NULL;
    ID3D11DeviceContext *Context = NULL;
    IDXGIDevice2 *Dxgi = NULL;
    ID3D11Texture2D *Texture, *Shared, *Staging, *Foreign;
    ID3D11Buffer *Buffer = NULL;
    IDXGIResource *TextureResource, *BufferResource, *SharedResource, *StagingResource, *ForeignResource;
    D3D11_BUFFER_DESC BufferDesc = {};
    HRESULT Hr;

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create || FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context)))
    {
        skip("no D3D11 device\n");
        FreeLibrary(Module);
        return;
    }
    Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Other, NULL, NULL);
    Hr = Device->QueryInterface(IID_IDXGIDevice2, reinterpret_cast<void **>(&Dxgi));
    ok(Hr == S_OK, "IDXGIDevice2: %#lx\n", Hr);
    if (!Dxgi)
    {
        if (Other) Other->Release();
        Context->Release();
        Device->Release();
        FreeLibrary(Module);
        return;
    }

    Texture = CreateTexture(Device, 0, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    Shared = CreateTexture(Device, D3D11_RESOURCE_MISC_SHARED, D3D11_USAGE_DEFAULT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    Staging = CreateTexture(Device, 0, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
    Foreign = Other ? CreateTexture(Other, 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0) : NULL;
    BufferDesc.ByteWidth = 256; BufferDesc.Usage = D3D11_USAGE_DEFAULT; BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Device->CreateBuffer(&BufferDesc, NULL, &Buffer);
    TextureResource = DxgiResource(Texture);
    BufferResource = DxgiResource(Buffer);
    SharedResource = DxgiResource(Shared);
    StagingResource = DxgiResource(Staging);
    ForeignResource = DxgiResource(Foreign);
    ok(TextureResource && BufferResource && SharedResource && StagingResource, "resources %p %p %p %p\n",
       TextureResource, BufferResource, SharedResource, StagingResource);
    if (TextureResource && BufferResource && SharedResource && StagingResource)
    {
        OFFER_OBJECTS Objects = { Device, Context, Dxgi, Texture, TextureResource, BufferResource, SharedResource,
                                  StagingResource, ForeignResource };

        CheckResidency(&Objects);
        CheckOffers(&Objects);
        CheckEvictionPriority(&Objects);
        CheckDevice4(&Objects);
        CheckNullArguments(&Objects);
    }
    if (ForeignResource) ForeignResource->Release();
    if (StagingResource) StagingResource->Release();
    if (SharedResource) SharedResource->Release();
    if (BufferResource) BufferResource->Release();
    if (TextureResource) TextureResource->Release();
    if (Foreign) Foreign->Release();
    if (Buffer) Buffer->Release();
    if (Staging) Staging->Release();
    if (Shared) Shared->Release();
    if (Texture) Texture->Release();
    Dxgi->Release();
    if (Other) Other->Release();
    Context->Release();
    Device->Release();
    FreeLibrary(Module);
}
