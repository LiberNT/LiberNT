/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the Direct3D 8 line pattern capability and its effect on drawing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <d3d8.h>

typedef IDirect3D8 *(WINAPI *PDIRECT3DCREATE8)(UINT);

struct line_vertex
{
    float x, y, z, rhw;
    DWORD color;
};

static UINT
DrawPatternedLine(
    _In_ IDirect3DDevice8 *Device,
    _In_ IDirect3DSurface8 *Target,
    _In_ DWORD Pattern)
{
    static const struct line_vertex Line[] =
    {
        {0.5f, 8.5f, 0.0f, 1.0f, 0xffffffff},
        {64.5f, 8.5f, 0.0f, 1.0f, 0xffffffff},
    };
    D3DLOCKED_RECT Locked;
    const DWORD *Row;
    UINT x, Lit = 0;
    HRESULT hr;

    IDirect3DDevice8_Clear(Device, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 0.0f, 0);
    IDirect3DDevice8_SetRenderState(Device, D3DRS_LINEPATTERN, Pattern);
    IDirect3DDevice8_BeginScene(Device);
    hr = IDirect3DDevice8_DrawPrimitiveUP(Device, D3DPT_LINELIST, 1, Line, sizeof(Line[0]));
    ok(hr == D3D_OK, "DrawPrimitiveUP failed, hr %#lx\n", hr);
    IDirect3DDevice8_EndScene(Device);

    hr = IDirect3DSurface8_LockRect(Target, &Locked, NULL, D3DLOCK_READONLY);
    ok(hr == D3D_OK, "LockRect failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return 0;
    Row = (const DWORD *)((const BYTE *)Locked.pBits + 8 * Locked.Pitch);
    for (x = 0; x < 64; x++)
    {
        if (Row[x] & 0x00ffffff)
            Lit++;
    }
    IDirect3DSurface8_UnlockRect(Target);
    return Lit;
}

START_TEST(LinePattern)
{
    D3DLINEPATTERN Pattern = {1, 0x00ff};
    D3DPRESENT_PARAMETERS Present;
    IDirect3DSurface8 *Target;
    IDirect3DDevice8 *Device;
    PDIRECT3DCREATE8 Create;
    D3DDISPLAYMODE Mode;
    IDirect3D8 *D3d;
    HMODULE Module;
    D3DCAPS8 Caps;
    UINT Solid, Patterned;
    HWND Window;
    HRESULT hr;

    Module = LoadLibraryW(L"d3d8.dll");
    if (!Module)
    {
        skip("d3d8.dll is unavailable\n");
        return;
    }
    Create = (PDIRECT3DCREATE8)GetProcAddress(Module, "Direct3DCreate8");
    D3d = Create ? Create(D3D_SDK_VERSION) : NULL;
    ok(D3d != NULL, "Direct3DCreate8 failed\n");
    if (!D3d)
    {
        FreeLibrary(Module);
        return;
    }

    hr = IDirect3D8_GetDeviceCaps(D3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &Caps);
    ok(hr == D3D_OK, "GetDeviceCaps failed, hr %#lx\n", hr);
    ok(!(Caps.RasterCaps & D3DPRASTERCAPS_PAT), "RasterCaps %#lx advertises line patterns\n", Caps.RasterCaps);

    Window = CreateWindowExW(0, L"static", L"d3d8 line pattern", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                             NULL, NULL, NULL, NULL);
    IDirect3D8_GetAdapterDisplayMode(D3d, D3DADAPTER_DEFAULT, &Mode);
    ZeroMemory(&Present, sizeof(Present));
    Present.Windowed = TRUE;
    Present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Present.BackBufferFormat = Mode.Format;
    Present.hDeviceWindow = Window;
    hr = IDirect3D8_CreateDevice(D3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &Present, &Device);
    if (FAILED(hr))
    {
        skip("CreateDevice failed, hr %#lx\n", hr);
        DestroyWindow(Window);
        IDirect3D8_Release(D3d);
        FreeLibrary(Module);
        return;
    }

    hr = IDirect3DDevice8_CreateRenderTarget(Device, 128, 16, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, TRUE, &Target);
    ok(hr == D3D_OK, "CreateRenderTarget failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IDirect3DDevice8_SetRenderTarget(Device, Target, NULL);
        IDirect3DDevice8_SetRenderState(Device, D3DRS_LIGHTING, FALSE);
        IDirect3DDevice8_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);
        IDirect3DDevice8_SetVertexShader(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        Solid = DrawPatternedLine(Device, Target, 0);
        Patterned = DrawPatternedLine(Device, Target, *(DWORD *)&Pattern);
        ok(Solid >= 60, "Solid line lit %u pixels\n", Solid);
        ok(Patterned == Solid, "Line pattern changed the line: %u of %u pixels lit\n", Patterned, Solid);
        IDirect3DSurface8_Release(Target);
    }

    IDirect3DDevice8_Release(Device);
    DestroyWindow(Window);
    IDirect3D8_Release(D3d);
    FreeLibrary(Module);
}
