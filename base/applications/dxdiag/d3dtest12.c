/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 rendering benchmark
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#define D3D12_TEST_FRAMES 2

typedef struct
{
    IDXGIFactory4 *Factory;
    ID3D12Device *Device;
    ID3D12CommandQueue *Queue;
    IDXGISwapChain3 *SwapChain;
    ID3D12DescriptorHeap *TargetHeap;
    ID3D12Resource *BackBuffers[D3D12_TEST_FRAMES];
    ID3D12CommandAllocator *Allocators[D3D12_TEST_FRAMES];
    ID3D12GraphicsCommandList *List;
    ID3D12RootSignature *RootSignature;
    ID3D12PipelineState *Pipeline;
    ID3D12Resource *Buffer;
    PD3DTEST_CLIP_VERTEX Mapped;
    ID3D12Fence *Fence;
    HANDLE FenceEvent;
    UINT64 FenceValue;
    UINT64 FrameFence[D3D12_TEST_FRAMES];
    UINT TargetStride;
    UINT Width;
    UINT Height;
} D3D12_TEST;

static BOOL D3D12Fail(PD3DTEST_RESULT Result, PCSTR Stage, HRESULT Status)
{
    return D3DTestFail(Result, "D3D12", Stage, Status);
}

static BOOL D3D12CreatePipeline(D3D12_TEST *Test, PD3DTEST_RESULT Result)
{
    static const D3D12_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_ROOT_SIGNATURE_DESC RootDesc = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc;
    ID3DBlob *RootBlob = NULL;
    ID3DBlob *VertexCode = NULL;
    ID3DBlob *PixelCode = NULL;
    BOOL Created = FALSE;
    HRESULT hr;

    hr = D3D12SerializeRootSignature(&RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootBlob, NULL);
    if (FAILED(hr))
        return D3D12Fail(Result, "serialize-root-signature", hr);
    hr = ID3D12Device_CreateRootSignature(Test->Device, 0, ID3D10Blob_GetBufferPointer(RootBlob), ID3D10Blob_GetBufferSize(RootBlob),
                                          &IID_ID3D12RootSignature, (void **)&Test->RootSignature);
    ID3D10Blob_Release(RootBlob);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-root-signature", hr);

    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "VSMain", "vs_5_0", 0, 0, &VertexCode, NULL);
    if (FAILED(hr))
        return D3D12Fail(Result, "compile-vertex-shader", hr);
    hr = D3DCompile(D3DTestShaderSource, strlen(D3DTestShaderSource), NULL, NULL, NULL, "PSMain", "ps_5_0", 0, 0, &PixelCode, NULL);
    if (FAILED(hr))
    {
        ID3D10Blob_Release(VertexCode);
        return D3D12Fail(Result, "compile-pixel-shader", hr);
    }

    ZeroMemory(&PipelineDesc, sizeof(PipelineDesc));
    PipelineDesc.pRootSignature = Test->RootSignature;
    PipelineDesc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(VertexCode);
    PipelineDesc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(VertexCode);
    PipelineDesc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(PixelCode);
    PipelineDesc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(PixelCode);
    PipelineDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    PipelineDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    PipelineDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    PipelineDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    PipelineDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    PipelineDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    PipelineDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    PipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    PipelineDesc.SampleMask = UINT_MAX;
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    PipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    PipelineDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    PipelineDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    PipelineDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    PipelineDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    PipelineDesc.DepthStencilState.BackFace = PipelineDesc.DepthStencilState.FrontFace;
    PipelineDesc.InputLayout.pInputElementDescs = Elements;
    PipelineDesc.InputLayout.NumElements = ARRAYSIZE(Elements);
    PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PipelineDesc.NumRenderTargets = 1;
    PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    PipelineDesc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(Test->Device, &PipelineDesc, &IID_ID3D12PipelineState, (void **)&Test->Pipeline);
    if (FAILED(hr))
        D3D12Fail(Result, "create-pipeline", hr);
    else
        Created = TRUE;

    ID3D10Blob_Release(PixelCode);
    ID3D10Blob_Release(VertexCode);
    return Created;
}

static BOOL D3D12CreateTargets(D3D12_TEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_TEST_FRAMES, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    D3D12_COMMAND_QUEUE_DESC QueueDesc = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
    DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE Handle;
    IDXGISwapChain1 *SwapChain;
    UINT i;
    HRESULT hr;

    hr = ID3D12Device_CreateCommandQueue(Test->Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Test->Queue);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-queue", hr);

    SwapChainDesc.Width = Test->Width;
    SwapChainDesc.Height = Test->Height;
    SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = D3D12_TEST_FRAMES;
    SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = IDXGIFactory4_CreateSwapChainForHwnd(Test->Factory, (IUnknown *)Test->Queue, hWnd, &SwapChainDesc, NULL, NULL, &SwapChain);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-swap-chain", hr);
    hr = IDXGISwapChain1_QueryInterface(SwapChain, &IID_IDXGISwapChain3, (void **)&Test->SwapChain);
    IDXGISwapChain1_Release(SwapChain);
    if (FAILED(hr))
        return D3D12Fail(Result, "query-swap-chain3", hr);

    hr = ID3D12Device_CreateDescriptorHeap(Test->Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Test->TargetHeap);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-descriptor-heap", hr);
    Test->TargetStride = ID3D12Device_GetDescriptorHandleIncrementSize(Test->Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Test->TargetHeap);

    for (i = 0; i < D3D12_TEST_FRAMES; ++i)
    {
        hr = IDXGISwapChain3_GetBuffer(Test->SwapChain, i, &IID_ID3D12Resource, (void **)&Test->BackBuffers[i]);
        if (FAILED(hr))
            return D3D12Fail(Result, "get-back-buffer", hr);
        ID3D12Device_CreateRenderTargetView(Test->Device, Test->BackBuffers[i], NULL, Handle);
        Handle.ptr += Test->TargetStride;

        hr = ID3D12Device_CreateCommandAllocator(Test->Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                 (void **)&Test->Allocators[i]);
        if (FAILED(hr))
            return D3D12Fail(Result, "create-command-allocator", hr);
    }
    return TRUE;
}

static BOOL D3D12Setup(D3D12_TEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    D3D12_HEAP_PROPERTIES HeapProperties = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };
    D3D12_RESOURCE_DESC BufferDesc = {0};
    D3D12_RANGE NoRead = { 0, 0 };
    RECT ClientRect;
    HRESULT hr;

    GetClientRect(hWnd, &ClientRect);
    Test->Width = ClientRect.right - ClientRect.left;
    Test->Height = ClientRect.bottom - ClientRect.top;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&Test->Factory);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-factory", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&Test->Device);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-device", hr);
    if (!D3D12CreateTargets(Test, hWnd, Result) || !D3D12CreatePipeline(Test, Result))
        return FALSE;

    hr = ID3D12Device_CreateCommandList(Test->Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Test->Allocators[0], Test->Pipeline,
                                        &IID_ID3D12GraphicsCommandList, (void **)&Test->List);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-command-list", hr);
    hr = ID3D12GraphicsCommandList_Close(Test->List);
    if (FAILED(hr))
        return D3D12Fail(Result, "close-command-list", hr);

    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Width = D3D12_TEST_FRAMES * D3DTEST_MAX_VERTEX_COUNT * sizeof(D3DTEST_CLIP_VERTEX);
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = ID3D12Device_CreateCommittedResource(Test->Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                              NULL, &IID_ID3D12Resource, (void **)&Test->Buffer);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-vertex-buffer", hr);
    hr = ID3D12Resource_Map(Test->Buffer, 0, &NoRead, (void **)&Test->Mapped);
    if (FAILED(hr))
        return D3D12Fail(Result, "map-vertex-buffer", hr);

    hr = ID3D12Device_CreateFence(Test->Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Test->Fence);
    if (FAILED(hr))
        return D3D12Fail(Result, "create-fence", hr);
    Test->FenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!Test->FenceEvent)
        return D3D12Fail(Result, "create-fence-event", HRESULT_FROM_WIN32(GetLastError()));
    return TRUE;
}

static BOOL D3D12Wait(D3D12_TEST *Test, UINT64 Value, PD3DTEST_RESULT Result)
{
    HRESULT hr;

    if (ID3D12Fence_GetCompletedValue(Test->Fence) >= Value)
        return TRUE;
    hr = ID3D12Fence_SetEventOnCompletion(Test->Fence, Value, Test->FenceEvent);
    if (FAILED(hr))
        return D3D12Fail(Result, "wait-gpu", hr);
    if (WaitForSingleObject(Test->FenceEvent, D3DTEST_DURATION_MS) != WAIT_OBJECT_0)
        return D3D12Fail(Result, "wait-gpu", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    return TRUE;
}

static VOID D3D12Transition(D3D12_TEST *Test, ID3D12Resource *Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
    D3D12_RESOURCE_BARRIER Barrier = {0};

    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Resource;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = Before;
    Barrier.Transition.StateAfter = After;
    ID3D12GraphicsCommandList_ResourceBarrier(Test->List, 1, &Barrier);
}

static BOOL D3D12Frame(D3D12_TEST *Test, DWORD Elapsed, PD3DTEST_RESULT Result)
{
    static const FLOAT Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    D3D12_VIEWPORT Viewport = { 0.0f, 0.0f, (FLOAT)Test->Width, (FLOAT)Test->Height, 0.0f, 1.0f };
    D3D12_RECT Scissor = { 0, 0, (LONG)Test->Width, (LONG)Test->Height };
    D3D12_VERTEX_BUFFER_VIEW View;
    D3D12_CPU_DESCRIPTOR_HANDLE Target;
    UINT Index, Count;
    HRESULT hr;

    Index = IDXGISwapChain3_GetCurrentBackBufferIndex(Test->SwapChain);
    if (Index >= D3D12_TEST_FRAMES)
        return D3D12Fail(Result, "back-buffer-index", E_UNEXPECTED);
    if (!D3D12Wait(Test, Test->FrameFence[Index], Result))
        return FALSE;

    Count = D3DTestBuildScene(Vertices, Elapsed, Test->Width, Test->Height, Result);
    D3DTestToClipSpace(Test->Mapped + Index * D3DTEST_MAX_VERTEX_COUNT, Vertices, Count, Test->Width, Test->Height);

    hr = ID3D12CommandAllocator_Reset(Test->Allocators[Index]);
    if (FAILED(hr))
        return D3D12Fail(Result, "reset-command-allocator", hr);
    hr = ID3D12GraphicsCommandList_Reset(Test->List, Test->Allocators[Index], Test->Pipeline);
    if (FAILED(hr))
        return D3D12Fail(Result, "reset-command-list", hr);

    Target = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Test->TargetHeap);
    Target.ptr += (SIZE_T)Index * Test->TargetStride;
    View.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(Test->Buffer) + (UINT64)Index * D3DTEST_MAX_VERTEX_COUNT * sizeof(D3DTEST_CLIP_VERTEX);
    View.SizeInBytes = Count * sizeof(D3DTEST_CLIP_VERTEX);
    View.StrideInBytes = sizeof(D3DTEST_CLIP_VERTEX);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(Test->List, Test->RootSignature);
    ID3D12GraphicsCommandList_RSSetViewports(Test->List, 1, &Viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(Test->List, 1, &Scissor);
    D3D12Transition(Test, Test->BackBuffers[Index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_OMSetRenderTargets(Test->List, 1, &Target, FALSE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(Test->List, Target, Black, 0, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(Test->List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(Test->List, 0, 1, &View);
    ID3D12GraphicsCommandList_DrawInstanced(Test->List, Count, 1, 0, 0);
    D3D12Transition(Test, Test->BackBuffers[Index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    hr = ID3D12GraphicsCommandList_Close(Test->List);
    if (FAILED(hr))
        return D3D12Fail(Result, "close-command-list", hr);

    ID3D12CommandQueue_ExecuteCommandLists(Test->Queue, 1, (ID3D12CommandList **)&Test->List);
    hr = IDXGISwapChain3_Present(Test->SwapChain, 0, 0);
    if (FAILED(hr))
        return D3D12Fail(Result, "present", hr);
    hr = ID3D12CommandQueue_Signal(Test->Queue, Test->Fence, ++Test->FenceValue);
    if (FAILED(hr))
        return D3D12Fail(Result, "signal", hr);
    Test->FrameFence[Index] = Test->FenceValue;
    return TRUE;
}

static BOOL D3D12Render(D3D12_TEST *Test, PD3DTEST_RESULT Result)
{
    DWORD Start, Elapsed;

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        if (!D3D12Frame(Test, Elapsed, Result))
            return FALSE;
        Result->Frames++;
    }
    return !Result->Stage;
}

static VOID D3D12Release(D3D12_TEST *Test)
{
    D3DTEST_RESULT Ignored = {0};
    UINT i;

    if (Test->Fence && Test->FenceEvent)
        D3D12Wait(Test, Test->FenceValue, &Ignored);
    if (Test->FenceEvent)
        CloseHandle(Test->FenceEvent);
    if (Test->Fence)
        ID3D12Fence_Release(Test->Fence);
    if (Test->Buffer)
        ID3D12Resource_Release(Test->Buffer);
    if (Test->List)
        ID3D12GraphicsCommandList_Release(Test->List);
    if (Test->Pipeline)
        ID3D12PipelineState_Release(Test->Pipeline);
    if (Test->RootSignature)
        ID3D12RootSignature_Release(Test->RootSignature);
    for (i = 0; i < D3D12_TEST_FRAMES; ++i)
    {
        if (Test->Allocators[i])
            ID3D12CommandAllocator_Release(Test->Allocators[i]);
        if (Test->BackBuffers[i])
            ID3D12Resource_Release(Test->BackBuffers[i]);
    }
    if (Test->TargetHeap)
        ID3D12DescriptorHeap_Release(Test->TargetHeap);
    if (Test->SwapChain)
        IDXGISwapChain3_Release(Test->SwapChain);
    if (Test->Queue)
        ID3D12CommandQueue_Release(Test->Queue);
    if (Test->Device)
        ID3D12Device_Release(Test->Device);
    if (Test->Factory)
        IDXGIFactory4_Release(Test->Factory);
}

BOOL D3D12Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3D12_TEST Test = {0};
    BOOL Passed;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    Passed = D3D12Setup(&Test, hWnd, Result) && D3D12Render(&Test, Result);
    D3D12Release(&Test);
    return Passed;
}
