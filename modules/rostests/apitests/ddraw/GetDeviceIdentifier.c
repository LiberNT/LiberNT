/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectDraw GetDeviceIdentifier rejects a missing output and unknown flags, clearing the identifier
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

static BOOL IsZero(const void *data, SIZE_T size)
{
    const BYTE *bytes = data;
    SIZE_T i;

    for (i = 0; i < size; i++)
        if (bytes[i]) return FALSE;
    return TRUE;
}

START_TEST(GetDeviceIdentifier)
{
    DDDEVICEIDENTIFIER2 identifier2;
    DDDEVICEIDENTIFIER identifier;
    IDirectDraw7 *ddraw7 = NULL;
    IDirectDraw4 *ddraw4 = NULL;
    IDirectDraw *ddraw = NULL;
    HRESULT hr;

    hr = DirectDrawCreate(NULL, &ddraw, NULL);
    ok(hr == DD_OK, "DirectDrawCreate: %#lx\n", hr);
    if (ddraw)
    {
        hr = IDirectDraw_QueryInterface(ddraw, &IID_IDirectDraw4, (void **)&ddraw4);
        ok(hr == DD_OK, "IDirectDraw4: %#lx\n", hr);
        IDirectDraw_Release(ddraw);
    }
    if (ddraw4)
    {
        hr = IDirectDraw4_GetDeviceIdentifier(ddraw4, NULL, 0);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw4 without an output: %#lx\n", hr);
        memset(&identifier, 0xcc, sizeof(identifier));
        hr = IDirectDraw4_GetDeviceIdentifier(ddraw4, &identifier, 2);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw4 flag 2: %#lx\n", hr);
        ok(IsZero(&identifier, sizeof(identifier)), "IDirectDraw4 flag 2: identifier not cleared\n");
        hr = IDirectDraw4_GetDeviceIdentifier(ddraw4, &identifier, 0);
        ok(hr == DD_OK, "IDirectDraw4: %#lx\n", hr);
        ok(identifier.szDescription[0] != 0, "IDirectDraw4: no description\n");
        IDirectDraw4_Release(ddraw4);
    }

    hr = DirectDrawCreateEx(NULL, (void **)&ddraw7, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK, "DirectDrawCreateEx: %#lx\n", hr);
    if (ddraw7)
    {
        hr = IDirectDraw7_GetDeviceIdentifier(ddraw7, NULL, 0);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 without an output: %#lx\n", hr);
        memset(&identifier2, 0xcc, sizeof(identifier2));
        hr = IDirectDraw7_GetDeviceIdentifier(ddraw7, &identifier2, 2);
        ok(hr == DDERR_INVALIDPARAMS, "IDirectDraw7 flag 2: %#lx\n", hr);
        ok(IsZero(&identifier2, FIELD_OFFSET(DDDEVICEIDENTIFIER2, dwWHQLLevel)), "IDirectDraw7 flag 2: identifier not cleared\n");
        hr = IDirectDraw7_GetDeviceIdentifier(ddraw7, &identifier2, 0);
        ok(hr == DD_OK, "IDirectDraw7: %#lx\n", hr);
        ok(identifier2.szDescription[0] != 0, "IDirectDraw7: no description\n");
        IDirectDraw7_Release(ddraw7);
    }
}
