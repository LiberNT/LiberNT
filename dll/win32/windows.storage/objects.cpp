/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ATL module for the shell storage objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"
#include "CShellTaskScheduler.h"

EXTERN_C HRESULT CMruClassFactory_CreateInstance(REFCLSID rclsid, REFIID riid, void **ppv);

void *operator new(size_t size)
{
    return ::HeapAlloc(::GetProcessHeap(), 0, size);
}

void operator delete(void *ptr)
{
    ::HeapFree(::GetProcessHeap(), 0, ptr);
}

void operator delete(void *ptr, size_t size)
{
    ::HeapFree(::GetProcessHeap(), 0, ptr);
}

class CStorageModule : public CComModule
{
};

static CStorageModule gModule;
static IUnknown *g_pSharedScheduler;

typedef HRESULT (*PFNSTORAGECREATE)(IUnknown *pUnkOuter, REFIID riid, void **ppvObject);

class CStorageClassFactory : public IClassFactory
{
    LONG m_cRefs;
    PFNSTORAGECREATE m_pfnCreate;

public:
    CStorageClassFactory(PFNSTORAGECREATE pfnCreate) : m_cRefs(1), m_pfnCreate(pfnCreate)
    {
        gModule.Lock();
    }

    virtual ~CStorageClassFactory()
    {
        gModule.Unlock();
    }

    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        {
            *ppvObject = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() override
    {
        return InterlockedIncrement(&m_cRefs);
    }

    STDMETHOD_(ULONG, Release)() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }

    STDMETHOD(CreateInstance)(IUnknown *pUnkOuter, REFIID riid, void **ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;
        *ppvObject = NULL;
        return m_pfnCreate(pUnkOuter, riid, ppvObject);
    }

    STDMETHOD(LockServer)(BOOL fLock) override
    {
        if (fLock)
            gModule.Lock();
        else
            gModule.Unlock();
        return S_OK;
    }
};

static HRESULT CreateSharedTaskScheduler(IUnknown *pUnkOuter, REFIID riid, void **ppvObject)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    if (!g_pSharedScheduler)
    {
        CComObject<CShellTaskScheduler> *pScheduler;
        IUnknown *pUnk;
        HRESULT hr = CComObject<CShellTaskScheduler>::CreateInstance(&pScheduler);
        if (FAILED(hr))
            return hr;
        hr = pScheduler->QueryInterface(IID_PPV_ARG(IUnknown, &pUnk));
        if (FAILED(hr))
        {
            delete pScheduler;
            return hr;
        }
        if (InterlockedCompareExchangePointer((PVOID *)&g_pSharedScheduler, pUnk, NULL) != NULL)
            pUnk->Release();
    }
    return g_pSharedScheduler->QueryInterface(riid, ppvObject);
}

static HRESULT CreateInternetFolder(IUnknown *pUnkOuter, REFIID riid, void **ppvObject)
{
    return CInternetFolder_CreateInstance(NULL, pUnkOuter, riid, ppvObject);
}

static HRESULT GetStorageClassObject(PFNSTORAGECREATE pfnCreate, REFIID riid, LPVOID *ppv)
{
    CStorageClassFactory *pFactory = new CStorageClassFactory(pfnCreate);
    if (!pFactory)
        return E_OUTOFMEMORY;
    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

EXTERN_C VOID
WSTORAGE_InitObjects(HINSTANCE hInstance)
{
    WSTORAGE_InitShell(hInstance);
    gModule.Init(NULL, hInstance, NULL);
}

EXTERN_C VOID
WSTORAGE_TermObjects(VOID)
{
    IUnknown *pShared = (IUnknown *)InterlockedExchangePointer((PVOID *)&g_pSharedScheduler, NULL);
    if (pShared)
        pShared->Release();
    WSTORAGE_TermShell();
    gModule.Term();
}

EXTERN_C HRESULT
WSTORAGE_GetObjectsClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    if (IsEqualCLSID(rclsid, CLSID_MruLongList) || IsEqualCLSID(rclsid, CLSID_MruPidlList))
        return CMruClassFactory_CreateInstance(rclsid, riid, ppv);

    if (IsEqualCLSID(rclsid, CLSID_SharedTaskScheduler))
        return GetStorageClassObject(CreateSharedTaskScheduler, riid, ppv);

    if (IsEqualCLSID(rclsid, CLSID_Internet))
        return GetStorageClassObject(CreateInternetFolder, riid, ppv);

    return gModule.DllGetClassObject(rclsid, riid, ppv);
}

EXTERN_C HRESULT
WSTORAGE_UpdateRegistry(BOOL bRegister)
{
    HRESULT hr = gModule.UpdateRegistryFromResource(IDR_STORAGEOBJECTS, bRegister, NULL);
    if (FAILED(hr))
        return hr;
    return bRegister ? gModule.RegisterServer(FALSE) : gModule.UnregisterServer(FALSE);
}

EXTERN_C BOOL
WSTORAGE_ObjectsInUse(VOID)
{
    return gModule.GetLockCount() != 0;
}

EXTERN_C VOID
WSTORAGE_LockModule(VOID)
{
    gModule.Lock();
}

EXTERN_C VOID
WSTORAGE_UnlockModule(VOID)
{
    gModule.Unlock();
}
