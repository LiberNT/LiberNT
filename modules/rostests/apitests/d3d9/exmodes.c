/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirect3D9Ex display mode queries reject a missing or wrongly sized filter or mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <wine/test.h>

typedef HRESULT (WINAPI *PFN_CREATE9EX)(UINT, IDirect3D9Ex **);

START_TEST(exmodes)
{
    D3DDISPLAYMODEFILTER filter = { sizeof(filter), D3DFMT_X8R8G8B8, D3DSCANLINEORDERING_UNKNOWN };
    D3DDISPLAYMODEFILTER short_filter = { 8, D3DFMT_X8R8G8B8, D3DSCANLINEORDERING_UNKNOWN };
    PFN_CREATE9EX create;
    D3DDISPLAYMODEEX mode;
    IDirect3D9Ex *d3d;
    HRESULT hr;
    UINT count;

    create = (PFN_CREATE9EX)GetProcAddress(GetModuleHandleW(L"d3d9.dll"), "Direct3DCreate9Ex");
    ok(create != NULL, "no Direct3DCreate9Ex\n");
    if (!create)
        return;
    hr = create(D3D_SDK_VERSION, &d3d);
    ok(hr == S_OK, "Direct3DCreate9Ex: %#lx\n", hr);
    if (FAILED(hr))
        return;

    count = IDirect3D9Ex_GetAdapterModeCountEx(d3d, D3DADAPTER_DEFAULT, NULL);
    ok(count == 0, "null filter: %u modes\n", count);
    count = IDirect3D9Ex_GetAdapterModeCountEx(d3d, D3DADAPTER_DEFAULT, &short_filter);
    ok(count == 0, "short filter: %u modes\n", count);
    count = IDirect3D9Ex_GetAdapterModeCountEx(d3d, D3DADAPTER_DEFAULT, &filter);
    ok(count != 0, "no X8R8G8B8 mode\n");

    memset(&mode, 0, sizeof(mode));
    mode.Size = sizeof(mode);
    hr = IDirect3D9Ex_EnumAdapterModesEx(d3d, D3DADAPTER_DEFAULT, NULL, 0, &mode);
    ok(hr == D3DERR_INVALIDCALL, "null filter: %#lx\n", hr);
    hr = IDirect3D9Ex_EnumAdapterModesEx(d3d, D3DADAPTER_DEFAULT, &short_filter, 0, &mode);
    ok(hr == D3DERR_INVALIDCALL, "short filter: %#lx\n", hr);
    mode.Size = 16;
    hr = IDirect3D9Ex_EnumAdapterModesEx(d3d, D3DADAPTER_DEFAULT, &filter, 0, &mode);
    ok(hr == D3DERR_INVALIDCALL, "short mode: %#lx\n", hr);
    mode.Size = sizeof(mode);
    hr = IDirect3D9Ex_EnumAdapterModesEx(d3d, D3DADAPTER_DEFAULT, &filter, 0, &mode);
    ok(hr == S_OK, "first mode: %#lx\n", hr);
    ok(mode.Format == D3DFMT_X8R8G8B8, "first mode format %u\n", mode.Format);
    hr = IDirect3D9Ex_EnumAdapterModesEx(d3d, D3DADAPTER_DEFAULT, &filter, count, &mode);
    ok(hr == D3DERR_INVALIDCALL, "mode past the count: %#lx\n", hr);

    IDirect3D9Ex_Release(d3d);
}
