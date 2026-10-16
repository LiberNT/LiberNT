/*
 * PROJECT:     LiberNT Photo Metadata Handler
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Class factory and module entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <initguid.h>

DEFINE_GUID(CLSID_ImagePropertyHandler, 0xa38b883c, 0x1682, 0x497e, 0x97, 0xb0, 0x0a, 0x3a, 0x9e, 0x80, 0x16, 0x82);

LONG ModuleObjects;
static LONG ModuleLocks;

typedef HRESULT (*CREATE_INSTANCE)(REFIID riid, void **ppv);

typedef struct _CLASS_FACTORY
{
    IClassFactory IClassFactory_iface;
    CREATE_INSTANCE Create;
} CLASS_FACTORY;

static inline CLASS_FACTORY *
impl_from_factory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, CLASS_FACTORY, IClassFactory_iface);
}

static HRESULT WINAPI
factory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
factory_AddRef(IClassFactory *iface)
{
    InterlockedIncrement(&ModuleLocks);
    return 2;
}

static ULONG WINAPI
factory_Release(IClassFactory *iface)
{
    InterlockedDecrement(&ModuleLocks);
    return 1;
}

static HRESULT WINAPI
factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return impl_from_factory(iface)->Create(riid, ppv);
}

static HRESULT WINAPI
factory_LockServer(IClassFactory *iface, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&ModuleLocks);
    else
        InterlockedDecrement(&ModuleLocks);
    return S_OK;
}

static const IClassFactoryVtbl ClassFactoryVtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateInstance,
    factory_LockServer,
};

static CLASS_FACTORY ImagePropertyHandlerFactory = { { &ClassFactoryVtbl }, ImagePropertyHandler_Create };

HRESULT WINAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualCLSID(rclsid, &CLSID_ImagePropertyHandler))
        return IClassFactory_QueryInterface(&ImagePropertyHandlerFactory.IClassFactory_iface, riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI
DllCanUnloadNow(void)
{
    return (ModuleObjects || ModuleLocks) ? S_FALSE : S_OK;
}
