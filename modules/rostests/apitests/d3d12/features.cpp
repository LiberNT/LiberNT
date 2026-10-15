/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device::CheckFeatureSupport size and capability contracts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

template <typename T> static void TestFeature(ID3D12Device *device, D3D12_FEATURE feature, const T &input)
{
    T data = input;
    HRESULT hr = device->CheckFeatureSupport(feature, &data, sizeof(data));
    ok(hr == S_OK, "feature %u: CheckFeatureSupport: %#lx\n", feature, hr);
    data = input;
    hr = device->CheckFeatureSupport(feature, &data, sizeof(data) - 1);
    ok(hr == E_INVALIDARG, "feature %u: short size: %#lx\n", feature, hr);
}

template <typename T> static void TestLongFeature(ID3D12Device *device, D3D12_FEATURE feature)
{
    BYTE data[sizeof(T) + 4] = {};
    HRESULT hr = device->CheckFeatureSupport(feature, data, sizeof(data));
    ok(hr == E_INVALIDARG, "feature %u: long size: %#lx\n", feature, hr);
}

template <typename T> static void TestGenericOnlyFeature(ID3D12Device *device, D3D12_FEATURE feature)
{
    T data;
    memset(&data, 0x55, sizeof(data));
    HRESULT hr = device->CheckFeatureSupport(feature, &data, sizeof(data));
    ok(hr == E_UNEXPECTED, "feature %u: %#lx\n", feature, hr);
    ok(*reinterpret_cast<const BYTE *>(&data) == 0x55 && reinterpret_cast<const BYTE *>(&data)[sizeof(data) - 1] == 0x55,
            "feature %u: output written\n", feature);
    hr = device->CheckFeatureSupport(feature, &data, sizeof(data) - 1);
    ok(hr == E_INVALIDARG, "feature %u: short size: %#lx\n", feature, hr);
    TestLongFeature<T>(device, feature);
}

static void RunFeatures(void *)
{
    static const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {};
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    D3D12_FEATURE_DATA_FORMAT_INFO format_info = {};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {};
    D3D12_FEATURE_DATA_ROOT_SIGNATURE root_signature = {};
    D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY priority = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 options19 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS20 options20 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21 = {};
    D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO placed = {};
    ID3D12Device *device;
    BYTE buffer[256] = {};
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;

    levels.NumFeatureLevels = 1;
    levels.pFeatureLevelsRequested = &level;
    format_support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    quality.SampleCount = 1;
    format_info.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_0;
    root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    priority.CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    priority.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS, D3D12_FEATURE_DATA_D3D12_OPTIONS());
    TestFeature(device, D3D12_FEATURE_ARCHITECTURE, D3D12_FEATURE_DATA_ARCHITECTURE());
    TestFeature(device, D3D12_FEATURE_FEATURE_LEVELS, levels);
    TestFeature(device, D3D12_FEATURE_FORMAT_SUPPORT, format_support);
    TestFeature(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, quality);
    TestFeature(device, D3D12_FEATURE_FORMAT_INFO, format_info);
    TestFeature(device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT());
    TestFeature(device, D3D12_FEATURE_SHADER_MODEL, shader_model);
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS1, D3D12_FEATURE_DATA_D3D12_OPTIONS1());
    TestFeature(device, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT,
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT());
    TestFeature(device, D3D12_FEATURE_ROOT_SIGNATURE, root_signature);
    TestFeature(device, D3D12_FEATURE_ARCHITECTURE1, D3D12_FEATURE_DATA_ARCHITECTURE1());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS2, D3D12_FEATURE_DATA_D3D12_OPTIONS2());
    TestFeature(device, D3D12_FEATURE_SHADER_CACHE, D3D12_FEATURE_DATA_SHADER_CACHE());
    TestFeature(device, D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, priority);
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS3, D3D12_FEATURE_DATA_D3D12_OPTIONS3());
    TestFeature(device, D3D12_FEATURE_EXISTING_HEAPS, D3D12_FEATURE_DATA_EXISTING_HEAPS());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS4, D3D12_FEATURE_DATA_D3D12_OPTIONS4());
    TestFeature(device, D3D12_FEATURE_SERIALIZATION, D3D12_FEATURE_DATA_SERIALIZATION());
    TestFeature(device, D3D12_FEATURE_CROSS_NODE, D3D12_FEATURE_DATA_CROSS_NODE());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS5, D3D12_FEATURE_DATA_D3D12_OPTIONS5());
    TestFeature(device, D3D12_FEATURE_DISPLAYABLE, D3D12_FEATURE_DATA_DISPLAYABLE());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS6, D3D12_FEATURE_DATA_D3D12_OPTIONS6());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS7, D3D12_FEATURE_DATA_D3D12_OPTIONS7());
    TestFeature(device, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT,
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS8, D3D12_FEATURE_DATA_D3D12_OPTIONS8());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS9, D3D12_FEATURE_DATA_D3D12_OPTIONS9());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS10, D3D12_FEATURE_DATA_D3D12_OPTIONS10());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS11, D3D12_FEATURE_DATA_D3D12_OPTIONS11());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS12, D3D12_FEATURE_DATA_D3D12_OPTIONS12());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS13, D3D12_FEATURE_DATA_D3D12_OPTIONS13());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS14, D3D12_FEATURE_DATA_D3D12_OPTIONS14());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS15, D3D12_FEATURE_DATA_D3D12_OPTIONS15());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS16, D3D12_FEATURE_DATA_D3D12_OPTIONS16());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS17, D3D12_FEATURE_DATA_D3D12_OPTIONS17());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS18, D3D12_FEATURE_DATA_D3D12_OPTIONS18());

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS18, &options18, sizeof(options18));
    ok(hr == S_OK && options18.RenderPassesValid, "RenderPassesValid %d, hr %#lx\n", options18.RenderPassesValid, hr);

    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS19, D3D12_FEATURE_DATA_D3D12_OPTIONS19());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS20, D3D12_FEATURE_DATA_D3D12_OPTIONS20());
    TestFeature(device, D3D12_FEATURE_D3D12_OPTIONS21, D3D12_FEATURE_DATA_D3D12_OPTIONS21());
    TestLongFeature<D3D12_FEATURE_DATA_D3D12_OPTIONS19>(device, D3D12_FEATURE_D3D12_OPTIONS19);
    TestLongFeature<D3D12_FEATURE_DATA_D3D12_OPTIONS20>(device, D3D12_FEATURE_D3D12_OPTIONS20);
    TestLongFeature<D3D12_FEATURE_DATA_D3D12_OPTIONS21>(device, D3D12_FEATURE_D3D12_OPTIONS21);

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS19, &options19, sizeof(options19));
    ok(hr == S_OK, "options19: %#lx\n", hr);
    ok(options19.SupportedSampleCountsWithNoOutputs & 1, "sample counts with no outputs %#x\n",
            options19.SupportedSampleCountsWithNoOutputs);
    ok(options19.MaxSamplerDescriptorHeapSize >= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE,
            "sampler heap size %u\n", options19.MaxSamplerDescriptorHeapSize);
    ok(options19.MaxSamplerDescriptorHeapSizeWithStaticSamplers >= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE,
            "sampler heap size with static samplers %u\n", options19.MaxSamplerDescriptorHeapSizeWithStaticSamplers);
    ok(options19.MaxViewDescriptorHeapSize >= D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2,
            "view heap size %u\n", options19.MaxViewDescriptorHeapSize);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS20, &options20, sizeof(options20));
    ok(hr == S_OK && (options20.RecreateAtTier == D3D12_RECREATE_AT_TIER_NOT_SUPPORTED
            || options20.RecreateAtTier == D3D12_RECREATE_AT_TIER_1), "recreate-at tier %d, hr %#lx\n",
            options20.RecreateAtTier, hr);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21));
    ok(hr == S_OK, "options21: %#lx\n", hr);
    ok(options21.ExecuteIndirectTier == D3D12_EXECUTE_INDIRECT_TIER_1_0
            || options21.ExecuteIndirectTier == D3D12_EXECUTE_INDIRECT_TIER_1_1,
            "execute indirect tier %d\n", options21.ExecuteIndirectTier);
    ok(options21.WorkGraphsTier == D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED
            || options21.WorkGraphsTier == D3D12_WORK_GRAPHS_TIER_1_0, "work graphs tier %d\n", options21.WorkGraphsTier);

    TestGenericOnlyFeature<D3D12_FEATURE_DATA_PREDICATION>(device, D3D12_FEATURE_PREDICATION);
    TestGenericOnlyFeature<D3D12_FEATURE_DATA_HARDWARE_COPY>(device, D3D12_FEATURE_HARDWARE_COPY);
    TestGenericOnlyFeature<D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO>(device,
            D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO);
    placed.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    placed.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    placed.DestHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    placed.Supported = 0x55;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO, &placed, sizeof(placed));
    ok(hr == E_UNEXPECTED && placed.Supported == 0x55, "placed resource support %d, hr %#lx\n", placed.Supported, hr);

    shader_model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x69);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    ok(hr == S_OK, "shader model 6.9 request: %#lx\n", hr);
    ok(shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_0 && shader_model.HighestShaderModel <= 0x69,
            "shader model %#x\n", shader_model.HighestShaderModel);

    hr = device->CheckFeatureSupport(static_cast<D3D12_FEATURE>(0x7fffffff), buffer, sizeof(buffer));
    ok(hr == E_INVALIDARG, "unknown feature: %#lx\n", hr);

    device->Release();
}

START_TEST(features)
{
    d3d12_test_bounded("features", RunFeatures, NULL, D3D12_TEST_BUDGET);
}
