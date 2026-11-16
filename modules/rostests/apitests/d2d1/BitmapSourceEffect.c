/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the built-in bitmap source effect's creation, inputs and property defaults
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <wincodec.h>

static ID2D1DeviceContext *
CreateContext(ID2D1Factory1 **Factory)
{
    ID2D1DeviceContext *Context = NULL;
    ID3D11Device *D3dDevice = NULL;
    ID2D1Device *Device = NULL;
    IDXGIDevice *Dxgi = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &D3dDevice, NULL, NULL);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &D3dDevice, NULL, NULL);
    if (FAILED(hr))
    {
        skip("No Direct3D 11 device, hr 0x%lx\n", hr);
        return NULL;
    }
    hr = ID3D11Device_QueryInterface(D3dDevice, &IID_IDXGIDevice, (void **)&Dxgi);
    if (SUCCEEDED(hr))
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)Factory);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory1_CreateDevice(*Factory, Dxgi, &Device);
    if (SUCCEEDED(hr))
        hr = ID2D1Device_CreateDeviceContext(Device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &Context);
    ok_hr(hr, S_OK);
    if (Device)
        ID2D1Device_Release(Device);
    if (Dxgi)
        IDXGIDevice_Release(Dxgi);
    ID3D11Device_Release(D3dDevice);
    return Context;
}

static UINT32
GetUInt(ID2D1Effect *Effect, UINT32 Index, D2D1_PROPERTY_TYPE Type)
{
    UINT32 Value = 0xdeadbeef;
    HRESULT hr = ID2D1Effect_GetValue(Effect, Index, Type, (BYTE *)&Value, sizeof(Value));

    ok_hr(hr, S_OK);
    return Value;
}

START_TEST(BitmapSourceEffect)
{
    IWICImagingFactory *Wic = NULL;
    ID2D1Factory1 *Factory = NULL;
    ID2D1DeviceContext *Context;
    IWICBitmap *Bitmap = NULL;
    ID2D1Effect *Effect = NULL;
    D2D1_VECTOR_2F Scale = { 0.0f, 0.0f };
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Context = CreateContext(&Factory);
    if (!Context)
    {
        CoUninitialize();
        return;
    }

    hr = ID2D1DeviceContext_CreateEffect(Context, &CLSID_D2D1BitmapSource, &Effect);
    ok_hr(hr, S_OK);
    if (Effect)
    {
        ok_int(ID2D1Effect_GetInputCount(Effect), 0);
        ok_int(ID2D1Effect_GetPropertyCount(Effect), 6);

        hr = ID2D1Effect_GetValue(Effect, D2D1_BITMAPSOURCE_PROP_SCALE, D2D1_PROPERTY_TYPE_VECTOR2,
                                  (BYTE *)&Scale, sizeof(Scale));
        ok_hr(hr, S_OK);
        ok(Scale.x == 1.0f && Scale.y == 1.0f, "scale (%f, %f)\n", Scale.x, Scale.y);
        ok_int(GetUInt(Effect, D2D1_BITMAPSOURCE_PROP_INTERPOLATION_MODE, D2D1_PROPERTY_TYPE_ENUM),
               D2D1_BITMAPSOURCE_INTERPOLATION_MODE_LINEAR);
        ok_int(GetUInt(Effect, D2D1_BITMAPSOURCE_PROP_ENABLE_DPI_CORRECTION, D2D1_PROPERTY_TYPE_BOOL), FALSE);
        ok_int(GetUInt(Effect, D2D1_BITMAPSOURCE_PROP_ALPHA_MODE, D2D1_PROPERTY_TYPE_ENUM),
               D2D1_BITMAPSOURCE_ALPHA_MODE_PREMULTIPLIED);
        ok_int(GetUInt(Effect, D2D1_BITMAPSOURCE_PROP_ORIENTATION, D2D1_PROPERTY_TYPE_ENUM),
               D2D1_BITMAPSOURCE_ORIENTATION_DEFAULT);

        hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                              (void **)&Wic);
        if (SUCCEEDED(hr))
            hr = IWICImagingFactory_CreateBitmap(Wic, 4, 4, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                                 &Bitmap);
        ok_hr(hr, S_OK);
        if (Bitmap)
        {
            IUnknown *Source = (IUnknown *)Bitmap;

            hr = ID2D1Effect_SetValue(Effect, D2D1_BITMAPSOURCE_PROP_WIC_BITMAP_SOURCE, D2D1_PROPERTY_TYPE_IUNKNOWN,
                                      (const BYTE *)&Source, sizeof(Source));
            ok_hr(hr, S_OK);
            IWICBitmap_Release(Bitmap);
        }
        if (Wic)
            IWICImagingFactory_Release(Wic);
        ID2D1Effect_Release(Effect);
    }

    ID2D1DeviceContext_Release(Context);
    ID2D1Factory1_Release(Factory);
    CoUninitialize();
}
