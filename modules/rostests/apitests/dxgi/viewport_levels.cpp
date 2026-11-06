/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pixels covered by fractional Direct3D 11 viewports at each feature level
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <d3d11.h>

#define TARGET_SIZE 64u

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                        UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static const DWORD VertexCode[] =
{
    0x43425844, 0x4fb19b86, 0x955fa240, 0x1a630688, 0x24eb9db4, 0x00000001, 0x000001e0, 0x00000006,
    0x00000038, 0x00000084, 0x000000d0, 0x00000134, 0x00000178, 0x000001ac, 0x53414e58, 0x00000044,
    0x00000044, 0xfffe0200, 0x00000020, 0x00000024, 0x00240000, 0x00240000, 0x00240000, 0x00240000,
    0x00240000, 0xfffe0200, 0x0200001f, 0x80000005, 0x900f0000, 0x02000001, 0xc00f0000, 0x80e40000,
    0x0000ffff, 0x50414e58, 0x00000044, 0x00000044, 0xfffe0200, 0x00000020, 0x00000024, 0x00240000,
    0x00240000, 0x00240000, 0x00240000, 0x00240000, 0xfffe0200, 0x0200001f, 0x80000005, 0x900f0000,
    0x02000001, 0xc00f0000, 0x80e40000, 0x0000ffff, 0x396e6f41, 0x0000005c, 0x0000005c, 0xfffe0200,
    0x00000034, 0x00000028, 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240001, 0x00000000,
    0xfffe0200, 0x0200001f, 0x80000005, 0x900f0000, 0x04000004, 0xc0030000, 0x90ff0000, 0xa0e40000,
    0x90e40000, 0x02000001, 0xc00c0000, 0x90e40000, 0x0000ffff, 0x52444853, 0x0000003c, 0x00010040,
    0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
    0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e, 0x4e475349, 0x0000002c,
    0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f,
    0x49534f50, 0x4e4f4954, 0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
    0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
};

static const DWORD PixelCode[] =
{
    0x43425844, 0xe7ffb369, 0x72bb84ee, 0x6f684dcd, 0xd367d788, 0x00000001, 0x00000158, 0x00000005,
    0x00000034, 0x00000080, 0x000000cc, 0x00000114, 0x00000124, 0x53414e58, 0x00000044, 0x00000044,
    0xffff0200, 0x00000014, 0x00000030, 0x00240001, 0x00300000, 0x00300000, 0x00240000, 0x00300000,
    0x00000000, 0x00000001, 0x00000000, 0xffff0200, 0x02000001, 0x800f0800, 0xa0e40000, 0x0000ffff,
    0x396e6f41, 0x00000044, 0x00000044, 0xffff0200, 0x00000014, 0x00000030, 0x00240001, 0x00300000,
    0x00300000, 0x00240000, 0x00300000, 0x00000000, 0x00000001, 0x00000000, 0xffff0200, 0x02000001,
    0x800f0800, 0xa0e40000, 0x0000ffff, 0x52444853, 0x00000040, 0x00000040, 0x00000010, 0x04000059,
    0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x06000036, 0x001020f2,
    0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e, 0x4e475349, 0x00000008, 0x00000000,
    0x00000008, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
    0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054,
};

struct VIEWPORT_CASE
{
    FLOAT Origin;
    FLOAT Width;
    FLOAT Height;
    BOOL PartialQuad;
    int Legacy[2];
    int Current[2];
};

/* Below feature level 11_0 the extents are integers truncated toward zero; from 11_0 they stay fractional. */
static const VIEWPORT_CASE Cases[] =
{
    {0.0f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 31}},
    {-1.0f, 32.0f, 32.0f, FALSE, {0, 30}, {0, 30}},
    {-0.75f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 30}},
    {-0.5f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 30}},
    {-0.4f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 30}},
    {-1.0f / 128.0f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 30}},
    {0.25f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 31}},
    {0.5f, 32.0f, 32.0f, FALSE, {0, 31}, {0, 31}},
    {0.75f, 32.0f, 32.0f, FALSE, {0, 31}, {1, 31}},
    {1.0f, 32.0f, 32.0f, FALSE, {1, 32}, {1, 32}},
    {4.0f, 32.25f, 32.25f, FALSE, {4, 35}, {4, 35}},
    {4.0f, 32.5f, 32.5f, FALSE, {4, 35}, {4, 35}},
    {4.0f, 32.75f, 32.75f, FALSE, {4, 35}, {4, 35}},
    {4.25f, 32.5f, 32.5f, FALSE, {4, 35}, {4, 35}},
    {4.5f, 32.5f, 32.5f, FALSE, {4, 35}, {4, 36}},
    {4.75f, 32.5f, 32.5f, FALSE, {4, 35}, {5, 36}},
    {0.0f, 8.9f, 32.0f, TRUE, {0, 5}, {0, 6}},
};

static void CoveredRange(const D3D11_MAPPED_SUBRESOURCE *Mapped, BOOL Rows, int Range[2])
{
    Range[0] = Range[1] = -1;
    for (UINT i = 0; i < TARGET_SIZE; ++i)
    {
        UINT x = Rows ? 16 : i, y = Rows ? i : 16;
        UINT32 Pixel = reinterpret_cast<const UINT32 *>(static_cast<const BYTE *>(Mapped->pData) + y * Mapped->RowPitch)[x];
        if (Pixel != 0xff00ff00) continue;
        if (Range[0] < 0) Range[0] = i;
        Range[1] = i;
    }
}

static void TraceAdapter(ID3D11Device *Device)
{
    IDXGIDevice *DxgiDevice = NULL;
    IDXGIAdapter *Adapter = NULL;
    DXGI_ADAPTER_DESC Desc = {};
    if (SUCCEEDED(Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice))))
    {
        if (SUCCEEDED(DxgiDevice->GetAdapter(&Adapter)))
        {
            if (SUCCEEDED(Adapter->GetDesc(&Desc))) trace("adapter %ls vendor %#x\n", Desc.Description, Desc.VendorId);
            Adapter->Release();
        }
        DxgiDevice->Release();
    }
}

static void CheckLevel(CREATE_DEVICE Create, D3D_FEATURE_LEVEL Level)
{
    static const FLOAT White[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const FLOAT Green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const FLOAT Quads[24] =
    {
        -1.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.5f, -1.0f, 0.0f, 0.5f, 1.0f, 0.0f,
    };
    static const D3D11_INPUT_ELEMENT_DESC Layout = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    D3D_FEATURE_LEVEL Got;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Device, &Got, &Context);
    if (FAILED(Hr))
    {
        skip("Feature level %#x: %#lx\n", Level, Hr);
        return;
    }
    TraceAdapter(Device);

    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = TextureDesc.Height = TARGET_SIZE;
    TextureDesc.MipLevels = TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE2D_DESC StagingDesc = TextureDesc;
    StagingDesc.Usage = D3D11_USAGE_STAGING;
    StagingDesc.BindFlags = 0;
    StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    D3D11_BUFFER_DESC VertexDesc = {sizeof(Quads), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    D3D11_BUFFER_DESC ColorDesc = {sizeof(Green), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA VertexData = {Quads, 0, 0}, ColorData = {Green, 0, 0};
    ID3D11Texture2D *Target = NULL, *Staging = NULL;
    ID3D11RenderTargetView *View = NULL;
    ID3D11Buffer *Vertices = NULL, *Color = NULL;
    ID3D11InputLayout *InputLayout = NULL;
    ID3D11VertexShader *VertexShader = NULL;
    ID3D11PixelShader *PixelShader = NULL;
    Hr = Device->CreateTexture2D(&TextureDesc, NULL, &Target);
    ok(Hr == S_OK, "CreateTexture2D: %#lx\n", Hr);
    Hr = Device->CreateTexture2D(&StagingDesc, NULL, &Staging);
    ok(Hr == S_OK, "CreateTexture2D staging: %#lx\n", Hr);
    if (Target) Device->CreateRenderTargetView(Target, NULL, &View);
    Device->CreateBuffer(&VertexDesc, &VertexData, &Vertices);
    Device->CreateBuffer(&ColorDesc, &ColorData, &Color);
    Device->CreateInputLayout(&Layout, 1, VertexCode, sizeof(VertexCode), &InputLayout);
    Device->CreateVertexShader(VertexCode, sizeof(VertexCode), NULL, &VertexShader);
    Device->CreatePixelShader(PixelCode, sizeof(PixelCode), NULL, &PixelShader);
    ok(View && Vertices && Color && InputLayout && VertexShader && PixelShader, "Feature level %#x: missing pipeline objects\n", Level);
    if (Staging && View && Vertices && Color && InputLayout && VertexShader && PixelShader)
    {
        UINT Stride = 3 * sizeof(FLOAT), Offset = 0;
        Context->IASetInputLayout(InputLayout);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        Context->IASetVertexBuffers(0, 1, &Vertices, &Stride, &Offset);
        Context->VSSetShader(VertexShader, NULL, 0);
        Context->PSSetShader(PixelShader, NULL, 0);
        Context->PSSetConstantBuffers(0, 1, &Color);
        Context->OMSetRenderTargets(1, &View, NULL);
        for (UINT i = 0; i < ARRAYSIZE(Cases); ++i)
        {
            const VIEWPORT_CASE &Case = Cases[i];
            const int *Expected = Level < D3D_FEATURE_LEVEL_11_0 ? Case.Legacy : Case.Current;
            D3D11_VIEWPORT Viewport = {Case.Origin, Case.Origin, Case.Width, Case.Height, 0.25f, 0.75f};
            D3D11_VIEWPORT Stored = Viewport;
            D3D11_VIEWPORT Queried = {};
            D3D11_MAPPED_SUBRESOURCE Mapped = {};
            UINT Count = 1;
            int Columns[2], Rows[2];
            if (Level < D3D_FEATURE_LEVEL_11_0)
            {
                Stored.TopLeftX = Stored.TopLeftY = static_cast<FLOAT>(static_cast<int>(Case.Origin));
                Stored.Width = static_cast<FLOAT>(static_cast<int>(Case.Width));
                Stored.Height = static_cast<FLOAT>(static_cast<int>(Case.Height));
            }
            Context->ClearRenderTargetView(View, White);
            Context->RSSetViewports(1, &Viewport);
            Context->RSGetViewports(&Count, &Queried);
            ok(Count == 1 && !memcmp(&Queried, &Stored, sizeof(Stored)),
               "level %#x origin %.4f: queried %u viewport (%.4f,%.4f) %.4f x %.4f depth %.4f..%.4f, expected (%.4f,%.4f) %.4f x %.4f\n", Level, Case.Origin, Count,
               Queried.TopLeftX, Queried.TopLeftY, Queried.Width, Queried.Height, Queried.MinDepth, Queried.MaxDepth,
               Stored.TopLeftX, Stored.TopLeftY, Stored.Width, Stored.Height);
            Context->Draw(4, Case.PartialQuad ? 4 : 0);
            Context->CopyResource(Staging, Target);
            Hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
            ok(Hr == S_OK, "Map: %#lx\n", Hr);
            if (FAILED(Hr)) continue;
            CoveredRange(&Mapped, FALSE, Columns);
            CoveredRange(&Mapped, TRUE, Rows);
            Context->Unmap(Staging, 0);
            ok(Columns[0] == Expected[0] && Columns[1] == Expected[1], "level %#x origin %.4f width %.4f: columns %d..%d, expected %d..%d\n",
               Level, Case.Origin, Case.Width, Columns[0], Columns[1], Expected[0], Expected[1]);
            if (!Case.PartialQuad)
                ok(Rows[0] == Expected[0] && Rows[1] == Expected[1], "level %#x origin %.4f height %.4f: rows %d..%d, expected %d..%d\n",
                   Level, Case.Origin, Case.Height, Rows[0], Rows[1], Expected[0], Expected[1]);
        }
    }
    if (PixelShader) PixelShader->Release();
    if (VertexShader) VertexShader->Release();
    if (InputLayout) InputLayout->Release();
    if (Color) Color->Release();
    if (Vertices) Vertices->Release();
    if (View) View->Release();
    if (Staging) Staging->Release();
    if (Target) Target->Release();
    Context->Release();
    Device->Release();
}

START_TEST(viewport_levels)
{
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    if (!Create)
    {
        skip("D3D11CreateDevice is unavailable\n");
        return;
    }
    CheckLevel(Create, D3D_FEATURE_LEVEL_10_0);
    CheckLevel(Create, D3D_FEATURE_LEVEL_10_1);
    CheckLevel(Create, D3D_FEATURE_LEVEL_11_0);
    FreeLibrary(Module);
}
