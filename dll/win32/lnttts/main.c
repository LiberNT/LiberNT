/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DLL entry points and class factory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "lnttts.h"

DEFINE_GUID(SPDFID_WaveFormatEx, 0xc31adbae, 0x527f, 0x4ff5, 0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c);
DEFINE_GUID(CLSID_LiberNTTtsEngine, 0x1d9ab4b8, 0x5861, 0x469b, 0x8d, 0x18, 0x9e, 0xe9, 0xa2, 0x3e, 0x5d, 0x2a);

static LONG ServerLocks;

static HRESULT WINAPI
Factory_QueryInterface(
    _In_ IClassFactory *Iface,
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    if (IsEqualIID(Iid, &IID_IUnknown) || IsEqualIID(Iid, &IID_IClassFactory))
    {
        *Object = Iface;
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Factory_AddRef(
    _In_ IClassFactory *Iface)
{
    UNREFERENCED_PARAMETER(Iface);
    return 2;
}

static ULONG WINAPI
Factory_Release(
    _In_ IClassFactory *Iface)
{
    UNREFERENCED_PARAMETER(Iface);
    return 1;
}

static HRESULT WINAPI
Factory_CreateInstance(
    _In_ IClassFactory *Iface,
    _In_opt_ IUnknown *Outer,
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    UNREFERENCED_PARAMETER(Iface);

    *Object = NULL;
    if (Outer)
        return CLASS_E_NOAGGREGATION;
    return EngineCreate(Iid, Object);
}

static HRESULT WINAPI
Factory_LockServer(
    _In_ IClassFactory *Iface,
    _In_ BOOL Lock)
{
    UNREFERENCED_PARAMETER(Iface);

    if (Lock)
        InterlockedIncrement(&ServerLocks);
    else
        InterlockedDecrement(&ServerLocks);
    return S_OK;
}

static const IClassFactoryVtbl FactoryVtbl =
{
    Factory_QueryInterface,
    Factory_AddRef,
    Factory_Release,
    Factory_CreateInstance,
    Factory_LockServer,
};

static IClassFactory Factory = { &FactoryVtbl };

BOOL WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}

HRESULT WINAPI
DllGetClassObject(
    _In_ REFCLSID Clsid,
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    if (!IsEqualCLSID(Clsid, &CLSID_LiberNTTtsEngine))
    {
        *Object = NULL;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return IClassFactory_QueryInterface(&Factory, Iid, Object);
}

HRESULT WINAPI
DllCanUnloadNow(VOID)
{
    return (EngineObjects || ServerLocks) ? S_FALSE : S_OK;
}
