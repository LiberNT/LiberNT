/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that windowed primary surface blits reach the window within the frame latency
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <wine/test.h>
#include <windows.h>
#include <ddraw.h>

#define WINDOW_SIZE 256
#define FRAME_COUNT 48
#define FRAME_SURFACES 16
#define FRAME_LATENCY 3

static BYTE FrameRed(UINT Frame)
{
    return (BYTE)(16 + (Frame % FRAME_SURFACES) * 12);
}

static HWND CreateTestWindow(VOID)
{
    WNDCLASSW Class = {0};
    HWND hWnd;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"PrimaryBltLatency";
    RegisterClassW(&Class);
    hWnd = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, NULL, WS_POPUP | WS_VISIBLE,
                           0, 0, WINDOW_SIZE, WINDOW_SIZE, NULL, NULL, Class.hInstance, NULL);
    if (hWnd)
    {
        SetForegroundWindow(hWnd);
        UpdateWindow(hWnd);
    }
    return hWnd;
}

static IDirectDrawSurface7 *CreateSurface(IDirectDraw7 *DirectDraw, DWORD Caps, DWORD Width, DWORD Height)
{
    IDirectDrawSurface7 *Surface = NULL;
    DDSURFACEDESC2 Desc = { sizeof(Desc) };
    HRESULT hr;

    Desc.dwFlags = DDSD_CAPS;
    Desc.ddsCaps.dwCaps = Caps;
    if (Width)
    {
        Desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
        Desc.dwWidth = Width;
        Desc.dwHeight = Height;
    }
    hr = IDirectDraw7_CreateSurface(DirectDraw, &Desc, &Surface, NULL);
    ok(hr == DD_OK, "CreateSurface(%#lx) returned %#lx\n", Caps, hr);
    return SUCCEEDED(hr) ? Surface : NULL;
}

static VOID BlitFrames(HWND hWnd, IDirectDrawSurface7 *Primary, IDirectDrawSurface7 **Frames)
{
    POINT Origin = { 0, 0 };
    RECT Target;
    UINT i, Late = 0, MaxLag = 0;
    HDC hdc;
    HRESULT hr;

    ClientToScreen(hWnd, &Origin);
    SetRect(&Target, Origin.x, Origin.y, Origin.x + WINDOW_SIZE, Origin.y + WINDOW_SIZE);
    hdc = GetDC(hWnd);
    for (i = 0; i < FRAME_COUNT; ++i)
    {
        BYTE Red;
        UINT Lag;

        hr = IDirectDrawSurface7_Blt(Primary, &Target, Frames[i % FRAME_SURFACES], NULL, DDBLT_WAIT, NULL);
        ok(hr == DD_OK, "Frame %u: primary blit returned %#lx\n", i, hr);
        if (i < FRAME_LATENCY)
            continue;
        Red = GetRValue(GetPixel(hdc, WINDOW_SIZE / 2, WINDOW_SIZE / 2));
        for (Lag = 0; Lag <= FRAME_LATENCY && FrameRed(i - Lag) != Red; ++Lag)
            ;
        if (Lag > FRAME_LATENCY)
            Late++;
        else if (Lag > MaxLag)
            MaxLag = Lag;
    }
    ReleaseDC(hWnd, hdc);
    ok(Late == 0, "%u of %u frames showed a colour older than %u blits\n", Late, FRAME_COUNT - FRAME_LATENCY, FRAME_LATENCY);
    trace("Largest lag between the last blit and the window: %u frames\n", MaxLag);
}

static BOOL FillFrames(IDirectDraw7 *DirectDraw, IDirectDrawSurface7 **Frames)
{
    DDPIXELFORMAT Format = { sizeof(Format) };
    UINT i;
    HRESULT hr;

    for (i = 0; i < FRAME_SURFACES; ++i)
    {
        DDBLTFX Fill = { sizeof(Fill) };

        Frames[i] = CreateSurface(DirectDraw, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, WINDOW_SIZE, WINDOW_SIZE);
        if (!Frames[i])
            return FALSE;
        Fill.dwFillColor = (DWORD)FrameRed(i) << 16;
        hr = IDirectDrawSurface7_Blt(Frames[i], NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &Fill);
        ok(hr == DD_OK, "Surface %u: colour fill returned %#lx\n", i, hr);
        if (FAILED(hr))
            return FALSE;
    }

    hr = IDirectDrawSurface7_GetPixelFormat(Frames[0], &Format);
    ok(hr == DD_OK, "GetPixelFormat returned %#lx\n", hr);
    if (SUCCEEDED(hr) && Format.dwRGBBitCount != 32)
        skip("Frame surfaces are %lu bits per pixel\n", Format.dwRGBBitCount);
    return SUCCEEDED(hr) && Format.dwRGBBitCount == 32;
}

static VOID TestPrimaryBltLatency(HWND hWnd, IDirectDraw7 *DirectDraw)
{
    IDirectDrawSurface7 *Frames[FRAME_SURFACES] = {0};
    IDirectDrawSurface7 *Primary;
    IDirectDrawClipper *Clipper = NULL;
    HRESULT hr = E_FAIL;
    UINT i;

    Primary = CreateSurface(DirectDraw, DDSCAPS_PRIMARYSURFACE, 0, 0);
    if (Primary)
    {
        hr = IDirectDraw7_CreateClipper(DirectDraw, 0, &Clipper, NULL);
        ok(hr == DD_OK, "CreateClipper returned %#lx\n", hr);
    }
    if (SUCCEEDED(hr))
    {
        IDirectDrawClipper_SetHWnd(Clipper, 0, hWnd);
        IDirectDrawSurface7_SetClipper(Primary, Clipper);
        if (FillFrames(DirectDraw, Frames))
            BlitFrames(hWnd, Primary, Frames);
    }

    for (i = 0; i < FRAME_SURFACES; ++i)
    {
        if (Frames[i])
            IDirectDrawSurface7_Release(Frames[i]);
    }
    if (Clipper)
        IDirectDrawClipper_Release(Clipper);
    if (Primary)
        IDirectDrawSurface7_Release(Primary);
}

START_TEST(PrimaryBltLatency)
{
    IDirectDraw7 *DirectDraw;
    HWND hWnd;
    HRESULT hr;

    hWnd = CreateTestWindow();
    ok(hWnd != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!hWnd)
        return;

    hr = DirectDrawCreateEx(NULL, (void **)&DirectDraw, &IID_IDirectDraw7, NULL);
    if (FAILED(hr))
    {
        skip("DirectDrawCreateEx returned %#lx\n", hr);
        DestroyWindow(hWnd);
        return;
    }
    hr = IDirectDraw7_SetCooperativeLevel(DirectDraw, hWnd, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel returned %#lx\n", hr);
    if (SUCCEEDED(hr))
        TestPrimaryBltLatency(hWnd, DirectDraw);

    IDirectDraw7_Release(DirectDraw);
    DestroyWindow(hWnd);
}
