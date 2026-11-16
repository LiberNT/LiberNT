/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest8.c
 * PURPOSE:         ReactX Direct3D 8 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d8.h>

#define D3D8_CHECK(stage, expression) \
    do \
    { \
        hr = (expression); \
        if (FAILED(hr)) \
        { \
            D3DTestFail(Result, "D3D8", stage, hr); \
            goto cleanup; \
        } \
    } while (0)

BOOL D3D8Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3DPRESENT_PARAMETERS PresentParameters;
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    IDirect3DDevice8 *Device = NULL;
    IDirect3D8 *Direct3D = NULL;
    RECT ClientRect;
    DWORD Start, Elapsed;
    UINT Count;
    BOOL Passed = FALSE;
    HRESULT hr;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    if (!(Direct3D = Direct3DCreate8(D3D_SDK_VERSION)))
    {
        D3DTestFail(Result, "D3D8", "create-d3d8", E_FAIL);
        goto cleanup;
    }

    ZeroMemory(&PresentParameters, sizeof(PresentParameters));
    GetClientRect(hWnd, &ClientRect);
    PresentParameters.BackBufferWidth = ClientRect.right - ClientRect.left;
    PresentParameters.BackBufferHeight = ClientRect.bottom - ClientRect.top;
    PresentParameters.BackBufferFormat = D3DFMT_UNKNOWN;
    PresentParameters.BackBufferCount = 1;
    PresentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    PresentParameters.hDeviceWindow = hWnd;
    PresentParameters.Windowed = TRUE;
    PresentParameters.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hr = IDirect3D8_CreateDevice(Direct3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &PresentParameters, &Device);
    if (FAILED(hr))
    {
        D3DTestFail(Result, "D3D8", "create-device", hr);
        goto cleanup;
    }

    D3D8_CHECK("disable-lighting", IDirect3DDevice8_SetRenderState(Device, D3DRS_LIGHTING, FALSE));
    D3D8_CHECK("disable-z", IDirect3DDevice8_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE));
    D3D8_CHECK("disable-culling", IDirect3DDevice8_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE));
    D3D8_CHECK("set-fvf", IDirect3DDevice8_SetVertexShader(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE));

    D3D8_CHECK("set-color-op", IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    D3D8_CHECK("set-color-arg1", IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    D3D8_CHECK("disable-stage-1", IDirect3DDevice8_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE));

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, PresentParameters.BackBufferWidth, PresentParameters.BackBufferHeight, Result);
        D3D8_CHECK("clear", IDirect3DDevice8_Clear(Device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0));
        D3D8_CHECK("begin-scene", IDirect3DDevice8_BeginScene(Device));
        hr = IDirect3DDevice8_DrawPrimitiveUP(Device, D3DPT_TRIANGLELIST, Count / 3, Vertices, sizeof(Vertices[0]));
        if (FAILED(hr))
        {
            D3DTestFail(Result, "D3D8", "draw", hr);
            IDirect3DDevice8_EndScene(Device);
            goto cleanup;
        }
        D3D8_CHECK("end-scene", IDirect3DDevice8_EndScene(Device));
        D3D8_CHECK("present", IDirect3DDevice8_Present(Device, NULL, NULL, NULL, NULL));
        Result->Frames++;
    }
    Passed = !Result->Stage;

cleanup:
    if (Device)
        IDirect3DDevice8_Release(Device);
    if (Direct3D)
        IDirect3D8_Release(Direct3D);
    return Passed;
}

#undef D3D8_CHECK
