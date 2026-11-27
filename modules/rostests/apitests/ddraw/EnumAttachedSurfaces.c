/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDrawSurface*::EnumAttachedSurfaces without a callback fails on every interface version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(EnumAttachedSurfaces)
{
    IDirectDrawSurface *surface = NULL;
    IDirectDrawSurface2 *surface2;
    IDirectDrawSurface3 *surface3;
    IDirectDrawSurface4 *surface4;
    IDirectDrawSurface7 *surface7;
    IDirectDraw *ddraw = NULL;
    DDSURFACEDESC desc;
    HRESULT hr;
    HWND window;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;
    window = CreateWindowW(L"static", L"EnumAttachedSurfaces", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, NULL,
                           NULL);
    hr = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel: %#lx\n", hr);
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = 16;
    desc.dwHeight = 16;
    hr = IDirectDraw_CreateSurface(ddraw, &desc, &surface, NULL);
    ok(hr == DD_OK, "CreateSurface: %#lx\n", hr);
    if (surface)
    {
        hr = IDirectDrawSurface_EnumAttachedSurfaces(surface, NULL, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface: %#lx\n", hr);
        if (SUCCEEDED(IDirectDrawSurface_QueryInterface(surface, &IID_IDirectDrawSurface2, (void **)&surface2)))
        {
            hr = IDirectDrawSurface2_EnumAttachedSurfaces(surface2, NULL, NULL);
            ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface2: %#lx\n", hr);
            IDirectDrawSurface2_Release(surface2);
        }
        if (SUCCEEDED(IDirectDrawSurface_QueryInterface(surface, &IID_IDirectDrawSurface3, (void **)&surface3)))
        {
            hr = IDirectDrawSurface3_EnumAttachedSurfaces(surface3, NULL, NULL);
            ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface3: %#lx\n", hr);
            IDirectDrawSurface3_Release(surface3);
        }
        if (SUCCEEDED(IDirectDrawSurface_QueryInterface(surface, &IID_IDirectDrawSurface4, (void **)&surface4)))
        {
            hr = IDirectDrawSurface4_EnumAttachedSurfaces(surface4, NULL, NULL);
            ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface4: %#lx\n", hr);
            IDirectDrawSurface4_Release(surface4);
        }
        if (SUCCEEDED(IDirectDrawSurface_QueryInterface(surface, &IID_IDirectDrawSurface7, (void **)&surface7)))
        {
            hr = IDirectDrawSurface7_EnumAttachedSurfaces(surface7, NULL, NULL);
            ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface7: %#lx\n", hr);
            IDirectDrawSurface7_Release(surface7);
        }
        IDirectDrawSurface_Release(surface);
    }
    IDirectDraw_Release(ddraw);
    DestroyWindow(window);
}
