/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Negotiates the DDI build and converts arguments for a build 0082 driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

static inline UINT Native12SelectDdiBuild(const UINT64 *versions, UINT32 count)
{
    UINT best = 0;

    for (UINT32 i = 0; i < count; ++i)
    {
        UINT build = versions[i] == D3D12DDI_SUPPORTED_0110 ? D3D12DDI_BUILD_VERSION_0110
                : versions[i] == D3D12DDI_SUPPORTED_0102 ? D3D12DDI_BUILD_VERSION_0102
                : versions[i] == D3D12DDI_SUPPORTED_0082 ? D3D12DDI_BUILD_VERSION_0082 : 0;
        if (build > best) best = build;
    }
    return best;
}

static inline D3D12DDI_RASTERIZER_DESC_0010 Native12RasterizerDesc0010(const D3D12DDI_RASTERIZER_DESC_0102 &input)
{
    D3D12DDI_RASTERIZER_DESC_0010 output = {};

    output.FillMode = input.FillMode;
    output.CullMode = input.CullMode;
    output.FrontCounterClockwise = input.FrontCounterClockwise;
    output.DepthBias = static_cast<INT>(input.DepthBias);
    output.DepthBiasClamp = input.DepthBiasClamp;
    output.SlopeScaledDepthBias = input.SlopeScaledDepthBias;
    output.DepthClipEnable = input.DepthClipEnable;
    output.ScissorEnable = input.ScissorEnable;
    output.MultisampleEnable = input.LineRasterizationMode == D3D12DDI_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE;
    output.AntialiasedLineEnable = input.LineRasterizationMode == D3D12DDI_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
    output.ForcedSampleCount = input.ForcedSampleCount;
    output.ConservativeRasterizationMode = input.ConservativeRasterizationMode;
    output.LibraryReference = input.LibraryReference;
    return output;
}

static inline D3D12DDI_STATIC_SAMPLER Native12StaticSampler0013(const D3D12DDI_STATIC_SAMPLER_0100 &input)
{
    D3D12DDI_STATIC_SAMPLER output = {};

    output.Filter = input.Filter;
    output.AddressU = input.AddressU;
    output.AddressV = input.AddressV;
    output.AddressW = input.AddressW;
    output.MipLODBias = input.MipLODBias;
    output.MaxAnisotropy = input.MaxAnisotropy;
    output.ComparisonFunc = input.ComparisonFunc;
    output.BorderColor = input.BorderColor;
    output.MinLOD = input.MinLOD;
    output.MaxLOD = input.MaxLOD;
    output.ShaderRegister = input.ShaderRegister;
    output.RegisterSpace = input.RegisterSpace;
    output.ShaderVisibility = input.ShaderVisibility;
    return output;
}

/* Before build 0088 this slot held the initial state; UTM's host refuses a buffer created in COPY_DEST on its
 * shared-memory heaps, and any buffer legally starts in COMMON and promotes. */
static inline D3D12DDI_BARRIER_LAYOUT Native12InitialSlot(UINT ddi_build, D3D12DDI_BARRIER_LAYOUT layout,
        D3D12_RESOURCE_DIMENSION dimension, D3D12_RESOURCE_STATES state)
{
    if (ddi_build >= D3D12DDI_BUILD_VERSION_0088) return layout;
    return static_cast<D3D12DDI_BARRIER_LAYOUT>(dimension == D3D12_RESOURCE_DIMENSION_BUFFER
            ? D3D12_RESOURCE_STATE_COMMON : state);
}
