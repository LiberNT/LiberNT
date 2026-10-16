/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Root arguments kept across an unordered access view clear
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static const DWORD cs_root_state_code[] =
{
    0x43425844, 0x45a5a4ae, 0x8812d98a, 0x39b010ef, 0x9d40da57, 0x00000001, 0x00000338, 0x00000005,
    0x00000034, 0x00000044, 0x00000054, 0x00000208, 0x0000029c, 0x4e475349, 0x00000008, 0x00000000,
    0x00000008, 0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x46454452, 0x000001ac, 0x00000002,
    0x000000b4, 0x00000003, 0x0000003c, 0x43530500, 0x00000000, 0x0000018c, 0x31314452, 0x0000003c,
    0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x0000009c, 0x00000004,
    0x00000004, 0x00000001, 0xffffffff, 0x00000000, 0x00000001, 0x00000001, 0x000000a4, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x000000a8, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x7074756f, 0xab007475,
    0xab006263, 0x736e6f63, 0x746e6174, 0xabab0073, 0x000000e4, 0x00000001, 0x000000f4, 0x00000010,
    0x00000000, 0x00000000, 0x000000e8, 0x00000001, 0x00000154, 0x00000010, 0x00000000, 0x00000000,
    0xab006263, 0x736e6f63, 0x746e6174, 0xabab0073, 0x0000011c, 0x00000000, 0x00000004, 0x00000002,
    0x00000130, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x765f6263, 0x65756c61,
    0xababab00, 0x746e6975, 0xababab00, 0x00130000, 0x00010001, 0x00000000, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000128, 0x0000017c, 0x00000000, 0x00000004, 0x00000002,
    0x00000130, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x736e6f63, 0x746e6174,
    0x6c61765f, 0xab006575, 0x33646b76, 0x68732d64, 0x72656461, 0x312e3220, 0x69672820, 0x65362074,
    0x62643365, 0xab002932, 0x52444853, 0x0000008c, 0x00050050, 0x00000023, 0x0100086a, 0x0400009b,
    0x00000001, 0x00000001, 0x00000001, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04000059,
    0x00208e46, 0x00000001, 0x00000001, 0x0300009d, 0x0011e001, 0x00000000, 0x080000a6, 0x0011e012,
    0x00000000, 0x00004001, 0x00000000, 0x00208006, 0x00000000, 0x00000000, 0x080000a6, 0x0011e012,
    0x00000000, 0x00004001, 0x00000004, 0x00208006, 0x00000001, 0x00000000, 0x0100003e, 0x54415453,
    0x00000094, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002,
};

static ID3D12Resource *CreateBuffer(ID3D12Device *device, D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = type;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 512;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "buffer of heap type %u: %#lx\n", type, hr);
    return resource;
}

static ID3D12DescriptorHeap *CreateViewHeap(ID3D12Device *device, BOOL shader_visible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    ID3D12DescriptorHeap *heap = NULL;

    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&heap));
    return heap;
}

static ID3D12RootSignature *CreateRootSignature(ID3D12Device *device)
{
    D3D12_ROOT_PARAMETER parameters[3] = {};
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    ID3D12RootSignature *root_signature = NULL;
    ID3DBlob *blob = NULL;
    HRESULT hr;

    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 1;
    parameters[1].Constants.Num32BitValues = 1;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 0;
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL);
    ok(hr == S_OK, "D3D12SerializeRootSignature: %#lx\n", hr);
    if (FAILED(hr)) return NULL;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&root_signature));
    ok(hr == S_OK, "CreateRootSignature: %#lx\n", hr);
    blob->Release();
    return root_signature;
}

static void RunRootState(void *)
{
    static const UINT clear_values[4] = { 7, 7, 7, 7 };
    ID3D12Resource *constants = NULL, *output = NULL, *cleared = NULL, *readback = NULL;
    ID3D12DescriptorHeap *cpu_heap = NULL, *gpu_heap = NULL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
    ID3D12RootSignature *root_signature = NULL;
    ID3D12PipelineState *pipeline = NULL;
    struct d3d12_test_context context;
    D3D12_RESOURCE_BARRIER barrier = {};
    D3D12_RANGE range = { 0, 512 };
    DWORD *data = NULL;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    root_signature = CreateRootSignature(context.device);
    if (root_signature)
    {
        pso_desc.pRootSignature = root_signature;
        pso_desc.CS.pShaderBytecode = cs_root_state_code;
        pso_desc.CS.BytecodeLength = sizeof(cs_root_state_code);
        hr = context.device->CreateComputePipelineState(&pso_desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline));
        ok(hr == S_OK, "CreateComputePipelineState: %#lx\n", hr);
    }
    constants = CreateBuffer(context.device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ);
    output = CreateBuffer(context.device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cleared = CreateBuffer(context.device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    readback = CreateBuffer(context.device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    cpu_heap = CreateViewHeap(context.device, FALSE);
    gpu_heap = CreateViewHeap(context.device, TRUE);
    ok(cpu_heap && gpu_heap, "descriptor heap creation failed\n");

    if (pipeline && constants && output && cleared && readback && cpu_heap && gpu_heap
            && SUCCEEDED(constants->Map(0, NULL, reinterpret_cast<void **>(&data))))
    {
        data[0] = 0x1234;
        constants->Unmap(0, NULL);

        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = 128;
        context.device->CreateUnorderedAccessView(cleared, NULL, &view, cpu_heap->GetCPUDescriptorHandleForHeapStart());
        context.device->CreateUnorderedAccessView(cleared, NULL, &view, gpu_heap->GetCPUDescriptorHandleForHeapStart());

        context.list->SetComputeRootSignature(root_signature);
        context.list->SetPipelineState(pipeline);
        context.list->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        context.list->SetComputeRoot32BitConstant(1, 0x5678, 0);
        context.list->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
        context.list->SetDescriptorHeaps(1, &gpu_heap);
        context.list->ClearUnorderedAccessViewUint(gpu_heap->GetGPUDescriptorHandleForHeapStart(),
                cpu_heap->GetCPUDescriptorHandleForHeapStart(), cleared, clear_values, 0, NULL);
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = cleared;
        context.list->ResourceBarrier(1, &barrier);
        context.list->Dispatch(1, 1, 1);
        d3d12_test_transition(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        d3d12_test_transition(context.list, cleared, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        context.list->CopyBufferRegion(readback, 0, output, 0, 8);
        context.list->CopyBufferRegion(readback, 256, cleared, 0, 4);
        ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "clear and dispatch did not complete\n");

        data = NULL;
        hr = readback->Map(0, &range, reinterpret_cast<void **>(&data));
        ok(hr == S_OK, "Map readback: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(data[0] == 0x1234, "root constant buffer view after the clear: got %#lx\n", data[0]);
            ok(data[1] == 0x5678, "root constant after the clear: got %#lx\n", data[1]);
            ok(data[64] == 7, "cleared view: got %#lx\n", data[64]);
            readback->Unmap(0, NULL);
        }
    }

    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (gpu_heap) gpu_heap->Release();
    if (cpu_heap) cpu_heap->Release();
    if (readback) readback->Release();
    if (cleared) cleared->Release();
    if (output) output->Release();
    if (constants) constants->Release();
    if (pipeline) pipeline->Release();
    if (root_signature) root_signature->Release();
    d3d12_test_destroy(&context);
}

START_TEST(rootstate)
{
    d3d12_test_bounded("rootstate", RunRootState, NULL, D3D12_TEST_BUDGET);
}
