/*
 * PROJECT:     LiberNT Printers Folder
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Module entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

HINSTANCE prnfldr_hInstance;

class CPrnfldrModule : public CComModule
{
};

static CPrnfldrModule gModule;

STDAPI_(BOOL)
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        prnfldr_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        gModule.Init(NULL, hinstDLL, NULL);
    }
    else if (fdwReason == DLL_PROCESS_DETACH && !lpvReserved)
    {
        gModule.Term();
    }
    return TRUE;
}

STDAPI
DllCanUnloadNow(VOID)
{
    return gModule.DllCanUnloadNow();
}

STDAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return gModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI
DllRegisterServer(VOID)
{
    return gModule.DllRegisterServer(FALSE);
}

STDAPI
DllUnregisterServer(VOID)
{
    return gModule.DllUnregisterServer(FALSE);
}
