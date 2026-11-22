/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11.1 constant buffer offsetting on a hardware device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wine/test.h>

#define SIZE 16

static const char VertexSource[] =
    "float4 main(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

static const char PixelSource[] =
    "float4 color;\n"
    "float4 main() : SV_Target { return color; }\n";

static ID3D11Device *Device;
static ID3D11DeviceContext1 *Context;

static void *CreateShader(const char *Source, const char *Target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    void *shader = NULL;
    HRESULT hr;

    hr = D3DCompile(Source, strlen(Source), NULL, NULL, NULL, "main", Target, 0, 0, &code, &errors);
    ok(hr == S_OK, "D3DCompile for %s: 0x%08lx %s\n", Target, hr,
       errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
    if (errors)
        ID3D10Blob_Release(errors);
    if (!code)
        return NULL;
    if (Target[0] == 'v')
        hr = ID3D11Device_CreateVertexShader(Device, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code),
                                             NULL, (ID3D11VertexShader **)&shader);
    else
        hr = ID3D11Device_CreatePixelShader(Device, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code),
                                            NULL, (ID3D11PixelShader **)&shader);
    ok(hr == S_OK, "Creating the %s shader: 0x%08lx\n", Target, hr);
    ID3D10Blob_Release(code);
    return shader;
}

static ID3D11Texture2D *CreateTarget(D3D11_USAGE Usage, UINT Bind, UINT Cpu)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    ID3D11Texture2D *texture = NULL;

    desc.Width = SIZE;
    desc.Height = SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = Usage;
    desc.BindFlags = Bind;
    desc.CPUAccessFlags = Cpu;
    ok(ID3D11Device_CreateTexture2D(Device, &desc, NULL, &texture) == S_OK, "CreateTexture2D failed\n");
    return texture;
}

static DWORD DrawAndRead(ID3D11RenderTargetView *View, ID3D11Texture2D *Target, ID3D11Texture2D *Staging)
{
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD texel = 0;

    ID3D11DeviceContext1_OMSetRenderTargets(Context, 1, &View, NULL);
    ID3D11DeviceContext1_Draw(Context, 3, 0);
    ID3D11DeviceContext1_CopyResource(Context, (ID3D11Resource *)Staging, (ID3D11Resource *)Target);
    if (ID3D11DeviceContext1_Map(Context, (ID3D11Resource *)Staging, 0, D3D11_MAP_READ, 0, &map) == S_OK)
    {
        texel = *(const DWORD *)((const BYTE *)map.pData + (SIZE / 2) * map.RowPitch + (SIZE / 2) * 4);
        ID3D11DeviceContext1_Unmap(Context, (ID3D11Resource *)Staging, 0);
    }
    return texel;
}

static void CheckBinding(UINT First, UINT Count, const char *Name)
{
    ID3D11Buffer *buffer = NULL;
    UINT first = 0xcccc, count = 0xcccc;

    ID3D11DeviceContext1_PSGetConstantBuffers1(Context, 0, 1, &buffer, &first, &count);
    ok(buffer != NULL, "%s: no constant buffer bound\n", Name);
    ok(first == First, "%s: first constant %u\n", Name, first);
    ok(count == Count, "%s: %u constants\n", Name, count);
    if (buffer)
        ID3D11Buffer_Release(buffer);
}

static void Test_Offsets(ID3D11Buffer *Buffer, ID3D11RenderTargetView *View, ID3D11Texture2D *Target,
                         ID3D11Texture2D *Staging)
{
    UINT first = 16, count = 16, bad_first = 8, large = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT + 16;

    ID3D11DeviceContext1_PSSetConstantBuffers1(Context, 0, 1, &Buffer, &first, &count);
    ok(DrawAndRead(View, Target, Staging) == 0xff00ff00, "The constant at offset 16 was not the one drawn\n");
    CheckBinding(16, 16, "offset 16");

    ID3D11DeviceContext1_PSSetConstantBuffers1(Context, 0, 1, &Buffer, &bad_first, &count);
    CheckBinding(16, 16, "an offset that is not a multiple of 16");
    ID3D11DeviceContext1_PSSetConstantBuffers1(Context, 0, 1, &Buffer, &first, NULL);
    CheckBinding(16, 16, "a NULL count array");
    ID3D11DeviceContext1_PSSetConstantBuffers1(Context, 0, 1, &Buffer, &first, &large);
    CheckBinding(16, 16, "more than 4096 constants");

    ID3D11DeviceContext1_PSSetConstantBuffers(Context, 0, 1, &Buffer);
    ok(DrawAndRead(View, Target, Staging) == 0xff0000ff, "The whole buffer did not draw its first constant\n");
}

START_TEST(cb_offset)
{
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    static const float constants[32][4] = {{1.0f, 0.0f, 0.0f, 1.0f}, [16] = {0.0f, 1.0f, 0.0f, 1.0f}};
    D3D11_FEATURE_DATA_D3D11_OPTIONS options;
    D3D11_SUBRESOURCE_DATA init = {constants};
    D3D11_BUFFER_DESC desc = {sizeof(constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_VIEWPORT viewport = {0, 0, SIZE, SIZE, 0, 1};
    ID3D11DeviceContext *context = NULL;
    ID3D11RenderTargetView *view = NULL;
    ID3D11Texture2D *target, *staging;
    ID3D11VertexShader *vertex;
    ID3D11PixelShader *pixel;
    ID3D11Buffer *buffer = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                           &Device, NULL, &context);
    if (FAILED(hr))
    {
        skip("no hardware feature level 11_0 device: 0x%08lx\n", hr);
        return;
    }
    hr = ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11DeviceContext1, (void **)&Context);
    ok(hr == S_OK, "No ID3D11DeviceContext1: 0x%08lx\n", hr);
    ID3D11DeviceContext_Release(context);
    if (FAILED(hr))
    {
        ID3D11Device_Release(Device);
        return;
    }

    memset(&options, 0xcc, sizeof(options));
    hr = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "CheckFeatureSupport: 0x%08lx\n", hr);
    ok(options.ConstantBufferOffsetting == TRUE, "ConstantBufferOffsetting is %d\n", options.ConstantBufferOffsetting);

    vertex = CreateShader(VertexSource, "vs_5_0");
    pixel = CreateShader(PixelSource, "ps_5_0");
    target = CreateTarget(D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
    staging = CreateTarget(D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
    ok(ID3D11Device_CreateBuffer(Device, &desc, &init, &buffer) == S_OK, "CreateBuffer failed\n");
    if (target)
        ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)target, NULL, &view) == S_OK,
           "CreateRenderTargetView failed\n");

    if (vertex && pixel && view && staging && buffer && options.ConstantBufferOffsetting == TRUE)
    {
        ID3D11DeviceContext1_RSSetViewports(Context, 1, &viewport);
        ID3D11DeviceContext1_IASetPrimitiveTopology(Context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext1_VSSetShader(Context, vertex, NULL, 0);
        ID3D11DeviceContext1_PSSetShader(Context, pixel, NULL, 0);
        Test_Offsets(buffer, view, target, staging);
        ID3D11DeviceContext1_ClearState(Context);
    }

    if (buffer)
        ID3D11Buffer_Release(buffer);
    if (view)
        ID3D11RenderTargetView_Release(view);
    if (staging)
        ID3D11Texture2D_Release(staging);
    if (target)
        ID3D11Texture2D_Release(target);
    if (pixel)
        ID3D11PixelShader_Release(pixel);
    if (vertex)
        ID3D11VertexShader_Release(vertex);
    ID3D11DeviceContext1_Release(Context);
    ID3D11Device_Release(Device);
}
