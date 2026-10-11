/*
 * PROJECT:     LiberNT Certificates Snap-in
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Module entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <initguid.h>
#include "guids.h"

HINSTANCE certmgr_hInstance;

class CCertMgrModule : public CComModule
{
};

static CCertMgrModule gModule;

HRESULT CertMgr_LoadString(UINT uId, LPOLESTR *ppsz)
{
    WCHAR szBuffer[512];

    *ppsz = NULL;
    if (!LoadStringW(certmgr_hInstance, uId, szBuffer, _countof(szBuffer)))
        return HRESULT_FROM_WIN32(GetLastError());
    return SHStrDupW(szBuffer, ppsz);
}

STDAPI_(BOOL)
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        certmgr_hInstance = hinstDLL;
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
