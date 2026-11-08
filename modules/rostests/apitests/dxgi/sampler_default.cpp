/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the default sampler a NULL sampler slot selects and device teardown with it bound
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <d3d11.h>
#include <d3dcompiler.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                        UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *COMPILE)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *,
                                  const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char VertexSource[] =
    "float4 main(uint id : SV_VertexID) : SV_Position"
    "{ return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0.0, 1.0); }";

/* Halfway between the two texels: the default linear filter gives grey, a point filter white. */
static const char PixelSource[] =
    "Texture2D t : register(t0); SamplerState s : register(s0);"
    "float4 main(float4 p : SV_Position) : SV_Target { return t.Sample(s, float2(0.5, 0.5)); }";

static ID3DBlob *CompileShader(COMPILE Compile, const char *Source, const char *Profile)
{
    ID3DBlob *Code = NULL, *Errors = NULL;
    HRESULT hr = Compile(Source, strlen(Source), NULL, NULL, NULL, "main", Profile, 0, 0, &Code, &Errors);

    ok(hr == S_OK, "Compile %s: %#lx %s\n", Profile, hr,
       Errors ? static_cast<const char *>(Errors->GetBufferPointer()) : "");
    if (Errors) Errors->Release();
    return SUCCEEDED(hr) ? Code : NULL;
}

static void DrawThroughNullSampler(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3DBlob *VertexCode,
                                   ID3DBlob *PixelCode)
{
    static const UINT Texels[2] = {0xff000000, 0xffffffff};
    ID3D11VertexShader *VertexShader = NULL;
    ID3D11PixelShader *PixelShader = NULL;
    ID3D11Texture2D *Source = NULL, *Target = NULL, *Staging = NULL;
    ID3D11ShaderResourceView *View = NULL;
    ID3D11RenderTargetView *TargetView = NULL;
    ID3D11SamplerState *Sampler = NULL, *Bound = reinterpret_cast<ID3D11SamplerState *>(1);
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Data = {Texels, sizeof(Texels), 0};
    D3D11_MAPPED_SUBRESOURCE Map = {};
    HRESULT hr;

    hr = Device->CreateVertexShader(VertexCode->GetBufferPointer(), VertexCode->GetBufferSize(), NULL, &VertexShader);
    ok_hex(hr, S_OK);
    hr = Device->CreatePixelShader(PixelCode->GetBufferPointer(), PixelCode->GetBufferSize(), NULL, &PixelShader);
    ok_hex(hr, S_OK);
    Desc.Width = 2;
    Desc.Height = 1;
    Desc.MipLevels = Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_IMMUTABLE;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = Device->CreateTexture2D(&Desc, &Data, &Source);
    ok_hex(hr, S_OK);
    if (Source) hr = Device->CreateShaderResourceView(Source, NULL, &View);
    ok_hex(hr, S_OK);
    Desc.Width = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = Device->CreateTexture2D(&Desc, NULL, &Target);
    ok_hex(hr, S_OK);
    if (Target) hr = Device->CreateRenderTargetView(Target, NULL, &TargetView);
    ok_hex(hr, S_OK);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = Device->CreateTexture2D(&Desc, NULL, &Staging);
    ok_hex(hr, S_OK);

    if (VertexShader && PixelShader && View && TargetView && Staging)
    {
        const D3D11_VIEWPORT Viewport = {0, 0, 1, 1, 0, 1};
        const FLOAT Clear[4] = {0, 0, 1, 0};

        Context->ClearRenderTargetView(TargetView, Clear);
        Context->OMSetRenderTargets(1, &TargetView, NULL);
        Context->VSSetShader(VertexShader, NULL, 0);
        Context->PSSetShader(PixelShader, NULL, 0);
        Context->PSSetShaderResources(0, 1, &View);
        Context->PSSetSamplers(0, 1, &Sampler);
        Context->PSGetSamplers(0, 1, &Bound);
        ok(Bound == NULL, "A NULL sampler slot reads back %p\n", Bound);
        if (Bound) Bound->Release();
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->RSSetViewports(1, &Viewport);
        Context->Draw(3, 0);
        Context->CopyResource(Staging, Target);
        hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
        ok_hex(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            UINT Pixel = *static_cast<const UINT *>(Map.pData);
            BOOL Grey = TRUE;

            for (UINT Channel = 0; Channel < 3; ++Channel)
            {
                UINT Value = (Pixel >> (Channel * 8)) & 0xff;
                if (Value != 0x7f && Value != 0x80) Grey = FALSE;
            }
            ok(Grey && (Pixel >> 24) == 0xff, "Sampled through a NULL sampler: %#x, expected 0xff7f7f7f or 0xff808080\n",
               Pixel);
            Context->Unmap(Staging, 0);
        }
    }

    if (Staging) Staging->Release();
    if (TargetView) TargetView->Release();
    if (Target) Target->Release();
    if (View) View->Release();
    if (Source) Source->Release();
    if (PixelShader) PixelShader->Release();
    if (VertexShader) VertexShader->Release();
}

START_TEST(sampler_default)
{
    static const D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll");
    HMODULE Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    CREATE_DEVICE Create = Runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice")) : NULL;
    COMPILE Compile = Compiler ? reinterpret_cast<COMPILE>(GetProcAddress(Compiler, "D3DCompile")) : NULL;
    ID3DBlob *VertexCode = NULL, *PixelCode = NULL;

    if (!Create || !Compile)
    {
        skip("D3D11CreateDevice or D3DCompile is unavailable\n");
        if (Compiler) FreeLibrary(Compiler);
        if (Runtime) FreeLibrary(Runtime);
        return;
    }
    VertexCode = CompileShader(Compile, VertexSource, "vs_4_0");
    PixelCode = CompileShader(Compile, PixelSource, "ps_4_0");

    /* The second device is created after the first was released with the default sampler in every slot. */
    for (UINT Round = 0; Round < 2 && VertexCode && PixelCode; ++Round)
    {
        ID3D11Device *Device = NULL;
        ID3D11DeviceContext *Context = NULL;
        HRESULT hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels, ARRAYSIZE(Levels), D3D11_SDK_VERSION,
                            &Device, NULL, &Context);

        if (FAILED(hr))
        {
            skip("No hardware Direct3D 11 device: %#lx\n", hr);
            break;
        }
        DrawThroughNullSampler(Device, Context, VertexCode, PixelCode);
        Context->ClearState();
        Context->Release();
        ok(Device->Release() == 0, "Round %u: the device is still referenced\n", Round);
    }

    if (PixelCode) PixelCode->Release();
    if (VertexCode) VertexCode->Release();
    FreeLibrary(Compiler);
    FreeLibrary(Runtime);
}
