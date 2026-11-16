/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 10 rendering benchmark
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define COBJMACROS
#include <d3d10.h>
#include <d3dcompiler.h>

typedef struct
{
    IDXGISwapChain *SwapChain;
    ID3D10Device *Device;
    ID3D10RenderTargetView *Target;
    ID3D10VertexShader *VertexShader;
    ID3D10PixelShader *PixelShader;
    ID3D10InputLayout *Layout;
    ID3D10Buffer *Buffer;
    ID3D10RasterizerState *Rasterizer;
    UINT Width;
    UINT Height;
} D3D10_TEST;

static BOOL D3D10Fail(PD3DTEST_RESULT Result, PCSTR Stage, HRESULT Status)
{
    return D3DTestFail(Result, "D3D10", Stage, Status);
}

static BOOL D3D10CreateShaders(D3D10_TEST *Test, PD3DTEST_RESULT Result)
{
    static const D3D10_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3DBlob *VertexCode = NULL;
    ID3DBlob *PixelCode = NULL;
    BOOL Created = FALSE;
    HRESULT hr;

    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &VertexCode, NULL);
    if (FAILED(hr))
        return D3D10Fail(Result, "compile-vertex-shader", hr);
    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &PixelCode, NULL);
    if (FAILED(hr))
        D3D10Fail(Result, "compile-pixel-shader", hr);
    else if (FAILED(hr = ID3D10Device_CreateVertexShader(Test->Device, ID3D10Blob_GetBufferPointer(VertexCode), ID3D10Blob_GetBufferSize(VertexCode), &Test->VertexShader)))
        D3D10Fail(Result, "create-vertex-shader", hr);
    else if (FAILED(hr = ID3D10Device_CreatePixelShader(Test->Device, ID3D10Blob_GetBufferPointer(PixelCode), ID3D10Blob_GetBufferSize(PixelCode), &Test->PixelShader)))
        D3D10Fail(Result, "create-pixel-shader", hr);
    else if (FAILED(hr = ID3D10Device_CreateInputLayout(Test->Device, Elements, ARRAYSIZE(Elements), ID3D10Blob_GetBufferPointer(VertexCode),
                                                         ID3D10Blob_GetBufferSize(VertexCode), &Test->Layout)))
        D3D10Fail(Result, "create-input-layout", hr);
    else
        Created = TRUE;

    if (PixelCode)
        ID3D10Blob_Release(PixelCode);
    ID3D10Blob_Release(VertexCode);
    return Created;
}

static BOOL D3D10Setup(D3D10_TEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {{0}};
    D3D10_BUFFER_DESC BufferDesc = {0};
    D3D10_RASTERIZER_DESC RasterizerDesc = {0};
    D3D10_VIEWPORT Viewport = {0};
    ID3D10Texture2D *BackBuffer;
    RECT ClientRect;
    UINT Stride = sizeof(D3DTEST_CLIP_VERTEX), Offset = 0;
    HRESULT hr;

    GetClientRect(hWnd, &ClientRect);
    Test->Width = ClientRect.right - ClientRect.left;
    Test->Height = ClientRect.bottom - ClientRect.top;

    SwapChainDesc.BufferDesc.Width = Test->Width;
    SwapChainDesc.BufferDesc.Height = Test->Height;
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = 1;
    SwapChainDesc.OutputWindow = hWnd;
    SwapChainDesc.Windowed = TRUE;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    hr = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &SwapChainDesc, &Test->SwapChain, &Test->Device);
    if (FAILED(hr))
        return D3D10Fail(Result, "create-device", hr);

    hr = IDXGISwapChain_GetBuffer(Test->SwapChain, 0, &IID_ID3D10Texture2D, (void **)&BackBuffer);
    if (FAILED(hr))
        return D3D10Fail(Result, "get-back-buffer", hr);
    hr = ID3D10Device_CreateRenderTargetView(Test->Device, (ID3D10Resource *)BackBuffer, NULL, &Test->Target);
    ID3D10Texture2D_Release(BackBuffer);
    if (FAILED(hr))
        return D3D10Fail(Result, "create-render-target", hr);

    if (!D3D10CreateShaders(Test, Result))
        return FALSE;

    BufferDesc.ByteWidth = D3DTEST_MAX_VERTEX_COUNT * sizeof(D3DTEST_CLIP_VERTEX);
    BufferDesc.Usage = D3D10_USAGE_DYNAMIC;
    BufferDesc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    BufferDesc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    hr = ID3D10Device_CreateBuffer(Test->Device, &BufferDesc, NULL, &Test->Buffer);
    if (FAILED(hr))
        return D3D10Fail(Result, "create-vertex-buffer", hr);

    RasterizerDesc.FillMode = D3D10_FILL_SOLID;
    RasterizerDesc.CullMode = D3D10_CULL_NONE;
    RasterizerDesc.DepthClipEnable = TRUE;
    hr = ID3D10Device_CreateRasterizerState(Test->Device, &RasterizerDesc, &Test->Rasterizer);
    if (FAILED(hr))
        return D3D10Fail(Result, "create-rasterizer-state", hr);
    ID3D10Device_RSSetState(Test->Device, Test->Rasterizer);

    Viewport.Width = Test->Width;
    Viewport.Height = Test->Height;
    Viewport.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(Test->Device, 1, &Viewport);
    ID3D10Device_OMSetRenderTargets(Test->Device, 1, &Test->Target, NULL);
    ID3D10Device_IASetInputLayout(Test->Device, Test->Layout);
    ID3D10Device_IASetPrimitiveTopology(Test->Device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D10Device_IASetVertexBuffers(Test->Device, 0, 1, &Test->Buffer, &Stride, &Offset);
    ID3D10Device_VSSetShader(Test->Device, Test->VertexShader);
    ID3D10Device_PSSetShader(Test->Device, Test->PixelShader);
    return TRUE;
}

static BOOL D3D10Render(D3D10_TEST *Test, PD3DTEST_RESULT Result)
{
    static const FLOAT Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    PD3DTEST_CLIP_VERTEX Mapped;
    DWORD Start, Elapsed;
    UINT Count;
    HRESULT hr;

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, Test->Width, Test->Height, Result);
        hr = ID3D10Buffer_Map(Test->Buffer, D3D10_MAP_WRITE_DISCARD, 0, (void **)&Mapped);
        if (FAILED(hr))
            return D3D10Fail(Result, "map-vertex-buffer", hr);
        D3DTestToClipSpace(Mapped, Vertices, Count, Test->Width, Test->Height);
        ID3D10Buffer_Unmap(Test->Buffer);

        ID3D10Device_ClearRenderTargetView(Test->Device, Test->Target, Black);
        ID3D10Device_Draw(Test->Device, Count, 0);
        hr = IDXGISwapChain_Present(Test->SwapChain, 0, 0);
        if (FAILED(hr))
            return D3D10Fail(Result, "present", hr);
        Result->Frames++;
    }
    return !Result->Stage;
}

static VOID D3D10Release(D3D10_TEST *Test)
{
    if (Test->Device)
        ID3D10Device_ClearState(Test->Device);
    if (Test->Rasterizer)
        ID3D10RasterizerState_Release(Test->Rasterizer);
    if (Test->Buffer)
        ID3D10Buffer_Release(Test->Buffer);
    if (Test->Layout)
        ID3D10InputLayout_Release(Test->Layout);
    if (Test->PixelShader)
        ID3D10PixelShader_Release(Test->PixelShader);
    if (Test->VertexShader)
        ID3D10VertexShader_Release(Test->VertexShader);
    if (Test->Target)
        ID3D10RenderTargetView_Release(Test->Target);
    if (Test->SwapChain)
        IDXGISwapChain_Release(Test->SwapChain);
    if (Test->Device)
        ID3D10Device_Release(Test->Device);
}

BOOL D3D10Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3D10_TEST Test = {0};
    BOOL Passed;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    Passed = D3D10Setup(&Test, hWnd, Result) && D3D10Render(&Test, Result);
    D3D10Release(&Test);
    return Passed;
}
