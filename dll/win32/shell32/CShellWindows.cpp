/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ShellWindows collection of the shell browser windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <exdispid.h>

static const GUID LIBID_ShellWindowsTypeLib = { 0xeab22ac0, 0x30c1, 0x11cf, { 0xa7, 0xeb, 0x00, 0x00, 0xc0, 0x5b, 0xae, 0x0b } };

class CVariantCopyPolicy
{
public:
    static HRESULT copy(VARIANT *pTo, const VARIANT *pFrom)
    {
        VariantInit(pTo);
        return VariantCopy(pTo, const_cast<VARIANT *>(pFrom));
    }

    static void init(VARIANT *p)
    {
        VariantInit(p);
    }

    static void destroy(VARIANT *p)
    {
        VariantClear(p);
    }
};

typedef CComEnum<IEnumVARIANT, &IID_IEnumVARIANT, VARIANT, CVariantCopyPolicy> CShellWindowsEnum;

struct SHELLWINDOW_ENTRY
{
    LONG Cookie;
    CComPtr<IDispatch> pDispatch;
    LONG hwnd;
    int swClass;
    DWORD ThreadId;
    CComHeapPtr<ITEMIDLIST> pidl;
    CComHeapPtr<ITEMIDLIST> pidlRoot;
    BOOL bPending;
    ULONG Activation;
};

class CShellWindows :
    public CComCoClass<CShellWindows, &CLSID_ShellWindows>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IShellWindows, &IID_IShellWindows, &LIBID_ShellWindowsTypeLib, 1, 1>,
    public IServiceProvider,
    public IConnectionPointContainerImpl<CShellWindows>,
    public IConnectionPointImpl<CShellWindows, &DIID_DShellWindowsEvents>
{
private:
    CAtlList<SHELLWINDOW_ENTRY *> m_Windows;
    LONG m_NextCookie;
    ULONG m_NextActivation;

    SHELLWINDOW_ENTRY *FindByCookie(LONG lCookie, POSITION *pPos = NULL)
    {
        POSITION pos = m_Windows.GetHeadPosition();
        while (pos)
        {
            POSITION cur = pos;
            SHELLWINDOW_ENTRY *pEntry = m_Windows.GetNext(pos);
            if (pEntry->Cookie == lCookie)
            {
                if (pPos)
                    *pPos = cur;
                return pEntry;
            }
        }
        return NULL;
    }

    static HRESULT PidlFromVariant(const VARIANT *pv, PIDLIST_ABSOLUTE *ppidl)
    {
        *ppidl = NULL;
        if (!pv)
            return S_FALSE;

        switch (V_VT(pv))
        {
            case VT_EMPTY:
            case VT_NULL:
                return S_FALSE;
            case VT_BSTR:
                if (!V_BSTR(pv) || !*V_BSTR(pv))
                    return S_FALSE;
                return SHParseDisplayName(V_BSTR(pv), NULL, ppidl, 0, NULL);
            case VT_ARRAY | VT_UI1:
            {
                SAFEARRAY *psa = V_ARRAY(pv);
                if (!psa || SafeArrayGetDim(psa) != 1 || psa->cbElements != 1)
                    return E_INVALIDARG;
                ULONG cb = psa->rgsabound[0].cElements;
                LPCITEMIDLIST pidl = (LPCITEMIDLIST)psa->pvData;
                if (cb < sizeof(USHORT) || ILGetSize(pidl) > cb)
                    return E_INVALIDARG;
                *ppidl = ILClone(pidl);
                return *ppidl ? S_OK : E_OUTOFMEMORY;
            }
            case VT_VARIANT | VT_BYREF:
                return PidlFromVariant(V_VARIANTREF(pv), ppidl);
        }
        return E_INVALIDARG;
    }

    void FireEvent(DISPID dispid, LONG lCookie)
    {
        CComDynamicUnkArray &vec = IConnectionPointImpl<CShellWindows, &DIID_DShellWindowsEvents>::m_vec;
        for (IUnknown **pp = vec.begin(); pp < vec.end(); pp++)
        {
            CComPtr<IDispatch> pDispatch;
            if (!*pp || FAILED((*pp)->QueryInterface(IID_PPV_ARG(IDispatch, &pDispatch))))
                continue;

            VARIANTARG arg;
            V_VT(&arg) = VT_I4;
            V_I4(&arg) = lCookie;
            DISPPARAMS params = { &arg, NULL, 1, 0 };
            pDispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, NULL, NULL, NULL);
        }
    }

    static BOOL IsListed(const SHELLWINDOW_ENTRY *pEntry)
    {
        return !pEntry->bPending && pEntry->pDispatch.p != NULL && pEntry->swClass != SWC_DESKTOP;
    }

    SHELLWINDOW_ENTRY *GetListedAt(LONG index)
    {
        POSITION pos = m_Windows.GetHeadPosition();
        while (pos)
        {
            SHELLWINDOW_ENTRY *pEntry = m_Windows.GetNext(pos);
            if (!IsListed(pEntry))
                continue;
            if (index-- == 0)
                return pEntry;
        }
        return NULL;
    }

public:
    CShellWindows() : m_NextCookie(1), m_NextActivation(1)
    {
    }

    ~CShellWindows()
    {
        while (!m_Windows.IsEmpty())
            delete m_Windows.RemoveHead();
    }

    // *** IShellWindows methods ***
    STDMETHOD(get_Count)(LONG *Count) override
    {
        LONG count = 0;

        if (!Count)
            return E_POINTER;
        POSITION pos = m_Windows.GetHeadPosition();
        while (pos)
        {
            if (IsListed(m_Windows.GetNext(pos)))
                count++;
        }
        *Count = count;
        return S_OK;
    }

    STDMETHOD(Item)(VARIANT index, IDispatch **Folder) override
    {
        CComVariant var;
        HRESULT hr;

        if (!Folder)
            return E_POINTER;
        *Folder = NULL;

        hr = VariantCopyInd(&var, &index);
        if (FAILED(hr))
            return hr;
        if (V_VT(&var) == VT_EMPTY || V_VT(&var) == VT_ERROR)
            return E_INVALIDARG;
        hr = VariantChangeType(&var, &var, 0, VT_I4);
        if (FAILED(hr))
            return E_INVALIDARG;

        SHELLWINDOW_ENTRY *pEntry = (V_I4(&var) >= 0) ? GetListedAt(V_I4(&var)) : NULL;
        if (!pEntry)
            return S_FALSE;
        return pEntry->pDispatch->QueryInterface(IID_PPV_ARG(IDispatch, Folder));
    }

    STDMETHOD(_NewEnum)(IUnknown **ppunk) override
    {
        CComObject<CShellWindowsEnum> *pEnum;
        CSimpleArray<VARIANT> items;
        HRESULT hr;

        if (!ppunk)
            return E_POINTER;
        *ppunk = NULL;

        POSITION pos = m_Windows.GetHeadPosition();
        while (pos)
        {
            SHELLWINDOW_ENTRY *pEntry = m_Windows.GetNext(pos);
            if (!IsListed(pEntry))
                continue;
            VARIANT v;
            V_VT(&v) = VT_DISPATCH;
            V_DISPATCH(&v) = pEntry->pDispatch;
            if (!items.Add(v))
                return E_OUTOFMEMORY;
        }

        hr = CComObject<CShellWindowsEnum>::CreateInstance(&pEnum);
        if (FAILED(hr))
            return hr;
        pEnum->AddRef();
        hr = pEnum->Init(items.GetData(), items.GetData() + items.GetSize(), NULL, AtlFlagCopy);
        if (SUCCEEDED(hr))
            hr = pEnum->QueryInterface(IID_IUnknown, (void **)ppunk);
        pEnum->Release();
        return hr;
    }

    STDMETHOD(Register)(IDispatch *pid, LONG hWnd, int swClass, LONG *plCookie) override
    {
        if (!hWnd || !plCookie)
            return E_POINTER;

        SHELLWINDOW_ENTRY *pEntry = new SHELLWINDOW_ENTRY();
        if (!pEntry)
            return E_OUTOFMEMORY;
        pEntry->Cookie = m_NextCookie++;
        pEntry->pDispatch = pid;
        pEntry->hwnd = hWnd;
        pEntry->swClass = swClass;
        pEntry->ThreadId = 0;
        pEntry->bPending = FALSE;
        pEntry->Activation = m_NextActivation++;
        m_Windows.AddTail(pEntry);

        *plCookie = pEntry->Cookie;
        FireEvent(DISPID_WINDOWREGISTERED, pEntry->Cookie);
        return S_OK;
    }

    STDMETHOD(RegisterPending)(LONG lThreadId, VARIANT *pvarloc, VARIANT *pvarlocRoot, int swClass, LONG *plCookie) override
    {
        HRESULT hr;

        if (!plCookie)
            return E_POINTER;

        SHELLWINDOW_ENTRY *pEntry = new SHELLWINDOW_ENTRY();
        if (!pEntry)
            return E_OUTOFMEMORY;
        hr = PidlFromVariant(pvarloc, &pEntry->pidl);
        if (SUCCEEDED(hr))
            hr = PidlFromVariant(pvarlocRoot, &pEntry->pidlRoot);
        if (FAILED(hr))
        {
            delete pEntry;
            return hr;
        }
        pEntry->Cookie = m_NextCookie++;
        pEntry->hwnd = 0;
        pEntry->swClass = swClass;
        pEntry->ThreadId = lThreadId;
        pEntry->bPending = TRUE;
        pEntry->Activation = m_NextActivation++;
        m_Windows.AddTail(pEntry);

        *plCookie = pEntry->Cookie;
        return S_OK;
    }

    STDMETHOD(Revoke)(LONG lCookie) override
    {
        POSITION pos;
        SHELLWINDOW_ENTRY *pEntry = FindByCookie(lCookie, &pos);
        if (!pEntry)
            return S_FALSE;

        BOOL bListed = IsListed(pEntry);
        m_Windows.RemoveAt(pos);
        delete pEntry;
        if (bListed)
            FireEvent(DISPID_WINDOWREVOKED, lCookie);
        return S_OK;
    }

    STDMETHOD(OnNavigate)(LONG lCookie, VARIANT *pvarLoc) override
    {
        PIDLIST_ABSOLUTE pidl;
        HRESULT hr;

        SHELLWINDOW_ENTRY *pEntry = FindByCookie(lCookie);
        if (!pEntry)
            return E_INVALIDARG;
        hr = PidlFromVariant(pvarLoc, &pidl);
        if (FAILED(hr))
            return hr;
        pEntry->pidl.Free();
        pEntry->pidl.Attach(pidl);
        return S_OK;
    }

    STDMETHOD(OnActivated)(LONG lCookie, VARIANT_BOOL fActive) override
    {
        SHELLWINDOW_ENTRY *pEntry = FindByCookie(lCookie);
        if (!pEntry)
            return E_INVALIDARG;
        if (fActive)
            pEntry->Activation = m_NextActivation++;
        return S_OK;
    }

    STDMETHOD(FindWindowSW)(VARIANT *pvarLoc, VARIANT *pvarLocRoot, int swClass, LONG *phwnd, int swfwOptions, IDispatch **ppdispOut) override
    {
        CComHeapPtr<ITEMIDLIST> pidl, pidlRoot;
        SHELLWINDOW_ENTRY *pFound = NULL;
        HRESULT hr;

        if (phwnd)
            *phwnd = 0;
        if (ppdispOut)
            *ppdispOut = NULL;

        if (swfwOptions & SWFO_COOKIEPASSED)
        {
            CComVariant var;
            if (!pvarLoc || FAILED(VariantChangeType(&var, pvarLoc, 0, VT_I4)))
                return E_INVALIDARG;
            pFound = FindByCookie(V_I4(&var));
            if (pFound && pFound->swClass != swClass)
                pFound = NULL;
        }
        else
        {
            if (swClass != SWC_DESKTOP)
            {
                hr = PidlFromVariant(pvarLoc, &pidl);
                if (FAILED(hr))
                    return hr;
                hr = PidlFromVariant(pvarLocRoot, &pidlRoot);
                if (FAILED(hr))
                    return hr;
            }

            POSITION pos = m_Windows.GetHeadPosition();
            while (pos)
            {
                SHELLWINDOW_ENTRY *pEntry = m_Windows.GetNext(pos);
                if (pEntry->swClass != swClass)
                    continue;
                if (pEntry->bPending && !(swfwOptions & SWFO_INCLUDEPENDING))
                    continue;
                if (swClass != SWC_DESKTOP)
                {
                    if (!pEntry->pidl || !pidl || !ILIsEqual(pEntry->pidl, pidl))
                        continue;
                    if ((pidlRoot != NULL) != (pEntry->pidlRoot != NULL))
                        continue;
                    if (pidlRoot && !ILIsEqual(pEntry->pidlRoot, pidlRoot))
                        continue;
                }
                if (!pFound)
                    pFound = pEntry;
                else if (swClass == SWC_DESKTOP ? pEntry->Activation < pFound->Activation
                                                : pEntry->Activation > pFound->Activation)
                    pFound = pEntry;
            }
        }

        if (!pFound)
            return S_FALSE;
        if ((swfwOptions & SWFO_NEEDDISPATCH) && !pFound->pDispatch)
            return S_FALSE;
        if (phwnd)
            *phwnd = pFound->hwnd;
        if (ppdispOut && (swfwOptions & SWFO_NEEDDISPATCH))
            return pFound->pDispatch->QueryInterface(IID_PPV_ARG(IDispatch, ppdispOut));
        return S_OK;
    }

    STDMETHOD(OnCreated)(LONG lCookie, IUnknown *punk) override
    {
        SHELLWINDOW_ENTRY *pEntry = FindByCookie(lCookie);
        if (!pEntry)
            return E_INVALIDARG;
        if (!punk)
            return E_POINTER;

        CComPtr<IDispatch> pDispatch;
        HRESULT hr = punk->QueryInterface(IID_PPV_ARG(IDispatch, &pDispatch));
        if (FAILED(hr))
            return hr;

        BOOL bWasListed = IsListed(pEntry);
        pEntry->pDispatch = pDispatch;
        pEntry->bPending = FALSE;
        if (!bWasListed && IsListed(pEntry))
            FireEvent(DISPID_WINDOWREGISTERED, lCookie);
        return S_OK;
    }

    STDMETHOD(ProcessAttachDetach)(VARIANT_BOOL fAttach) override
    {
        return S_OK;
    }

    // *** IServiceProvider methods ***
    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_SHELLWINDOWS)
    DECLARE_NOT_AGGREGATABLE(CShellWindows)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CShellWindows)
        COM_INTERFACE_ENTRY_IID(IID_IShellWindows, IShellWindows)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_IConnectionPointContainer, IConnectionPointContainer)
    END_COM_MAP()

    BEGIN_CONNECTION_POINT_MAP(CShellWindows)
        CONNECTION_POINT_ENTRY(DIID_DShellWindowsEvents)
    END_CONNECTION_POINT_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_ShellWindows, CShellWindows)
