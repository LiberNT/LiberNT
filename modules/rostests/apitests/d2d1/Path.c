/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests what ID2D1DeviceContext draws for path geometries, curves, holes and fractional clips
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <float.h>
#include <math.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1_1.h>

#define SIZE 64
#define PI 3.14159265f

typedef struct _PIXEL_RANGE
{
    UINT x;
    UINT y;
    BYTE min;
    BYTE max;
} PIXEL_RANGE;

typedef struct _PATH_TEST
{
    ID2D1Factory1 *Factory;
    ID2D1DeviceContext *Context;
    ID2D1Bitmap1 *Target;
    ID2D1Bitmap1 *Staging;
    ID2D1SolidColorBrush *Black;
} PATH_TEST;

static const D2D1_MATRIX_3X2_F Identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};

static void BeginFrame(PATH_TEST *Test)
{
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};

    ID2D1DeviceContext_SetTarget(Test->Context, (ID2D1Image *)Test->Target);
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
    ID2D1DeviceContext_BeginDraw(Test->Context);
    ID2D1DeviceContext_Clear(Test->Context, &white);
}

static void EndFrame(PATH_TEST *Test, const char *Name, const PIXEL_RANGE *Checks, UINT Count)
{
    D2D1_MAPPED_RECT mapped;
    const BYTE *pixel;
    HRESULT hr;
    UINT i;

    hr = ID2D1DeviceContext_EndDraw(Test->Context, NULL, NULL);
    ok(hr == S_OK, "%s: EndDraw returned 0x%lx\n", Name, hr);
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);

    hr = ID2D1Bitmap1_CopyFromBitmap(Test->Staging, NULL, (ID2D1Bitmap *)Test->Target, NULL);
    ok_hr(hr, S_OK);
    hr = ID2D1Bitmap1_Map(Test->Staging, D2D1_MAP_OPTIONS_READ, &mapped);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    for (i = 0; i < Count; i++)
    {
        pixel = mapped.bits + Checks[i].y * mapped.pitch + Checks[i].x * 4;
        ok(pixel[0] >= Checks[i].min && pixel[0] <= Checks[i].max && pixel[1] == pixel[0] && pixel[2] == pixel[0] &&
           pixel[3] == 255, "%s: pixel (%u,%u) is %u,%u,%u,%u, expected gray %u..%u\n", Name, Checks[i].x,
           Checks[i].y, pixel[0], pixel[1], pixel[2], pixel[3], Checks[i].min, Checks[i].max);
    }

    ID2D1Bitmap1_Unmap(Test->Staging);
}

typedef void (*PATH_BUILDER)(ID2D1GeometrySink *Sink);

static ID2D1PathGeometry1 *BuildPath(PATH_TEST *Test, D2D1_FILL_MODE Mode, PATH_BUILDER Builder)
{
    ID2D1PathGeometry1 *path = NULL;
    ID2D1GeometrySink *sink = NULL;
    HRESULT hr;

    hr = ID2D1Factory1_CreatePathGeometry(Test->Factory, &path);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return NULL;
    hr = ID2D1PathGeometry1_Open(path, &sink);
    ok_hr(hr, S_OK);
    ID2D1GeometrySink_SetFillMode(sink, Mode);
    Builder(sink);
    hr = ID2D1GeometrySink_Close(sink);
    ok_hr(hr, S_OK);
    ID2D1GeometrySink_Release(sink);
    return path;
}

static void FillTestPath(PATH_TEST *Test, const char *Name, D2D1_FILL_MODE Mode, PATH_BUILDER Builder,
                     const PIXEL_RANGE *Checks, UINT Count)
{
    ID2D1PathGeometry1 *path = BuildPath(Test, Mode, Builder);

    if (!path)
        return;
    BeginFrame(Test);
    ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)path, (ID2D1Brush *)Test->Black, NULL);
    EndFrame(Test, Name, Checks, Count);
    ID2D1PathGeometry1_Release(path);
}

static void Star(ID2D1GeometrySink *Sink)
{
    UINT i;

    for (i = 0; i < 10; i++)
    {
        float angle = -PI / 2 + i * PI / 5;
        float radius = (i & 1) ? 10.0f : 24.0f;
        D2D1_POINT_2F p = {32.0f + radius * cosf(angle), 32.0f + radius * sinf(angle)};

        if (!i)
            ID2D1GeometrySink_BeginFigure(Sink, p, D2D1_FIGURE_BEGIN_FILLED);
        else
            ID2D1GeometrySink_AddLine(Sink, p);
    }
    ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
}

static void Pentagram(ID2D1GeometrySink *Sink)
{
    UINT i;

    for (i = 0; i < 5; i++)
    {
        float angle = -PI / 2 + i * 4 * PI / 5;
        D2D1_POINT_2F p = {32.0f + 26.0f * cosf(angle), 32.0f + 26.0f * sinf(angle)};

        if (!i)
            ID2D1GeometrySink_BeginFigure(Sink, p, D2D1_FIGURE_BEGIN_FILLED);
        else
            ID2D1GeometrySink_AddLine(Sink, p);
    }
    ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
}

static void Ring(ID2D1GeometrySink *Sink)
{
    UINT ring, k;

    for (ring = 0; ring < 2; ring++)
    {
        float radius = ring ? 12.0f : 20.0f;

        for (k = 0; k < 64; k++)
        {
            float angle = k * 2 * PI / 64;
            D2D1_POINT_2F p = {32.0f + radius * cosf(angle), 32.0f + radius * sinf(angle)};

            if (!k)
                ID2D1GeometrySink_BeginFigure(Sink, p, D2D1_FIGURE_BEGIN_FILLED);
            else
                ID2D1GeometrySink_AddLine(Sink, p);
        }
        ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
    }
}

static void BezierCircle(ID2D1GeometrySink *Sink)
{
    const float r = 20.0f, k = 0.5522847f * r;
    D2D1_POINT_2F start = {32.0f + r, 32.0f};
    D2D1_BEZIER_SEGMENT segs[4] =
    {
        {{32 + r, 32 + k}, {32 + k, 32 + r}, {32, 32 + r}},
        {{32 - k, 32 + r}, {32 - r, 32 + k}, {32 - r, 32}},
        {{32 - r, 32 - k}, {32 - k, 32 - r}, {32, 32 - r}},
        {{32 + k, 32 - r}, {32 + r, 32 - k}, {32 + r, 32}},
    };

    ID2D1GeometrySink_BeginFigure(Sink, start, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_AddBeziers(Sink, segs, 4);
    ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
}

static void RoundedButton(ID2D1GeometrySink *Sink)
{
    const float l = 10.0f, t = 16.0f, r = 54.0f, b = 48.0f, c = 8.0f;
    D2D1_POINT_2F p;
    D2D1_QUADRATIC_BEZIER_SEGMENT q;

    p.x = l + c; p.y = t;
    ID2D1GeometrySink_BeginFigure(Sink, p, D2D1_FIGURE_BEGIN_FILLED);
    p.x = r - c; p.y = t;
    ID2D1GeometrySink_AddLine(Sink, p);
    q.point1.x = r; q.point1.y = t; q.point2.x = r; q.point2.y = t + c;
    ID2D1GeometrySink_AddQuadraticBezier(Sink, &q);
    p.x = r; p.y = b - c;
    ID2D1GeometrySink_AddLine(Sink, p);
    q.point1.x = r; q.point1.y = b; q.point2.x = r - c; q.point2.y = b;
    ID2D1GeometrySink_AddQuadraticBezier(Sink, &q);
    p.x = l + c; p.y = b;
    ID2D1GeometrySink_AddLine(Sink, p);
    q.point1.x = l; q.point1.y = b; q.point2.x = l; q.point2.y = b - c;
    ID2D1GeometrySink_AddQuadraticBezier(Sink, &q);
    p.x = l; p.y = t + c;
    ID2D1GeometrySink_AddLine(Sink, p);
    q.point1.x = l; q.point1.y = t; q.point2.x = l + c; q.point2.y = t;
    ID2D1GeometrySink_AddQuadraticBezier(Sink, &q);
    ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
}

static void TestSimplePaths(PATH_TEST *Test)
{
    static const PIXEL_RANGE star[] =
    {
        {4, 4, 255, 255}, {32, 32, 0, 0}, {32, 14, 0, 0}, {42, 17, 255, 255}, {60, 60, 255, 255}, {32, 4, 255, 255},
    };
    static const PIXEL_RANGE ring[] = {{32, 32, 255, 255}, {32, 16, 0, 0}, {32, 4, 255, 255}, {4, 32, 255, 255}};
    static const PIXEL_RANGE circle[] = {{32, 32, 0, 0}, {45, 32, 0, 0}, {56, 32, 255, 255}, {32, 56, 255, 255}};
    static const PIXEL_RANGE button[] = {{32, 32, 0, 0}, {12, 20, 0, 0}, {2, 2, 255, 255}, {10, 17, 120, 255}};

    FillTestPath(Test, "star", D2D1_FILL_MODE_ALTERNATE, Star, star, ARRAYSIZE(star));
    FillTestPath(Test, "ring alternate", D2D1_FILL_MODE_ALTERNATE, Ring, ring, ARRAYSIZE(ring));
    FillTestPath(Test, "bezier circle", D2D1_FILL_MODE_ALTERNATE, BezierCircle, circle, ARRAYSIZE(circle));
    FillTestPath(Test, "rounded button", D2D1_FILL_MODE_ALTERNATE, RoundedButton, button, ARRAYSIZE(button));
}

static void TestFillModes(PATH_TEST *Test)
{
    static const PIXEL_RANGE alternate[] = {{32, 33, 255, 255}, {32, 20, 0, 0}, {4, 4, 255, 255}};
    static const PIXEL_RANGE winding[] = {{32, 33, 0, 0}, {32, 20, 0, 0}, {4, 4, 255, 255}};

    FillTestPath(Test, "pentagram alternate", D2D1_FILL_MODE_ALTERNATE, Pentagram, alternate, ARRAYSIZE(alternate));
    FillTestPath(Test, "pentagram winding", D2D1_FILL_MODE_WINDING, Pentagram, winding, ARRAYSIZE(winding));
}

static void TestRepeatedPaths(PATH_TEST *Test)
{
    static const PIXEL_RANGE checks[] =
    {
        {32, 32, 0, 0}, {32, 14, 0, 0}, {4, 4, 255, 255}, {32, 52, 255, 255}, {32, 4, 255, 255},
    };
    D2D1_MATRIX_3X2_F matrix = Identity;
    ID2D1PathGeometry1 *path;
    UINT i;

    for (i = 0; i < 3; i++)
    {
        path = BuildPath(Test, D2D1_FILL_MODE_ALTERNATE, Star);
        if (!path)
            return;
        BeginFrame(Test);
        matrix._31 = i ? 20.0f : 0.0f;
        ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
        ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)path, (ID2D1Brush *)Test->Black, NULL);
        ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
        if (i)
        {
            static const PIXEL_RANGE shifted[] = {{52, 32, 0, 0}, {32, 32, 255, 255}, {4, 4, 255, 255}, {63, 4, 255, 255}};
            EndFrame(Test, "repeated star shifted", shifted, ARRAYSIZE(shifted));
        }
        else
        {
            EndFrame(Test, "repeated star", checks, ARRAYSIZE(checks));
        }
        ID2D1PathGeometry1_Release(path);
    }
}

static void TestScaledCurve(PATH_TEST *Test)
{
    static const PIXEL_RANGE checks[] = {{32, 32, 0, 0}, {60, 32, 0, 0}, {63, 63, 0, 0}};
    D2D1_MATRIX_3X2_F matrix = {{{3.0f, 0.0f, 0.0f, 3.0f, -64.0f, -64.0f}}};
    ID2D1PathGeometry1 *path = BuildPath(Test, D2D1_FILL_MODE_ALTERNATE, BezierCircle);

    if (!path)
        return;
    BeginFrame(Test);
    ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
    ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)path, (ID2D1Brush *)Test->Black, NULL);
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
    EndFrame(Test, "bezier circle scaled", checks, ARRAYSIZE(checks));
    ID2D1PathGeometry1_Release(path);
}

static void TestFractionalClip(PATH_TEST *Test)
{
    static const PIXEL_RANGE checks[] =
    {
        {5, 5, 255, 255}, {25, 25, 0, 0}, {10, 25, 120, 136}, {40, 25, 120, 136}, {25, 10, 120, 136},
        {10, 10, 183, 199}, {41, 41, 255, 255},
    };
    D2D1_RECT_F clip = {10.5f, 10.5f, 40.5f, 40.5f}, fill = {0.0f, 0.0f, SIZE, SIZE};

    BeginFrame(Test);
    ID2D1DeviceContext_PushAxisAlignedClip(Test->Context, &clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1DeviceContext_FillRectangle(Test->Context, &fill, (ID2D1Brush *)Test->Black);
    ID2D1DeviceContext_PopAxisAlignedClip(Test->Context);
    EndFrame(Test, "fractional clip", checks, ARRAYSIZE(checks));
}

static void TestEllipseEdge(PATH_TEST *Test)
{
    static const PIXEL_RANGE checks[] = {{32, 32, 0, 0}, {52, 32, 100, 160}, {54, 32, 255, 255}, {4, 4, 255, 255}};
    D2D1_ELLIPSE ellipse = {{32.0f, 32.0f}, 20.5f, 20.5f};

    BeginFrame(Test);
    ID2D1DeviceContext_FillEllipse(Test->Context, &ellipse, (ID2D1Brush *)Test->Black);
    EndFrame(Test, "ellipse edge", checks, ARRAYSIZE(checks));
}

static void TestManyPaths(PATH_TEST *Test)
{
    static const PIXEL_RANGE checks[] = {{32, 32, 0, 0}, {2, 2, 255, 255}};
    D2D1_MATRIX_3X2_F matrix = Identity;
    ID2D1PathGeometry1 *path;
    UINT i;

    BeginFrame(Test);
    for (i = 0; i < 1500; i++)
    {
        float scale = 0.5f + (i % 150) / 300.0f;

        path = BuildPath(Test, D2D1_FILL_MODE_ALTERNATE, Star);
        if (!path)
            break;
        matrix._11 = matrix._22 = scale;
        matrix._31 = 32.0f - 32.0f * scale + (i % 7) * 0.13f;
        matrix._32 = 32.0f - 32.0f * scale + (i % 11) * 0.07f;
        ID2D1DeviceContext_SetTransform(Test->Context, &matrix);
        ID2D1DeviceContext_FillGeometry(Test->Context, (ID2D1Geometry *)path, (ID2D1Brush *)Test->Black, NULL);
        ID2D1PathGeometry1_Release(path);
    }
    ID2D1DeviceContext_SetTransform(Test->Context, &Identity);
    EndFrame(Test, "many paths", checks, ARRAYSIZE(checks));
}

static BOOL CreateTest(PATH_TEST *Test, ID3D11Device *Device)
{
    D2D1_BITMAP_PROPERTIES1 target_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                            D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_BITMAP_PROPERTIES1 staging_props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                             D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_SIZE_U size = {SIZE, SIZE};
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
        hr = ID2D1DeviceContext_CreateSolidColorBrush(Test->Context, &black, NULL, &Test->Black);
    ok_hr(hr, S_OK);
    return SUCCEEDED(hr);
}

static void DestroyTest(PATH_TEST *Test)
{
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

START_TEST(Path)
{
    PATH_TEST test;
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
        TestSimplePaths(&test);
        TestFillModes(&test);
        TestRepeatedPaths(&test);
        TestScaledCurve(&test);
        TestFractionalClip(&test);
        TestEllipseEdge(&test);
        TestManyPaths(&test);
    }
    DestroyTest(&test);
    ID3D11Device_Release(device);
}
