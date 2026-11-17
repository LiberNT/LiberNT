/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the pixels ID2D1RenderTarget::FillMesh covers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1.h>
#include <wincodec.h>

#define SIZE 64

typedef struct _MESH_TEST
{
    ID2D1Factory *Factory;
    IWICImagingFactory *Wic;
    IWICBitmap *Bitmap;
    ID2D1RenderTarget *Target;
    ID2D1SolidColorBrush *Brush;
} MESH_TEST;

typedef struct _MESH_PIXEL
{
    UINT x;
    UINT y;
    BOOL Covered;
} MESH_PIXEL;

static const D2D1_COLOR_F Red = { 1.0f, 0.0f, 0.0f, 1.0f };
static const D2D1_COLOR_F Blue = { 0.0f, 0.0f, 1.0f, 1.0f };

static void
CheckPixels(MESH_TEST *Test, const MESH_PIXEL *Pixels, UINT Count, const char *Name)
{
    WICRect Rect = { 0, 0, SIZE, SIZE };
    IWICBitmapLock *Lock = NULL;
    UINT Stride = 0, BufferSize = 0;
    BYTE *Bits = NULL;
    const BYTE *Pixel;
    HRESULT hr;
    UINT i;

    hr = IWICBitmap_Lock(Test->Bitmap, &Rect, WICBitmapLockRead, &Lock);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    IWICBitmapLock_GetStride(Lock, &Stride);
    hr = IWICBitmapLock_GetDataPointer(Lock, &BufferSize, &Bits);
    ok_hr(hr, S_OK);
    for (i = 0; SUCCEEDED(hr) && i < Count; i++)
    {
        Pixel = Bits + (SIZE_T)Pixels[i].y * Stride + (SIZE_T)Pixels[i].x * 4;
        if (Pixels[i].Covered)
            ok(Pixel[0] == 255 && Pixel[1] == 0 && Pixel[2] == 0 && Pixel[3] == 255,
               "%s: pixel (%u,%u) is %u,%u,%u,%u, expected the mesh colour\n", Name, Pixels[i].x, Pixels[i].y,
               Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
        else
            ok(Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 255 && Pixel[3] == 255,
               "%s: pixel (%u,%u) is %u,%u,%u,%u, expected the background\n", Name, Pixels[i].x, Pixels[i].y,
               Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
    }
    IWICBitmapLock_Release(Lock);
}

static ID2D1Mesh *
CreateSquareMesh(MESH_TEST *Test)
{
    static const D2D1_TRIANGLE Triangles[] =
    {
        { { 8.0f, 8.0f }, { 24.0f, 8.0f }, { 24.0f, 24.0f } },
        { { 8.0f, 8.0f }, { 8.0f, 24.0f }, { 24.0f, 24.0f } },
        { { 40.0f, 40.0f }, { 56.0f, 40.0f }, { 40.0f, 56.0f } },
    };
    ID2D1TessellationSink *Sink = NULL;
    ID2D1Mesh *Mesh = NULL;
    HRESULT hr;

    hr = ID2D1RenderTarget_CreateMesh(Test->Target, &Mesh);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return NULL;
    hr = ID2D1Mesh_Open(Mesh, &Sink);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ID2D1TessellationSink_AddTriangles(Sink, Triangles, ARRAYSIZE(Triangles));
        hr = ID2D1TessellationSink_Close(Sink);
        ok_hr(hr, S_OK);
        ID2D1TessellationSink_Release(Sink);
    }
    return Mesh;
}

static HRESULT
DrawMesh(MESH_TEST *Test, ID2D1Mesh *Mesh, D2D1_ANTIALIAS_MODE Antialias, const D2D1_MATRIX_3X2_F *Transform)
{
    static const D2D1_MATRIX_3X2_F Identity = {{{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }}};

    ID2D1RenderTarget_SetTransform(Test->Target, Transform ? Transform : &Identity);
    ID2D1RenderTarget_BeginDraw(Test->Target);
    ID2D1RenderTarget_Clear(Test->Target, &Red);
    ID2D1RenderTarget_SetAntialiasMode(Test->Target, Antialias);
    ID2D1RenderTarget_FillMesh(Test->Target, Mesh, (ID2D1Brush *)Test->Brush);
    ID2D1RenderTarget_SetAntialiasMode(Test->Target, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1RenderTarget_SetTransform(Test->Target, &Identity);
    return ID2D1RenderTarget_EndDraw(Test->Target, NULL, NULL);
}

static void
TestTriangles(MESH_TEST *Test)
{
    static const MESH_PIXEL Square[] =
    {
        { 16, 16, TRUE }, { 9, 23, TRUE }, { 23, 9, TRUE }, { 4, 4, FALSE }, { 30, 16, FALSE },
        { 42, 42, TRUE }, { 54, 54, FALSE }, { 50, 41, TRUE },
    };
    static const MESH_PIXEL Moved[] =
    {
        { 16, 16, FALSE }, { 48, 16, TRUE }, { 4, 4, FALSE },
    };
    static const D2D1_MATRIX_3X2_F Translate = {{{ 1.0f, 0.0f, 0.0f, 1.0f, 32.0f, 0.0f }}};
    ID2D1Mesh *Mesh = CreateSquareMesh(Test);
    HRESULT hr;

    if (!Mesh)
        return;
    hr = DrawMesh(Test, Mesh, D2D1_ANTIALIAS_MODE_ALIASED, NULL);
    ok_hr(hr, S_OK);
    CheckPixels(Test, Square, ARRAYSIZE(Square), "aliased");

    /* The mesh is drawn under the target transform. */
    hr = DrawMesh(Test, Mesh, D2D1_ANTIALIAS_MODE_ALIASED, &Translate);
    ok_hr(hr, S_OK);
    CheckPixels(Test, Moved, ARRAYSIZE(Moved), "translated");
    ID2D1Mesh_Release(Mesh);
}

static void
TestTessellated(MESH_TEST *Test)
{
    static const MESH_PIXEL Disc[] =
    {
        { 32, 32, TRUE }, { 32, 12, TRUE }, { 12, 32, TRUE }, { 4, 4, FALSE }, { 58, 58, FALSE },
    };
    D2D1_ELLIPSE Ellipse = { { 32.0f, 32.0f }, 24.0f, 24.0f };
    ID2D1EllipseGeometry *Geometry = NULL;
    ID2D1TessellationSink *Sink = NULL;
    ID2D1Mesh *Mesh = NULL;
    HRESULT hr;

    hr = ID2D1Factory_CreateEllipseGeometry(Test->Factory, &Ellipse, &Geometry);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1RenderTarget_CreateMesh(Test->Target, &Mesh);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = ID2D1Mesh_Open(Mesh, &Sink);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1EllipseGeometry_Tessellate(Geometry, NULL, 0.25f, Sink);
        ok_hr(hr, S_OK);
        hr = ID2D1TessellationSink_Close(Sink);
        ok_hr(hr, S_OK);
        ID2D1TessellationSink_Release(Sink);
        hr = DrawMesh(Test, Mesh, D2D1_ANTIALIAS_MODE_ALIASED, NULL);
        ok_hr(hr, S_OK);
        CheckPixels(Test, Disc, ARRAYSIZE(Disc), "tessellated ellipse");
    }
    if (Mesh)
        ID2D1Mesh_Release(Mesh);
    if (Geometry)
        ID2D1EllipseGeometry_Release(Geometry);
}

static BOOL
CreateTest(MESH_TEST *Test)
{
    D2D1_RENDER_TARGET_PROPERTIES Props = { D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                            { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96.0f, 96.0f,
                                            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT };
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&Test->Factory);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                              (void **)&Test->Wic);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateBitmap(Test->Wic, SIZE, SIZE, &GUID_WICPixelFormat32bppPBGRA,
                                             WICBitmapCacheOnLoad, &Test->Bitmap);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(Test->Factory, Test->Bitmap, &Props, &Test->Target);
    if (SUCCEEDED(hr))
        hr = ID2D1RenderTarget_CreateSolidColorBrush(Test->Target, &Blue, NULL, &Test->Brush);
    ok_hr(hr, S_OK);
    return SUCCEEDED(hr);
}

static void
DestroyTest(MESH_TEST *Test)
{
    if (Test->Brush)
        ID2D1SolidColorBrush_Release(Test->Brush);
    if (Test->Target)
        ID2D1RenderTarget_Release(Test->Target);
    if (Test->Bitmap)
        IWICBitmap_Release(Test->Bitmap);
    if (Test->Wic)
        IWICImagingFactory_Release(Test->Wic);
    if (Test->Factory)
        ID2D1Factory_Release(Test->Factory);
}

START_TEST(FillMesh)
{
    MESH_TEST Test;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ZeroMemory(&Test, sizeof(Test));
    if (CreateTest(&Test))
    {
        TestTriangles(&Test);
        TestTessellated(&Test);
    }
    DestroyTest(&Test);
    CoUninitialize();
}
