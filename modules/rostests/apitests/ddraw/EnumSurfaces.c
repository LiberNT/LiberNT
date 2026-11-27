/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDraw7::EnumSurfaces refuses colour keys and surface memory in a can-be-created description
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

static HRESULT WINAPI CountSurface(IDirectDrawSurface7 *surface, DDSURFACEDESC2 *desc, void *context)
{
    ++*(ULONG *)context;
    if (surface)
        IDirectDrawSurface7_Release(surface);
    return DDENUMRET_OK;
}

START_TEST(EnumSurfaces)
{
    static const struct
    {
        const char *name;
        DWORD flags;
        DWORD caps;
        HRESULT expected;
    }
    tests[] =
    {
        { "caps and size", DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT, DDSCAPS_OFFSCREENPLAIN, DD_OK },
        { "caps", DDSD_CAPS, DDSCAPS_OFFSCREENPLAIN, DD_OK },
        { "size", DDSD_WIDTH | DDSD_HEIGHT, 0, DD_OK },
        { "nothing", 0, 0, DD_OK },
        { "pitch", DDSD_PITCH, 0, DD_OK },
        { "source blit key", DDSD_CKSRCBLT, 0, DDERR_INVALIDPARAMS },
        { "caps and source blit key", DDSD_CAPS | DDSD_CKSRCBLT, DDSCAPS_OFFSCREENPLAIN, DDERR_INVALIDPARAMS },
        { "destination blit key", DDSD_CKDESTBLT, 0, DDERR_INVALIDPARAMS },
        { "source overlay key", DDSD_CKSRCOVERLAY, 0, DDERR_INVALIDPARAMS },
        { "destination overlay key", DDSD_CKDESTOVERLAY, 0, DDERR_INVALIDPARAMS },
        { "surface memory", DDSD_LPSURFACE, 0, DDERR_INVALIDPARAMS },
    };
    IDirectDraw7 *ddraw = NULL;
    DDSURFACEDESC2 desc;
    ULONG count;
    HRESULT hr;
    HWND window;
    UINT i;

    hr = DirectDrawCreateEx(NULL, (void **)&ddraw, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK, "DirectDrawCreateEx: %#lx\n", hr);
    if (!ddraw)
        return;
    window = CreateWindowW(L"static", L"EnumSurfaces", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, NULL, NULL);
    hr = IDirectDraw7_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel: %#lx\n", hr);

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = tests[i].flags;
        desc.ddsCaps.dwCaps = tests[i].caps;
        desc.dwWidth = 8;
        desc.dwHeight = 8;
        count = 0;
        hr = IDirectDraw7_EnumSurfaces(ddraw, DDENUMSURFACES_MATCH | DDENUMSURFACES_CANBECREATED, &desc, &count,
                                       CountSurface);
        ok(hr == tests[i].expected, "%s: %#lx\n", tests[i].name, hr);
        if (FAILED(tests[i].expected))
            ok(count == 0, "%s: %lu surfaces\n", tests[i].name, count);
    }

    IDirectDraw7_Release(ddraw);
    DestroyWindow(window);
}
