/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDraw2::GetAvailableVidMem rejects missing capabilities or outputs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(AvailableVidMem)
{
    IDirectDraw2 *ddraw2 = NULL;
    IDirectDraw *ddraw = NULL;
    DWORD total, free;
    DDSCAPS caps;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;
    hr = IDirectDraw_QueryInterface(ddraw, &IID_IDirectDraw2, (void **)&ddraw2);
    ok(hr == DD_OK, "IDirectDraw2: %#lx\n", hr);
    IDirectDraw_Release(ddraw);
    if (!ddraw2)
        return;

    hr = IDirectDraw2_GetAvailableVidMem(ddraw2, NULL, &total, &free);
    ok(hr == DDERR_INVALIDPARAMS, "no capabilities: %#lx\n", hr);
    memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw2_GetAvailableVidMem(ddraw2, &caps, NULL, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "no outputs: %#lx\n", hr);
    total = free = 0xcccccccc;
    hr = IDirectDraw2_GetAvailableVidMem(ddraw2, &caps, &total, &free);
    ok(hr == DD_OK, "video memory: %#lx\n", hr);
    ok(total != 0xcccccccc && total != 0, "video memory total %#lx\n", total);
    ok(free != 0xcccccccc, "video memory free not written\n");

    IDirectDraw2_Release(ddraw2);
}
