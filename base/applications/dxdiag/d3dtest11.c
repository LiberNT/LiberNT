/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 rendering benchmark
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define COBJMACROS
#include <d3d11.h>
#include <d3dcompiler.h>

typedef struct
{
    IDXGISwapChain *SwapChain;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    ID3D11RenderTargetView *Target;
    ID3D11VertexShader *VertexShader;
    ID3D11PixelShader *PixelShader;
    ID3D11InputLayout *Layout;
    ID3D11Buffer *Buffer;
    ID3D11RasterizerState *Rasterizer;
    UINT Width;
    UINT Height;
} D3D11_TEST;

static BOOL D3D11Fail(PD3DTEST_RESULT Result, PCSTR Stage, HRESULT Status)
{
    return D3DTestFail(Result, "D3D11", Stage, Status);
}

static BOOL D3D11CreateShaders(D3D11_TEST *Test, PD3DTEST_RESULT Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3DBlob *VertexCode = NULL;
    ID3DBlob *PixelCode = NULL;
    BOOL Created = FALSE;
    HRESULT hr;

    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &VertexCode, NULL);
    if (FAILED(hr))
        return D3D11Fail(Result, "compile-vertex-shader", hr);
    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &PixelCode, NULL);
    if (FAILED(hr))
        D3D11Fail(Result, "compile-pixel-shader", hr);
    else if (FAILED(hr = ID3D11Device_CreateVertexShader(Test->Device, ID3D10Blob_GetBufferPointer(VertexCode), ID3D10Blob_GetBufferSize(VertexCode), NULL, &Test->VertexShader)))
        D3D11Fail(Result, "create-vertex-shader", hr);
    else if (FAILED(hr = ID3D11Device_CreatePixelShader(Test->Device, ID3D10Blob_GetBufferPointer(PixelCode), ID3D10Blob_GetBufferSize(PixelCode), NULL, &Test->PixelShader)))
        D3D11Fail(Result, "create-pixel-shader", hr);
    else if (FAILED(hr = ID3D11Device_CreateInputLayout(Test->Device, Elements, ARRAYSIZE(Elements), ID3D10Blob_GetBufferPointer(VertexCode),
                                                         ID3D10Blob_GetBufferSize(VertexCode), &Test->Layout)))
        D3D11Fail(Result, "create-input-layout", hr);
    else
        Created = TRUE;

    if (PixelCode)
        ID3D10Blob_Release(PixelCode);
    ID3D10Blob_Release(VertexCode);
    return Created;
}

static BOOL D3D11Setup(D3D11_TEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {{0}};
    D3D11_BUFFER_DESC BufferDesc = {0};
    D3D11_RASTERIZER_DESC RasterizerDesc = {0};
    D3D11_VIEWPORT Viewport = {0};
    ID3D11Texture2D *BackBuffer;
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
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &SwapChainDesc, &Test->SwapChain,
                                       &Test->Device, NULL, &Test->Context);
    if (FAILED(hr))
        return D3D11Fail(Result, "create-device", hr);

    hr = IDXGISwapChain_GetBuffer(Test->SwapChain, 0, &IID_ID3D11Texture2D, (void **)&BackBuffer);
    if (FAILED(hr))
        return D3D11Fail(Result, "get-back-buffer", hr);
    hr = ID3D11Device_CreateRenderTargetView(Test->Device, (ID3D11Resource *)BackBuffer, NULL, &Test->Target);
    ID3D11Texture2D_Release(BackBuffer);
    if (FAILED(hr))
        return D3D11Fail(Result, "create-render-target", hr);

    if (!D3D11CreateShaders(Test, Result))
        return FALSE;

    BufferDesc.ByteWidth = D3DTEST_MAX_VERTEX_COUNT * sizeof(D3DTEST_CLIP_VERTEX);
    BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(Test->Device, &BufferDesc, NULL, &Test->Buffer);
    if (FAILED(hr))
        return D3D11Fail(Result, "create-vertex-buffer", hr);

    RasterizerDesc.FillMode = D3D11_FILL_SOLID;
    RasterizerDesc.CullMode = D3D11_CULL_NONE;
    RasterizerDesc.DepthClipEnable = TRUE;
    hr = ID3D11Device_CreateRasterizerState(Test->Device, &RasterizerDesc, &Test->Rasterizer);
    if (FAILED(hr))
        return D3D11Fail(Result, "create-rasterizer-state", hr);
    ID3D11DeviceContext_RSSetState(Test->Context, Test->Rasterizer);

    Viewport.Width = Test->Width;
    Viewport.Height = Test->Height;
    Viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(Test->Context, 1, &Viewport);
    ID3D11DeviceContext_OMSetRenderTargets(Test->Context, 1, &Test->Target, NULL);
    ID3D11DeviceContext_IASetInputLayout(Test->Context, Test->Layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(Test->Context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(Test->Context, 0, 1, &Test->Buffer, &Stride, &Offset);
    ID3D11DeviceContext_VSSetShader(Test->Context, Test->VertexShader, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Test->Context, Test->PixelShader, NULL, 0);
    return TRUE;
}

static BOOL D3D11Render(D3D11_TEST *Test, PD3DTEST_RESULT Result)
{
    static const FLOAT Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    D3D11_MAPPED_SUBRESOURCE Mapped;
    DWORD Start, Elapsed;
    UINT Count;
    HRESULT hr;

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, Test->Width, Test->Height, Result);
        hr = ID3D11DeviceContext_Map(Test->Context, (ID3D11Resource *)Test->Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
        if (FAILED(hr))
            return D3D11Fail(Result, "map-vertex-buffer", hr);
        D3DTestToClipSpace(Mapped.pData, Vertices, Count, Test->Width, Test->Height);
        ID3D11DeviceContext_Unmap(Test->Context, (ID3D11Resource *)Test->Buffer, 0);

        ID3D11DeviceContext_ClearRenderTargetView(Test->Context, Test->Target, Black);
        ID3D11DeviceContext_Draw(Test->Context, Count, 0);
        hr = IDXGISwapChain_Present(Test->SwapChain, 0, 0);
        if (FAILED(hr))
            return D3D11Fail(Result, "present", hr);
        Result->Frames++;
    }
    return !Result->Stage;
}

static VOID D3D11Release(D3D11_TEST *Test)
{
    if (Test->Context)
        ID3D11DeviceContext_ClearState(Test->Context);
    if (Test->Rasterizer)
        ID3D11RasterizerState_Release(Test->Rasterizer);
    if (Test->Buffer)
        ID3D11Buffer_Release(Test->Buffer);
    if (Test->Layout)
        ID3D11InputLayout_Release(Test->Layout);
    if (Test->PixelShader)
        ID3D11PixelShader_Release(Test->PixelShader);
    if (Test->VertexShader)
        ID3D11VertexShader_Release(Test->VertexShader);
    if (Test->Target)
        ID3D11RenderTargetView_Release(Test->Target);
    if (Test->SwapChain)
        IDXGISwapChain_Release(Test->SwapChain);
    if (Test->Context)
        ID3D11DeviceContext_Release(Test->Context);
    if (Test->Device)
        ID3D11Device_Release(Test->Device);
}

BOOL D3D11Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3D11_TEST Test = {0};
    BOOL Passed;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    Passed = D3D11Setup(&Test, hWnd, Result) && D3D11Render(&Test, Result);
    D3D11Release(&Test);
    return Passed;
}
