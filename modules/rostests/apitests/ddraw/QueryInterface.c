/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectDraw QueryInterface without an output pointer fails instead of writing through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(QueryInterface)
{
    IDirectDraw7 *ddraw7 = NULL;
    IDirectDraw *ddraw = NULL;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (ddraw)
    {
        hr = IDirectDraw_QueryInterface(ddraw, &IID_IDirectDraw7, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw: %#lx\n", hr);
        IDirectDraw_Release(ddraw);
    }

    hr = DirectDrawCreateEx(NULL, (void **)&ddraw7, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK, "DirectDrawCreateEx: %#lx\n", hr);
    if (ddraw7)
    {
        hr = IDirectDraw7_QueryInterface(ddraw7, &IID_IDirectDraw7, NULL);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7: %#lx\n", hr);
        IDirectDraw7_Release(ddraw7);
    }
}
