/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests D3D11 format support flags and DXGI overlay support queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_3.h>

static UINT QuerySupport(ID3D11Device *Device, DXGI_FORMAT Format, HRESULT *Result)
{
    UINT Support = 0;

    *Result = Device->CheckFormatSupport(Format, &Support);
    return Support;
}

static void TestFormatSupport(ID3D11Device *Device)
{
    D3D_FEATURE_LEVEL Level = Device->GetFeatureLevel();
    HRESULT Hr;
    UINT Support;

    Support = QuerySupport(Device, DXGI_FORMAT_R8G8B8A8_UNORM, &Hr);
    ok(Hr == S_OK, "R8G8B8A8_UNORM: %#lx\n", Hr);
    ok(Support & D3D11_FORMAT_SUPPORT_TEXTURE2D, "R8G8B8A8_UNORM support %#x lacks TEXTURE2D\n", Support);
    ok(Support & D3D11_FORMAT_SUPPORT_CPU_LOCKABLE, "R8G8B8A8_UNORM support %#x lacks CPU_LOCKABLE\n", Support);
    if (Level >= D3D_FEATURE_LEVEL_10_0)
        ok(Support & D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT,
           "R8G8B8A8_UNORM support %#x lacks CAST_WITHIN_BIT_LAYOUT\n", Support);
    if (Support & D3D11_FORMAT_SUPPORT_DISPLAY)
        ok(Support & D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST,
           "R8G8B8A8_UNORM support %#x lacks BACK_BUFFER_CAST\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_B8G8R8X8_UNORM, &Hr);
    if (Hr == S_OK)
        ok(!(Support & D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST),
           "B8G8R8X8_UNORM support %#x has BACK_BUFFER_CAST\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_R32_FLOAT, &Hr);
    ok(Hr == S_OK, "R32_FLOAT: %#lx\n", Hr);
    if (Level >= D3D_FEATURE_LEVEL_10_0 && (Support & D3D11_FORMAT_SUPPORT_BUFFER))
        ok(Support & D3D11_FORMAT_SUPPORT_SO_BUFFER, "R32_FLOAT support %#x lacks SO_BUFFER\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_R16G16B16A16_FLOAT, &Hr);
    if (Hr == S_OK)
        ok(!(Support & D3D11_FORMAT_SUPPORT_SO_BUFFER), "R16G16B16A16_FLOAT support %#x has SO_BUFFER\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_R11G11B10_FLOAT, &Hr);
    if (Hr == S_OK)
        ok(!(Support & D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT),
           "R11G11B10_FLOAT support %#x has CAST_WITHIN_BIT_LAYOUT\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_A8_UNORM, &Hr);
    if (Hr == S_OK)
        ok(!(Support & D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT),
           "A8_UNORM support %#x has CAST_WITHIN_BIT_LAYOUT\n", Support);

    Support = QuerySupport(Device, DXGI_FORMAT_P010, &Hr);
    ok(Hr == S_OK || Hr == E_FAIL, "P010: %#lx\n", Hr);
}

static void TestRequiredSupport(ID3D11Device *Device)
{
    static const struct
    {
        DXGI_FORMAT Format;
        UINT Required[3];
    } Tests[] =
    {
        {DXGI_FORMAT_R32G32B32A32_TYPELESS, {0x001210f0, 0x001210f0, 0x001210f0}},
        {DXGI_FORMAT_R32G32B32A32_FLOAT, {0x0016f1fb, 0x0056f3fb, 0x02f6f3fb}},
        {DXGI_FORMAT_R11G11B10_FLOAT, {0x0006f3f3, 0x0066f3f3, 0x02e6f3f3}},
        {DXGI_FORMAT_R8G8B8A8_UNORM, {0x101ef3f3, 0x107ef3f3, 0x12fef3f3}},
        {DXGI_FORMAT_R32_FLOAT, {0x0016f5fb, 0x00f6f7fb, 0x06f6f7fb}},
        {DXGI_FORMAT_D24_UNORM_S8_UINT, {0x001310b0, 0x003310b0, 0x003310b0}},
        {DXGI_FORMAT_BC7_UNORM, {0, 0, 0x009213e0}},
        {DXGI_FORMAT_NV12, {0x3802c320, 0x3882c320, 0x3a82c320}},
    };
    D3D_FEATURE_LEVEL Level = Device->GetFeatureLevel();
    UINT Index = Level >= D3D_FEATURE_LEVEL_11_0 ? 2 : Level >= D3D_FEATURE_LEVEL_10_1 ? 1 : 0;

    if (Level < D3D_FEATURE_LEVEL_10_0)
    {
        skip("Feature level %#x has no D3D10+ format requirements\n", Level);
        return;
    }
    for (UINT i = 0; i < ARRAYSIZE(Tests); ++i)
    {
        UINT Support = 0;
        HRESULT Hr = Device->CheckFormatSupport(Tests[i].Format, &Support);

        if (!Tests[i].Required[Index])
            continue;
        ok(Hr == S_OK, "level %#x format %u: %#lx\n", Level, Tests[i].Format, Hr);
        ok((Support & Tests[i].Required[Index]) == Tests[i].Required[Index],
           "level %#x format %u support %#x lacks required %#x\n",
           Level, Tests[i].Format, Support, Tests[i].Required[Index] & ~Support);
    }
}

static void TestListedSupport(ID3D11Device *Device)
{
    static const struct
    {
        DXGI_FORMAT Format;
        UINT Disallowed;
    } Tests[] =
    {
        {DXGI_FORMAT_R32G32B32A32_TYPELESS, D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_LOAD
            | D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET | D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD},
        {DXGI_FORMAT_B8G8R8A8_TYPELESS, D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_BLENDABLE
            | D3D11_FORMAT_SUPPORT_SHADER_LOAD},
        {DXGI_FORMAT_D32_FLOAT, D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_LOAD},
        {DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_LOAD},
        {DXGI_FORMAT_NV12, D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_MIP},
    };
    const UINT MipGeneration = D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_MIP;
    D3D_FEATURE_LEVEL Level = Device->GetFeatureLevel();
    UINT Support;
    HRESULT Hr;

    if (Level < D3D_FEATURE_LEVEL_10_0)
    {
        skip("Feature level %#x has no D3D10+ format list\n", Level);
        return;
    }
    for (UINT i = 0; i < ARRAYSIZE(Tests); ++i)
    {
        Support = QuerySupport(Device, Tests[i].Format, &Hr);
        ok(!(Support & Tests[i].Disallowed), "level %#x format %u support %#x has disallowed %#x\n",
           Level, Tests[i].Format, Support, Support & Tests[i].Disallowed);
    }
    Support = QuerySupport(Device, DXGI_FORMAT_R1_UNORM, &Hr);
    ok(Hr == E_FAIL && !Support, "level %#x R1_UNORM: %#lx, support %#x\n", Level, Hr, Support);
    for (UINT Format = 1; Format <= DXGI_FORMAT_B4G4R4A4_UNORM; ++Format)
    {
        Support = QuerySupport(Device, static_cast<DXGI_FORMAT>(Format), &Hr);
        ok(!(Support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) == ((Support & MipGeneration) != MipGeneration),
           "level %#x format %u support %#x: MIP_AUTOGEN does not follow render target, sample and mip\n",
           Level, Format, Support);
    }
}

static void TestOverlaySupport(ID3D11Device *Device)
{
    IDXGIDevice *DxgiDevice = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIOutput *Output = NULL;
    IDXGIOutput3 *Output3 = NULL;
    UINT Flags = 0xdead;
    HRESULT Hr;

    if (FAILED(Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice))))
    {
        skip("No IDXGIDevice\n");
        return;
    }
    if (SUCCEEDED(DxgiDevice->GetAdapter(&Adapter)) && SUCCEEDED(Adapter->EnumOutputs(0, &Output)) &&
        SUCCEEDED(Output->QueryInterface(__uuidof(IDXGIOutput3), reinterpret_cast<void **>(&Output3))))
    {
        Hr = Output3->CheckOverlaySupport(DXGI_FORMAT_B8G8R8A8_UNORM, Device, &Flags);
        ok(Hr == S_OK, "CheckOverlaySupport: %#lx\n", Hr);
        ok(Flags != 0xdead, "CheckOverlaySupport did not write the flags\n");
        Output3->Release();
    }
    else
    {
        skip("No IDXGIOutput3 on output 0\n");
    }
    if (Output) Output->Release();
    if (Adapter) Adapter->Release();
    DxgiDevice->Release();
}

static void TestTrim(ID3D11Device *Device)
{
    ID3D11DeviceContext *Context = NULL;
    IDXGIDevice3 *DxgiDevice = NULL;
    ID3D11Texture2D *Texture = NULL;
    D3D11_TEXTURE2D_DESC Desc = {};
    HRESULT Hr;

    if (FAILED(Device->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void **>(&DxgiDevice))))
    {
        skip("No IDXGIDevice3\n");
        return;
    }
    Device->GetImmediateContext(&Context);
    Context->ClearState();
    DxgiDevice->Trim();

    Desc.Width = Desc.Height = 16;
    Desc.MipLevels = Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
    ok(Hr == S_OK, "CreateTexture2D after Trim: %#lx\n", Hr);
    if (Texture) Texture->Release();
    Context->Release();
    DxgiDevice->Release();
}

START_TEST(format_support)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice is unavailable\n"); FreeLibrary(Module); return; }
    ID3D11Device *Device = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, NULL);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Module); return; }

    TestFormatSupport(Device);
    TestRequiredSupport(Device);
    TestListedSupport(Device);
    TestOverlaySupport(Device);
    TestTrim(Device);

    Device->Release();
    static const D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_10_1};
    for (UINT i = 0; i < ARRAYSIZE(Levels); ++i)
    {
        Device = NULL;
        Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Levels[i], 1, D3D11_SDK_VERSION, &Device, NULL, NULL);
        ok(Hr == S_OK, "D3D11CreateDevice level %#x: %#lx\n", Levels[i], Hr);
        if (FAILED(Hr)) continue;
        TestRequiredSupport(Device);
        TestListedSupport(Device);
        Device->Release();
    }
    FreeLibrary(Module);
}
