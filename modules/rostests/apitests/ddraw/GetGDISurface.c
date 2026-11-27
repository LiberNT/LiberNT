/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDraw*::GetGDISurface without an output pointer fails instead of writing through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(GetGDISurface)
{
    IDirectDraw7 *ddraw7 = NULL;
    IDirectDraw4 *ddraw4 = NULL;
    IDirectDraw2 *ddraw2 = NULL;
    IDirectDraw *ddraw = NULL;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;
    hr = IDirectDraw_GetGDISurface(ddraw, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw: %#lx\n", hr);
    if (SUCCEEDED(IDirectDraw_QueryInterface(ddraw, &IID_IDirectDraw2, (void **)&ddraw2)))
    {
        hr = IDirectDraw2_GetGDISurface(ddraw2, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw2: %#lx\n", hr);
        IDirectDraw2_Release(ddraw2);
    }
    if (SUCCEEDED(IDirectDraw_QueryInterface(ddraw, &IID_IDirectDraw4, (void **)&ddraw4)))
    {
        hr = IDirectDraw4_GetGDISurface(ddraw4, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw4: %#lx\n", hr);
        IDirectDraw4_Release(ddraw4);
    }
    IDirectDraw_Release(ddraw);

    hr = DirectDrawCreateEx(NULL, (void **)&ddraw7, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK, "DirectDrawCreateEx: %#lx\n", hr);
    if (ddraw7)
    {
        hr = IDirectDraw7_GetGDISurface(ddraw7, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7: %#lx\n", hr);
        IDirectDraw7_Release(ddraw7);
    }
}
