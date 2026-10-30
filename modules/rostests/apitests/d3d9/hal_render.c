/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 9 HAL clears, draws, shaders and presents read back exactly
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdlib.h>
#include <wine/test.h>

#define SIZE 128
#define TEX_SIZE 64
#define COLOR_MASK 0x00FFFFFF

typedef struct
{
    float x, y, z, rhw;
    DWORD color;
} COLOR_VERTEX;

typedef struct
{
    float x, y, z, rhw;
    float u, v;
} TEX_VERTEX;

typedef struct
{
    float x, y, z;
    float u, v;
} SHADER_VERTEX;

/* vs_2_0: dcl_position v0; dcl_texcoord v1; mov oPos, v0; mov oT0, v1 */
static const DWORD ClipVertexShader[] =
{
    0xFFFE0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x0200001F, 0x80000005, 0x900F0001,
    0x02000001, 0xC00F0000, 0x90E40000,
    0x02000001, 0xE00F0000, 0x90E40001,
    0x0000FFFF,
};

/* ps_2_0: mov oC0, c0 */
static const DWORD ConstantPixelShader[] =
{
    0xFFFF0200,
    0x02000001, 0x800F0800, 0xA0E40000,
    0x0000FFFF,
};

/* ps_2_0: dcl t0; dcl_2d s0; texld r0, t0, s0; mul r0, r0, c0; mov oC0, r0 */
static const DWORD ModulatePixelShader[] =
{
    0xFFFF0200,
    0x0200001F, 0x80000000, 0xB00F0000,
    0x0200001F, 0x90000000, 0xA00F0800,
    0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800,
    0x03000005, 0x800F0000, 0x80E40000, 0xA0E40000,
    0x02000001, 0x800F0800, 0x80E40000,
    0x0000FFFF,
};

static IDirect3DDevice9 *Device;
static IDirect3DSurface9 *RenderTarget;
static IDirect3DSurface9 *Readback;
static DWORD Pixels[SIZE * SIZE];

static BOOL ColorNear(DWORD value, DWORD expected, int tolerance)
{
    int shift;

    for (shift = 0; shift < 24; shift += 8)
    {
        if (abs((int)((value >> shift) & 0xFF) - (int)((expected >> shift) & 0xFF)) > tolerance)
            return FALSE;
    }
    return TRUE;
}

static BOOL ReadPixels(void)
{
    D3DLOCKED_RECT locked;
    HRESULT hr;
    int y;

    hr = IDirect3DDevice9_GetRenderTargetData(Device, RenderTarget, Readback);
    ok(hr == D3D_OK, "GetRenderTargetData failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = IDirect3DSurface9_LockRect(Readback, &locked, NULL, D3DLOCK_READONLY);
    ok(hr == D3D_OK, "LockRect failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    for (y = 0; y < SIZE; y++)
        memcpy(&Pixels[y * SIZE], (BYTE *)locked.pBits + y * locked.Pitch, SIZE * sizeof(DWORD));
    IDirect3DSurface9_UnlockRect(Readback);
    return TRUE;
}

static DWORD Pixel(int x, int y)
{
    return Pixels[y * SIZE + x] & COLOR_MASK;
}

static unsigned int CountPixelsNear(DWORD expected, int tolerance)
{
    unsigned int count = 0;
    unsigned int i;

    for (i = 0; i < SIZE * SIZE; i++)
        count += ColorNear(Pixels[i], expected, tolerance);
    return count;
}

static void Clear(DWORD color)
{
    HRESULT hr;

    hr = IDirect3DDevice9_Clear(Device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
    ok(hr == D3D_OK, "Clear failed: 0x%08lx\n", hr);
}

static void DrawColorQuad(float left, float top, float right, float bottom, float z, DWORD color)
{
    COLOR_VERTEX quad[] =
    {
        {left, top, z, 1.0f, color},
        {right, top, z, 1.0f, color},
        {left, bottom, z, 1.0f, color},
        {right, bottom, z, 1.0f, color},
    };
    HRESULT hr;

    hr = IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    ok(hr == D3D_OK, "SetFVF failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_BeginScene(Device);
    ok(hr == D3D_OK, "BeginScene failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
    ok(hr == D3D_OK, "DrawPrimitiveUP failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_EndScene(Device);
    ok(hr == D3D_OK, "EndScene failed: 0x%08lx\n", hr);
}

static IDirect3DTexture9 *CreateQuadrantTexture(void)
{
    IDirect3DTexture9 *texture = NULL;
    D3DLOCKED_RECT locked;
    HRESULT hr;
    int x, y;

    hr = IDirect3DDevice9_CreateTexture(Device, TEX_SIZE, TEX_SIZE, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                        &texture, NULL);
    ok(hr == D3D_OK, "CreateTexture failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return NULL;
    hr = IDirect3DTexture9_LockRect(texture, 0, &locked, NULL, 0);
    ok(hr == D3D_OK, "LockRect failed: 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        IDirect3DTexture9_Release(texture);
        return NULL;
    }
    for (y = 0; y < TEX_SIZE; y++)
    {
        DWORD *row = (DWORD *)((BYTE *)locked.pBits + y * locked.Pitch);

        for (x = 0; x < TEX_SIZE; x++)
            row[x] = y < TEX_SIZE / 2 ? (x < TEX_SIZE / 2 ? 0xFFFF0000 : 0xFF00FF00)
                                      : (x < TEX_SIZE / 2 ? 0xFF0000FF : 0xFFFFFFFF);
    }
    IDirect3DTexture9_UnlockRect(texture, 0);
    return texture;
}

static void Test_Clear(void)
{
    Clear(0xFFFF0000);
    if (ReadPixels())
        ok(CountPixelsNear(0xFF0000, 0) == SIZE * SIZE, "red clear: pixel 0 is 0x%06lx\n", Pixel(0, 0));

    Clear(0xFF0000FF);
    if (ReadPixels())
        ok(CountPixelsNear(0x0000FF, 0) == SIZE * SIZE, "blue clear: pixel 0 is 0x%06lx\n", Pixel(0, 0));
}

static void Test_Triangle(void)
{
    static const COLOR_VERTEX triangle[] =
    {
        {0.0f, 0.0f, 0.5f, 1.0f, 0xFF00FF00},
        {SIZE, 0.0f, 0.5f, 1.0f, 0xFF00FF00},
        {0.0f, SIZE, 0.5f, 1.0f, 0xFF00FF00},
    };
    unsigned int covered;
    HRESULT hr;

    Clear(0xFF000000);
    IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    hr = IDirect3DDevice9_BeginScene(Device);
    ok(hr == D3D_OK, "BeginScene failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLELIST, 1, triangle, sizeof(triangle[0]));
    ok(hr == D3D_OK, "DrawPrimitiveUP failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_EndScene(Device);
    ok(hr == D3D_OK, "EndScene failed: 0x%08lx\n", hr);

    if (!ReadPixels())
        return;
    ok(Pixel(8, 8) == 0x00FF00, "inside the triangle: 0x%06lx\n", Pixel(8, 8));
    ok(Pixel(SIZE - 8, SIZE - 8) == 0x000000, "outside the triangle: 0x%06lx\n", Pixel(SIZE - 8, SIZE - 8));
    covered = CountPixelsNear(0x00FF00, 0);
    ok(covered > SIZE * SIZE * 45 / 100 && covered < SIZE * SIZE * 55 / 100,
       "triangle covers %u of %u pixels\n", covered, SIZE * SIZE);
}

static void Test_Texture(void)
{
    static const TEX_VERTEX quad[] =
    {
        {-0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f},
        {SIZE - 0.5f, -0.5f, 0.5f, 1.0f, 1.0f, 0.0f},
        {-0.5f, SIZE - 0.5f, 0.5f, 1.0f, 0.0f, 1.0f},
        {SIZE - 0.5f, SIZE - 0.5f, 0.5f, 1.0f, 1.0f, 1.0f},
    };
    IDirect3DTexture9 *texture;
    HRESULT hr;

    texture = CreateQuadrantTexture();
    if (!texture)
        return;

    Clear(0xFF000000);
    IDirect3DDevice9_SetTexture(Device, 0, (IDirect3DBaseTexture9 *)texture);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_TEX1);
    hr = IDirect3DDevice9_BeginScene(Device);
    ok(hr == D3D_OK, "BeginScene failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
    ok(hr == D3D_OK, "DrawPrimitiveUP failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_EndScene(Device);
    ok(hr == D3D_OK, "EndScene failed: 0x%08lx\n", hr);
    IDirect3DDevice9_SetTexture(Device, 0, NULL);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DTexture9_Release(texture);

    if (!ReadPixels())
        return;
    ok(CountPixelsNear(0xFF0000, 1) == SIZE * SIZE / 4, "red quadrant has %u pixels\n", CountPixelsNear(0xFF0000, 1));
    ok(CountPixelsNear(0x00FF00, 1) == SIZE * SIZE / 4, "green quadrant has %u pixels\n", CountPixelsNear(0x00FF00, 1));
    ok(CountPixelsNear(0x0000FF, 1) == SIZE * SIZE / 4, "blue quadrant has %u pixels\n", CountPixelsNear(0x0000FF, 1));
    ok(CountPixelsNear(0xFFFFFF, 1) == SIZE * SIZE / 4, "white quadrant has %u pixels\n", CountPixelsNear(0xFFFFFF, 1));
    ok(ColorNear(Pixel(4, 4), 0xFF0000, 1) && ColorNear(Pixel(SIZE - 4, 4), 0x00FF00, 1) &&
       ColorNear(Pixel(4, SIZE - 4), 0x0000FF, 1) && ColorNear(Pixel(SIZE - 4, SIZE - 4), 0xFFFFFF, 1),
       "quadrants 0x%06lx 0x%06lx 0x%06lx 0x%06lx\n", Pixel(4, 4), Pixel(SIZE - 4, 4), Pixel(4, SIZE - 4),
       Pixel(SIZE - 4, SIZE - 4));
}

static void Test_Depth(void)
{
    Clear(0xFF000000);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZFUNC, D3DCMP_LESS);
    DrawColorQuad(0.0f, 0.0f, SIZE * 0.75f, SIZE * 0.75f, 0.25f, 0xFFFF0000);
    DrawColorQuad(SIZE * 0.25f, SIZE * 0.25f, SIZE, SIZE, 0.75f, 0xFF0000FF);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);

    if (!ReadPixels())
        return;
    ok(Pixel(SIZE / 2, SIZE / 2) == 0xFF0000, "nearer quad wins the overlap: 0x%06lx\n", Pixel(SIZE / 2, SIZE / 2));
    ok(Pixel(SIZE - 8, SIZE - 8) == 0x0000FF, "farther quad outside the overlap: 0x%06lx\n", Pixel(SIZE - 8, SIZE - 8));
    ok(Pixel(SIZE - 8, 8) == 0x000000, "uncovered corner: 0x%06lx\n", Pixel(SIZE - 8, 8));
}

static void Test_Shaders(const D3DCAPS9 *caps)
{
    static const SHADER_VERTEX quad[] =
    {
        {-1.0f, 1.0f, 0.5f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.5f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
        {1.0f, -1.0f, 0.5f, 1.0f, 1.0f},
    };
    static const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    static const float halfGreen[4] = {1.0f, 0.5f, 1.0f, 1.0f};
    IDirect3DVertexShader9 *vertexShader = NULL;
    IDirect3DPixelShader9 *constantShader = NULL;
    IDirect3DPixelShader9 *modulateShader = NULL;
    IDirect3DTexture9 *texture;
    HRESULT hr;

    if (caps->VertexShaderVersion < D3DVS_VERSION(2, 0) || caps->PixelShaderVersion < D3DPS_VERSION(2, 0))
    {
        skip("Shader model 2 is not available: VS 0x%08lx, PS 0x%08lx\n", caps->VertexShaderVersion,
             caps->PixelShaderVersion);
        return;
    }

    hr = IDirect3DDevice9_CreateVertexShader(Device, ClipVertexShader, &vertexShader);
    ok(hr == D3D_OK, "CreateVertexShader failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_CreatePixelShader(Device, ConstantPixelShader, &constantShader);
    ok(hr == D3D_OK, "CreatePixelShader failed: 0x%08lx\n", hr);
    hr = IDirect3DDevice9_CreatePixelShader(Device, ModulatePixelShader, &modulateShader);
    ok(hr == D3D_OK, "CreatePixelShader failed: 0x%08lx\n", hr);
    texture = CreateQuadrantTexture();
    if (!vertexShader || !constantShader || !modulateShader || !texture)
        goto cleanup;

    IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZ | D3DFVF_TEX1);
    IDirect3DDevice9_SetVertexShader(Device, vertexShader);
    IDirect3DDevice9_SetPixelShader(Device, constantShader);
    IDirect3DDevice9_SetPixelShaderConstantF(Device, 0, magenta, 1);
    Clear(0xFF000000);
    IDirect3DDevice9_BeginScene(Device);
    hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
    ok(hr == D3D_OK, "constant shader draw failed: 0x%08lx\n", hr);
    IDirect3DDevice9_EndScene(Device);
    if (ReadPixels())
        ok(CountPixelsNear(0xFF00FF, 0) == SIZE * SIZE, "constant shader: pixel 0 is 0x%06lx\n", Pixel(0, 0));

    IDirect3DDevice9_SetPixelShader(Device, modulateShader);
    IDirect3DDevice9_SetPixelShaderConstantF(Device, 0, halfGreen, 1);
    IDirect3DDevice9_SetTexture(Device, 0, (IDirect3DBaseTexture9 *)texture);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    Clear(0xFF000000);
    IDirect3DDevice9_BeginScene(Device);
    hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
    ok(hr == D3D_OK, "texture shader draw failed: 0x%08lx\n", hr);
    IDirect3DDevice9_EndScene(Device);
    IDirect3DDevice9_SetTexture(Device, 0, NULL);
    if (ReadPixels())
    {
        ok(ColorNear(Pixel(4, 4), 0xFF0000, 1), "red quadrant: 0x%06lx\n", Pixel(4, 4));
        ok(ColorNear(Pixel(SIZE - 4, 4), 0x008000, 1), "green quadrant at half: 0x%06lx\n", Pixel(SIZE - 4, 4));
        ok(ColorNear(Pixel(4, SIZE - 4), 0x0000FF, 1), "blue quadrant: 0x%06lx\n", Pixel(4, SIZE - 4));
        ok(ColorNear(Pixel(SIZE - 4, SIZE - 4), 0xFF80FF, 1), "white quadrant at half green: 0x%06lx\n",
           Pixel(SIZE - 4, SIZE - 4));
    }

cleanup:
    IDirect3DDevice9_SetVertexShader(Device, NULL);
    IDirect3DDevice9_SetPixelShader(Device, NULL);
    if (texture)
        IDirect3DTexture9_Release(texture);
    if (modulateShader)
        IDirect3DPixelShader9_Release(modulateShader);
    if (constantShader)
        IDirect3DPixelShader9_Release(constantShader);
    if (vertexShader)
        IDirect3DVertexShader9_Release(vertexShader);
}

static void Test_Frames(void)
{
    IDirect3DSurface9 *backBuffer = NULL;
    unsigned int frame;
    HRESULT hr = D3D_OK;

    hr = IDirect3DDevice9_GetBackBuffer(Device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    ok(hr == D3D_OK, "GetBackBuffer failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    IDirect3DDevice9_SetRenderTarget(Device, 0, backBuffer);

    for (frame = 0; frame < 120 && SUCCEEDED(hr); frame++)
    {
        Clear((frame & 1) ? 0xFFFF8000 : 0xFF008000);
        DrawColorQuad(SIZE * 0.25f, SIZE * 0.25f, SIZE * 0.75f, SIZE * 0.75f, 0.5f, 0xFFFFFFFF);
        hr = IDirect3DDevice9_Present(Device, NULL, NULL, NULL, NULL);
    }
    ok(hr == D3D_OK, "Present failed at frame %u: 0x%08lx\n", frame, hr);

    IDirect3DDevice9_SetRenderTarget(Device, 0, RenderTarget);
    IDirect3DSurface9_Release(backBuffer);
}

static void Test_ScreenPresent(HWND window)
{
    IDirect3DSurface9 *backBuffer = NULL;
    HDC screen = GetDC(NULL);
    COLORREF corner = CLR_INVALID, center = CLR_INVALID;
    unsigned int attempt;
    HRESULT hr;

    hr = IDirect3DDevice9_GetBackBuffer(Device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    ok(hr == D3D_OK, "GetBackBuffer failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto cleanup;
    IDirect3DDevice9_SetRenderTarget(Device, 0, backBuffer);
    SetForegroundWindow(window);
    Clear(0xFF0080FF);
    DrawColorQuad(SIZE * 0.25f, SIZE * 0.25f, SIZE * 0.75f, SIZE * 0.75f, 0.5f, 0xFFFF8000);
    hr = IDirect3DDevice9_Present(Device, NULL, NULL, NULL, NULL);
    ok(hr == D3D_OK, "Present failed: 0x%08lx\n", hr);
    IDirect3DDevice9_SetRenderTarget(Device, 0, RenderTarget);
    IDirect3DSurface9_Release(backBuffer);

    for (attempt = 0; attempt < 50; attempt++)
    {
        corner = GetPixel(screen, 8, 8);
        center = GetPixel(screen, SIZE / 2, SIZE / 2);
        if (corner == RGB(0x00, 0x80, 0xFF) && center == RGB(0xFF, 0x80, 0x00))
            break;
        Sleep(20);
    }
    ok(corner == RGB(0x00, 0x80, 0xFF), "screen corner: 0x%06lx\n", corner);
    ok(center == RGB(0xFF, 0x80, 0x00), "screen center: 0x%06lx\n", center);

cleanup:
    ReleaseDC(NULL, screen);
}

START_TEST(hal_render)
{
    D3DPRESENT_PARAMETERS params = {0};
    D3DADAPTER_IDENTIFIER9 identifier = {0};
    D3DCAPS9 caps;
    IDirect3D9 *d3d;
    HWND window;
    HRESULT hr;

    window = CreateWindowA("static", "hal_render", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window)
        return;

    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    ok(d3d != NULL, "Direct3DCreate9 failed\n");
    if (!d3d)
        goto cleanup;

    hr = IDirect3D9_GetAdapterIdentifier(d3d, D3DADAPTER_DEFAULT, 0, &identifier);
    ok(hr == D3D_OK, "GetAdapterIdentifier failed: 0x%08lx\n", hr);
    hr = IDirect3D9_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    ok(hr == D3D_OK, "GetDeviceCaps failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto cleanup;
    trace("Adapter: %s, %04lx:%04lx; VS 0x%08lx, PS 0x%08lx\n", identifier.Description, identifier.VendorId,
          identifier.DeviceId, caps.VertexShaderVersion, caps.PixelShaderVersion);

    params.BackBufferWidth = SIZE;
    params.BackBufferHeight = SIZE;
    params.BackBufferFormat = D3DFMT_X8R8G8B8;
    params.BackBufferCount = 1;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = window;
    params.Windowed = TRUE;
    params.EnableAutoDepthStencil = TRUE;
    params.AutoDepthStencilFormat = D3DFMT_D24S8;
    params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &Device);
    ok(hr == D3D_OK, "CreateDevice failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto cleanup;

    hr = IDirect3DDevice9_CreateRenderTarget(Device, SIZE, SIZE, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                             &RenderTarget, NULL);
    ok(hr == D3D_OK, "CreateRenderTarget failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto cleanup;
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(Device, SIZE, SIZE, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                      &Readback, NULL);
    ok(hr == D3D_OK, "CreateOffscreenPlainSurface failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto cleanup;

    IDirect3DDevice9_SetRenderTarget(Device, 0, RenderTarget);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);

    Test_Clear();
    Test_Triangle();
    Test_Texture();
    Test_Depth();
    Test_Shaders(&caps);
    Test_Frames();
    Test_ScreenPresent(window);

cleanup:
    if (Readback)
        IDirect3DSurface9_Release(Readback);
    if (RenderTarget)
        IDirect3DSurface9_Release(RenderTarget);
    if (Device)
        ok(IDirect3DDevice9_Release(Device) == 0, "device still referenced\n");
    if (d3d)
        IDirect3D9_Release(d3d);
    DestroyWindow(window);
}
