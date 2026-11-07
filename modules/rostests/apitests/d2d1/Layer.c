/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests what ID2D1DeviceContext::PushLayer and PopLayer draw on a bitmap target
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <float.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1_1.h>

#define SIZE 64

typedef struct _PIXEL_CHECK
{
    UINT x;
    UINT y;
    BYTE gray;
} PIXEL_CHECK;

typedef struct _LAYER_TEST
{
    ID2D1Factory1 *Factory;
    ID2D1DeviceContext *Context;
    ID2D1Bitmap1 *Target;
    ID2D1Bitmap1 *Staging;
    ID2D1SolidColorBrush *Black;
    ID2D1BitmapBrush1 *MaskBrush;
} LAYER_TEST;

static const D2D1_MATRIX_3X2_F Identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};

static D2D1_MATRIX_3X2_F Translation(float x, float y)
{
    D2D1_MATRIX_3X2_F matrix = Identity;

    matrix._31 = x;
    matrix._32 = y;
    return matrix;
}

static D2D1_LAYER_PARAMETERS1 DefaultLayer(void)
{
    D2D1_LAYER_PARAMETERS1 parameters;

    ZeroMemory(&parameters, sizeof(parameters));
    parameters.contentBounds.left = -FLT_MAX;
    parameters.contentBounds.top = -FLT_MAX;
    parameters.contentBounds.right = FLT_MAX;
    parameters.contentBounds.bottom = FLT_MAX;
    parameters.maskAntialiasMode = D2D1_ANTIALIAS_MODE_ALIASED;
    parameters.maskTransform = Identity;
    parameters.opacity = 1.0f;
    return parameters;
}

static ID2D1Geometry *CreateCircle(LAYER_TEST *Test, float x, float y, float radius)
{
    ID2D1EllipseGeometry *ellipse = NULL;
    D2D1_ELLIPSE desc;
    HRESULT hr;

    desc.point.x = x;
    desc.point.y = y;
    desc.radiusX = desc.radiusY = radius;
    hr = ID2D1Factory1_CreateEllipseGeometry(Test->Factory, &desc, &ellipse);
    ok_hr(hr, S_OK);
    return (ID2D1Geometry *)ellipse;
}

static void FillBlack(LAYER_TEST *Test, float left, float top, float right, float bottom)
{
    D2D1_RECT_F rect = {left, top, right, bottom};

    ID2D1DeviceContext_FillRectangle(Test->Context, &rect, (ID2D1Brush *)Test->Black);
}

static void BeginFrame(LAYER_TEST *Test, float Dpi)
{
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};

    ID2D1DeviceContext_SetTarget(Test->Context, (ID2D1Image *)Test->Target);
    ID2D1DeviceContext_SetDpi(Test->Context, Dpi, Dpi);
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
    ID2D1DeviceContext_BeginDraw(Test->Context);
    ID2D1DeviceContext_Clear(Test->Context, &white);
}

static void EndFrame(LAYER_TEST *Test, const char *Name, HRESULT Expected, const PIXEL_CHECK *Checks, UINT Count)
{
    D2D1_MAPPED_RECT mapped;
    const BYTE *pixel;
    HRESULT hr;
    UINT i;

    hr = ID2D1DeviceContext_EndDraw(Test->Context, NULL, NULL);
    ok(hr == Expected, "%s: EndDraw returned 0x%lx, expected 0x%lx\n", Name, hr, Expected);
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
    ID2D1DeviceContext_SetDpi(Test->Context, 96.0f, 96.0f);

    hr = ID2D1Bitmap1_CopyFromBitmap(Test->Staging, NULL, (ID2D1Bitmap *)Test->Target, NULL);
    ok_hr(hr, S_OK);
    hr = ID2D1Bitmap1_Map(Test->Staging, D2D1_MAP_OPTIONS_READ, &mapped);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    for (i = 0; i < Count; i++)
    {
        pixel = mapped.bits + Checks[i].y * mapped.pitch + Checks[i].x * 4;
        ok(abs(pixel[0] - Checks[i].gray) <= 1 && abs(pixel[1] - Checks[i].gray) <= 1 &&
           abs(pixel[2] - Checks[i].gray) <= 1 && pixel[3] == 255,
           "%s: pixel (%u,%u) is %u,%u,%u,%u, expected gray %u\n", Name, Checks[i].x, Checks[i].y,
           pixel[0], pixel[1], pixel[2], pixel[3], Checks[i].gray);
    }

    ID2D1Bitmap1_Unmap(Test->Staging);
}

static void TestOpacityBrush(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{2, 2, 255}, {32, 32, 0}, {61, 61, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.opacityBrush = (ID2D1Brush *)Test->MaskBrush;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "opacity brush", S_OK, checks, ARRAYSIZE(checks));
}

static void TestGeometricMask(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{2, 2, 255}, {32, 32, 0}, {61, 61, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.geometricMask = CreateCircle(Test, 32, 32, 24);
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "geometric mask", S_OK, checks, ARRAYSIZE(checks));
    ID2D1Geometry_Release(parameters.geometricMask);
}

static void TestGroupOpacity(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{4, 32, 128}, {32, 32, 128}, {60, 32, 128}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.opacity = 0.5f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, 40, SIZE);
    FillBlack(Test, 24, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "group opacity", S_OK, checks, ARRAYSIZE(checks));
}

static void TestContentBounds(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{2, 2, 255}, {20, 20, 0}, {32, 32, 0}, {50, 50, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.contentBounds.left = parameters.contentBounds.top = 16.0f;
    parameters.contentBounds.right = parameters.contentBounds.bottom = 48.0f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "content bounds", S_OK, checks, ARRAYSIZE(checks));
}

static void TestTransforms(LAYER_TEST *Test)
{
    static const PIXEL_CHECK world_checks[] = {{2, 2, 255}, {16, 16, 255}, {32, 32, 0}, {42, 32, 0}};
    static const PIXEL_CHECK mask_checks[] = {{2, 2, 255}, {6, 6, 255}, {32, 32, 0}, {42, 32, 0}};
    static const PIXEL_CHECK pop_checks[] = {{2, 2, 255}, {32, 32, 0}, {52, 32, 255}};
    static const PIXEL_CHECK brush_checks[] = {{12, 12, 255}, {32, 32, 0}, {48, 48, 0}, {61, 61, 0}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();
    D2D1_MATRIX_3X2_F matrix;

    parameters.geometricMask = CreateCircle(Test, 16, 16, 12);
    BeginFrame(Test, 96.0f);
    matrix = Translation(16, 16);
    ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, -16, -16, 48, 48);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "world transform at push", S_OK, world_checks, ARRAYSIZE(world_checks));
    ID2D1Geometry_Release(parameters.geometricMask);

    parameters.geometricMask = CreateCircle(Test, 0, 0, 12);
    parameters.maskTransform = Translation(32, 32);
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "mask transform", S_OK, mask_checks, ARRAYSIZE(mask_checks));
    ID2D1Geometry_Release(parameters.geometricMask);

    parameters.geometricMask = CreateCircle(Test, 32, 32, 12);
    parameters.maskTransform = Identity;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    matrix = Translation(20, 0);
    ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "world transform changed before pop", S_OK, pop_checks, ARRAYSIZE(pop_checks));
    ID2D1Geometry_Release(parameters.geometricMask);

    parameters = DefaultLayer();
    parameters.opacityBrush = (ID2D1Brush *)Test->MaskBrush;
    BeginFrame(Test, 96.0f);
    matrix = Translation(16, 16);
    ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, -16, -16, 48, 48);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "opacity brush under world transform", S_OK, brush_checks, ARRAYSIZE(brush_checks));
}

static void TestNesting(LAYER_TEST *Test)
{
    static const PIXEL_CHECK opacity_checks[] = {{2, 2, 192}, {32, 32, 192}, {61, 61, 192}};
    static const PIXEL_CHECK mask_checks[] = {{2, 2, 255}, {32, 32, 128}, {61, 61, 255}};
    D2D1_LAYER_PARAMETERS1 outer = DefaultLayer(), inner = DefaultLayer();

    inner.opacity = 0.5f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &inner, NULL);
    ID2D1DeviceContext_PushLayer(Test->Context, &inner, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "nested opacity", S_OK, opacity_checks, ARRAYSIZE(opacity_checks));

    outer.geometricMask = CreateCircle(Test, 32, 32, 24);
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &outer, NULL);
    ID2D1DeviceContext_PushLayer(Test->Context, &inner, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "nested mask and opacity", S_OK, mask_checks, ARRAYSIZE(mask_checks));
    ID2D1Geometry_Release(outer.geometricMask);
}

static void TestDpi(LAYER_TEST *Test)
{
    static const PIXEL_CHECK mask_checks[] = {{2, 2, 255}, {32, 32, 0}, {61, 61, 255}};
    static const PIXEL_CHECK bounds_checks[] = {{2, 2, 128}, {30, 30, 128}, {34, 34, 255}, {61, 61, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.geometricMask = CreateCircle(Test, 64.0f / 3.0f, 64.0f / 3.0f, 16);
    BeginFrame(Test, 144.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "mask at 144 dpi", S_OK, mask_checks, ARRAYSIZE(mask_checks));
    ID2D1Geometry_Release(parameters.geometricMask);

    parameters = DefaultLayer();
    parameters.opacity = 0.5f;
    parameters.contentBounds.left = parameters.contentBounds.top = 0.0f;
    parameters.contentBounds.right = parameters.contentBounds.bottom = 64.0f / 3.0f;
    BeginFrame(Test, 144.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "content bounds at 144 dpi", S_OK, bounds_checks, ARRAYSIZE(bounds_checks));
}

static void TestRenderTargetInterface(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{2, 2, 128}, {32, 32, 128}, {61, 61, 128}};
    D2D1_LAYER_PARAMETERS parameters;
    D2D1_LAYER_PARAMETERS1 defaults = DefaultLayer();
    ID2D1Layer *layer = NULL;
    HRESULT hr;

    memcpy(&parameters, &defaults, sizeof(parameters));
    parameters.opacity = 0.5f;
    hr = ID2D1DeviceContext_CreateLayer(Test->Context, NULL, &layer);
    ok_hr(hr, S_OK);
    BeginFrame(Test, 96.0f);
    Test->Context->lpVtbl->PushLayer(Test->Context, &parameters, layer);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "ID2D1RenderTarget::PushLayer", S_OK, checks, ARRAYSIZE(checks));
    if (layer)
        ID2D1Layer_Release(layer);
}

static void TestClearAndClip(LAYER_TEST *Test)
{
    static const PIXEL_CHECK clear_checks[] = {{2, 2, 128}, {32, 32, 128}, {61, 61, 128}};
    static const PIXEL_CHECK clip_checks[] = {{2, 2, 255}, {20, 20, 128}, {32, 32, 128}, {50, 50, 255}};
    static const PIXEL_CHECK juce_checks[] = {{2, 2, 255}, {12, 32, 255}, {32, 32, 0}, {50, 50, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_RECT_F clip;

    parameters.opacity = 0.5f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    ID2D1DeviceContext_Clear(Test->Context, &black);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "Clear inside a layer", S_OK, clear_checks, ARRAYSIZE(clear_checks));

    clip.left = clip.top = 16.0f;
    clip.right = clip.bottom = 48.0f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushAxisAlignedClip(Test->Context, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    ID2D1DeviceContext_PopAxisAlignedClip(Test->Context);
    EndFrame(Test, "layer inside an axis aligned clip", S_OK, clip_checks, ARRAYSIZE(clip_checks));

    parameters = DefaultLayer();
    parameters.opacityBrush = (ID2D1Brush *)Test->MaskBrush;
    clip.right = clip.bottom = 64.0f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushAxisAlignedClip(Test->Context, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    ID2D1DeviceContext_PopAxisAlignedClip(Test->Context);
    EndFrame(Test, "opacity brush inside an axis aligned clip", S_OK, juce_checks, ARRAYSIZE(juce_checks));
}

static void TestSolidOpacityBrush(LAYER_TEST *Test)
{
    static const PIXEL_CHECK checks[] = {{2, 2, 128}, {32, 32, 128}, {61, 61, 128}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();
    D2D1_COLOR_F half = {0.0f, 0.0f, 0.0f, 0.5f};
    ID2D1SolidColorBrush *brush = NULL;
    HRESULT hr;

    hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &half, NULL, &brush);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    parameters.opacityBrush = (ID2D1Brush *)brush;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    ID2D1DeviceContext_PopLayer(Test->Context);
    EndFrame(Test, "solid opacity brush", S_OK, checks, ARRAYSIZE(checks));
    ID2D1SolidColorBrush_Release(brush);
}

static void TestUnbalanced(LAYER_TEST *Test)
{
    static const PIXEL_CHECK black_checks[] = {{2, 2, 0}, {32, 32, 0}, {61, 61, 0}};
    static const PIXEL_CHECK white_checks[] = {{2, 2, 255}, {32, 32, 255}, {61, 61, 255}};
    D2D1_LAYER_PARAMETERS1 parameters = DefaultLayer();

    parameters.opacity = 0.5f;
    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    EndFrame(Test, "EndDraw with a pushed layer", D2DERR_PUSH_POP_UNBALANCED, black_checks, ARRAYSIZE(black_checks));

    BeginFrame(Test, 96.0f);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    EndFrame(Test, "frame after an unbalanced frame", S_OK, black_checks, ARRAYSIZE(black_checks));

    BeginFrame(Test, 96.0f);
    ID2D1DeviceContext_PopLayer(Test->Context);
    FillBlack(Test, 0, 0, SIZE, SIZE);
    EndFrame(Test, "PopLayer without PushLayer", D2DERR_PUSH_POP_UNBALANCED, white_checks, ARRAYSIZE(white_checks));
}

static BOOL CreateTest(LAYER_TEST *Test, ID3D11Device *Device)
{
    D2D1_BITMAP_PROPERTIES1 target_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                            D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_BITMAP_PROPERTIES1 staging_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                             D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    D2D1_BITMAP_PROPERTIES1 mask_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                          D2D1_BITMAP_OPTIONS_NONE, NULL};
    D2D1_BITMAP_BRUSH_PROPERTIES1 bitmap_brush_props = {D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                                        D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR};
    D2D1_BRUSH_PROPERTIES brush_props = {1.0f, {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}}};
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_SIZE_U size = {SIZE, SIZE};
    ID2D1Bitmap1 *mask = NULL;
    ID2D1Device *device = NULL;
    IDXGIDevice *dxgi = NULL;
    BYTE *mask_data;
    HRESULT hr;
    int x, y;

    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&dxgi);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&Test->Factory);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory1_CreateDevice(Test->Factory, dxgi, &device);
    if (SUCCEEDED(hr))
        hr = ID2D1Device_CreateDeviceContext(device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &Test->Context);
    ok_hr(hr, S_OK);
    if (device)
        ID2D1Device_Release(device);
    IDXGIDevice_Release(dxgi);
    if (FAILED(hr))
        return FALSE;

    mask_data = HeapAlloc(GetProcessHeap(), 0, SIZE * SIZE * 4);
    if (!mask_data)
        return FALSE;
    for (y = 0; y < SIZE; y++)
    {
        for (x = 0; x < SIZE; x++)
        {
            BYTE alpha = ((x - 32) * (x - 32) + (y - 32) * (y - 32) <= 24 * 24) ? 255 : 0;
            memset(&mask_data[(y * SIZE + x) * 4], alpha, 4);
        }
    }

    hr = ID2D1DeviceContext_CreateBitmap(Test->Context, size, NULL, 0, &target_props, &Test->Target);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateBitmap(Test->Context, size, NULL, 0, &staging_props, &Test->Staging);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateBitmap(Test->Context, size, mask_data, SIZE * 4, &mask_props, &mask);
    ok_hr(hr, S_OK);
    HeapFree(GetProcessHeap(), 0, mask_data);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &black, NULL, &Test->Black);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateBitmapBrush(Test->Context, (ID2D1Bitmap *)mask, &bitmap_brush_props,
                                                  &brush_props, &Test->MaskBrush);
    ok_hr(hr, S_OK);
    if (mask)
        ID2D1Bitmap1_Release(mask);
    return SUCCEEDED(hr);
}

static void DestroyTest(LAYER_TEST *Test)
{
    if (Test->MaskBrush)
        ID2D1BitmapBrush1_Release(Test->MaskBrush);
    if (Test->Black)
        ID2D1SolidColorBrush_Release(Test->Black);
    if (Test->Staging)
        ID2D1Bitmap1_Release(Test->Staging);
    if (Test->Target)
        ID2D1Bitmap1_Release(Test->Target);
    if (Test->Context)
        ID2D1DeviceContext_Release(Test->Context);
    if (Test->Factory)
        ID2D1Factory1_Release(Test->Factory);
}

START_TEST(Layer)
{
    LAYER_TEST test;
    ID3D11Device *device = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr))
    {
        skip("No Direct3D 11 device, hr 0x%lx\n", hr);
        return;
    }

    ZeroMemory(&test, sizeof(test));
    if (CreateTest(&test, device))
    {
        TestOpacityBrush(&test);
        TestGeometricMask(&test);
        TestGroupOpacity(&test);
        TestContentBounds(&test);
        TestTransforms(&test);
        TestNesting(&test);
        TestDpi(&test);
        TestRenderTargetInterface(&test);
        TestClearAndClip(&test);
        TestSolidOpacityBrush(&test);
        TestUnbalanced(&test);
    }
    DestroyTest(&test);
    ID3D11Device_Release(device);
}
