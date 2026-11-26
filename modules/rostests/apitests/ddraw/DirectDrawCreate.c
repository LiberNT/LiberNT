/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectDrawCreate and DirectDrawCreateEx argument checks leave the output alone on failure
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(DirectDrawCreate)
{
    IUnknown *outer = (IUnknown *)(ULONG_PTR)1;
    IDirectDraw *ddraw;
    void *object;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, NULL, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "null output: %#lx\n", hr);

    ddraw = (IDirectDraw *)(ULONG_PTR)1;
    hr = DirectDrawCreate(NULL, &ddraw, outer);
    ok(hr == CLASS_E_NOAGGREGATION, "aggregation: %#lx\n", hr);
    ok(ddraw == (IDirectDraw *)(ULONG_PTR)1, "aggregation: output %p\n", ddraw);

    object = (void *)(ULONG_PTR)1;
    hr = DirectDrawCreateEx(NULL, &object, &IID_IDirectDraw7, outer);
    ok(hr == CLASS_E_NOAGGREGATION, "Ex aggregation: %#lx\n", hr);
    ok(object == (void *)(ULONG_PTR)1, "Ex aggregation: output %p\n", object);

    object = (void *)(ULONG_PTR)1;
    hr = DirectDrawCreateEx(NULL, &object, &IID_IDirectDraw, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "Ex IDirectDraw: %#lx\n", hr);
    ok(object == (void *)(ULONG_PTR)1, "Ex IDirectDraw: output %p\n", object);

    object = (void *)(ULONG_PTR)1;
    hr = DirectDrawCreateEx(NULL, &object, &IID_IDirectDraw4, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "Ex IDirectDraw4: %#lx\n", hr);
    ok(object == (void *)(ULONG_PTR)1, "Ex IDirectDraw4: output %p\n", object);

    ddraw = NULL;
    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "default device: %#lx\n", hr);
    ok(ddraw != NULL, "default device: no object\n");
    if (ddraw)
        IDirectDraw_Release(ddraw);
}
