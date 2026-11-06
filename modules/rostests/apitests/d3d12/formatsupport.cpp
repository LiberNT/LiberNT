/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device::CheckFeatureSupport reports the format support Direct3D 12 requires
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"

#define LAST_LISTED_FORMAT 115
#define UNDEFINED_SUPPORT1_BITS 0x00022000u
#define VIDEO_SUPPORT1_BITS (D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT | D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT \
        | D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT | D3D12_FORMAT_SUPPORT1_VIDEO_ENCODER)

struct required_format
{
    DXGI_FORMAT format;
    UINT support1;
    BOOL exact;
};

static const required_format required_formats[] =
{
    { DXGI_FORMAT_R32G32B32A32_TYPELESS, 0x001010f0, TRUE },
    { DXGI_FORMAT_R32G32B32_TYPELESS, 0x001010f0, TRUE },
    { DXGI_FORMAT_R8G8B8A8_TYPELESS, 0x001010f0, TRUE },
    { DXGI_FORMAT_R32G8X24_TYPELESS, 0x001010b0, TRUE },
    { DXGI_FORMAT_R24G8_TYPELESS, 0x001010b0, TRUE },
    { DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0x003110b0, TRUE },
    { DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 0x04d017b0, TRUE },
    { DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 0x005011b0, TRUE },
    { DXGI_FORMAT_D24_UNORM_S8_UINT, 0x003110b0, TRUE },
    { DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 0x04d017b0, TRUE },
    { DXGI_FORMAT_X24_TYPELESS_G8_UINT, 0x005011b0, TRUE },
    { DXGI_FORMAT_D32_FLOAT, 0x003110b0, TRUE },
    { DXGI_FORMAT_D16_UNORM, 0x003110b0, TRUE },
    { DXGI_FORMAT_R32_FLOAT, 0x06f4d7fb, TRUE },
    { DXGI_FORMAT_R32_UINT, 0x027051ff, TRUE },
    { DXGI_FORMAT_R16_UNORM, 0x06f4d7f3, TRUE },
    { DXGI_FORMAT_R16_UINT, 0x027051f7, TRUE },
    { DXGI_FORMAT_R8G8B8A8_UNORM, 0x03fcd3f3, TRUE },
    { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 0x01fcd3f0, TRUE },
    { DXGI_FORMAT_B8G8R8A8_UNORM, 0x01fcd3f3, FALSE },
    { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 0x01fcd3f0, TRUE },
    { DXGI_FORMAT_R10G10B10A2_UNORM, 0x03fcd3f3, TRUE },
    { DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, 0x01180020, TRUE },
    { DXGI_FORMAT_A8_UNORM, 0x02e4d3f0, TRUE },
    { DXGI_FORMAT_BC1_TYPELESS, 0x001010e0, TRUE },
    { DXGI_FORMAT_BC1_UNORM, 0x009013e0, TRUE },
    { DXGI_FORMAT_BC3_UNORM_SRGB, 0x009013e0, TRUE },
    { DXGI_FORMAT_BC5_SNORM, 0x009013e0, TRUE },
    { DXGI_FORMAT_BC7_UNORM, 0x009013e0, TRUE },
    { DXGI_FORMAT_AYUV, 0x0280d320, FALSE },
    { DXGI_FORMAT_NV12, 0x0280c320, FALSE },
    { DXGI_FORMAT_YUY2, 0x02800320, FALSE },
    { DXGI_FORMAT_P8, 0x00000020, FALSE },
};

static const DXGI_FORMAT logic_op_formats[] =
{
    DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R32G32_UINT,
    DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R16G16_UINT,
    DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R8_UINT,
};

static D3D12_FEATURE_DATA_FORMAT_SUPPORT QuerySupport(ID3D12Device *device, DXGI_FORMAT format)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT data = {};
    HRESULT hr;

    data.Format = format;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof(data));
    ok(hr == S_OK, "format %u: CheckFeatureSupport: %#lx\n", format, hr);
    return data;
}

static void RunFormatSupport(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT unsupported = {};
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    ID3D12Device *device;
    HRESULT hr;
    UINT format;

    if (!(device = d3d12_test_create_device())) return;

    for (const required_format &entry : required_formats)
    {
        UINT support = QuerySupport(device, entry.format).Support1;

        /* Video support is the driver's: Windows 11 reports it for R8G8B8A8_UNORM on NVIDIA, not on virtio-gpu. */
        if (entry.exact)
            ok((support & ~VIDEO_SUPPORT1_BITS) == entry.support1, "format %u: got %#x, expected %#x\n", entry.format,
               support, entry.support1);
        else
            ok((support & entry.support1) == entry.support1, "format %u: got %#x, required %#x\n", entry.format,
               support, entry.support1);
    }

    for (format = 1; format <= LAST_LISTED_FORMAT; ++format)
    {
        UINT support;

        if (format == DXGI_FORMAT_R1_UNORM) continue;
        support = QuerySupport(device, static_cast<DXGI_FORMAT>(format)).Support1;
        ok(!(support & UNDEFINED_SUPPORT1_BITS), "format %u: got undefined bits in %#x\n", format, support);
    }

    unsupported.Format = DXGI_FORMAT_R1_UNORM;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &unsupported, sizeof(unsupported));
    ok(hr == E_FAIL, "R1_UNORM: CheckFeatureSupport: %#lx\n", hr);
    ok(unsupported.Support1 == D3D12_FORMAT_SUPPORT1_NONE, "R1_UNORM: got %#x\n", unsupported.Support1);
    unsupported.Format = static_cast<DXGI_FORMAT>(LAST_LISTED_FORMAT + 1);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &unsupported, sizeof(unsupported));
    ok(hr == E_FAIL, "format %u: CheckFeatureSupport: %#lx\n", LAST_LISTED_FORMAT + 1, hr);

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "options: CheckFeatureSupport: %#lx\n", hr);
    for (DXGI_FORMAT entry : logic_op_formats)
    {
        UINT support = QuerySupport(device, entry).Support2;

        ok(!!(support & D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP) == !!options.OutputMergerLogicOp,
           "format %u: got %#x with logic op %u\n", entry, support, options.OutputMergerLogicOp);
    }
    ok(!(QuerySupport(device, DXGI_FORMAT_R8G8B8A8_UNORM).Support2 & D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP),
       "R8G8B8A8_UNORM reports logic op\n");
    ok(!!(QuerySupport(device, DXGI_FORMAT_R8G8B8A8_UNORM).Support2 & D3D12_FORMAT_SUPPORT2_TILED)
       == (options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED),
       "R8G8B8A8_UNORM tiled support is wrong for tier %u\n", options.TiledResourcesTier);
    ok(!(QuerySupport(device, DXGI_FORMAT_D24_UNORM_S8_UINT).Support2 & D3D12_FORMAT_SUPPORT2_TILED),
       "D24_UNORM_S8_UINT reports tiled support\n");
    ok(!(QuerySupport(device, DXGI_FORMAT_NV12).Support2 & D3D12_FORMAT_SUPPORT2_TILED),
       "NV12 reports tiled support\n");
    ok(!!(QuerySupport(device, DXGI_FORMAT_R32_FLOAT).Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE),
       "R32_FLOAT has no atomic exchange\n");
    ok(!!(QuerySupport(device, DXGI_FORMAT_NV12).Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE),
       "NV12 has no typed store\n");

    quality.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    quality.SampleCount = 4;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality, sizeof(quality));
    ok(hr == S_OK, "quality: CheckFeatureSupport: %#lx\n", hr);
    ok(quality.NumQualityLevels >= 1, "D24_UNORM_S8_UINT has no 4x multisampling\n");
    ok(!!(QuerySupport(device, DXGI_FORMAT_R24_UNORM_X8_TYPELESS).Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD),
       "A multisampled D24_UNORM_S8_UINT cannot be read\n");

    device->Release();
}

START_TEST(formatsupport)
{
    d3d12_test_bounded("formatsupport", RunFormatSupport, NULL, D3D12_TEST_BUDGET);
}
