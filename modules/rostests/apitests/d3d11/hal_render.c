/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 hardware clears, draws, compute and UAV writes read back exactly
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <wine/test.h>

#define SIZE 64
#define UAV_WIDTH 640
#define UAV_HEIGHT 480

static const char VertexSource[] =
    "float4 main(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

static const char ConstantPixelSource[] =
    "float4 color;\n"
    "float4 main() : SV_Target { return color; }\n";

static const char TexturePixelSource[] =
    "Texture2D t;\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }\n";

static const char ComputeSource[] =
    "RWBuffer<uint> u;\n"
    "[numthreads(4, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { u[id.x] = id.x * 3 + 1; }\n";

static const char PixelUavSource[] =
    "RWTexture2D<float> u : register(u1);\n"
    "float4 main(float4 p : SV_Position) : SV_Target { u[int2(p.xy)] = 2.0f; return 0; }\n";

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

static ID3D11Texture2D *CreateTexture(UINT Width, UINT Height, DXGI_FORMAT Format, D3D11_USAGE Usage, UINT Bind,
                                      UINT Cpu, const void *Data, UINT Pitch)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    ID3D11Texture2D *texture = NULL;
    HRESULT hr;

    desc.Width = Width;
    desc.Height = Height;
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
    ok(hr == S_OK, "CreateTexture2D: 0x%08lx\n", hr);
    return texture;
}

static void SetViewport(UINT Width, UINT Height)
{
    D3D11_VIEWPORT viewport = {0};

    viewport.Width = (float)Width;
    viewport.Height = (float)Height;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(Context, 1, &viewport);
}

static DWORD ReadPixel(ID3D11Texture2D *Target, UINT X, UINT Y)
{
    ID3D11Texture2D *staging;
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD value = 0xDEADBEEF;
    HRESULT hr;

    staging = CreateTexture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, NULL, 0);
    if (!staging)
        return value;
    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)staging, (ID3D11Resource *)Target);
    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    ok(hr == S_OK, "Map: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        value = ((const DWORD *)((const BYTE *)map.pData + Y * map.RowPitch))[X];
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    return value;
}

static float ReadFloat(ID3D11Texture2D *Texture, UINT Width, UINT Height, UINT X, UINT Y)
{
    ID3D11Texture2D *staging;
    D3D11_MAPPED_SUBRESOURCE map;
    float value = -1.0f;
    HRESULT hr;

    staging = CreateTexture(Width, Height, DXGI_FORMAT_R32_FLOAT, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, NULL, 0);
    if (!staging)
        return value;
    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)staging, (ID3D11Resource *)Texture);
    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    ok(hr == S_OK, "Map: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        value = ((const float *)((const BYTE *)map.pData + Y * map.RowPitch))[X];
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    return value;
}

static void Test_ClearAndDraw(ID3D11VertexShader *Vertex)
{
    static const float clear[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const float color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    D3D11_BUFFER_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    ID3D11RenderTargetView *view = NULL;
    ID3D11PixelShader *pixel;
    ID3D11Texture2D *target;
    ID3D11Buffer *constants = NULL;

    target = CreateTexture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                           D3D11_BIND_RENDER_TARGET, 0, NULL, 0);
    pixel = CreatePixelShader(ConstantPixelSource);
    if (!target || !pixel)
        return;
    ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)target, NULL, &view) == S_OK,
       "CreateRenderTargetView failed\n");
    desc.ByteWidth = sizeof(color);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.Usage = D3D11_USAGE_DEFAULT;
    init.pSysMem = color;
    ok(ID3D11Device_CreateBuffer(Device, &desc, &init, &constants) == S_OK, "CreateBuffer failed\n");

    ID3D11DeviceContext_ClearRenderTargetView(Context, view, clear);
    ok(ReadPixel(target, 8, 8) == 0xFFFF0000, "clear: 0x%08lx\n", ReadPixel(target, 8, 8));

    SetViewport(SIZE, SIZE);
    ID3D11DeviceContext_OMSetRenderTargets(Context, 1, &view, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(Context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(Context, Vertex, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Context, pixel, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(Context, 0, 1, &constants);
    ID3D11DeviceContext_Draw(Context, 3, 0);
    ok(ReadPixel(target, 8, 8) == 0xFF0000FF, "inside the triangle: 0x%08lx\n", ReadPixel(target, 8, 8));
    ok(ReadPixel(target, SIZE - 1, SIZE - 1) == 0xFF0000FF, "corner of the target: 0x%08lx\n",
       ReadPixel(target, SIZE - 1, SIZE - 1));

    ID3D11DeviceContext_OMSetRenderTargets(Context, 0, NULL, NULL);
    ID3D11Buffer_Release(constants);
    ID3D11RenderTargetView_Release(view);
    ID3D11PixelShader_Release(pixel);
    ID3D11Texture2D_Release(target);
}

static void Test_Texture(ID3D11VertexShader *Vertex)
{
    DWORD texels[SIZE * SIZE];
    ID3D11ShaderResourceView *resource = NULL;
    ID3D11RenderTargetView *view = NULL;
    ID3D11Texture2D *source, *target;
    ID3D11PixelShader *pixel;
    UINT x, y;

    for (y = 0; y < SIZE; y++)
    {
        for (x = 0; x < SIZE; x++)
            texels[y * SIZE + x] = 0xFF000000 | (y << 8) | x;
    }
    source = CreateTexture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_IMMUTABLE,
                           D3D11_BIND_SHADER_RESOURCE, 0, texels, SIZE * 4);
    target = CreateTexture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                           D3D11_BIND_RENDER_TARGET, 0, NULL, 0);
    pixel = CreatePixelShader(TexturePixelSource);
    if (!source || !target || !pixel)
        return;
    ok(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource *)source, NULL, &resource) == S_OK,
       "CreateShaderResourceView failed\n");
    ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)target, NULL, &view) == S_OK,
       "CreateRenderTargetView failed\n");

    SetViewport(SIZE, SIZE);
    ID3D11DeviceContext_OMSetRenderTargets(Context, 1, &view, NULL);
    ID3D11DeviceContext_VSSetShader(Context, Vertex, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Context, pixel, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(Context, 0, 1, &resource);
    ID3D11DeviceContext_Draw(Context, 3, 0);
    ok(ReadPixel(target, 5, 9) == texels[9 * SIZE + 5], "texel (5,9): 0x%08lx\n", ReadPixel(target, 5, 9));
    ok(ReadPixel(target, 60, 3) == texels[3 * SIZE + 60], "texel (60,3): 0x%08lx\n", ReadPixel(target, 60, 3));

    ID3D11DeviceContext_OMSetRenderTargets(Context, 0, NULL, NULL);
    ID3D11RenderTargetView_Release(view);
    ID3D11ShaderResourceView_Release(resource);
    ID3D11PixelShader_Release(pixel);
    ID3D11Texture2D_Release(target);
    ID3D11Texture2D_Release(source);
}

static void Test_Compute(void)
{
    D3D11_BUFFER_DESC desc = {0};
    D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc = {0};
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11UnorderedAccessView *view = NULL;
    ID3D11Buffer *buffer = NULL, *staging = NULL;
    ID3D11ComputeShader *shader = NULL;
    ID3DBlob *code = Compile(ComputeSource, "cs_5_0");
    HRESULT hr;
    UINT i;

    if (!code)
        return;
    ok(ID3D11Device_CreateComputeShader(Device, ID3D10Blob_GetBufferPointer(code),
                                        ID3D10Blob_GetBufferSize(code), NULL, &shader) == S_OK,
       "CreateComputeShader failed\n");
    ID3D10Blob_Release(code);
    desc.ByteWidth = 4 * sizeof(UINT);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ok(ID3D11Device_CreateBuffer(Device, &desc, NULL, &buffer) == S_OK, "CreateBuffer failed\n");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ok(ID3D11Device_CreateBuffer(Device, &desc, NULL, &staging) == S_OK, "CreateBuffer failed\n");
    view_desc.Format = DXGI_FORMAT_R32_UINT;
    view_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view_desc.Buffer.NumElements = 4;
    ok(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource *)buffer, &view_desc, &view) == S_OK,
       "CreateUnorderedAccessView failed\n");
    if (!shader || !buffer || !staging || !view)
        return;

    ID3D11DeviceContext_CSSetShader(Context, shader, NULL, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(Context, 0, 1, &view, NULL);
    ID3D11DeviceContext_Dispatch(Context, 1, 1, 1);
    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)staging, (ID3D11Resource *)buffer);
    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    ok(hr == S_OK, "Map: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        for (i = 0; i < 4; i++)
            ok(((const UINT *)map.pData)[i] == i * 3 + 1, "element %u: %u\n", i, ((const UINT *)map.pData)[i]);
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)staging, 0);
    }

    ID3D11DeviceContext_CSSetShader(Context, NULL, NULL, 0);
    ID3D11UnorderedAccessView_Release(view);
    ID3D11Buffer_Release(staging);
    ID3D11Buffer_Release(buffer);
    ID3D11ComputeShader_Release(shader);
}

static void WaitForGpu(void)
{
    D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
    ID3D11Query *query = NULL;
    BOOL done = FALSE;

    ok(ID3D11Device_CreateQuery(Device, &desc, &query) == S_OK, "CreateQuery failed\n");
    if (!query)
        return;
    ID3D11DeviceContext_End(Context, (ID3D11Asynchronous *)query);
    while (ID3D11DeviceContext_GetData(Context, (ID3D11Asynchronous *)query, &done, sizeof(done), 0) != S_OK || !done)
        Sleep(1);
    ID3D11Query_Release(query);
}

static ULONG ElapsedMs(LARGE_INTEGER Start)
{
    LARGE_INTEGER now, frequency;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (ULONG)((now.QuadPart - Start.QuadPart) * 1000 / frequency.QuadPart);
}

static void DrawPixelUav(ID3D11VertexShader *Vertex, BOOL ColorTarget, const char *Name)
{
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D11RenderTargetView *color_view = NULL;
    ID3D11UnorderedAccessView *view = NULL;
    ID3D11Texture2D *texture, *target = NULL;
    ID3D11PixelShader *pixel;
    LARGE_INTEGER start;
    ULONG draw_ms;
    float value;

    texture = CreateTexture(UAV_WIDTH, UAV_HEIGHT, DXGI_FORMAT_R32_FLOAT, D3D11_USAGE_DEFAULT,
                            D3D11_BIND_UNORDERED_ACCESS, 0, NULL, 0);
    pixel = CreatePixelShader(PixelUavSource);
    if (!texture || !pixel)
        return;
    ok(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource *)texture, NULL, &view) == S_OK,
       "CreateUnorderedAccessView failed\n");
    if (ColorTarget)
    {
        target = CreateTexture(UAV_WIDTH, UAV_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET, 0, NULL, 0);
        ok(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)target, NULL, &color_view) == S_OK,
           "CreateRenderTargetView failed\n");
    }

    ID3D11DeviceContext_ClearUnorderedAccessViewFloat(Context, view, zero);
    SetViewport(UAV_WIDTH, UAV_HEIGHT);
    ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(Context, ColorTarget ? 1 : 0, ColorTarget ? &color_view : NULL,
                                                                  NULL, 1, 1, &view, NULL);
    ID3D11DeviceContext_VSSetShader(Context, Vertex, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Context, pixel, NULL, 0);
    QueryPerformanceCounter(&start);
    ID3D11DeviceContext_Draw(Context, 3, 0);
    WaitForGpu();
    draw_ms = ElapsedMs(start);
    QueryPerformanceCounter(&start);
    value = ReadFloat(texture, UAV_WIDTH, UAV_HEIGHT, 300, 200);
    trace("%s: draw %lu ms, read %lu ms\n", Name, draw_ms, ElapsedMs(start));
    ok(value == 2.0f, "%s: texel reads %f\n", Name, value);

    ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(Context, 0, NULL, NULL, 0, 0, NULL, NULL);
    if (color_view)
        ID3D11RenderTargetView_Release(color_view);
    if (target)
        ID3D11Texture2D_Release(target);
    ID3D11UnorderedAccessView_Release(view);
    ID3D11PixelShader_Release(pixel);
    ID3D11Texture2D_Release(texture);
}

START_TEST(hal_render)
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
    vertex = CreateVertexShader();
    if (vertex)
    {
        Test_ClearAndDraw(vertex);
        Test_Texture(vertex);
        Test_Compute();
        DrawPixelUav(vertex, TRUE, "UAV with a color target");
        DrawPixelUav(vertex, FALSE, "UAV without a color target");
        ID3D11VertexShader_Release(vertex);
    }
    ID3D11DeviceContext_Release(Context);
    ID3D11Device_Release(Device);
}
