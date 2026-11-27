/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDraw::EnumDisplayModes rejects a missing callback, unknown flags and a bad description size
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

static HRESULT WINAPI CountMode(DDSURFACEDESC *desc, void *context)
{
    ++*(ULONG *)context;
    return DDENUMRET_OK;
}

static HRESULT WINAPI CountMode2(DDSURFACEDESC2 *desc, void *context)
{
    ++*(ULONG *)context;
    return DDENUMRET_OK;
}

START_TEST(EnumDisplayModes)
{
    IDirectDraw7 *ddraw7 = NULL;
    IDirectDraw *ddraw = NULL;
    DDSURFACEDESC2 desc2;
    DDSURFACEDESC desc;
    ULONG count;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (ddraw)
    {
        hr = IDirectDraw_EnumDisplayModes(ddraw, 0, NULL, NULL, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "no callback: %#lx\n", hr);
        count = 0;
        hr = IDirectDraw_EnumDisplayModes(ddraw, 0x10, NULL, &count, CountMode);
        ok(hr == DDERR_INVALIDPARAMS, "unknown flag: %#lx\n", hr);
        ok(count == 0, "unknown flag: %lu modes\n", count);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = 3;
        count = 0;
        hr = IDirectDraw_EnumDisplayModes(ddraw, 0, &desc, &count, CountMode);
        ok(hr == DDERR_INVALIDPARAMS, "description size 3: %#lx\n", hr);
        ok(count == 0, "description size 3: %lu modes\n", count);
        desc.dwSize = sizeof(DDSURFACEDESC2);
        count = 0;
        hr = IDirectDraw_EnumDisplayModes(ddraw, 0, &desc, &count, CountMode);
        ok(hr == DDERR_INVALIDPARAMS, "DDSURFACEDESC2 size: %#lx\n", hr);
        ok(count == 0, "DDSURFACEDESC2 size: %lu modes\n", count);
        count = 0;
        hr = IDirectDraw_EnumDisplayModes(ddraw, DDEDM_REFRESHRATES | DDEDM_STANDARDVGAMODES, NULL, &count, CountMode);
        ok(hr == DD_OK, "both flags: %#lx\n", hr);
        ok(count != 0, "both flags: no mode\n");
        IDirectDraw_Release(ddraw);
    }

    hr = DirectDrawCreateEx(NULL, (void **)&ddraw7, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK, "DirectDrawCreateEx: %#lx\n", hr);
    if (ddraw7)
    {
        hr = IDirectDraw7_EnumDisplayModes(ddraw7, 0, NULL, NULL, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 no callback: %#lx\n", hr);
        count = 0;
        hr = IDirectDraw7_EnumDisplayModes(ddraw7, 0x10, NULL, &count, CountMode2);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 unknown flag: %#lx\n", hr);
        ok(count == 0, "IDirectDraw7 unknown flag: %lu modes\n", count);
        memset(&desc2, 0, sizeof(desc2));
        desc2.dwSize = 3;
        count = 0;
        hr = IDirectDraw7_EnumDisplayModes(ddraw7, 0, &desc2, &count, CountMode2);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 description size 3: %#lx\n", hr);
        ok(count == 0, "IDirectDraw7 description size 3: %lu modes\n", count);
        desc2.dwSize = sizeof(DDSURFACEDESC);
        count = 0;
        hr = IDirectDraw7_EnumDisplayModes(ddraw7, 0, &desc2, &count, CountMode2);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 DDSURFACEDESC size: %#lx\n", hr);
        ok(count == 0, "IDirectDraw7 DDSURFACEDESC size: %lu modes\n", count);
        IDirectDraw7_Release(ddraw7);
    }
}
