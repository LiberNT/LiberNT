/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectDraw windowed blit and full screen flip benchmarks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define DDTEST_SQUARE 100
#define DDTEST_MODE_WIDTH 640
#define DDTEST_MODE_HEIGHT 480
#define DDTEST_MODE_BPP 32

typedef struct
{
    IDirectDraw7 *DirectDraw;
    IDirectDrawSurface7 *Primary;
    IDirectDrawSurface7 *Back;
    IDirectDrawClipper *Clipper;
    BOOL Fullscreen;
    BOOL ModeSet;
    RECT Target;
    LONG Width;
    LONG Height;
} DDTEST;

static LONG DDTestBounce(DWORD Elapsed, DWORD PixelsPerSecond, LONG Range)
{
    LONG Position;

    if (Range <= 0)
        return 0;
    Position = (LONG)(((ULONGLONG)Elapsed * PixelsPerSecond / 1000) % (2 * (ULONGLONG)Range));
    return Position > Range ? 2 * Range - Position : Position;
}

static IDirectDrawSurface7 *DDTestCreateSurface(IDirectDraw7 *DirectDraw, DWORD Flags, DWORD Caps, DWORD Width, DWORD Height, DWORD BackBuffers, HRESULT *Status)
{
    IDirectDrawSurface7 *Surface = NULL;
    DDSURFACEDESC2 Desc;

    ZeroMemory(&Desc, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DDSD_CAPS | Flags;
    Desc.ddsCaps.dwCaps = Caps;
    Desc.dwWidth = Width;
    Desc.dwHeight = Height;
    Desc.dwBackBufferCount = BackBuffers;
    *Status = IDirectDraw7_CreateSurface(DirectDraw, &Desc, &Surface, NULL);
    return SUCCEEDED(*Status) ? Surface : NULL;
}

static BOOL DDTestSetupWindowed(DDTEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    POINT Origin = { 0, 0 };
    RECT Client;
    HRESULT hr;

    hr = IDirectDraw7_SetCooperativeLevel(Test->DirectDraw, hWnd, DDSCL_NORMAL);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW", "set-cooperative-level", hr);

    GetClientRect(hWnd, &Client);
    ClientToScreen(hWnd, &Origin);
    Test->Width = Client.right - Client.left;
    Test->Height = Client.bottom - Client.top;
    SetRect(&Test->Target, Origin.x, Origin.y, Origin.x + Test->Width, Origin.y + Test->Height);

    Test->Primary = DDTestCreateSurface(Test->DirectDraw, 0, DDSCAPS_PRIMARYSURFACE, 0, 0, 0, &hr);
    if (!Test->Primary)
        return D3DTestFail(Result, "DDRAW", "create-primary", hr);
    hr = IDirectDraw7_CreateClipper(Test->DirectDraw, 0, &Test->Clipper, NULL);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW", "create-clipper", hr);
    hr = IDirectDrawClipper_SetHWnd(Test->Clipper, 0, hWnd);
    if (SUCCEEDED(hr))
        hr = IDirectDrawSurface7_SetClipper(Test->Primary, Test->Clipper);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW", "set-clipper", hr);

    Test->Back = DDTestCreateSurface(Test->DirectDraw, DDSD_WIDTH | DDSD_HEIGHT, DDSCAPS_OFFSCREENPLAIN, Test->Width, Test->Height, 0, &hr);
    if (!Test->Back)
        return D3DTestFail(Result, "DDRAW", "create-back-buffer", hr);
    return TRUE;
}

static BOOL DDTestSetupFullscreen(DDTEST *Test, HWND hWnd, PD3DTEST_RESULT Result)
{
    DDSCAPS2 Caps = { DDSCAPS_BACKBUFFER };
    HRESULT hr;

    hr = IDirectDraw7_SetCooperativeLevel(Test->DirectDraw, hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW_FULLSCREEN", "set-cooperative-level", hr);
    hr = IDirectDraw7_SetDisplayMode(Test->DirectDraw, DDTEST_MODE_WIDTH, DDTEST_MODE_HEIGHT, DDTEST_MODE_BPP, 0, 0);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW_FULLSCREEN", "set-display-mode", hr);
    Test->ModeSet = TRUE;
    Test->Width = DDTEST_MODE_WIDTH;
    Test->Height = DDTEST_MODE_HEIGHT;

    Test->Primary = DDTestCreateSurface(Test->DirectDraw, DDSD_BACKBUFFERCOUNT, DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX, 0, 0, 1, &hr);
    if (!Test->Primary)
        return D3DTestFail(Result, "DDRAW_FULLSCREEN", "create-flip-chain", hr);
    hr = IDirectDrawSurface7_GetAttachedSurface(Test->Primary, &Caps, &Test->Back);
    if (FAILED(hr))
        return D3DTestFail(Result, "DDRAW_FULLSCREEN", "get-back-buffer", hr);
    return TRUE;
}

static BOOL DDTestFrame(DDTEST *Test, DWORD Elapsed, PCSTR Api, PD3DTEST_RESULT Result)
{
    DDBLTFX Fill;
    RECT Square;
    HDC hdc;
    LONG x, y;
    HRESULT hr;

    ZeroMemory(&Fill, sizeof(Fill));
    Fill.dwSize = sizeof(Fill);
    hr = IDirectDrawSurface7_Blt(Test->Back, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &Fill);
    if (FAILED(hr))
        return D3DTestFail(Result, Api, "clear", hr);

    x = DDTestBounce(Elapsed, 300, Test->Width - DDTEST_SQUARE);
    y = DDTestBounce(Elapsed, 200, Test->Height - DDTEST_SQUARE);
    SetRect(&Square, x, y, x + DDTEST_SQUARE, y + DDTEST_SQUARE);
    hr = IDirectDrawSurface7_GetDC(Test->Back, &hdc);
    if (FAILED(hr))
        return D3DTestFail(Result, Api, "get-dc", hr);
    FillRect(hdc, &Square, (HBRUSH)GetStockObject(WHITE_BRUSH));
    D3DTestDrawStats(hdc, Result, Test->Height);
    IDirectDrawSurface7_ReleaseDC(Test->Back, hdc);

    if (Test->Fullscreen)
        hr = IDirectDrawSurface7_Flip(Test->Primary, NULL, DDFLIP_WAIT);
    else
        hr = IDirectDrawSurface7_Blt(Test->Primary, &Test->Target, Test->Back, NULL, DDBLT_WAIT, NULL);
    if (FAILED(hr))
        return D3DTestFail(Result, Api, Test->Fullscreen ? "flip" : "present", hr);
    return TRUE;
}

static BOOL DDTestRender(DDTEST *Test, PCSTR Api, PD3DTEST_RESULT Result)
{
    DWORD Start, Elapsed;

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        if (!DDTestFrame(Test, Elapsed, Api, Result))
            return FALSE;
        Result->Frames++;
    }
    return !Result->Stage;
}

static VOID DDTestRelease(DDTEST *Test)
{
    if (Test->Back)
        IDirectDrawSurface7_Release(Test->Back);
    if (Test->Primary)
        IDirectDrawSurface7_Release(Test->Primary);
    if (Test->Clipper)
        IDirectDrawClipper_Release(Test->Clipper);
    if (Test->ModeSet)
        IDirectDraw7_RestoreDisplayMode(Test->DirectDraw);
    if (Test->DirectDraw)
        IDirectDraw7_Release(Test->DirectDraw);
}

static BOOL DDTestRun(GUID *lpDevice, HWND hWnd, BOOL Fullscreen, PCSTR Api, PD3DTEST_RESULT Result)
{
    DDTEST Test;
    HRESULT hr;
    BOOL Passed = FALSE;

    ZeroMemory(&Test, sizeof(Test));
    Test.Fullscreen = Fullscreen;
    hr = DirectDrawCreateEx(lpDevice, (void **)&Test.DirectDraw, &IID_IDirectDraw7, NULL);
    if (FAILED(hr))
        D3DTestFail(Result, Api, "create-ddraw", hr);
    else if (Fullscreen ? DDTestSetupFullscreen(&Test, hWnd, Result) : DDTestSetupWindowed(&Test, hWnd, Result))
        Passed = DDTestRender(&Test, Api, Result);
    DDTestRelease(&Test);
    return Passed;
}

BOOL DDWindowedTest(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    UNREFERENCED_PARAMETER(BootMode);
    return DDTestRun(lpDevice, hWnd, FALSE, "DDRAW", Result);
}

BOOL DDFullscreenTest(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    UNREFERENCED_PARAMETER(BootMode);
    return DDTestRun(lpDevice, hWnd, TRUE, "DDRAW_FULLSCREEN", Result);
}
