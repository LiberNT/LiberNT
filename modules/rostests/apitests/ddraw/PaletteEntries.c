/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirectDrawPalette entry access stays inside the palette and rejects bad ranges and flags
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

START_TEST(PaletteEntries)
{
    PALETTEENTRY entries[300], read[300];
    IDirectDrawPalette *palette = NULL;
    IDirectDraw *ddraw = NULL;
    HRESULT hr;
    HWND window;
    UINT i;

    for (i = 0; i < ARRAYSIZE(entries); i++)
    {
        entries[i].peRed = (BYTE)i;
        entries[i].peGreen = (BYTE)(255 - i);
        entries[i].peBlue = (BYTE)(i * 3);
        entries[i].peFlags = 0;
    }
    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (!ddraw)
        return;
    window = CreateWindowW(L"static", L"PaletteEntries", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, NULL, NULL);
    hr = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel: %#lx\n", hr);
    hr = IDirectDraw_CreatePalette(ddraw, DDPCAPS_4BIT, entries, &palette, NULL);
    ok(hr == DD_OK, "CreatePalette: %#lx\n", hr);
    if (palette)
    {
        hr = IDirectDrawPalette_GetEntries(palette, 0, 0, 256, read);
        ok(hr == DDERR_INVALIDPARAMS, "get 256 of 16: %#lx\n", hr);
        hr = IDirectDrawPalette_GetEntries(palette, 1, 0, 1, read);
        ok(hr == DDERR_INVALIDPARAMS, "get with flags: %#lx\n", hr);
        hr = IDirectDrawPalette_GetEntries(palette, 0, 0, 0, read);
        ok(hr == DDERR_INVALIDPARAMS, "get none: %#lx\n", hr);
        hr = IDirectDrawPalette_GetEntries(palette, 0, 255, 2, read);
        ok(hr == DDERR_INVALIDPARAMS, "get past the end: %#lx\n", hr);
        hr = IDirectDrawPalette_SetEntries(palette, 0, 255, 1, &entries[60]);
        ok(hr == DDERR_INVALIDPARAMS, "set past the end: %#lx\n", hr);
        hr = IDirectDrawPalette_SetEntries(palette, 0, 1, 300, entries);
        ok(hr == DDERR_INVALIDPARAMS, "set 300: %#lx\n", hr);
        hr = IDirectDrawPalette_SetEntries(palette, 0, 1, 2, &entries[100]);
        ok(hr == DD_OK, "set two: %#lx\n", hr);
        memset(read, 0xcc, sizeof(read));
        hr = IDirectDrawPalette_GetEntries(palette, 0, 0, 16, read);
        ok(hr == DD_OK, "get 16: %#lx\n", hr);
        ok(read[1].peRed == 100 && read[2].peRed == 101 && read[3].peRed == 3,
           "entries %u %u %u\n", read[1].peRed, read[2].peRed, read[3].peRed);
        IDirectDrawPalette_Release(palette);
    }
    IDirectDraw_Release(ddraw);
    DestroyWindow(window);
}
