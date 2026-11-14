/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a started runtime keeps mscoree.dll loaded
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <initguid.h>
#include <mscoree.h>

typedef HRESULT (WINAPI *PFN_CorBindToRuntimeEx)(LPCWSTR, LPCWSTR, DWORD, REFCLSID, REFIID, void **);

START_TEST(CorBindToRuntimeEx)
{
    PFN_CorBindToRuntimeEx pCorBindToRuntimeEx;
    ICorRuntimeHost *Host;
    IUnknown *DomainSetup;
    HMODULE Mscoree, Loaded;
    HRESULT hr;

    ok(GetModuleHandleW(L"mscoree.dll") == NULL, "mscoree.dll is already loaded\n");

    Mscoree = LoadLibraryW(L"mscoree.dll");
    ok(Mscoree != NULL, "LoadLibraryW failed with %lu\n", GetLastError());
    if (!Mscoree)
        return;

    pCorBindToRuntimeEx = (PFN_CorBindToRuntimeEx)GetProcAddress(Mscoree, "CorBindToRuntimeEx");
    ok(pCorBindToRuntimeEx != NULL, "CorBindToRuntimeEx is not exported\n");
    if (!pCorBindToRuntimeEx)
    {
        FreeLibrary(Mscoree);
        return;
    }

    hr = pCorBindToRuntimeEx(L"v4.0.30319", NULL, STARTUP_LOADER_OPTIMIZATION_SINGLE_DOMAIN,
                             &CLSID_CorRuntimeHost, &IID_ICorRuntimeHost, (void **)&Host);
    if (FAILED(hr))
    {
        skip("No .NET 4 runtime, CorBindToRuntimeEx returned 0x%lx\n", hr);
        FreeLibrary(Mscoree);
        return;
    }

    hr = ICorRuntimeHost_Start(Host);
    ok_hr(hr, S_OK);

    ok(FreeLibrary(Mscoree), "FreeLibrary failed with %lu\n", GetLastError());
    Loaded = GetModuleHandleW(L"mscoree.dll");
    ok(Loaded == Mscoree, "mscoree.dll was unloaded, GetModuleHandleW returned %p\n", Loaded);
    if (Loaded != Mscoree)
        return;

    DomainSetup = NULL;
    hr = ICorRuntimeHost_CreateDomainSetup(Host, &DomainSetup);
    ok_hr(hr, S_OK);
    ok(DomainSetup != NULL, "CreateDomainSetup returned no object\n");
    if (DomainSetup)
        IUnknown_Release(DomainSetup);

    ICorRuntimeHost_Release(Host);
}
