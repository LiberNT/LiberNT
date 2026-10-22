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
    TestOverlaySupport(Device);
    TestTrim(Device);

    Device->Release();
    FreeLibrary(Module);
}
