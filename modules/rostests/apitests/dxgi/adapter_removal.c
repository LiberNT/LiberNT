/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests DXGIDeclareAdapterRemovalSupport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winerror.h>

typedef HRESULT (WINAPI *PFN_DXGIDECLAREADAPTERREMOVALSUPPORT)(void);

START_TEST(adapter_removal)
{
    PFN_DXGIDECLAREADAPTERREMOVALSUPPORT pDXGIDeclareAdapterRemovalSupport;
    HMODULE Dxgi;
    HRESULT hr;

    Dxgi = LoadLibraryW(L"dxgi.dll");
    ok(Dxgi != NULL, "dxgi.dll failed to load, error %lu\n", GetLastError());
    if (!Dxgi)
        return;

    pDXGIDeclareAdapterRemovalSupport = (PFN_DXGIDECLAREADAPTERREMOVALSUPPORT)GetProcAddress(Dxgi, "DXGIDeclareAdapterRemovalSupport");
    ok(pDXGIDeclareAdapterRemovalSupport != NULL, "DXGIDeclareAdapterRemovalSupport is missing\n");
    if (pDXGIDeclareAdapterRemovalSupport)
    {
        hr = pDXGIDeclareAdapterRemovalSupport();
        ok(hr == S_OK, "first call returned %#lx\n", hr);
        hr = pDXGIDeclareAdapterRemovalSupport();
        ok(hr == DXGI_ERROR_ALREADY_EXISTS, "second call returned %#lx\n", hr);
    }

    FreeLibrary(Dxgi);
}
