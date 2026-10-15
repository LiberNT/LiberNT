/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Descriptor heap samplers: custom border colours and CreateSampler2
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static const DWORD texels[4] = { 0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu };

struct sampler_test
{
    struct d3d12_test_context context;
    ID3D12RootSignature *signature;
    ID3D12PipelineState *pipeline;
    ID3D12DescriptorHeap *srv_heap, *sampler_heap;
    ID3D12Resource *texture, *upload, *target;
};

static ID3D12RootSignature *CreateRootSignature(ID3D12Device *device)
{
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    D3D12_ROOT_PARAMETER parameters[2] = {};
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    ID3D12RootSignature *signature = NULL;
    ID3DBlob *blob = NULL;
    HRESULT hr;

    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 1;
    for (UINT i = 0; i < 2; ++i)
    {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL);
    ok(hr == S_OK, "D3D12SerializeRootSignature: %#lx\n", hr);
    if (FAILED(hr)) return NULL;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature),
            reinterpret_cast<void **>(&signature));
    ok(hr == S_OK, "CreateRootSignature: %#lx\n", hr);
    blob->Release();
    return signature;
}

static BOOL Setup(struct sampler_test *test)
{
    D3D12_HEAP_PROPERTIES default_heap = { D3D12_HEAP_TYPE_DEFAULT }, upload_heap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_RESOURCE_DESC texture_desc = {}, buffer_desc = {};
    ID3D12Device *device;
    void *data = NULL;
    HRESULT hr;

    ZeroMemory(test, sizeof(*test));
    if (!d3d12_test_init(&test->context)) return FALSE;
    device = test->context.device;
    test->signature = CreateRootSignature(device);

    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 4;
    texture_desc.Height = 1;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&test->texture));
    ok(hr == S_OK, "texture: %#lx\n", hr);
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&test->upload));
    ok(hr == S_OK, "upload buffer: %#lx\n", hr);
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void **>(&test->srv_heap));
    ok(hr == S_OK, "SRV heap: %#lx\n", hr);
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hr = device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void **>(&test->sampler_heap));
    ok(hr == S_OK, "sampler heap: %#lx\n", hr);
    test->target = d3d12_test_create_target(device, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!test->signature || !test->texture || !test->upload || !test->srv_heap || !test->sampler_heap || !test->target)
        return FALSE;

    pso_desc.pRootSignature = test->signature;
    pso_desc.VS.pShaderBytecode = vs_fullscreen_code;
    pso_desc.VS.BytecodeLength = vs_fullscreen_code_size;
    pso_desc.PS.pShaderBytecode = ps_sample_code;
    pso_desc.PS.BytecodeLength = ps_sample_code_size;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = ~0u;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;
    hr = device->CreateGraphicsPipelineState(&pso_desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&test->pipeline));
    ok(hr == S_OK, "pipeline: %#lx\n", hr);
    if (!test->pipeline) return FALSE;

    hr = test->upload->Map(0, NULL, &data);
    ok(hr == S_OK, "Map: %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    memcpy(data, texels, sizeof(texels));
    test->upload->Unmap(0, NULL);
    device->CreateShaderResourceView(test->texture, NULL, test->srv_heap->GetCPUDescriptorHandleForHeapStart());
    device->CreateRenderTargetView(test->target, NULL, d3d12_test_rtv(&test->context, 0));

    {
        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};

        dst.pResource = test->texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = test->upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = 4;
        src.PlacedFootprint.Footprint.Height = 1;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        test->context.list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        d3d12_test_transition(test->context.list, test->texture, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    return d3d12_test_execute(&test->context, test->context.list, D3D12_TEST_WAIT);
}

static void Cleanup(struct sampler_test *test)
{
    if (test->pipeline) test->pipeline->Release();
    if (test->target) test->target->Release();
    if (test->sampler_heap) test->sampler_heap->Release();
    if (test->srv_heap) test->srv_heap->Release();
    if (test->upload) test->upload->Release();
    if (test->texture) test->texture->Release();
    if (test->signature) test->signature->Release();
    d3d12_test_destroy(&test->context);
}

static DWORD Draw(struct sampler_test *test)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(&test->context, 0);
    ID3D12DescriptorHeap *heaps[2] = { test->srv_heap, test->sampler_heap };
    ID3D12GraphicsCommandList *list = test->context.list;
    DWORD texel = 0;

    test->context.allocator->Reset();
    list->Reset(test->context.allocator, NULL);
    d3d12_test_bind_target(&test->context, list, 1, &rtv);
    list->SetGraphicsRootSignature(test->signature);
    list->SetDescriptorHeaps(2, heaps);
    list->SetGraphicsRootDescriptorTable(0, test->srv_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(1, test->sampler_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetPipelineState(test->pipeline);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    ok(d3d12_test_execute(&test->context, list, D3D12_TEST_WAIT), "draw did not complete\n");
    ok(d3d12_test_read_texel(&test->context, test->target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
            "readback failed\n");
    return texel;
}

static BOOL ColourNear(DWORD a, DWORD b)
{
    for (UINT shift = 0; shift < 32; shift += 8)
    {
        int ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
        if (ca - cb > 1 || cb - ca > 1) return FALSE;
    }
    return TRUE;
}

static void RunSampler(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options17 = {};
    struct sampler_test test;
    ID3D12Device11 *device11 = NULL;
    D3D12_SAMPLER_DESC desc = {};
    D3D12_SAMPLER_DESC2 desc2 = {};
    DWORD texel;

    if (!Setup(&test))
    {
        Cleanup(&test);
        return;
    }

    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.BorderColor[0] = 0.2f;
    desc.BorderColor[1] = 0.4f;
    desc.BorderColor[2] = 0.6f;
    desc.BorderColor[3] = 1.0f;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    test.context.device->CreateSampler(&desc, test.sampler_heap->GetCPUDescriptorHandleForHeapStart());
    texel = Draw(&test);
    ok(ColourNear(texel, 0xff996633u), "CreateSampler custom border colour: got %08lx\n", texel);

    desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    test.context.device->CreateSampler(&desc, test.sampler_heap->GetCPUDescriptorHandleForHeapStart());
    texel = Draw(&test);
    ok(texel == texels[3], "CreateSampler clamp: got %08lx\n", texel);

    if (FAILED(test.context.device->QueryInterface(__uuidof(ID3D12Device11), reinterpret_cast<void **>(&device11))))
    {
        Cleanup(&test);
        return;
    }
    desc2.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    desc2.AddressU = desc2.AddressV = desc2.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    desc2.MaxAnisotropy = 1;
    desc2.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc2.FloatBorderColor[0] = 0.6f;
    desc2.FloatBorderColor[1] = 0.4f;
    desc2.FloatBorderColor[2] = 0.2f;
    desc2.FloatBorderColor[3] = 1.0f;
    desc2.MaxLOD = D3D12_FLOAT32_MAX;
    device11->CreateSampler2(&desc2, test.sampler_heap->GetCPUDescriptorHandleForHeapStart());
    texel = Draw(&test);
    ok(ColourNear(texel, 0xff336699u), "CreateSampler2 custom border colour: got %08lx\n", texel);

    test.context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &options17, sizeof(options17));
    if (options17.NonNormalizedCoordinateSamplersSupported)
    {
        desc2.AddressU = desc2.AddressV = desc2.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc2.MaxLOD = 0.0f;
        desc2.Flags = D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES;
        device11->CreateSampler2(&desc2, test.sampler_heap->GetCPUDescriptorHandleForHeapStart());
        texel = Draw(&test);
        ok(texel == texels[2], "CreateSampler2 non-normalised coordinates: got %08lx\n", texel);
    }
    else trace("Non-normalised coordinate samplers are not supported, skipping them\n");

    ok(test.context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            test.context.device->GetDeviceRemovedReason());
    device11->Release();
    Cleanup(&test);
}

START_TEST(sampler)
{
    d3d12_test_bounded("sampler", RunSampler, NULL, D3D12_TEST_BUDGET);
}
