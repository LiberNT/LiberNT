/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D11DeviceContext1::ClearView with rectangles on a hardware device
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11_1.h>
#include <wine/test.h>

#define SIZE 16

static DWORD ReadPixel(ID3D11DeviceContext1 *Context, ID3D11Texture2D *Staging, UINT X, UINT Y)
{
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD value = 0xdeadbeef;

    if (ID3D11DeviceContext1_Map(Context, (ID3D11Resource *)Staging, 0, D3D11_MAP_READ, 0, &map) == S_OK)
    {
        value = ((const DWORD *)((const BYTE *)map.pData + Y * map.RowPitch))[X];
        ID3D11DeviceContext1_Unmap(Context, (ID3D11Resource *)Staging, 0);
    }
    return value;
}

static void TestClearView(ID3D11Device *device, ID3D11DeviceContext1 *context1, ID3D11Texture2D *target,
                          ID3D11Texture2D *staging)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const FLOAT red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    D3D11_RECT rect = { 4, 4, 12, 12 };
    ID3D11RenderTargetView *view = NULL;
    HRESULT hr;

    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &view);
    ok(hr == S_OK, "render target view: 0x%08lx\n", hr);
    if (view == NULL)
        return;

    ID3D11DeviceContext1_ClearRenderTargetView(context1, view, black);
    ID3D11DeviceContext1_ClearView(context1, (ID3D11View *)view, red, &rect, 1);
    ID3D11DeviceContext1_CopyResource(context1, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    ok(ReadPixel(context1, staging, 8, 8) == 0xffff0000, "inside the rectangle: 0x%08lx\n", ReadPixel(context1, staging, 8, 8));
    ok(ReadPixel(context1, staging, 4, 4) == 0xffff0000, "first pixel of the rectangle: 0x%08lx\n", ReadPixel(context1, staging, 4, 4));
    ok(ReadPixel(context1, staging, 3, 3) == 0xff000000, "left of the rectangle: 0x%08lx\n", ReadPixel(context1, staging, 3, 3));
    ok(ReadPixel(context1, staging, 12, 12) == 0xff000000, "right edge is exclusive: 0x%08lx\n", ReadPixel(context1, staging, 12, 12));

    ID3D11DeviceContext1_ClearView(context1, (ID3D11View *)view, black, NULL, 0);
    ID3D11DeviceContext1_CopyResource(context1, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    ok(ReadPixel(context1, staging, 8, 8) == 0xff000000, "no rectangle clears the whole view: 0x%08lx\n", ReadPixel(context1, staging, 8, 8));
    ID3D11RenderTargetView_Release(view);
}

START_TEST(clear_view)
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {0};
    D3D11_TEXTURE2D_DESC desc = {0};
    ID3D11Texture2D *target = NULL, *staging = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11DeviceContext1 *context1 = NULL;
    ID3D11Device *device = NULL;
    D3D_FEATURE_LEVEL level = 0;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                           &device, &level, &context);
    if (FAILED(hr))
    {
        skip("no hardware feature level 11_0 device: 0x%08lx\n", hr);
        return;
    }
    hr = ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11DeviceContext1, (void **)&context1);
    ok(hr == S_OK, "ID3D11DeviceContext1: 0x%08lx\n", hr);
    hr = ID3D11Device_CheckFeatureSupport(device, D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "D3D11_FEATURE_D3D11_OPTIONS: 0x%08lx\n", hr);
    /* The Direct3D 11.1 options are mandatory at feature level 11_1 */
    if (level >= D3D_FEATURE_LEVEL_11_1)
        ok(options.ClearView, "a feature level 11_1 device does not report ClearView\n");

    desc.Width = SIZE;
    desc.Height = SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &target);
    ok(hr == S_OK, "render target: 0x%08lx\n", hr);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    ok(hr == S_OK, "staging texture: 0x%08lx\n", hr);

    if (context1 == NULL || !options.ClearView)
        skip("the device does not offer ClearView\n");
    else if (target != NULL && staging != NULL)
        TestClearView(device, context1, target, staging);

    if (staging) ID3D11Texture2D_Release(staging);
    if (target) ID3D11Texture2D_Release(target);
    if (context1) ID3D11DeviceContext1_Release(context1);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
}
