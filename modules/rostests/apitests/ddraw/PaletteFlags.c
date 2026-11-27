/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDraw::CreatePalette accepts only consistent palette capability combinations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(PaletteFlags)
{
    static const struct
    {
        DWORD flags;
        HRESULT expected;
    }
    tests[] =
    {
        { DDPCAPS_1BIT, DD_OK },
        { DDPCAPS_2BIT, DD_OK },
        { DDPCAPS_4BIT, DD_OK },
        { DDPCAPS_8BIT, DD_OK },
        { DDPCAPS_1BIT | DDPCAPS_8BITENTRIES, DD_OK },
        { DDPCAPS_4BIT | DDPCAPS_8BITENTRIES, DD_OK },
        { DDPCAPS_8BIT | DDPCAPS_ALLOW256, DD_OK },
        { DDPCAPS_4BIT | DDPCAPS_ALPHA, DD_OK },
        { DDPCAPS_8BIT | DDPCAPS_ALPHA, DD_OK },
        { DDPCAPS_8BIT | DDPCAPS_8BITENTRIES, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BITENTRIES | DDPCAPS_ALPHA, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_8BITENTRIES | DDPCAPS_ALPHA, DDERR_INVALIDPARAMS },
        { DDPCAPS_4BIT | DDPCAPS_8BITENTRIES | DDPCAPS_ALPHA, DDERR_INVALIDPARAMS },
        { DDPCAPS_4BIT | DDPCAPS_ALLOW256, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACE, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACE | DDPCAPS_ALLOW256, DDERR_INVALIDPARAMS },
        { DDPCAPS_4BIT | DDPCAPS_PRIMARYSURFACE, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACELEFT, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_VSYNC, DDERR_INVALIDPARAMS },
        { DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACE | DDPCAPS_ALPHA, DDERR_INVALIDPARAMS },
    };
    IDirectDrawPalette *palette;
    PALETTEENTRY entries[256];
    IDirectDraw *ddraw = NULL;
    HRESULT hr;
    HWND window;
    UINT i;

    memset(entries, 0, sizeof(entries));
    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;
    window = CreateWindowW(L"static", L"PaletteFlags", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, NULL, NULL);
    hr = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel: %#lx\n", hr);

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        palette = NULL;
        hr = IDirectDraw_CreatePalette(ddraw, tests[i].flags, entries, &palette, NULL);
        ok(hr == tests[i].expected, "flags %#lx: %#lx\n", tests[i].flags, hr);
        if (palette)
            IDirectDrawPalette_Release(palette);
    }

    IDirectDraw_Release(ddraw);
    DestroyWindow(window);
}
