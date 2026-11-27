/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectDraw monitor frequency and vertical blank waits reject bad arguments
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(VerticalBlank)
{
    IDirectDraw *ddraw = NULL;
    DWORD frequency;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;

    hr = IDirectDraw_GetMonitorFrequency(ddraw, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "frequency without an output: %#lx\n", hr);
    frequency = 0xcccccccc;
    hr = IDirectDraw_GetMonitorFrequency(ddraw, &frequency);
    ok(hr == DD_OK, "frequency: %#lx\n", hr);
    ok(frequency != 0xcccccccc, "frequency not written\n");
    hr = IDirectDraw_WaitForVerticalBlank(ddraw, 0x10, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "unknown wait flag: %#lx\n", hr);
    hr = IDirectDraw_WaitForVerticalBlank(ddraw, DDWAITVB_BLOCKBEGINEVENT, NULL);
    ok(hr == DDERR_UNSUPPORTED, "wait with an event: %#lx\n", hr);
    hr = IDirectDraw_WaitForVerticalBlank(ddraw, DDWAITVB_BLOCKBEGIN, NULL);
    ok(hr == DD_OK, "wait for the blank start: %#lx\n", hr);
    hr = IDirectDraw_WaitForVerticalBlank(ddraw, DDWAITVB_BLOCKEND, NULL);
    ok(hr == DD_OK, "wait for the blank end: %#lx\n", hr);

    IDirectDraw_Release(ddraw);
}
