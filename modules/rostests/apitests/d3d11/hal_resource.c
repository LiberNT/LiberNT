/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 resource contents survive copies, volume clears and depth-stencil views
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <wine/test.h>

#define DEPTH_SIZE 16

static const char VertexSource[] =
    "float4 main(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

static const char ComparePixelSource[] =
    "Texture2D t;\n"
    "SamplerComparisonState s;\n"
    "float main() : SV_Target { return t.SampleCmpLevelZero(s, float2(0.5f, 0.5f), 0.5f); }\n";

static const char StencilPixelSource[] =
    "Texture2D<uint4> t;\n"
    "float main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)).g; }\n";

static ID3D11Device *Device;
static ID3D11DeviceContext *Context;

static ID3DBlob *Compile(const char *Source, const char *Target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr;

    hr = D3DCompile(Source, strlen(Source), NULL, NULL, NULL, "main", Target, 0, 0, &code, &errors);
    ok(hr == S_OK, "D3DCompile for %s: 0x%08lx %s\n", Target, hr,
       errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
    if (errors)
        ID3D10Blob_Release(errors);
    return code;
}

static ID3D11PixelShader *CreatePixelShader(const char *Source)
{
    ID3D11PixelShader *shader = NULL;
    ID3DBlob *code = Compile(Source, "ps_5_0");

    if (code)
    {
        ok(ID3D11Device_CreatePixelShader(Device, ID3D10Blob_GetBufferPointer(code),
                                          ID3D10Blob_GetBufferSize(code), NULL, &shader) == S_OK,
           "CreatePixelShader failed\n");
        ID3D10Blob_Release(code);
    }
    return shader;
}

static ID3D11VertexShader *CreateVertexShader(void)
{
    ID3D11VertexShader *shader = NULL;
    ID3DBlob *code = Compile(VertexSource, "vs_5_0");

    if (code)
    {
        ok(ID3D11Device_CreateVertexShader(Device, ID3D10Blob_GetBufferPointer(code),
                                           ID3D10Blob_GetBufferSize(code), NULL, &shader) == S_OK,
           "CreateVertexShader failed\n");
        ID3D10Blob_Release(code);
    }
    return shader;
}

static ID3D11Texture2D *CreateTexture(UINT Size, DXGI_FORMAT Format, D3D11_USAGE Usage, UINT Bind, UINT Cpu,
                                      const void *Data, UINT Pitch)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    ID3D11Texture2D *texture = NULL;
    HRESULT hr;

    desc.Width = Size;
    desc.Height = Size;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = Usage;
    desc.BindFlags = Bind;
    desc.CPUAccessFlags = Cpu;
    init.pSysMem = Data;
    init.SysMemPitch = Pitch;
    hr = ID3D11Device_CreateTexture2D(Device, &desc, Data ? &init : NULL, &texture);
    ok(hr == S_OK, "CreateTexture2D for format %u: 0x%08lx\n", Format, hr);
    return texture;
}

static ID3D11Texture3D *CreateVolume(UINT Width, UINT Depth, UINT Mips, DXGI_FORMAT Format, D3D11_USAGE Usage,
                                     UINT Bind, UINT Cpu, UINT Misc)
{
    D3D11_TEXTURE3D_DESC desc = {0};
    ID3D11Texture3D *texture = NULL;
    HRESULT hr;

    desc.Width = Width;
    desc.Height = Width;
    desc.Depth = Depth;
    desc.MipLevels = Mips;
    desc.Format = Format;
    desc.Usage = Usage;
    desc.BindFlags = Bind;
    desc.CPUAccessFlags = Cpu;
    desc.MiscFlags = Misc;
    hr = ID3D11Device_CreateTexture3D(Device, &desc, NULL, &texture);
    ok(hr == S_OK, "CreateTexture3D for format %u: 0x%08lx\n", Format, hr);
    return texture;
}

static BOOL ColorClose(DWORD Actual, DWORD Expected)
{
    int shift;

    for (shift = 0; shift < 32; shift += 8)
    {
        if (abs((int)((Actual >> shift) & 0xff) - (int)((Expected >> shift) & 0xff)) > 1)
            return FALSE;
    }
    return TRUE;
}

static void CheckTextureBits(ID3D11Texture2D *Source, DXGI_FORMAT Format, const DWORD *Expected, const char *Name)
{
    ID3D11Texture2D *staging = CreateTexture(2, Format, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, NULL, 0);
    D3D11_MAPPED_SUBRESOURCE map;
    UINT i;

    if (!staging)
        return;
    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)staging, (ID3D11Resource *)Source);
    ok(ID3D11DeviceContext_Map(Context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map) == S_OK, "Map failed\n");
    for (i = 0; i < 4; ++i)
    {
        DWORD actual = *(const DWORD *)((const BYTE *)map.pData + (i / 2) * map.RowPitch + (i % 2) * 4);

        ok(actual == Expected[i], "%s: texel %u is 0x%08lx, expected 0x%08lx\n", Name, i, actual, Expected[i]);
    }
    ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);
}

static void Test_FloatBits(void)
{
    static const DWORD bits[] = {0xff0000ff, 0x7c013c00, 0x80000001, 0xffffffff};
    static const struct
    {
        DXGI_FORMAT Source;
        DXGI_FORMAT Destination;
        const char *Name;
    }
    copies[] =
    {
        {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, "float to float"},
        {DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_FLOAT, "typeless to float"},
        {DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_TYPELESS, "sint to typeless"},
        {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, "32-bit float to float"},
    };
    ID3D11Texture2D *source, *destination;
    UINT i;

    for (i = 0; i < ARRAYSIZE(copies); ++i)
    {
        source = CreateTexture(2, copies[i].Source, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, bits, 8);
        destination = CreateTexture(2, copies[i].Destination, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, NULL, 0);
        if (source && destination)
        {
            ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)destination, (ID3D11Resource *)source);
            CheckTextureBits(destination, copies[i].Destination, bits, copies[i].Name);
        }
        if (destination)
            ID3D11Texture2D_Release(destination);
        if (source)
            ID3D11Texture2D_Release(source);
    }

    destination = CreateTexture(2, DXGI_FORMAT_R16G16_FLOAT, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, NULL, 0);
    if (destination)
    {
        ID3D11DeviceContext_UpdateSubresource(Context, (ID3D11Resource *)destination, 0, NULL, bits, 8, 0);
        CheckTextureBits(destination, DXGI_FORMAT_R16G16_FLOAT, bits, "UpdateSubresource");
        ID3D11Texture2D_Release(destination);
    }
}

static void CheckVolume(ID3D11Texture3D *Source, UINT Subresource, UINT Width, UINT Depth, DXGI_FORMAT Format,
                        DWORD (*Expected)(UINT, UINT, UINT, DWORD), DWORD Argument, const char *Name)
{
    ID3D11Texture3D *staging = CreateVolume(Width, Depth, 1, Format, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0);
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_BOX box = {0, 0, 0, Width, Width, Depth};
    UINT x, y, z, wrong = 0;
    DWORD actual = 0, expected = 0;

    if (!staging)
        return;
    ID3D11DeviceContext_CopySubresourceRegion(Context, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                              (ID3D11Resource *)Source, Subresource, &box);
    ok(ID3D11DeviceContext_Map(Context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map) == S_OK, "Map failed\n");
    for (z = 0; z < Depth; ++z)
    {
        for (y = 0; y < Width; ++y)
        {
            for (x = 0; x < Width; ++x)
            {
                DWORD texel = *(const DWORD *)((const BYTE *)map.pData + z * map.DepthPitch + y * map.RowPitch + x * 4);
                DWORD want = Expected(x, y, z, Argument);

                if (!ColorClose(texel, want) && !wrong++)
                {
                    actual = texel;
                    expected = want;
                }
            }
        }
    }
    ok(!wrong, "%s: %u texels differ, first 0x%08lx, expected 0x%08lx\n", Name, wrong, actual, expected);
    ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)staging, 0);
    ID3D11Texture3D_Release(staging);
}

static DWORD SolidTexel(UINT X, UINT Y, UINT Z, DWORD Value)
{
    return Value;
}

static DWORD AddressTexel(UINT X, UINT Y, UINT Z, DWORD Unused)
{
    return 0xff000000 | (Z << 16) | (Y << 8) | X;
}

static void Test_VolumeClearAndSlices(void)
{
    static const float color[] = {0.1f, 0.5f, 0.3f, 0.75f};
    static const float green[] = {0.0f, 1.0f, 0.0f, 0.5f};
    ID3D11RenderTargetView *view = NULL;
    ID3D11Texture3D *volume;
    DWORD data[8 * 8 * 4];
    UINT x, y, z;

    volume = CreateVolume(8, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0);
    if (!volume)
        return;
    ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)volume, NULL, &view) == S_OK,
       "CreateRenderTargetView failed\n");
    if (view)
    {
        ID3D11DeviceContext_ClearRenderTargetView(Context, view, color);
        CheckVolume(volume, 0, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, SolidTexel, 0xbf4c7f19, "first clear");
        ID3D11DeviceContext_ClearRenderTargetView(Context, view, green);
        CheckVolume(volume, 0, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, SolidTexel, 0x8000ff00, "second clear");
        ID3D11DeviceContext_ClearRenderTargetView(Context, view, color);
        CheckVolume(volume, 0, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, SolidTexel, 0xbf4c7f19, "third clear");
        ID3D11RenderTargetView_Release(view);
    }

    for (z = 0; z < 4; ++z)
    {
        for (y = 0; y < 8; ++y)
        {
            for (x = 0; x < 8; ++x)
                data[(z * 8 + y) * 8 + x] = AddressTexel(x, y, z, 0);
        }
    }
    ID3D11DeviceContext_UpdateSubresource(Context, (ID3D11Resource *)volume, 0, NULL, data, 8 * 4, 8 * 8 * 4);
    CheckVolume(volume, 0, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, AddressTexel, 0, "slice contents");
    ID3D11Texture3D_Release(volume);
}

static void Test_VolumeUavClear(void)
{
    static const UINT values[] = {1, 2, 3, 4};
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {0};
    ID3D11UnorderedAccessView *view = NULL;
    ID3D11Texture3D *volume;

    desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    desc.Texture3D.WSize = ~0u;

    volume = CreateVolume(8, 1, 1, DXGI_FORMAT_R16G16_UINT, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0);
    if (volume)
    {
        desc.Format = DXGI_FORMAT_R16G16_UINT;
        ok(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource *)volume, &desc, &view) == S_OK,
           "CreateUnorderedAccessView failed\n");
        if (view)
        {
            ID3D11DeviceContext_ClearUnorderedAccessViewUint(Context, view, values);
            CheckVolume(volume, 0, 8, 1, DXGI_FORMAT_R16G16_UINT, SolidTexel, 0x00020001, "UAV clear");
            ID3D11UnorderedAccessView_Release(view);
            view = NULL;
        }
        ID3D11Texture3D_Release(volume);
    }

    volume = CreateVolume(8, 1, 2, DXGI_FORMAT_R32_FLOAT, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0);
    if (volume)
    {
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.Texture3D.MipSlice = 1;
        ok(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource *)volume, &desc, &view) == S_OK,
           "CreateUnorderedAccessView failed\n");
        if (view)
        {
            ID3D11DeviceContext_ClearUnorderedAccessViewUint(Context, view, values);
            CheckVolume(volume, 1, 4, 1, DXGI_FORMAT_R32_FLOAT, SolidTexel, 1, "UAV clear of a mip");
            ID3D11UnorderedAccessView_Release(view);
        }
        ID3D11Texture3D_Release(volume);
    }
}

static void Test_VolumeGenerateMips(void)
{
    DWORD *data = malloc(8 * 8 * 8 * sizeof(*data));
    ID3D11ShaderResourceView *view = NULL;
    ID3D11Texture3D *volume;
    UINT i;

    volume = CreateVolume(8, 8, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    if (data && volume)
    {
        for (i = 0; i < 8 * 8 * 8; ++i)
            data[i] = 0xff0000ff;
        ok(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource *)volume, NULL, &view) == S_OK,
           "CreateShaderResourceView failed\n");
        ID3D11DeviceContext_UpdateSubresource(Context, (ID3D11Resource *)volume, 0, NULL, data, 8 * 4, 8 * 8 * 4);
        if (view)
        {
            ID3D11DeviceContext_GenerateMips(Context, view);
            CheckVolume(volume, 1, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, SolidTexel, 0xff0000ff, "generated mip");
            ID3D11ShaderResourceView_Release(view);
        }
    }
    if (volume)
        ID3D11Texture3D_Release(volume);
    free(data);
}

static float DrawFloat(ID3D11VertexShader *Vertex, ID3D11PixelShader *Pixel, ID3D11ShaderResourceView *View,
                       ID3D11SamplerState *Sampler, ID3D11RenderTargetView *Target, ID3D11Texture2D *Texture,
                       ID3D11Texture2D *Staging)
{
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D11ShaderResourceView *none = NULL;
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_VIEWPORT viewport = {0};
    float value = -1.0f;

    viewport.Width = DEPTH_SIZE;
    viewport.Height = DEPTH_SIZE;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(Context, 1, &viewport);
    ID3D11DeviceContext_ClearRenderTargetView(Context, Target, black);
    ID3D11DeviceContext_OMSetRenderTargets(Context, 1, &Target, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(Context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(Context, Vertex, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Context, Pixel, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(Context, 0, 1, &View);
    ID3D11DeviceContext_PSSetSamplers(Context, 0, 1, &Sampler);
    ID3D11DeviceContext_Draw(Context, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(Context, 0, 1, &none);
    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)Staging, (ID3D11Resource *)Texture);
    if (ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Staging, 0, D3D11_MAP_READ, 0, &map) == S_OK)
    {
        value = *(const float *)map.pData;
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Staging, 0);
    }
    return value;
}

static void Test_DepthStencilViews(ID3D11VertexShader *Vertex)
{
    static const struct
    {
        DXGI_FORMAT Texture;
        DXGI_FORMAT DepthStencil;
        DXGI_FORMAT Depth;
        DXGI_FORMAT Stencil;
        const char *Name;
    }
    formats[] =
    {
        {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
         DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, "D32S8"},
        {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN, "D32"},
        {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
         DXGI_FORMAT_X24_TYPELESS_G8_UINT, "D24S8"},
        {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN, "D16"},
    };
    static const UINT8 stencils[] = {100, 255, 0, 3};
    ID3D11PixelShader *compare, *stencil;
    D3D11_SAMPLER_DESC sampler_desc = {0};
    ID3D11SamplerState *sampler = NULL;
    ID3D11RenderTargetView *target = NULL;
    ID3D11Texture2D *color, *staging;
    UINT i, j;

    compare = CreatePixelShader(ComparePixelSource);
    stencil = CreatePixelShader(StencilPixelSource);
    color = CreateTexture(DEPTH_SIZE, DXGI_FORMAT_R32_FLOAT, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, NULL, 0);
    staging = CreateTexture(DEPTH_SIZE, DXGI_FORMAT_R32_FLOAT, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, NULL, 0);
    sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_GREATER;
    if (!compare || !stencil || !color || !staging)
        goto done;
    ok(ID3D11Device_CreateSamplerState(Device, &sampler_desc, &sampler) == S_OK, "CreateSamplerState failed\n");
    ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)color, NULL, &target) == S_OK,
       "CreateRenderTargetView failed\n");

    for (i = 0; i < ARRAYSIZE(formats); ++i)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {0};
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {0};
        ID3D11ShaderResourceView *depth_view = NULL, *stencil_view = NULL;
        ID3D11DepthStencilView *dsv = NULL;
        ID3D11Texture2D *texture;
        float value;

        texture = CreateTexture(DEPTH_SIZE, formats[i].Texture, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL, 0, NULL, 0);
        if (!texture)
            continue;
        dsv_desc.Format = formats[i].DepthStencil;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ok(ID3D11Device_CreateDepthStencilView(Device, (ID3D11Resource *)texture, &dsv_desc, &dsv) == S_OK,
           "%s: CreateDepthStencilView failed\n", formats[i].Name);
        view_desc.Format = formats[i].Depth;
        view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D.MipLevels = 1;
        ok(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource *)texture, &view_desc, &depth_view) == S_OK,
           "%s: depth view failed\n", formats[i].Name);

        if (dsv && depth_view)
        {
            ID3D11DeviceContext_ClearDepthStencilView(Context, dsv, D3D11_CLEAR_DEPTH, 0.5f, 0);
            value = DrawFloat(Vertex, compare, depth_view, sampler, target, color, staging);
            ok(value == 0.0f, "%s: 0.5 > cleared 0.5 gave %f\n", formats[i].Name, value);
            ID3D11DeviceContext_ClearDepthStencilView(Context, dsv, D3D11_CLEAR_DEPTH, 0.25f, 0);
            value = DrawFloat(Vertex, compare, depth_view, sampler, target, color, staging);
            ok(value == 1.0f, "%s: 0.5 > cleared 0.25 gave %f\n", formats[i].Name, value);
        }

        if (formats[i].Stencil != DXGI_FORMAT_UNKNOWN)
        {
            view_desc.Format = formats[i].Stencil;
            ok(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource *)texture, &view_desc, &stencil_view) == S_OK,
               "%s: stencil view failed\n", formats[i].Name);
        }
        if (dsv && stencil_view)
        {
            for (j = 0; j < ARRAYSIZE(stencils); ++j)
            {
                ID3D11DeviceContext_ClearDepthStencilView(Context, dsv, D3D11_CLEAR_STENCIL, 0.0f, stencils[j]);
                value = DrawFloat(Vertex, stencil, stencil_view, sampler, target, color, staging);
                ok(value == (float)stencils[j], "%s: stencil read %f after a clear to %u\n",
                   formats[i].Name, value, stencils[j]);
            }
        }

        if (stencil_view)
            ID3D11ShaderResourceView_Release(stencil_view);
        if (depth_view)
            ID3D11ShaderResourceView_Release(depth_view);
        if (dsv)
            ID3D11DepthStencilView_Release(dsv);
        ID3D11Texture2D_Release(texture);
    }

done:
    if (target)
        ID3D11RenderTargetView_Release(target);
    if (sampler)
        ID3D11SamplerState_Release(sampler);
    if (staging)
        ID3D11Texture2D_Release(staging);
    if (color)
        ID3D11Texture2D_Release(color);
    if (stencil)
        ID3D11PixelShader_Release(stencil);
    if (compare)
        ID3D11PixelShader_Release(compare);
}

static BOOL CALLBACK FirstThreadWindow(HWND Window, LPARAM Parameter)
{
    char name[64] = "";

    if (GetWindowThreadProcessId(Window, NULL) != GetCurrentThreadId())
        return TRUE;
    GetClassNameA(Window, name, sizeof(name));
    if (!*(HWND *)Parameter)
        *(HWND *)Parameter = Window;
    trace("window of this thread: %p class %s\n", Window, name);
    return TRUE;
}

static void Test_FirstThreadWindow(void)
{
    DXGI_SWAP_CHAIN_DESC desc = {0};
    ID3D11Device *device = NULL;
    HWND window, first = NULL;
    HRESULT hr;

    window = CreateWindowW(L"static", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindowW failed\n");
    EnumWindows(FirstThreadWindow, (LPARAM)&first);
    ok(first == window, "first window before the device is %p, expected %p\n", first, window);

    desc.BufferDesc.Width = 800;
    desc.BufferDesc.Height = 600;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.Windowed = TRUE;
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                       &desc, NULL, &device, NULL, NULL);
    ok(hr == S_OK, "D3D11CreateDeviceAndSwapChain: 0x%08lx\n", hr);
    first = NULL;
    EnumWindows(FirstThreadWindow, (LPARAM)&first);
    ok(first == window, "first window after the device is %p, expected %p\n", first, window);
    if (device)
        ID3D11Device_Release(device);
    DestroyWindow(window);
}

START_TEST(hal_resource)
{
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    ID3D11VertexShader *vertex;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                           &Device, NULL, &Context);
    if (FAILED(hr))
    {
        skip("no hardware feature level 11_0 device: 0x%08lx\n", hr);
        return;
    }
    Test_FloatBits();
    Test_VolumeClearAndSlices();
    Test_VolumeUavClear();
    Test_VolumeGenerateMips();
    vertex = CreateVertexShader();
    if (vertex)
    {
        Test_DepthStencilViews(vertex);
        ID3D11VertexShader_Release(vertex);
    }
    ID3D11DeviceContext_Release(Context);
    ID3D11Device_Release(Device);
    Test_FirstThreadWindow();
}
