/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Context menu that invokes a method of an automation object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

class CAutomationCM :
    public CComCoClass<CAutomationCM, &CLSID_AutomationCM>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenu,
    public IShellExtInit,
    public IPersistPropertyBag,
    public IShellFolder,
    public IInitializeLoadTrust
{
    CLSID m_clsid;
    CComHeapPtr<WCHAR> m_pszMethod;
    CComHeapPtr<WCHAR> m_pszCommand;
    BOOL m_bLoaded;
    BOOL m_bTrusted;

public:
    CAutomationCM() : m_clsid(CLSID_NULL), m_bLoaded(FALSE), m_bTrusted(FALSE) {}

    // IContextMenu
    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override
    {
        if (!m_bLoaded)
            return E_FAIL;
        if (idCmdFirst > idCmdLast)
            return E_INVALIDARG;
        if (!InsertMenuW(hMenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst, m_pszCommand))
            return HRESULT_FROM_WIN32(GetLastError());
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2);
    }

    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpici) override
    {
        CComPtr<IDispatch> pdisp;
        DISPPARAMS dp = { NULL, NULL, 0, 0 };
        LPOLESTR pszMethod;
        DISPID dispid;
        HRESULT hr;

        if (!lpici || lpici->cbSize < sizeof(CMINVOKECOMMANDINFO))
            return E_INVALIDARG;
        if (IS_INTRESOURCE(lpici->lpVerb) ? LOWORD(lpici->lpVerb) != 0 : lstrcmpiA(lpici->lpVerb, "open"))
            return E_INVALIDARG;
        if (!m_bLoaded)
            return E_FAIL;
        if (!m_bTrusted)
            return E_ACCESSDENIED;

        hr = CoCreateInstance(m_clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_PPV_ARG(IDispatch, &pdisp));
        if (FAILED(hr))
            return hr;
        pszMethod = m_pszMethod;
        hr = pdisp->GetIDsOfNames(IID_NULL, &pszMethod, 1, LOCALE_USER_DEFAULT, &dispid);
        if (FAILED(hr))
            return hr;
        return pdisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    }

    STDMETHOD(GetCommandString)(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax) override
    {
        if (idCmd != 0)
            return E_INVALIDARG;
        switch (uType)
        {
            case GCS_VERBA:
                return StringCchCopyA(pszName, cchMax, "open");
            case GCS_VERBW:
                return StringCchCopyW((LPWSTR)pszName, cchMax, L"open");
            case GCS_VALIDATEA:
            case GCS_VALIDATEW:
                return S_OK;
        }
        return E_NOTIMPL;
    }

    // IShellExtInit
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID) override
    {
        return S_OK;
    }

    // IPersist
    STDMETHOD(GetClassID)(CLSID *pClassID) override
    {
        if (!pClassID)
            return E_POINTER;
        *pClassID = CLSID_AutomationCM;
        return S_OK;
    }

    // IPersistPropertyBag
    STDMETHOD(InitNew)() override
    {
        m_clsid = CLSID_NULL;
        m_pszMethod.Free();
        m_pszCommand.Free();
        m_bLoaded = FALSE;
        return S_OK;
    }

    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override
    {
        WCHAR szValue[MAX_PATH], szCommand[MAX_PATH];
        HRESULT hr;

        if (!pPropBag)
            return E_INVALIDARG;
        InitNew();

        hr = SHPropertyBag_ReadStr(pPropBag, L"CLSID", szValue, _countof(szValue));
        if (FAILED(hr))
            return hr;
        hr = CLSIDFromString(szValue, &m_clsid);
        if (FAILED(hr))
            return hr;
        hr = SHPropertyBag_ReadStr(pPropBag, L"method", szValue, _countof(szValue));
        if (FAILED(hr))
            return hr;
        hr = SHStrDupW(szValue, &m_pszMethod);
        if (FAILED(hr))
            return hr;
        hr = SHPropertyBag_ReadStr(pPropBag, L"command", szValue, _countof(szValue));
        if (FAILED(hr))
            return hr;
        if (szValue[0] == L'@')
        {
            hr = SHLoadIndirectString(szValue, szCommand, _countof(szCommand), NULL);
            if (FAILED(hr))
                return hr;
            hr = SHStrDupW(szCommand, &m_pszCommand);
        }
        else
        {
            hr = SHStrDupW(szValue, &m_pszCommand);
        }
        if (FAILED(hr))
            return hr;
        m_bLoaded = TRUE;
        return S_OK;
    }

    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override
    {
        return E_NOTIMPL;
    }

    // IShellFolder
    STDMETHOD(ParseDisplayName)(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(EnumObjects)(HWND hwndOwner, SHCONTF dwFlags, LPENUMIDLIST *ppEnumIDList) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, LPVOID *ppvOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT *prgfInOut, LPVOID *ppvOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *lpName) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetNameOf)(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, SHGDNF uFlags, PITEMID_CHILD *pPidlOut) override
    {
        return E_NOTIMPL;
    }

    // IInitializeLoadTrust
    STDMETHOD(LoadingDataFromRegistry)() override
    {
        m_bTrusted = TRUE;
        return S_OK;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_AUTOMATIONCM)
    DECLARE_NOT_AGGREGATABLE(CAutomationCM)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CAutomationCM)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IInitializeLoadTrust, IInitializeLoadTrust)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_AutomationCM, CAutomationCM)

class CShellInstanceObjectFactory :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IClassFactory
{
    CLSID m_clsidInstance;
    WCHAR m_szInstanceKey[MAX_PATH];

public:
    HRESULT Initialize(REFCLSID clsidInstance, LPCWSTR pszInstanceKey)
    {
        m_clsidInstance = clsidInstance;
        return StringCchCopyW(m_szInstanceKey, _countof(m_szInstanceKey), pszInstanceKey);
    }

    STDMETHOD(CreateInstance)(IUnknown *pUnkOuter, REFIID riid, void **ppvObject) override
    {
        CComPtr<IUnknown> punk;
        HKEY hKey;
        HRESULT hr;

        if (!ppvObject)
            return E_POINTER;
        *ppvObject = NULL;
        if (pUnkOuter)
            return CLASS_E_NOAGGREGATION;

        hr = SHCoCreateInstance(NULL, &m_clsidInstance, NULL, IID_PPV_ARG(IUnknown, &punk));
        if (FAILED(hr))
            return hr;

        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, m_szInstanceKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            HKEY hBagKey;
            if (RegOpenKeyExW(hKey, L"InitPropertyBag", 0, KEY_READ, &hBagKey) == ERROR_SUCCESS)
            {
                CComPtr<IPropertyBag> pbag;
                CComPtr<IPersistPropertyBag> pppb;
                CComPtr<IInitializeLoadTrust> pTrust;
                hr = punk->QueryInterface(IID_PPV_ARG(IPersistPropertyBag, &pppb));
                if (SUCCEEDED(hr))
                    hr = SHCreatePropertyBagOnRegKey(hBagKey, NULL, STGM_READ, IID_PPV_ARG(IPropertyBag, &pbag));
                if (SUCCEEDED(hr))
                {
                    if (SUCCEEDED(punk->QueryInterface(IID_PPV_ARG(IInitializeLoadTrust, &pTrust))))
                        pTrust->LoadingDataFromRegistry();
                    hr = pppb->Load(pbag, NULL);
                }
                RegCloseKey(hBagKey);
            }
            else if (RegOpenKeyExW(hKey, L"InitStream", 0, KEY_READ, &hBagKey) == ERROR_SUCCESS)
            {
                CComPtr<IPersistStream> pps;
                CComPtr<IStream> pstm;
                hr = punk->QueryInterface(IID_PPV_ARG(IPersistStream, &pps));
                if (SUCCEEDED(hr))
                {
                    pstm.Attach(SHOpenRegStream2W(hBagKey, NULL, NULL, STGM_READ));
                    hr = pstm ? pps->Load(pstm) : E_FAIL;
                }
                RegCloseKey(hBagKey);
            }
            RegCloseKey(hKey);
        }
        if (FAILED(hr))
            return hr;
        return punk->QueryInterface(riid, ppvObject);
    }

    STDMETHOD(LockServer)(BOOL fLock) override
    {
        if (fLock)
            _pAtlModule->Lock();
        else
            _pAtlModule->Unlock();
        return S_OK;
    }

    BEGIN_COM_MAP(CShellInstanceObjectFactory)
        COM_INTERFACE_ENTRY_IID(IID_IClassFactory, IClassFactory)
    END_COM_MAP()
};

HRESULT SHELL32_GetInstanceObjectFactory(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    WCHAR szKey[MAX_PATH], szClsid[39], szInstance[39];
    DWORD cbInstance = sizeof(szInstance);
    CLSID clsidInstance;
    HRESULT hr;

    *ppv = NULL;
    StringFromGUID2(rclsid, szClsid, _countof(szClsid));
    hr = StringCchPrintfW(szKey, _countof(szKey), L"CLSID\\%s\\Instance", szClsid);
    if (FAILED(hr))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (RegGetValueW(HKEY_CLASSES_ROOT, szKey, L"CLSID", RRF_RT_REG_SZ, NULL, szInstance, &cbInstance) != ERROR_SUCCESS ||
        FAILED(CLSIDFromString(szInstance, &clsidInstance)))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return ShellObjectCreatorInit<CShellInstanceObjectFactory>(clsidInstance, (LPCWSTR)szKey, riid, ppv);
}
