/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests what ID2D1DeviceContext::SetPrimitiveBlend does to the pixels a primitive covers
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1_1.h>

#define SIZE 64

typedef struct _BLEND_PIXEL
{
    UINT x;
    UINT y;
    BYTE b, g, r, a;
} BLEND_PIXEL;

typedef enum _BLEND_SHAPE
{
    SHAPE_RECT,
    SHAPE_GEOMETRY,
    SHAPE_ELLIPSE,
    SHAPE_BITMAP,
} BLEND_SHAPE;

typedef struct _BLEND_CASE
{
    const char *Name;
    D2D1_COLOR_F Base;
    D2D1_PRIMITIVE_BLEND Blend;
    D2D1_ANTIALIAS_MODE Antialias;
    D2D1_COLOR_F Brush;
    BLEND_SHAPE Shape;
    D2D1_RECT_F Rect;
    BOOL Clip;
    D2D1_RECT_F ClipRect;
    BLEND_PIXEL Checks[4];
} BLEND_CASE;

typedef struct _BLEND_TEST
{
    ID2D1Factory1 *Factory;
    ID2D1DeviceContext *Context;
    ID2D1Bitmap1 *Target;
    ID2D1Bitmap1 *Staging;
    ID2D1Bitmap1 *Clear;
} BLEND_TEST;

#define RED {1.0f, 0.0f, 0.0f, 1.0f}
#define NONE {0.0f, 0.0f, 0.0f, 0.0f}

static const BLEND_CASE Cases[] =
{
    {"source over keeps the base under a transparent brush", RED, D2D1_PRIMITIVE_BLEND_SOURCE_OVER,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 0, 0, 255, 255}, {4, 4, 0, 0, 255, 255}}},
    {"copy writes a transparent brush", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 0, 0, 0, 0}, {8, 8, 0, 0, 0, 0}, {23, 23, 0, 0, 0, 0}, {24, 16, 0, 0, 255, 255}}},
    {"copy writes a half transparent brush", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {0.0f, 0.0f, 1.0f, 0.5f}, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 128, 0, 0, 128}, {4, 4, 0, 0, 255, 255}}},
    {"copy on a fractional rectangle", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_RECT, {8.5f, 8.5f, 24.5f, 24.5f}, FALSE, {0},
     {{16, 16, 0, 0, 0, 0}, {8, 16, 0, 0, 128, 128}, {24, 16, 0, 0, 128, 128}, {4, 4, 0, 0, 255, 255}}},
    {"copy on a fractional aliased rectangle", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_ALIASED, NONE, SHAPE_RECT, {8.5f, 8.5f, 24.5f, 24.5f}, FALSE, {0},
     {{16, 16, 0, 0, 0, 0}, {9, 16, 0, 0, 0, 0}, {25, 16, 0, 0, 255, 255}, {4, 4, 0, 0, 255, 255}}},
    {"copy under an axis aligned clip", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_GEOMETRY, {0, 0, SIZE, SIZE}, TRUE, {10, 10, 20, 20},
     {{15, 15, 0, 0, 0, 0}, {10, 10, 0, 0, 0, 0}, {20, 15, 0, 0, 255, 255}, {5, 5, 0, 0, 255, 255}}},
    {"copy on an ellipse", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_ELLIPSE, {22, 22, 42, 42}, FALSE, {0},
     {{32, 32, 0, 0, 0, 0}, {32, 10, 0, 0, 255, 255}, {4, 4, 0, 0, 255, 255}}},
    {"copy with a transparent bitmap", RED, D2D1_PRIMITIVE_BLEND_COPY,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, NONE, SHAPE_BITMAP, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 0, 0, 0, 0}, {4, 4, 0, 0, 255, 255}}},
    {"add", {0.0f, 0.0f, 1.0f, 0.5f}, D2D1_PRIMITIVE_BLEND_ADD,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.0f, 0.0f, 0.25f}, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 128, 0, 64, 192}, {4, 4, 128, 0, 0, 128}}},
    {"min", {0.5f, 0.5f, 0.5f, 1.0f}, D2D1_PRIMITIVE_BLEND_MIN,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.25f, 0.0f, 1.0f}, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 0, 64, 128, 255}, {4, 4, 128, 128, 128, 255}}},
    {"min with a half transparent brush", {0.5f, 0.5f, 0.5f, 1.0f}, D2D1_PRIMITIVE_BLEND_MIN,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.25f, 0.0f, 0.5f}, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 128, 128, 128, 255}, {4, 4, 128, 128, 128, 255}}},
    {"min on a fractional rectangle", {0.75f, 0.75f, 0.75f, 1.0f}, D2D1_PRIMITIVE_BLEND_MIN,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.25f, 0.0f, 1.0f}, SHAPE_RECT, {8.5f, 8.5f, 24.5f, 24.5f}, FALSE, {0},
     {{16, 16, 0, 64, 191, 255}, {8, 16, 128, 159, 191, 255}, {24, 16, 128, 159, 191, 255}, {4, 4, 191, 191, 191, 255}}},
    {"max on a fractional rectangle", {0.5f, 0.5f, 0.5f, 1.0f}, D2D1_PRIMITIVE_BLEND_MAX,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.25f, 0.0f, 1.0f}, SHAPE_RECT, {8.5f, 8.5f, 24.5f, 24.5f}, FALSE, {0},
     {{16, 16, 128, 128, 255, 255}, {8, 16, 128, 128, 128, 255}, {24, 16, 128, 128, 128, 255}, {4, 4, 128, 128, 128, 255}}},
    {"max", {0.5f, 0.5f, 0.5f, 1.0f}, D2D1_PRIMITIVE_BLEND_MAX,
     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, {1.0f, 0.25f, 0.0f, 1.0f}, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
     {{16, 16, 128, 128, 255, 255}, {4, 4, 128, 128, 128, 255}}},
};

static void DrawCase(BLEND_TEST *Test, const BLEND_CASE *Case)
{
    static const D2D1_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    ID2D1RectangleGeometry *geometry;
    ID2D1SolidColorBrush *brush;
    D2D1_ELLIPSE ellipse;
    HRESULT hr;

    hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &Case->Brush, NULL, &brush);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    ID2D1DeviceContext_SetTarget(Test->Context, (ID2D1Image *)Test->Target);
    ID2D1DeviceContext_SetTransform(Test->Context, &identity);
    ID2D1DeviceContext_BeginDraw(Test->Context);
    ID2D1DeviceContext_Clear(Test->Context, &Case->Base);
    ID2D1DeviceContext_SetAntialiasMode(Test->Context, Case->Antialias);
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, Case->Blend);
    ok(ID2D1DeviceContext_GetPrimitiveBlend(Test->Context) == Case->Blend, "%s: blend not stored\n", Case->Name);
    if (Case->Clip)
        ID2D1DeviceContext_PushAxisAlignedClip(Test->Context, &Case->ClipRect, D2D1_ANTIALIAS_MODE_ALIASED);

    switch (Case->Shape)
    {
        case SHAPE_RECT:
            ID2D1DeviceContext_FillRectangle(Test->Context, &Case->Rect, (ID2D1Brush *)brush);
            break;
        case SHAPE_GEOMETRY:
            hr = ID2D1Factory1_CreateRectangleGeometry(Test->Factory, &Case->Rect, &geometry);
            ok_hr(hr, S_OK);
            if (SUCCEEDED(hr))
            {
                ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
                ID2D1RectangleGeometry_Release(geometry);
            }
            break;
        case SHAPE_ELLIPSE:
            ellipse.point.x = (Case->Rect.left + Case->Rect.right) / 2.0f;
            ellipse.point.y = (Case->Rect.top + Case->Rect.bottom) / 2.0f;
            ellipse.radiusX = (Case->Rect.right - Case->Rect.left) / 2.0f;
            ellipse.radiusY = (Case->Rect.bottom - Case->Rect.top) / 2.0f;
            ID2D1DeviceContext_FillEllipse(Test->Context, &ellipse, (ID2D1Brush *)brush);
            break;
        case SHAPE_BITMAP:
            ID2D1DeviceContext_DrawBitmap(Test->Context, (ID2D1Bitmap *)Test->Clear, &Case->Rect, 1.0f,
                                          D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL, NULL);
            break;
    }

    if (Case->Clip)
        ID2D1DeviceContext_PopAxisAlignedClip(Test->Context);
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    ID2D1DeviceContext_SetAntialiasMode(Test->Context, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    hr = ID2D1DeviceContext_EndDraw(Test->Context, NULL, NULL);
    ok(hr == S_OK, "%s: EndDraw returned 0x%lx\n", Case->Name, hr);
    ID2D1SolidColorBrush_Release(brush);
}

static void CheckCase(BLEND_TEST *Test, const BLEND_CASE *Case)
{
    D2D1_MAPPED_RECT mapped;
    const BLEND_PIXEL *check;
    const BYTE *pixel;
    HRESULT hr;
    UINT i;

    hr = ID2D1Bitmap1_CopyFromBitmap(Test->Staging, NULL, (ID2D1Bitmap *)Test->Target, NULL);
    ok_hr(hr, S_OK);
    hr = ID2D1Bitmap1_Map(Test->Staging, D2D1_MAP_OPTIONS_READ, &mapped);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    for (i = 0; i < ARRAYSIZE(Case->Checks); i++)
    {
        check = &Case->Checks[i];
        if (!check->x && !check->y)
            continue;
        pixel = mapped.bits + check->y * mapped.pitch + check->x * 4;
        ok(abs(pixel[0] - check->b) <= 2 && abs(pixel[1] - check->g) <= 2 &&
           abs(pixel[2] - check->r) <= 2 && abs(pixel[3] - check->a) <= 2,
           "%s: pixel (%u,%u) is %u,%u,%u,%u, expected %u,%u,%u,%u\n", Case->Name, check->x, check->y,
           pixel[0], pixel[1], pixel[2], pixel[3], check->b, check->g, check->r, check->a);
    }

    ID2D1Bitmap1_Unmap(Test->Staging);
}

static void TestTileRepaint(BLEND_TEST *Test)
{
    static const BLEND_CASE expected =
        {"repeated clipped tile repaint", RED, D2D1_PRIMITIVE_BLEND_SOURCE_OVER, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
         NONE, SHAPE_GEOMETRY, {0, 0, SIZE, SIZE}, TRUE, {8, 8, 24, 24},
         {{16, 16, 0, 0, 0, 128}, {8, 8, 0, 0, 0, 128}, {24, 16, 0, 0, 255, 255}, {4, 4, 0, 0, 255, 255}}};
    D2D1_COLOR_F transparent = NONE, shadow = {0.0f, 0.0f, 0.0f, 0.5f}, red = RED;
    ID2D1SolidColorBrush *brush;
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;
    UINT i;

    hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &transparent, NULL, &brush);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = ID2D1Factory1_CreateRectangleGeometry(Test->Factory, &expected.Rect, &geometry);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
    {
        ID2D1SolidColorBrush_Release(brush);
        return;
    }

    ID2D1DeviceContext_SetTarget(Test->Context, (ID2D1Image *)Test->Target);
    ID2D1DeviceContext_BeginDraw(Test->Context);
    ID2D1DeviceContext_Clear(Test->Context, &red);
    for (i = 0; i < 8; ++i)
    {
        ID2D1DeviceContext_PushAxisAlignedClip(Test->Context, &expected.ClipRect, D2D1_ANTIALIAS_MODE_ALIASED);
        ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_COPY);
        ID2D1SolidColorBrush_SetColor(brush, &transparent);
        ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
        ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        ID2D1SolidColorBrush_SetColor(brush, &shadow);
        ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
        ID2D1DeviceContext_PopAxisAlignedClip(Test->Context);
    }
    hr = ID2D1DeviceContext_EndDraw(Test->Context, NULL, NULL);
    ok_hr(hr, S_OK);
    CheckCase(Test, &expected);
    ID2D1RectangleGeometry_Release(geometry);
    ID2D1SolidColorBrush_Release(brush);
}

static void TestCopyLayer(BLEND_TEST *Test)
{
    static const BLEND_CASE expected =
        {"layer composition with copy selected", RED, D2D1_PRIMITIVE_BLEND_COPY, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
         NONE, SHAPE_RECT, {8, 8, 24, 24}, FALSE, {0},
         {{16, 16, 0, 0, 128, 255}, {4, 4, 0, 0, 255, 255}}};
    D2D1_LAYER_PARAMETERS1 parameters = {{0, 0, SIZE, SIZE}, NULL, D2D1_ANTIALIAS_MODE_ALIASED,
        {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}}, 1.0f, NULL, D2D1_LAYER_OPTIONS1_NONE};
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 0.5f}, red = RED;
    ID2D1SolidColorBrush *brush;
    HRESULT hr;

    hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &black, NULL, &brush);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ID2D1DeviceContext_SetTarget(Test->Context, (ID2D1Image *)Test->Target);
    ID2D1DeviceContext_BeginDraw(Test->Context);
    ID2D1DeviceContext_Clear(Test->Context, &red);
    ID2D1DeviceContext_PushLayer(Test->Context, &parameters, NULL);
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_COPY);
    ID2D1DeviceContext_FillRectangle(Test->Context, &expected.Rect, (ID2D1Brush *)brush);
    ID2D1DeviceContext_PopLayer(Test->Context);
    ok(ID2D1DeviceContext_GetPrimitiveBlend(Test->Context) == D2D1_PRIMITIVE_BLEND_COPY,
       "PopLayer changed the primitive blend\n");
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    hr = ID2D1DeviceContext_EndDraw(Test->Context, NULL, NULL);
    ok_hr(hr, S_OK);
    CheckCase(Test, &expected);
    ID2D1SolidColorBrush_Release(brush);
}

static void TestInvalidBlend(BLEND_TEST *Test)
{
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_COPY);
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, (D2D1_PRIMITIVE_BLEND)(D2D1_PRIMITIVE_BLEND_MAX + 1));
    ok(ID2D1DeviceContext_GetPrimitiveBlend(Test->Context) == D2D1_PRIMITIVE_BLEND_COPY,
       "an unknown blend changed the stored blend to %u\n", ID2D1DeviceContext_GetPrimitiveBlend(Test->Context));
    ID2D1DeviceContext_SetPrimitiveBlend(Test->Context, D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

static BOOL CreateTest(BLEND_TEST *Test, ID3D11Device *Device)
{
    D2D1_BITMAP_PROPERTIES1 target_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                            D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_BITMAP_PROPERTIES1 staging_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                             D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    D2D1_BITMAP_PROPERTIES1 clear_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                           D2D1_BITMAP_OPTIONS_NONE, NULL};
    static const BYTE clear_data[16 * 16 * 4];
    D2D1_SIZE_U size = {SIZE, SIZE}, clear_size = {16, 16};
    ID2D1Device *device = NULL;
    IDXGIDevice *dxgi = NULL;
    HRESULT hr;

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

    hr = ID2D1DeviceContext_CreateBitmap(Test->Context, size, NULL, 0, &target_props, &Test->Target);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateBitmap(Test->Context, size, NULL, 0, &staging_props, &Test->Staging);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1DeviceContext_CreateBitmap(Test->Context, clear_size, clear_data, 16 * 4, &clear_props, &Test->Clear);
    ok_hr(hr, S_OK);
    return SUCCEEDED(hr);
}

static void DestroyTest(BLEND_TEST *Test)
{
    if (Test->Clear)
        ID2D1Bitmap1_Release(Test->Clear);
    if (Test->Staging)
        ID2D1Bitmap1_Release(Test->Staging);
    if (Test->Target)
        ID2D1Bitmap1_Release(Test->Target);
    if (Test->Context)
        ID2D1DeviceContext_Release(Test->Context);
    if (Test->Factory)
        ID2D1Factory1_Release(Test->Factory);
}

START_TEST(PrimitiveBlend)
{
    ID3D11Device *device = NULL;
    BLEND_TEST test;
    HRESULT hr;
    UINT i;

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
        for (i = 0; i < ARRAYSIZE(Cases); i++)
        {
            DrawCase(&test, &Cases[i]);
            CheckCase(&test, &Cases[i]);
        }
        TestTileRepaint(&test);
        TestCopyLayer(&test);
        TestInvalidBlend(&test);
    }
    DestroyTest(&test);
    ID3D11Device_Release(device);
}
