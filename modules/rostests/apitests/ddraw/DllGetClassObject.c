/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ddraw DllGetClassObject and its factory refuse bad outputs and interfaces cleanly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <wine/test.h>

typedef HRESULT (WINAPI *PFN_GETCLASSOBJECT)(REFCLSID, REFIID, void **);

START_TEST(DllGetClassObject)
{
    PFN_GETCLASSOBJECT get_class;
    IClassFactory *factory;
    void *object;
    HRESULT hr;

    get_class = (PFN_GETCLASSOBJECT)GetProcAddress(GetModuleHandleW(L"ddraw.dll"), "DllGetClassObject");
    ok(get_class != NULL, "no DllGetClassObject\n");
    if (!get_class)
        return;

    hr = get_class(&CLSID_DirectDraw, &IID_IClassFactory, NULL);
    ok(hr == E_INVALIDARG, "no output: %#lx\n", hr);
    object = (void *)(ULONG_PTR)1;
    hr = get_class(&CLSID_DirectDraw, &IID_IDirectDraw, &object);
    ok(hr == E_NOINTERFACE, "IDirectDraw: %#lx\n", hr);
    ok(object == NULL, "IDirectDraw: output %p\n", object);
    factory = NULL;
    hr = get_class(&CLSID_DirectDraw, &IID_IClassFactory, (void **)&factory);
    ok(hr == S_OK, "IClassFactory: %#lx\n", hr);
    ok(factory != NULL, "IClassFactory: no factory\n");
    if (factory)
    {
        object = (void *)(ULONG_PTR)1;
        hr = IClassFactory_CreateInstance(factory, NULL, &IID_IDirectDrawClipper, &object);
        ok(hr == E_NOINTERFACE, "CreateInstance(IDirectDrawClipper): %#lx\n", hr);
        ok(object == NULL, "CreateInstance(IDirectDrawClipper): output %p\n", object);
        IClassFactory_Release(factory);
    }
}
