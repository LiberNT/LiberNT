/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DDI build negotiation and the argument conversions for a build 0082 driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"
#include <d3dkmthk.h>
#include <d3d12umddi.h>
#include "../../../../dll/directx/wine/d3d12/native12_ddi0082.h"

static void TestSelectBuild(void)
{
    const UINT64 Only0082[] = {D3D12DDI_SUPPORTED_0082};
    const UINT64 Unknown[] = {D3D12DDI_SUPPORTED_0022};
    const UINT64 Mixed[] = {D3D12DDI_SUPPORTED_0082, D3D12DDI_SUPPORTED_0110, D3D12DDI_SUPPORTED_0102};

    ok_int(Native12SelectDdiBuild(Only0082, ARRAYSIZE(Only0082)), D3D12DDI_BUILD_VERSION_0082);
    ok_int(Native12SelectDdiBuild(Unknown, ARRAYSIZE(Unknown)), 0);
    ok_int(Native12SelectDdiBuild(Mixed, ARRAYSIZE(Mixed)), D3D12DDI_BUILD_VERSION_0110);
    ok_int(Native12SelectDdiBuild(NULL, 0), 0);
}

static void TestRasterizer(void)
{
    D3D12DDI_RASTERIZER_DESC_0102 Input = {};
    D3D12DDI_RASTERIZER_DESC_0010 Output;

    Input.FillMode = D3D12DDI_FILL_MODE_WIREFRAME;
    Input.CullMode = D3D12DDI_CULL_MODE_BACK;
    Input.FrontCounterClockwise = TRUE;
    Input.DepthBias = -7.0f;
    Input.DepthClipEnable = TRUE;
    Input.ScissorEnable = TRUE;
    Input.ForcedSampleCount = 4;
    Output = Native12RasterizerDesc0010(Input);
    ok_int(Output.FillMode, D3D12DDI_FILL_MODE_WIREFRAME);
    ok_int(Output.CullMode, D3D12DDI_CULL_MODE_BACK);
    ok_int(Output.FrontCounterClockwise, TRUE);
    ok_int(Output.DepthBias, -7);
    ok_int(Output.DepthClipEnable, TRUE);
    ok_int(Output.ScissorEnable, TRUE);
    ok_int(Output.ForcedSampleCount, 4);
    ok_int(Output.MultisampleEnable, FALSE);
    ok_int(Output.AntialiasedLineEnable, FALSE);

    Input.LineRasterizationMode = D3D12DDI_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE;
    Output = Native12RasterizerDesc0010(Input);
    ok_int(Output.MultisampleEnable, TRUE);
    ok_int(Output.AntialiasedLineEnable, FALSE);

    Input.LineRasterizationMode = D3D12DDI_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
    Output = Native12RasterizerDesc0010(Input);
    ok_int(Output.MultisampleEnable, FALSE);
    ok_int(Output.AntialiasedLineEnable, TRUE);
}

static void TestStaticSampler(void)
{
    D3D12DDI_STATIC_SAMPLER_0100 Input = {};
    D3D12DDI_STATIC_SAMPLER Output;

    Input.MaxAnisotropy = 8;
    Input.ShaderRegister = 3;
    Input.RegisterSpace = 2;
    Input.Flags = static_cast<D3D12DDI_SAMPLER_FLAGS_0096>(1);
    Output = Native12StaticSampler0013(Input);
    ok_int(Output.MaxAnisotropy, 8);
    ok_int(Output.ShaderRegister, 3);
    ok_int(Output.RegisterSpace, 2);
}

static void TestInitialSlot(void)
{
    ok_int(Native12InitialSlot(D3D12DDI_BUILD_VERSION_0082, D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET,
                               D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_STATE_RENDER_TARGET),
           D3D12_RESOURCE_STATE_RENDER_TARGET);
    ok_int(Native12InitialSlot(D3D12DDI_BUILD_VERSION_0082, D3D12DDI_BARRIER_LAYOUT_UNDEFINED,
                               D3D12_RESOURCE_DIMENSION_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST),
           D3D12_RESOURCE_STATE_COMMON);
    ok_int(Native12InitialSlot(D3D12DDI_BUILD_VERSION_0102, D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET,
                               D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_STATE_RENDER_TARGET),
           D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET);
}

START_TEST(ddibuild)
{
    TestSelectBuild();
    TestRasterizer();
    TestStaticSampler();
    TestInitialSlot();
}
