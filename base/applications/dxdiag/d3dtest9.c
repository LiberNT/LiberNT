/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest9.c
 * PURPOSE:         ReactX Direct3D 9 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d9.h>

BOOL D3D9Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    D3DPRESENT_PARAMETERS PresentParameters;
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    IDirect3DDevice9 *Device = NULL;
    IDirect3D9 *Direct3D = NULL;
    RECT ClientRect;
    DWORD Start, Elapsed;
    UINT Count;
    BOOL Passed = FALSE;
    HRESULT hr;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    if (!(Direct3D = Direct3DCreate9(D3D_SDK_VERSION)))
    {
        D3DTestFail(Result, "D3D9", "create-d3d9", E_FAIL);
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
    PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hr = IDirect3D9_CreateDevice(Direct3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &PresentParameters, &Device);
    if (FAILED(hr))
    {
        D3DTestFail(Result, "D3D9", "create-device", hr);
        goto cleanup;
    }

    IDirect3DDevice9_SetRenderState(Device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, PresentParameters.BackBufferWidth, PresentParameters.BackBufferHeight, Result);
        if (FAILED(hr = IDirect3DDevice9_Clear(Device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0)))
        {
            D3DTestFail(Result, "D3D9", "clear", hr);
            goto cleanup;
        }
        if (FAILED(hr = IDirect3DDevice9_BeginScene(Device)))
        {
            D3DTestFail(Result, "D3D9", "begin-scene", hr);
            goto cleanup;
        }
        hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLELIST, Count / 3, Vertices, sizeof(Vertices[0]));
        IDirect3DDevice9_EndScene(Device);
        if (FAILED(hr))
        {
            D3DTestFail(Result, "D3D9", "draw", hr);
            goto cleanup;
        }
        if (FAILED(hr = IDirect3DDevice9_Present(Device, NULL, NULL, NULL, NULL)))
        {
            D3DTestFail(Result, "D3D9", "present", hr);
            goto cleanup;
        }
        Result->Frames++;
    }
    Passed = !Result->Stage;

cleanup:
    if (Device)
        IDirect3DDevice9_Release(Device);
    if (Direct3D)
        IDirect3D9_Release(Direct3D);
    return Passed;
}
