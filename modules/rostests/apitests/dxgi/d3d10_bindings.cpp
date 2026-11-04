/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 10 pipeline bindings of objects whose last application reference is released
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <float.h>

typedef HRESULT (WINAPI *CREATE_DEVICE10)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device **);
typedef HRESULT (WINAPI *COMPILE)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *,
                                  const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static void Check(const char *Name, ULONG Released, void *Got, void *Original, BOOL Kept)
{
    ok(Released == 0, "%s: release returned %lu\n", Name, Released);
    ok(Got == (Kept ? Original : NULL), "%s: got %p, expected %p\n", Name, Got, Kept ? Original : NULL);
}

static ID3DBlob *Compile(COMPILE Compiler, const char *Source, const char *Profile)
{
    ID3DBlob *Code = NULL, *Errors = NULL;

    if (!Compiler || FAILED(Compiler(Source, strlen(Source), NULL, NULL, NULL, "main", Profile, 0, 0, &Code, &Errors)))
        Code = NULL;
    if (Errors)
        Errors->Release();
    return Code;
}

static void TestStates(ID3D10Device *Device)
{
    D3D10_SAMPLER_DESC Sampler = {D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP, D3D10_TEXTURE_ADDRESS_CLAMP,
                                  D3D10_TEXTURE_ADDRESS_CLAMP, 0.0f, 1, D3D10_COMPARISON_NEVER, {0}, -FLT_MAX, FLT_MAX};
    D3D10_RASTERIZER_DESC Rasterizer = {D3D10_FILL_SOLID, D3D10_CULL_BACK, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE};
    D3D10_BLEND_DESC Blend = {};
    D3D10_DEPTH_STENCIL_DESC Depth = {};
    ID3D10SamplerState *SamplerState, *GotSampler, *Again;
    ID3D10RasterizerState *RasterizerState, *GotRasterizer;
    ID3D10BlendState *BlendState, *GotBlend;
    ID3D10DepthStencilState *DepthState, *GotDepth;
    FLOAT Factor[4];
    UINT Mask, Ref;

    for (UINT Variant = 0; Variant < 2; ++Variant)
    {
        Sampler.AddressU = Variant ? D3D10_TEXTURE_ADDRESS_WRAP : D3D10_TEXTURE_ADDRESS_CLAMP;
        if (SUCCEEDED(Device->CreateSamplerState(&Sampler, &SamplerState)))
        {
            Device->CreateSamplerState(&Sampler, &Again);
            ok(Again == SamplerState, "sampler %s: second create %p, expected %p\n", Variant ? "wrap" : "default", Again, SamplerState);
            Again->Release();
            Device->PSSetSamplers(0, 1, &SamplerState);
            ULONG Count = SamplerState->Release();
            GotSampler = NULL;
            Device->PSGetSamplers(0, 1, &GotSampler);
            Check(Variant ? "sampler wrap" : "sampler default", Count, GotSampler, SamplerState, !Variant);
            if (GotSampler) GotSampler->Release();
            GotSampler = NULL;
            Device->PSSetSamplers(0, 1, &GotSampler);
        }

        Rasterizer.CullMode = Variant ? D3D10_CULL_NONE : D3D10_CULL_BACK;
        if (SUCCEEDED(Device->CreateRasterizerState(&Rasterizer, &RasterizerState)))
        {
            Device->RSSetState(RasterizerState);
            ULONG Count = RasterizerState->Release();
            GotRasterizer = NULL;
            Device->RSGetState(&GotRasterizer);
            Check(Variant ? "rasterizer cull none" : "rasterizer default", Count, GotRasterizer, RasterizerState, !Variant);
            if (GotRasterizer) GotRasterizer->Release();
            Device->RSSetState(NULL);
        }

        Blend.RenderTargetWriteMask[0] = Variant ? D3D10_COLOR_WRITE_ENABLE_RED : D3D10_COLOR_WRITE_ENABLE_ALL;
        Blend.SrcBlend = Blend.SrcBlendAlpha = D3D10_BLEND_ONE;
        Blend.DestBlend = Blend.DestBlendAlpha = D3D10_BLEND_ZERO;
        Blend.BlendOp = Blend.BlendOpAlpha = D3D10_BLEND_OP_ADD;
        for (UINT i = 1; i < 8; ++i) Blend.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;
        if (SUCCEEDED(Device->CreateBlendState(&Blend, &BlendState)))
        {
            Device->OMSetBlendState(BlendState, NULL, ~0u);
            ULONG Count = BlendState->Release();
            GotBlend = NULL;
            Device->OMGetBlendState(&GotBlend, Factor, &Mask);
            Check(Variant ? "blend red only" : "blend default", Count, GotBlend, BlendState, !Variant);
            if (GotBlend) GotBlend->Release();
            Device->OMSetBlendState(NULL, NULL, ~0u);
        }

        Depth.DepthEnable = TRUE;
        Depth.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
        Depth.DepthFunc = Variant ? D3D10_COMPARISON_ALWAYS : D3D10_COMPARISON_LESS;
        Depth.StencilReadMask = Depth.StencilWriteMask = 0xff;
        Depth.FrontFace.StencilFailOp = Depth.FrontFace.StencilDepthFailOp = Depth.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
        Depth.FrontFace.StencilFunc = D3D10_COMPARISON_ALWAYS;
        Depth.BackFace = Depth.FrontFace;
        if (SUCCEEDED(Device->CreateDepthStencilState(&Depth, &DepthState)))
        {
            Device->OMSetDepthStencilState(DepthState, 0);
            ULONG Count = DepthState->Release();
            GotDepth = NULL;
            Device->OMGetDepthStencilState(&GotDepth, &Ref);
            Check(Variant ? "depth always" : "depth default", Count, GotDepth, DepthState, !Variant);
            if (GotDepth) GotDepth->Release();
            Device->OMSetDepthStencilState(NULL, 0);
        }
    }
}

static void TestViews(ID3D10Device *Device)
{
    D3D10_TEXTURE2D_DESC Desc = {32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D10_USAGE_DEFAULT,
                                 D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET, 0, 0};
    D3D10_BUFFER_DESC BufferDesc = {256, D3D10_USAGE_DEFAULT, 0, 0, 0};
    ID3D10Texture2D *Texture = NULL, *DepthTexture = NULL;
    ID3D10ShaderResourceView *Srv, *GotSrv;
    ID3D10RenderTargetView *Rtv, *GotRtv;
    ID3D10DepthStencilView *Dsv, *GotDsv;
    ID3D10Buffer *Buffer, *GotBuffer;
    D3D10_QUERY_DESC QueryDesc = {D3D10_QUERY_OCCLUSION_PREDICATE, 0};
    ID3D10Predicate *Predicate, *GotPredicate;
    UINT Stride = 16, Offset = 0;
    DXGI_FORMAT Format;
    BOOL Value;

    if (FAILED(Device->CreateTexture2D(&Desc, NULL, &Texture)))
        return;
    if (SUCCEEDED(Device->CreateShaderResourceView(Texture, NULL, &Srv)))
    {
        Device->PSSetShaderResources(0, 1, &Srv);
        ULONG Count = Srv->Release();
        GotSrv = NULL;
        Device->PSGetShaderResources(0, 1, &GotSrv);
        Check("srv", Count, GotSrv, Srv, FALSE);
        if (GotSrv) GotSrv->Release();
        GotSrv = NULL;
        Device->PSSetShaderResources(0, 1, &GotSrv);
    }
    if (SUCCEEDED(Device->CreateRenderTargetView(Texture, NULL, &Rtv)))
    {
        Device->OMSetRenderTargets(1, &Rtv, NULL);
        ULONG Count = Rtv->Release();
        GotRtv = NULL;
        Device->OMGetRenderTargets(1, &GotRtv, NULL);
        Check("rtv", Count, GotRtv, Rtv, FALSE);
        if (GotRtv) GotRtv->Release();
        Device->OMSetRenderTargets(0, NULL, NULL);
    }
    Desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    Desc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(Device->CreateTexture2D(&Desc, NULL, &DepthTexture))
        && SUCCEEDED(Device->CreateDepthStencilView(DepthTexture, NULL, &Dsv)))
    {
        Device->OMSetRenderTargets(0, NULL, Dsv);
        ULONG Count = Dsv->Release();
        GotDsv = NULL;
        Device->OMGetRenderTargets(0, NULL, &GotDsv);
        Check("dsv", Count, GotDsv, Dsv, FALSE);
        if (GotDsv) GotDsv->Release();
        Device->OMSetRenderTargets(0, NULL, NULL);
    }
    if (DepthTexture) DepthTexture->Release();
    Texture->Release();

    for (UINT Kind = 0; Kind < 3; ++Kind)
    {
        static const UINT Binds[] = {D3D10_BIND_VERTEX_BUFFER, D3D10_BIND_INDEX_BUFFER, D3D10_BIND_CONSTANT_BUFFER};
        static const char *const Names[] = {"vertex buffer", "index buffer", "constant buffer"};
        BufferDesc.BindFlags = Binds[Kind];
        if (FAILED(Device->CreateBuffer(&BufferDesc, NULL, &Buffer)))
            continue;
        GotBuffer = NULL;
        if (Kind == 0) Device->IASetVertexBuffers(0, 1, &Buffer, &Stride, &Offset);
        if (Kind == 1) Device->IASetIndexBuffer(Buffer, DXGI_FORMAT_R16_UINT, 0);
        if (Kind == 2) Device->VSSetConstantBuffers(0, 1, &Buffer);
        ULONG Count = Buffer->Release();
        if (Kind == 0) Device->IAGetVertexBuffers(0, 1, &GotBuffer, &Stride, &Offset);
        if (Kind == 1) Device->IAGetIndexBuffer(&GotBuffer, &Format, &Offset);
        if (Kind == 2) Device->VSGetConstantBuffers(0, 1, &GotBuffer);
        Check(Names[Kind], Count, GotBuffer, Buffer, FALSE);
        if (GotBuffer) GotBuffer->Release();
        GotBuffer = NULL;
        Device->IASetVertexBuffers(0, 1, &GotBuffer, &Stride, &Offset);
        Device->IASetIndexBuffer(NULL, DXGI_FORMAT_UNKNOWN, 0);
        Device->VSSetConstantBuffers(0, 1, &GotBuffer);
    }

    if (SUCCEEDED(Device->CreatePredicate(&QueryDesc, &Predicate)))
    {
        Device->SetPredication(Predicate, TRUE);
        ULONG Count = Predicate->Release();
        GotPredicate = NULL;
        Device->GetPredication(&GotPredicate, &Value);
        Check("predicate", Count, GotPredicate, Predicate, FALSE);
        if (GotPredicate) GotPredicate->Release();
        Device->SetPredication(NULL, FALSE);
    }
}

static void TestShaders(ID3D10Device *Device, COMPILE Compiler)
{
    static const char VsSource[] = "float4 main(float4 p : POSITION) : SV_POSITION { return p; }";
    static const char PsSource[] = "float4 main() : SV_TARGET { return 1; }";
    static const D3D10_INPUT_ELEMENT_DESC Element = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                                                     D3D10_INPUT_PER_VERTEX_DATA, 0};
    ID3DBlob *Vs = Compile(Compiler, VsSource, "vs_4_0"), *Ps = Compile(Compiler, PsSource, "ps_4_0");
    ID3D10VertexShader *VertexShader, *GotVertexShader;
    ID3D10PixelShader *PixelShader, *GotPixelShader;
    ID3D10InputLayout *Layout, *GotLayout;

    if (!Vs || !Ps)
    {
        skip("no shader compiler\n");
        if (Vs) Vs->Release();
        if (Ps) Ps->Release();
        return;
    }
    if (SUCCEEDED(Device->CreateVertexShader(Vs->GetBufferPointer(), Vs->GetBufferSize(), &VertexShader)))
    {
        Device->VSSetShader(VertexShader);
        ULONG Count = VertexShader->Release();
        GotVertexShader = NULL;
        Device->VSGetShader(&GotVertexShader);
        Check("vertex shader", Count, GotVertexShader, VertexShader, FALSE);
        if (GotVertexShader) GotVertexShader->Release();
        Device->VSSetShader(NULL);
    }
    if (SUCCEEDED(Device->CreatePixelShader(Ps->GetBufferPointer(), Ps->GetBufferSize(), &PixelShader)))
    {
        Device->PSSetShader(PixelShader);
        ULONG Count = PixelShader->Release();
        GotPixelShader = NULL;
        Device->PSGetShader(&GotPixelShader);
        Check("pixel shader", Count, GotPixelShader, PixelShader, FALSE);
        if (GotPixelShader) GotPixelShader->Release();
        Device->PSSetShader(NULL);
    }
    if (SUCCEEDED(Device->CreateInputLayout(&Element, 1, Vs->GetBufferPointer(), Vs->GetBufferSize(), &Layout)))
    {
        Device->IASetInputLayout(Layout);
        ULONG Count = Layout->Release();
        GotLayout = NULL;
        Device->IAGetInputLayout(&GotLayout);
        Check("input layout", Count, GotLayout, Layout, FALSE);
        if (GotLayout) GotLayout->Release();
        Device->IASetInputLayout(NULL);
    }
    Vs->Release();
    Ps->Release();
}

START_TEST(d3d10_bindings)
{
    HMODULE Module = LoadLibraryW(L"d3d10.dll");
    HMODULE CompilerModule = LoadLibraryW(L"d3dcompiler_47.dll");
    CREATE_DEVICE10 Create = Module ? reinterpret_cast<CREATE_DEVICE10>(GetProcAddress(Module, "D3D10CreateDevice")) : NULL;
    COMPILE Compiler = CompilerModule ? reinterpret_cast<COMPILE>(GetProcAddress(CompilerModule, "D3DCompile")) : NULL;
    ID3D10Device *Device = NULL;

    if (!Create || FAILED(Create(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &Device)))
    {
        skip("no hardware D3D10 device\n");
        if (Module) FreeLibrary(Module);
        return;
    }
    TestStates(Device);
    TestViews(Device);
    TestShaders(Device, Compiler);
    Device->Release();
    if (CompilerModule) FreeLibrary(CompilerModule);
    FreeLibrary(Module);
}
