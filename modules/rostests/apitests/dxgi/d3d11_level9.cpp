/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 feature level 9 devices report the documented level 9 format support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <d3d11.h>

#define LAST_FORMAT 115

typedef HRESULT (WINAPI *PFN_CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                            UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static UINT
Support(ID3D11Device *Device, DXGI_FORMAT Format)
{
    UINT Value = 0;

    Device->CheckFormatSupport(Format, &Value);
    return Value;
}

static void
CheckLevel(PFN_CREATE_DEVICE Create, D3D_FEATURE_LEVEL Level)
{
    ID3D11Device *Device = NULL;
    D3D_FEATURE_LEVEL Obtained = (D3D_FEATURE_LEVEL)0;
    UINT Format;

    ok_hex(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Device, &Obtained, NULL), S_OK);
    if (Device == NULL)
        return;
    ok(Obtained == Level, "Got level %#x, expected %#x\n", Obtained, Level);
    ok(Device->GetFeatureLevel() == Level, "Device reports level %#x, expected %#x\n", Device->GetFeatureLevel(), Level);

    for (Format = 0; Format <= LAST_FORMAT; ++Format)
    {
        UINT Value = Support(Device, (DXGI_FORMAT)Format);

        if (Value != 0)
            trace("level %#x format %u support %#x\n", Level, Format, Value);
        ok((Value & D3D11_FORMAT_SUPPORT_BUFFER) == 0, "Format %u has BUFFER: %#x\n", Format, Value);
        ok((Value & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON) == 0, "Format %u has sample_c: %#x\n", Format, Value);
        ok((Value & D3D11_FORMAT_SUPPORT_SHADER_GATHER) == 0, "Format %u has gather: %#x\n", Format, Value);
    }

    ok((Support(Device, DXGI_FORMAT_R8G8B8A8_UNORM) & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0, "R8G8B8A8_UNORM is no render target\n");
    ok((Support(Device, DXGI_FORMAT_R16_UINT) & D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER) != 0, "R16_UINT is no index buffer\n");
    ok(((Support(Device, DXGI_FORMAT_R32_UINT) & D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER) != 0) == (Level >= D3D_FEATURE_LEVEL_9_2),
       "R32_UINT index buffer support is wrong at level %#x\n", Level);
    ok(((Support(Device, DXGI_FORMAT_R32G32B32A32_FLOAT) & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0) == (Level >= D3D_FEATURE_LEVEL_9_2),
       "R32G32B32A32_FLOAT texture support is wrong at level %#x\n", Level);
    ok(((Support(Device, DXGI_FORMAT_R16G16B16A16_FLOAT) & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0) == (Level >= D3D_FEATURE_LEVEL_9_3),
       "R16G16B16A16_FLOAT blending is wrong at level %#x\n", Level);
    ok(Support(Device, DXGI_FORMAT_R32G32B32_FLOAT) == D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER,
       "R32G32B32_FLOAT is more than a vertex format at level %#x\n", Level);
    ok(Support(Device, DXGI_FORMAT_R8G8_UINT) == 0, "R8G8_UINT is supported at level %#x\n", Level);
    Device->Release();
}

START_TEST(d3d11_level9)
{
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    PFN_CREATE_DEVICE Create;

    if (Module == NULL)
    {
        skip("d3d11.dll is not available\n");
        return;
    }
    Create = (PFN_CREATE_DEVICE)GetProcAddress(Module, "D3D11CreateDevice");
    ok(Create != NULL, "D3D11CreateDevice is not exported\n");
    if (Create == NULL)
        return;
    CheckLevel(Create, D3D_FEATURE_LEVEL_9_1);
    CheckLevel(Create, D3D_FEATURE_LEVEL_9_2);
    CheckLevel(Create, D3D_FEATURE_LEVEL_9_3);
}
