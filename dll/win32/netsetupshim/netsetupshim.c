/*
 * PROJECT:     LiberNT Network Setup Shim
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hosts CLSID_CNetCfg in front of the network setup engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <netcfgx.h>

typedef HRESULT (WINAPI *PFN_DLLGETCLASSOBJECT)(REFCLSID, REFIID, void **);
typedef HRESULT (WINAPI *PFN_DLLCANUNLOADNOW)(void);

static HMODULE engine_module;

static HMODULE get_engine_module(void)
{
    HMODULE module, previous;

    if (engine_module)
        return engine_module;

    module = LoadLibraryW(L"netcfgx.dll");
    if (!module)
        return NULL;

    previous = InterlockedCompareExchangePointer((void **)&engine_module, module, NULL);
    if (previous)
    {
        FreeLibrary(module);
        return previous;
    }
    return module;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    PFN_DLLGETCLASSOBJECT get_class_object;
    HMODULE module;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (!IsEqualCLSID(rclsid, &CLSID_CNetCfg))
        return CLASS_E_CLASSNOTAVAILABLE;

    module = get_engine_module();
    if (!module)
        return HRESULT_FROM_WIN32(GetLastError());

    get_class_object = (PFN_DLLGETCLASSOBJECT)GetProcAddress(module, "DllGetClassObject");
    if (!get_class_object)
        return CLASS_E_CLASSNOTAVAILABLE;

    return get_class_object(rclsid, riid, ppv);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    PFN_DLLCANUNLOADNOW can_unload_now;

    if (!engine_module)
        return S_OK;

    can_unload_now = (PFN_DLLCANUNLOADNOW)GetProcAddress(engine_module, "DllCanUnloadNow");
    return can_unload_now ? can_unload_now() : S_FALSE;
}
