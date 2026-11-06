/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D11Device::CheckFeatureSupport answers that depend on the feature level
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11_4.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ID3D11Device *CreateAtLevel(CREATE_DEVICE Create, D3D_FEATURE_LEVEL Level)
{
    ID3D11Device *Device = NULL;

    if (FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Device, NULL, NULL)))
        return NULL;
    return Device;
}

static void TestSizes(ID3D11Device *Device)
{
    static const struct { D3D11_FEATURE Feature; UINT Size; } Sizes[] =
    {
        { D3D11_FEATURE_D3D11_OPTIONS4, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS4) },
        { D3D11_FEATURE_SHADER_CACHE, sizeof(D3D11_FEATURE_DATA_SHADER_CACHE) },
        { D3D11_FEATURE_D3D11_OPTIONS5, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS5) },
        { D3D11_FEATURE_DISPLAYABLE, sizeof(D3D11_FEATURE_DATA_DISPLAYABLE) },
    };
    BYTE Data[64];
    HRESULT Hr;

    for (UINT i = 0; i < ARRAYSIZE(Sizes); ++i)
    {
        Hr = Device->CheckFeatureSupport(Sizes[i].Feature, Data, Sizes[i].Size);
        ok(Hr == S_OK, "feature %u size %u: %#lx\n", Sizes[i].Feature, Sizes[i].Size, Hr);
        Hr = Device->CheckFeatureSupport(Sizes[i].Feature, Data, Sizes[i].Size + 4);
        ok(Hr == E_INVALIDARG, "feature %u size %u: %#lx\n", Sizes[i].Feature, Sizes[i].Size + 4, Hr);
    }
    for (UINT Feature = D3D11_FEATURE_DISPLAYABLE + 1; Feature <= D3D11_FEATURE_DISPLAYABLE + 3; ++Feature)
    {
        Hr = Device->CheckFeatureSupport(static_cast<D3D11_FEATURE>(Feature), Data, 4);
        ok(Hr == E_INVALIDARG, "feature %u: %#lx\n", Feature, Hr);
    }
}

static void TestLevel(ID3D11Device *Device, D3D_FEATURE_LEVEL Level, D3D11_TILED_RESOURCES_TIER DriverTier)
{
    D3D11_TILED_RESOURCES_TIER Cap = Level >= D3D_FEATURE_LEVEL_11_1 ? D3D11_TILED_RESOURCES_TIER_3
            : Level >= D3D_FEATURE_LEVEL_11_0 ? D3D11_TILED_RESOURCES_TIER_1 : D3D11_TILED_RESOURCES_NOT_SUPPORTED;
    D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT Shadow = {};
    D3D11_FEATURE_DATA_D3D9_OPTIONS1 D3D9Options = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options1 = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 Options2 = {};
    D3D11_FEATURE_DATA_DOUBLES Doubles = {};
    HRESULT Hr;

    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &Doubles, sizeof(Doubles));
    ok(Hr == S_OK && (Level >= D3D_FEATURE_LEVEL_11_0 || !Doubles.DoublePrecisionFloatShaderOps),
       "level %#x doubles: %#lx %d\n", Level, Hr, Doubles.DoublePrecisionFloatShaderOps);

    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &Options1, sizeof(Options1));
    ok(Hr == S_OK, "level %#x options1: %#lx\n", Level, Hr);
    ok(Options1.TiledResourcesTier <= (Cap < D3D11_TILED_RESOURCES_TIER_2 ? Cap : D3D11_TILED_RESOURCES_TIER_2),
       "level %#x options1 tiled tier %d\n", Level, Options1.TiledResourcesTier);
    ok(Options1.MinMaxFiltering == (DriverTier >= D3D11_TILED_RESOURCES_TIER_2),
       "level %#x min/max filtering %d with tier %d\n", Level, Options1.MinMaxFiltering, DriverTier);

    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &Options2, sizeof(Options2));
    ok(Hr == S_OK, "level %#x options2: %#lx\n", Level, Hr);
    ok(Options2.TiledResourcesTier <= Cap, "level %#x options2 tiled tier %d\n", Level, Options2.TiledResourcesTier);
    if (Level < D3D_FEATURE_LEVEL_11_1)
    {
        ok(!Options2.ROVsSupported && !Options2.ConservativeRasterizationTier && !Options2.PSSpecifiedStencilRefSupported,
           "level %#x ROVs %d conservative %d stencil ref %d\n", Level, Options2.ROVsSupported,
           Options2.ConservativeRasterizationTier, Options2.PSSpecifiedStencilRefSupported);
    }
    if (Level < D3D_FEATURE_LEVEL_11_0)
        ok(!Options2.TypedUAVLoadAdditionalFormats, "level %#x typed UAV loads\n", Level);

    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D9_SHADOW_SUPPORT, &Shadow, sizeof(Shadow));
    ok(Hr == S_OK && Shadow.SupportsDepthAsTextureWithLessEqualComparisonFilter, "level %#x shadow: %#lx %d\n",
       Level, Hr, Shadow.SupportsDepthAsTextureWithLessEqualComparisonFilter);
    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D9_OPTIONS1, &D3D9Options, sizeof(D3D9Options));
    ok(Hr == S_OK && D3D9Options.FullNonPow2TextureSupported && D3D9Options.DepthAsTextureWithLessEqualComparisonFilterSupported &&
       D3D9Options.SimpleInstancingSupported && D3D9Options.TextureCubeFaceRenderTargetWithNonCubeDepthStencilSupported,
       "level %#x D3D9 options1: %#lx %d %d %d %d\n", Level, Hr, D3D9Options.FullNonPow2TextureSupported,
       D3D9Options.DepthAsTextureWithLessEqualComparisonFilterSupported, D3D9Options.SimpleInstancingSupported,
       D3D9Options.TextureCubeFaceRenderTargetWithNonCubeDepthStencilSupported);
}

START_TEST(feature_support)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 Options2 = {};
    D3D11_TILED_RESOURCES_TIER DriverTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    ID3D11Device *Device;

    if (!Create)
    {
        skip("d3d11.dll is unavailable\n");
        return;
    }
    /* A feature level 11_1 device reports the driver's tiled resources tier unclamped. */
    Device = CreateAtLevel(Create, D3D_FEATURE_LEVEL_11_1);
    if (!Device)
    {
        skip("no feature level 11_1 device\n");
        FreeLibrary(Module);
        return;
    }
    if (SUCCEEDED(Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &Options2, sizeof(Options2))))
        DriverTier = Options2.TiledResourcesTier;
    TestSizes(Device);
    Device->Release();
    for (UINT i = 0; i < ARRAYSIZE(Levels); ++i)
    {
        Device = CreateAtLevel(Create, Levels[i]);
        if (!Device)
        {
            skip("no device at feature level %#x\n", Levels[i]);
            continue;
        }
        TestLevel(Device, Levels[i], DriverTier);
        Device->Release();
    }
    FreeLibrary(Module);
}
