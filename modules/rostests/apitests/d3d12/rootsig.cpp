/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Root signature version 1.2: serialization, conversion, validation and static sampler flags
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static const BYTE reference_v12[] =
{
    0x44, 0x58, 0x42, 0x43, 0x99, 0xe5, 0xaa, 0x03, 0x4b, 0xdb, 0xc6, 0x2e, 0xe5, 0x80, 0x05, 0x42,
    0xae, 0x45, 0x40, 0xc9, 0x01, 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x52, 0x54, 0x53, 0x30, 0x50, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static D3D12_STATIC_SAMPLER_DESC1 Sampler(UINT shader_register, D3D12_SAMPLER_FLAGS flags,
        D3D12_STATIC_BORDER_COLOR border, D3D12_FILTER filter)
{
    D3D12_STATIC_SAMPLER_DESC1 sampler = {};

    sampler.Filter = filter;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = border;
    sampler.ShaderRegister = shader_register;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.Flags = flags;
    return sampler;
}

static HRESULT Serialize12(const D3D12_STATIC_SAMPLER_DESC1 *samplers, UINT count, const D3D12_ROOT_PARAMETER1 *parameters,
        UINT parameter_count, ID3DBlob **blob, BOOL *has_error)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    ID3DBlob *error = NULL;
    HRESULT hr;

    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    desc.Desc_1_2.NumParameters = parameter_count;
    desc.Desc_1_2.pParameters = parameters;
    desc.Desc_1_2.NumStaticSamplers = count;
    desc.Desc_1_2.pStaticSamplers = samplers;
    *blob = NULL;
    hr = D3D12SerializeVersionedRootSignature(&desc, blob, &error);
    if (has_error) *has_error = error != NULL;
    if (error) error->Release();
    return hr;
}

static void TestFeature(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE data;
    HRESULT hr;

    data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &data, sizeof(data));
    ok(hr == S_OK && data.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_2, "version 1.2: %#lx, %#x\n", hr,
            data.HighestVersion);
    data.HighestVersion = static_cast<D3D_ROOT_SIGNATURE_VERSION>(4);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &data, sizeof(data));
    ok(hr == E_INVALIDARG, "unknown version: %#lx\n", hr);
}

static void TestSerialization(void)
{
    D3D12_STATIC_SAMPLER_DESC1 samplers[2];
    ID3D12VersionedRootSignatureDeserializer *deserializer = NULL;
    ID3D12RootSignatureDeserializer *deserializer0 = NULL;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc;
    ID3DBlob *blob;
    HRESULT hr;

    samplers[0] = Sampler(0, D3D12_SAMPLER_FLAG_NONE, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            D3D12_FILTER_MIN_MAG_MIP_POINT);
    hr = Serialize12(samplers, 1, NULL, 0, &blob, NULL);
    ok(hr == S_OK, "version 1.2: %#lx\n", hr);
    if (blob)
    {
        ok(blob->GetBufferSize() == sizeof(reference_v12)
                && !memcmp(blob->GetBufferPointer(), reference_v12, sizeof(reference_v12)),
                "version 1.2 blob differs from Windows (size %Iu)\n", blob->GetBufferSize());
        blob->Release();
    }

    samplers[0] = Sampler(0, D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            D3D12_FILTER_MIN_MAG_MIP_POINT);
    samplers[1] = Sampler(1, D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    hr = Serialize12(samplers, 2, NULL, 0, &blob, NULL);
    ok(hr == S_OK, "two samplers: %#lx\n", hr);
    if (!blob) return;
    ok(blob->GetBufferSize() == 180, "two samplers: size %Iu\n", blob->GetBufferSize());

    hr = D3D12CreateVersionedRootSignatureDeserializer(blob->GetBufferPointer(), blob->GetBufferSize(),
            __uuidof(ID3D12VersionedRootSignatureDeserializer), reinterpret_cast<void **>(&deserializer));
    ok(hr == S_OK, "versioned deserializer: %#lx\n", hr);
    if (deserializer)
    {
        desc = deserializer->GetUnconvertedRootSignatureDesc();
        ok(desc && desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2, "unconverted version %#x\n", desc ? desc->Version : 0);
        if (desc && desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
        {
            ok(desc->Desc_1_2.NumStaticSamplers == 2, "sampler count %u\n", desc->Desc_1_2.NumStaticSamplers);
            ok(desc->Desc_1_2.pStaticSamplers[0].Flags == D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES
                    && desc->Desc_1_2.pStaticSamplers[1].Flags == D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR
                    && desc->Desc_1_2.pStaticSamplers[1].BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT
                    && desc->Desc_1_2.pStaticSamplers[1].ShaderRegister == 1, "flags %#x, %#x, border %#x\n",
                    desc->Desc_1_2.pStaticSamplers[0].Flags, desc->Desc_1_2.pStaticSamplers[1].Flags,
                    desc->Desc_1_2.pStaticSamplers[1].BorderColor);
        }
        desc = NULL;
        hr = deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc);
        ok(hr == S_OK && desc && desc->Desc_1_0.NumStaticSamplers == 2
                && desc->Desc_1_0.pStaticSamplers[1].BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT,
                "at version 1.0: %#lx\n", hr);
        desc = NULL;
        hr = deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
        ok(hr == S_OK && desc && desc->Desc_1_1.NumStaticSamplers == 2
                && desc->Desc_1_1.pStaticSamplers[0].ShaderRegister == 0, "at version 1.1: %#lx\n", hr);
        desc = NULL;
        hr = deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_2, &desc);
        ok(hr == S_OK && desc && desc->Desc_1_2.pStaticSamplers[0].Flags == D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                "at version 1.2: %#lx\n", hr);
        deserializer->Release();
    }
    hr = D3D12CreateRootSignatureDeserializer(blob->GetBufferPointer(), blob->GetBufferSize(),
            __uuidof(ID3D12RootSignatureDeserializer), reinterpret_cast<void **>(&deserializer0));
    ok(hr == S_OK, "version 1.0 deserializer: %#lx\n", hr);
    if (deserializer0)
    {
        const D3D12_ROOT_SIGNATURE_DESC *desc0 = deserializer0->GetRootSignatureDesc();
        ok(desc0 && desc0->NumStaticSamplers == 2, "version 1.0 deserializer sampler count %u\n",
                desc0 ? desc0->NumStaticSamplers : 0);
        deserializer0->Release();
    }
    blob->Release();

    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {};
        D3D12_STATIC_SAMPLER_DESC plain = {};

        plain.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        plain.AddressU = plain.AddressV = plain.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        plain.MaxAnisotropy = 1;
        plain.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        plain.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT;
        plain.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        versioned.Desc_1_1.NumStaticSamplers = 1;
        versioned.Desc_1_1.pStaticSamplers = &plain;
        blob = NULL;
        hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, NULL);
        ok(hr == S_OK, "version 1.1 with an integer border colour: %#lx\n", hr);
        if (blob)
        {
            deserializer = NULL;
            D3D12CreateVersionedRootSignatureDeserializer(blob->GetBufferPointer(), blob->GetBufferSize(),
                    __uuidof(ID3D12VersionedRootSignatureDeserializer), reinterpret_cast<void **>(&deserializer));
            if (deserializer)
            {
                desc = NULL;
                hr = deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_2, &desc);
                ok(hr == S_OK && desc && desc->Desc_1_2.pStaticSamplers[0].Flags == D3D12_SAMPLER_FLAG_NONE
                        && desc->Desc_1_2.pStaticSamplers[0].BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT,
                        "version 1.1 at 1.2: %#lx\n", hr);
                deserializer->Release();
            }
            blob->Release();
        }
        versioned.Version = static_cast<D3D_ROOT_SIGNATURE_VERSION>(4);
        blob = NULL;
        hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, NULL);
        ok(hr == E_INVALIDARG && !blob, "unknown version: %#lx\n", hr);
    }
}

static void TestValidation(void)
{
    static const struct
    {
        const char *name;
        D3D12_SAMPLER_FLAGS flags;
        D3D12_STATIC_BORDER_COLOR border;
        D3D12_FILTER filter;
        UINT case_id;
        HRESULT expected;
    }
    tests[] =
    {
        { "integer colour without flag", D3D12_SAMPLER_FLAG_NONE, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT,
                D3D12_FILTER_MIN_MAG_MIP_POINT, 0, S_OK },
        { "unknown flag", static_cast<D3D12_SAMPLER_FLAGS>(4), D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
                D3D12_FILTER_MIN_MAG_MIP_POINT, 0, E_INVALIDARG },
        { "integer flag with a float colour", D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
                D3D12_FILTER_MIN_MAG_MIP_POINT, 0, E_INVALIDARG },
        { "non-normalised linear mip", D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0, E_INVALIDARG },
        { "non-normalised anisotropic", D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_ANISOTROPIC, 1, E_INVALIDARG },
        { "non-normalised max LOD", D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_MIN_MAG_MIP_POINT, 2, E_INVALIDARG },
        { "non-normalised wrap", D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_MIN_MAG_MIP_POINT, 3, E_INVALIDARG },
        { "non-normalised point", D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_MIN_MAG_MIP_POINT, 0, S_OK },
    };

    for (UINT i = 0; i < ARRAYSIZE(tests); ++i)
    {
        D3D12_STATIC_SAMPLER_DESC1 sampler = Sampler(0, tests[i].flags, tests[i].border, tests[i].filter);
        ID3DBlob *blob;
        BOOL has_error;
        HRESULT hr;

        if (tests[i].case_id == 1) sampler.MaxAnisotropy = 4;
        if (tests[i].case_id == 2) sampler.MaxLOD = 4.0f;
        if (tests[i].case_id == 3) sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        hr = Serialize12(&sampler, 1, NULL, 0, &blob, &has_error);
        ok(hr == tests[i].expected, "%s: %#lx\n", tests[i].name, hr);
        if (FAILED(tests[i].expected))
            ok(!blob && has_error, "%s: blob %p, error blob %d\n", tests[i].name, blob, has_error);
        if (blob) blob->Release();
    }
}

static ID3D12RootSignature *CreateRootSignature12(ID3D12Device *device, const D3D12_STATIC_SAMPLER_DESC1 *sampler,
        BOOL with_table, HRESULT *result)
{
    D3D12_DESCRIPTOR_RANGE1 range = {};
    D3D12_ROOT_PARAMETER1 parameter = {};
    ID3D12RootSignature *signature = NULL;
    ID3DBlob *blob;
    HRESULT hr;

    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    hr = Serialize12(sampler, 1, with_table ? &parameter : NULL, with_table ? 1 : 0, &blob, NULL);
    if (SUCCEEDED(hr))
    {
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&signature));
        blob->Release();
    }
    *result = hr;
    return signature;
}

static void TestCreation(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options = {};
    D3D12_STATIC_SAMPLER_DESC1 sampler;
    ID3D12RootSignature *signature;
    HRESULT hr;

    sampler = Sampler(0, D3D12_SAMPLER_FLAG_NONE, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_FILTER_MIN_MAG_MIP_POINT);
    signature = CreateRootSignature12(device, &sampler, FALSE, &hr);
    ok(hr == S_OK, "version 1.2 root signature: %#lx\n", hr);
    if (signature) signature->Release();
    sampler = Sampler(0, D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT,
            D3D12_FILTER_MIN_MAG_MIP_POINT);
    signature = CreateRootSignature12(device, &sampler, FALSE, &hr);
    ok(hr == S_OK, "integer border colour: %#lx\n", hr);
    if (signature) signature->Release();

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS17: %#lx\n", hr);
    if (!options.NonNormalizedCoordinateSamplersSupported)
    {
        trace("Non-normalised coordinate samplers are not supported, skipping them\n");
        return;
    }
    sampler = Sampler(0, D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            D3D12_FILTER_MIN_MAG_MIP_POINT);
    signature = CreateRootSignature12(device, &sampler, FALSE, &hr);
    ok(hr == S_OK, "non-normalised sampler: %#lx\n", hr);
    if (signature) signature->Release();
}

static void RunSampling(void *)
{
    static const DWORD texels[4] = { 0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu };
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options = {};
    D3D12_HEAP_PROPERTIES default_heap = { D3D12_HEAP_TYPE_DEFAULT }, upload_heap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_RESOURCE_DESC texture_desc = {}, buffer_desc = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    struct d3d12_test_context context;
    D3D12_STATIC_SAMPLER_DESC1 sampler;
    ID3D12RootSignature *signature = NULL;
    ID3D12PipelineState *pipeline = NULL;
    ID3D12DescriptorHeap *srv_heap = NULL;
    ID3D12Resource *texture = NULL, *upload = NULL, *target = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    void *data = NULL;
    DWORD texel;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &options, sizeof(options));
    if (!options.NonNormalizedCoordinateSamplersSupported)
    {
        d3d12_test_destroy(&context);
        return;
    }

    sampler = Sampler(0, D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            D3D12_FILTER_MIN_MAG_MIP_POINT);
    signature = CreateRootSignature12(context.device, &sampler, TRUE, &hr);
    ok(hr == S_OK, "sampling root signature: %#lx\n", hr);

    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 4;
    texture_desc.Height = 1;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    hr = context.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&texture));
    ok(hr == S_OK, "texture: %#lx\n", hr);
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&upload));
    ok(hr == S_OK, "upload buffer: %#lx\n", hr);
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = context.device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void **>(&srv_heap));
    ok(hr == S_OK, "SRV heap: %#lx\n", hr);
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!signature || !texture || !upload || !srv_heap || !target) goto done;

    pso_desc.pRootSignature = signature;
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
    hr = context.device->CreateGraphicsPipelineState(&pso_desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&pipeline));
    ok(hr == S_OK, "sampling pipeline: %#lx\n", hr);
    if (!pipeline) goto done;

    hr = upload->Map(0, NULL, &data);
    ok(hr == S_OK, "Map: %#lx\n", hr);
    if (FAILED(hr)) goto done;
    memcpy(data, texels, sizeof(texels));
    upload->Unmap(0, NULL);
    context.device->CreateShaderResourceView(texture, NULL, srv_heap->GetCPUDescriptorHandleForHeapStart());
    rtv = d3d12_test_rtv(&context, 0);
    context.device->CreateRenderTargetView(target, NULL, rtv);

    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 4;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    context.list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    d3d12_test_transition(context.list, texture, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    d3d12_test_bind_target(&context, context.list, 1, &rtv);
    context.list->SetGraphicsRootSignature(signature);
    context.list->SetDescriptorHeaps(1, &srv_heap);
    context.list->SetGraphicsRootDescriptorTable(0, srv_heap->GetGPUDescriptorHandleForHeapStart());
    context.list->SetPipelineState(pipeline);
    context.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.list->DrawInstanced(3, 1, 0, 0);
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "sampling draw did not complete\n");
    texel = 0;
    ok(d3d12_test_read_texel(&context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel), "readback failed\n");
    ok(texel == texels[2], "non-normalised sampling at (2.5, 0.5): got %08lx, expected %08lx\n", texel, texels[2]);

done:
    if (pipeline) pipeline->Release();
    if (target) target->Release();
    if (srv_heap) srv_heap->Release();
    if (upload) upload->Release();
    if (texture) texture->Release();
    if (signature) signature->Release();
    d3d12_test_destroy(&context);
}

static void RunRootSignature(void *)
{
    ID3D12Device *device;

    TestSerialization();
    TestValidation();
    if (!(device = d3d12_test_create_device())) return;
    TestFeature(device);
    TestCreation(device);
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    device->Release();
    RunSampling(NULL);
}

START_TEST(rootsig)
{
    d3d12_test_bounded("rootsig", RunRootSignature, NULL, D3D12_TEST_BUDGET);
}
