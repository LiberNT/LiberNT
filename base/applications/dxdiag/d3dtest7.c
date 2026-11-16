/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest7.c
 * PURPOSE:         ReactX Direct3D 7 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d.h>

#define D3D7_CHECK(stage, expression) \
    do \
    { \
        hr = (expression); \
        if (FAILED(hr)) \
        { \
            D3DTestFail(Result, "D3D7", stage, hr); \
            goto cleanup; \
        } \
    } while (0)

BOOL D3D7Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    IDirect3DDevice7 *Device = NULL;
    IDirectDrawSurface7 *Primary = NULL;
    IDirectDrawSurface7 *Target = NULL;
    IDirectDrawClipper *Clipper = NULL;
    IDirectDraw7 *DirectDraw = NULL;
    IDirect3D7 *Direct3D = NULL;
    DDSURFACEDESC2 SurfaceDesc;
    D3DVIEWPORT7 Viewport;
    RECT ClientRect;
    RECT ScreenRect;
    DWORD Start, Elapsed;
    UINT Width, Height, Count;
    BOOL Passed = FALSE;
    HRESULT hr;

    D3D7_CHECK("create-ddraw", DirectDrawCreateEx(lpDevice, (void **)&DirectDraw, &IID_IDirectDraw7, NULL));
    D3D7_CHECK("set-cooperative-level", IDirectDraw7_SetCooperativeLevel(DirectDraw, hWnd, DDSCL_NORMAL));
    D3D7_CHECK("query-d3d7", IDirectDraw7_QueryInterface(DirectDraw, &IID_IDirect3D7, (void **)&Direct3D));

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    D3D7_CHECK("create-primary", IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Primary, NULL));

    D3D7_CHECK("create-clipper", IDirectDraw7_CreateClipper(DirectDraw, 0, &Clipper, NULL));
    D3D7_CHECK("set-clipper-window", IDirectDrawClipper_SetHWnd(Clipper, 0, hWnd));
    D3D7_CHECK("set-primary-clipper", IDirectDrawSurface7_SetClipper(Primary, Clipper));

    GetClientRect(hWnd, &ClientRect);
    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    Width = ClientRect.right - ClientRect.left;
    Height = ClientRect.bottom - ClientRect.top;
    SurfaceDesc.dwWidth = Width;
    SurfaceDesc.dwHeight = Height;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    D3D7_CHECK("create-target", IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Target, NULL));

    hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DHALDevice, Target, &Device);
    if (FAILED(hr) && !BootMode)
        hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DRGBDevice, Target, &Device);
    if (FAILED(hr))
    {
        D3DTestFail(Result, "D3D7", "create-device", hr);
        goto cleanup;
    }

    ZeroMemory(&Viewport, sizeof(Viewport));
    Viewport.dwWidth = SurfaceDesc.dwWidth;
    Viewport.dwHeight = SurfaceDesc.dwHeight;
    Viewport.dvMaxZ = 1.0f;
    D3D7_CHECK("set-viewport", IDirect3DDevice7_SetViewport(Device, &Viewport));
    D3D7_CHECK("disable-lighting", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_LIGHTING, FALSE));
    D3D7_CHECK("disable-z", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_ZENABLE, FALSE));
    D3D7_CHECK("disable-culling", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE));

    D3D7_CHECK("set-color-op", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    D3D7_CHECK("set-color-arg1", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    D3D7_CHECK("disable-stage-1", IDirect3DDevice7_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE));

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, Width, Height, Result);
        D3D7_CHECK("clear", IDirect3DDevice7_Clear(Device, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0));
        D3D7_CHECK("begin-scene", IDirect3DDevice7_BeginScene(Device));
        hr = IDirect3DDevice7_DrawPrimitive(Device, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, Vertices, Count, 0);
        if (FAILED(hr))
        {
            D3DTestFail(Result, "D3D7", "draw", hr);
            IDirect3DDevice7_EndScene(Device);
            goto cleanup;
        }
        D3D7_CHECK("end-scene", IDirect3DDevice7_EndScene(Device));

        GetClientRect(hWnd, &ScreenRect);
        MapWindowPoints(hWnd, NULL, (POINT *)&ScreenRect, 2);
        D3D7_CHECK("present", IDirectDrawSurface7_Blt(Primary, &ScreenRect, Target, NULL, DDBLT_WAIT, NULL));
        Result->Frames++;
    }
    Passed = !Result->Stage;

cleanup:
    if (Device)
        IDirect3DDevice7_Release(Device);
    if (Target)
        IDirectDrawSurface7_Release(Target);
    if (Primary)
        IDirectDrawSurface7_Release(Primary);
    if (Clipper)
        IDirectDrawClipper_Release(Clipper);
    if (Direct3D)
        IDirect3D7_Release(Direct3D);
    if (DirectDraw)
        IDirectDraw7_Release(DirectDraw);
    return Passed;
}

#undef D3D7_CHECK
