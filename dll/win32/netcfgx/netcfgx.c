/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Configuration of network devices
 * FILE:            dll/win32/netcfgx/netcfgx.c
 * PURPOSE:         Network devices installer
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

#include "precomp.h"

#include <olectl.h>


HINSTANCE netcfgx_hInstance;
const GUID CLSID_TcpipConfigNotifyObject      = {0xA907657F, 0x6FDF, 0x11D0, {0x8E, 0xFB, 0x00, 0xC0, 0x4F, 0xD9, 0x12, 0xB2}};

static INTERFACE_TABLE InterfaceTable[] =
{
    {
        &CLSID_CNetCfg,
        INetCfg_Constructor
    },
    {
        &CLSID_TcpipConfigNotifyObject,
        TcpipConfigNotify_Constructor
    },
    {
        NULL,
        NULL
    }
};

BOOL
WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID fImpLoad)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            netcfgx_hInstance = hinstDLL;
            DisableThreadLibraryCalls(netcfgx_hInstance);
            InitCommonControls();
            break;

        default:
            break;
    }

    return TRUE;
}

HRESULT
WINAPI
DllCanUnloadNow(void)
{
    return S_FALSE;
}

STDAPI
DllRegisterServer(void)
{
    return S_OK;
}

STDAPI
DllUnregisterServer(void)
{
    //FIXME
    // implement unregistering services
    //
    return S_OK;
}

STDAPI
DllGetClassObject(
    REFCLSID rclsid,
    REFIID riid,
    LPVOID* ppv)
{
    UINT i;
    HRESULT hres = E_OUTOFMEMORY;
    IClassFactory * pcf = NULL;

    if (!ppv)
        return E_INVALIDARG;

    *ppv = NULL;

    for (i = 0; InterfaceTable[i].riid; i++)
    {
        if (IsEqualIID(InterfaceTable[i].riid, rclsid))
        {
            pcf = IClassFactory_fnConstructor(InterfaceTable[i].lpfnCI, NULL, NULL);
            break;
        }
    }

    if (!pcf)
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    hres = IClassFactory_QueryInterface(pcf, riid, ppv);
    IClassFactory_Release(pcf);

    return hres;
}

DWORD
WINAPI
NetCfgDiagRepairRegistryBindings(
    DWORD dwParam1)
{
    ERR("NetCfgDiagRepairRegistryBindings(%lx)\n", dwParam1);
    return ERROR_SUCCESS;
}
