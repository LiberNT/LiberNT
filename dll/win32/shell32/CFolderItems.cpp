/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     FolderItem(s) implementation
 * COPYRIGHT:   Copyright 2015-2018 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2026 Ahmed ARIF (arif.ing@outlook.com)
 */

#include "precomp.h"
#include "prop.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

HRESULT SHELL_InvokeCommandOnContextMenu(_In_opt_ HWND hWnd, _In_ IContextMenu *pCM, _In_opt_ PCWSTR pszVerb, _In_ UINT cVerbs,
                                         _In_opt_ PCWSTR pszArgs, _In_ UINT fCMIC, _In_ UINT fCMF, _In_opt_ IUnknown *pSite)
{
    // Note: We can't use SHLWAPI::SHInvokeCommandOnContextMenu because it does not have an arguments parameter
    if (!pCM)
        return E_INVALIDARG;

    HRESULT hr = S_OK;
    int iDefItem = 0;
    HMENU hMenu = NULL;
    HCURSOR hOldCursor = SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_WAIT)));
    CMINVOKECOMMANDINFOEX ici = { sizeof(ici), fCMIC, hWnd, NULL, NULL, NULL, SW_SHOWNORMAL };
    CHAR szVerb[MAX_PATH], szArgs[MAX_PATH * 3];

    if (pSite)
        IUnknown_SetSite(pCM, pSite);

    if (IS_INTRESOURCE(pszVerb))
    {
        ici.lpVerb = MAKEINTRESOURCEA(pszVerb);
        if (!cVerbs)
        {
            hMenu = CreatePopupMenu();
            if (hMenu)
            {
                hr = pCM->QueryContextMenu(hMenu, 0, 1, MAXSHORT, fCMF | CMF_DEFAULTONLY);
                if ((iDefItem = GetMenuDefaultItem(hMenu, 0, 0)) != -1)
                    ici.lpVerb = MAKEINTRESOURCEA(iDefItem - 1);
            }
        }
        ici.lpVerbW = MAKEINTRESOURCEW(ici.lpVerb);
    }
    else
    {
        ici.fMask |= CMIC_MASK_UNICODE;
        ici.lpVerbW = pszVerb;
        if (pszVerb && SHUnicodeToAnsi(pszVerb, szVerb, _countof(szVerb)))
            ici.lpVerb = szVerb;
    }

    if (pszArgs)
    {
        ici.fMask |= CMIC_MASK_UNICODE;
        ici.lpParametersW = pszArgs;
        if (pszArgs && SHUnicodeToAnsi(pszArgs, szArgs, _countof(szArgs)))
            ici.lpParameters = szArgs;
    }

    SetCursor(hOldCursor);

    if (!FAILED_UNEXPECTEDLY(hr) && (iDefItem != -1 || ici.lpVerbW))
        hr = pCM->InvokeCommand((LPCMINVOKECOMMANDINFO)&ici);
    if (pSite)
        IUnknown_SetSite(pCM, NULL);
    if (hMenu)
        DestroyMenu(hMenu);
    return hr;
}

static inline HRESULT GetUIObjectOfFolderItems(FolderItems *pFI, REFIID riid, void **ppv)
{
    VARIANT v;
    V_VT(&v) = VT_DISPATCH;
    V_DISPATCH(&v) = static_cast<IDispatch*>(pFI);
    return CFolder::GetUIObjectFromVariant(v, riid, ppv);
}

CFolderItem::CFolderItem()
    : m_SafetyOptions(0)
{
}

CFolderItem::~CFolderItem()
{
}

HRESULT CFolderItem::Initialize(Folder* folder, LPCITEMIDLIST idlist)
{
    m_Folder = folder;
    return SHILClone(idlist, &m_idlist);
}

HRESULT STDMETHODCALLTYPE CFolderItem::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_FolderItem;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItem::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    if (!pidl)
        return E_INVALIDARG;

    CComPtr<IDispatch> pApplication;
    HRESULT hr = CShellDispatch_Constructor(IID_PPV_ARG(IDispatch, &pApplication));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComHeapPtr<ITEMIDLIST> pidlParent(ILClone(pidl));
    if (!pidlParent)
        return E_OUTOFMEMORY;
    ILRemoveLastID(pidlParent);

    CComPtr<Folder> pFolder;
    hr = CFolder::CreateInstance(pidlParent, pApplication, IID_PPV_ARG(Folder, &pFolder));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    m_idlist.Free();
    m_Folder = pFolder;
    return SHILClone(pidl, &m_idlist);
}

HRESULT STDMETHODCALLTYPE CFolderItem::GetCurFolder(PIDLIST_ABSOLUTE *ppidl)
{
    if (!ppidl)
        return E_POINTER;
    *ppidl = NULL;
    if (!m_idlist)
        return S_FALSE;
    return SHILClone(m_idlist, ppidl);
}

HRESULT STDMETHODCALLTYPE CFolderItem::GetInterfaceSafetyOptions(REFIID riid, DWORD *pdwSupportedOptions, DWORD *pdwEnabledOptions)
{
    if (!pdwSupportedOptions || !pdwEnabledOptions)
        return E_POINTER;

    CComPtr<IUnknown> pUnk;
    if (FAILED(QueryInterface(riid, (void **)&pUnk)))
        return E_NOINTERFACE;

    *pdwSupportedOptions = INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA;
    *pdwEnabledOptions = m_SafetyOptions;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItem::SetInterfaceSafetyOptions(REFIID riid, DWORD dwOptionSetMask, DWORD dwEnabledOptions)
{
    CComPtr<IUnknown> pUnk;
    if (FAILED(QueryInterface(riid, (void **)&pUnk)))
        return E_NOINTERFACE;

    if (dwOptionSetMask & ~(INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA))
        return E_FAIL;

    m_SafetyOptions = (m_SafetyOptions & ~dwOptionSetMask) | (dwEnabledOptions & dwOptionSetMask);
    return S_OK;
}

inline HRESULT CFolderItem::GetParentShellFolderAndItem(REFIID riid, void**ppv, PCUITEMID_CHILD &pidlLast)
{
    return SHBindToParent(GetAbsoluteIDList(), riid, ppv, &pidlLast);
}

HRESULT CFolderItem::GetParentAndItem(const VARIANT *pV, IParentAndItem **ppPAI)
{
    if (!pV)
        return E_POINTER;
    pV = VariantDerefVariant(pV);

    IUnknown *pUnk = NULL;
    switch (V_VT(pV))
    {
        case VT_DISPATCH | VT_BYREF:
            pUnk = V_DISPATCHREF(pV) ? *V_DISPATCHREF(pV) : NULL;
            break;
        case VT_DISPATCH:
            pUnk = V_DISPATCH(pV);
            break;
    }
    return pUnk ? pUnk->QueryInterface(IID_PPV_ARG(IParentAndItem, ppPAI)) : E_INVALIDARG;
}

PITEMID_CHILD CFolderItem::CloneLeafPidl(const VARIANT *pV)
{
    CComPtr<IParentAndItem> pPAI;
    PITEMID_CHILD pidl;
    if (SUCCEEDED(GetParentAndItem(pV, &pPAI)) && SUCCEEDED(pPAI->GetParentAndItem(NULL, NULL, &pidl)))
        return pidl;
    return NULL;
}

HRESULT CFolderItem::GetFindDataFromIDList(WIN32_FIND_DATA &wfd)
{
    CComPtr<IShellFolder2> psf;
    PCUITEMID_CHILD pidlLeaf;
    HRESULT hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder2, &psf), pidlLeaf);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return SHGetDataFromIDListW(psf, pidlLeaf, SHGDFIL_FINDDATA, &wfd, sizeof(wfd));
}

// *** FolderItem methods ***
HRESULT STDMETHODCALLTYPE CFolderItem::get_Application(IDispatch **ppid)
{
    TRACE("(%p, %p)\n", this, ppid);
    return m_Folder->get_Application(ppid);
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_Parent(IDispatch **ppid)
{
    TRACE("(%p, %p)\n", this, ppid);

    if (!ppid)
        return E_INVALIDARG;
    return m_Folder->QueryInterface(IID_PPV_ARG(IDispatch, ppid));
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_Name(BSTR *pbs)
{
    TRACE("(%p, %p)\n", this, pbs);

    *pbs = NULL;

    CComPtr<IShellFolder> Parent;
    PCUITEMID_CHILD last_part;
    HRESULT hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder, &Parent), last_part);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    STRRET strret;
    hr = Parent->GetDisplayNameOf(last_part, SHGDN_INFOLDER, &strret);
    if (!FAILED_UNEXPECTEDLY(hr))
        hr = StrRetToBSTR(&strret, last_part, pbs);

    return hr;
}

HRESULT STDMETHODCALLTYPE CFolderItem::put_Name(BSTR bs)
{
    TRACE("(%p, %s)\n", this, wine_dbgstr_w(bs));

    CComPtr<IShellFolder> psf;
    PCUITEMID_CHILD pidlLeaf;
    HRESULT hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder, &psf), pidlLeaf);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    LPITEMIDLIST pidlNew;
    if (SUCCEEDED(hr = psf->SetNameOf(GetHwnd(), pidlLeaf, bs, SHGDN_INFOLDER, &pidlNew)) && pidlNew)
    {
        LPITEMIDLIST pidlClone = ILClone(m_idlist);
        ILRemoveLastID(pidlClone);
        if (pidlClone && SUCCEEDED(SHILAppend(pidlNew, &pidlClone)))
        {
            m_idlist.Free();
            m_idlist.Attach(pidlClone);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_Path(BSTR *pbs)
{
    CComPtr<IShellFolder> psfDesktop;

    HRESULT hr = SHGetDesktopFolder(&psfDesktop);
    if (!SUCCEEDED(hr))
        return hr;

    STRRET strret;
    hr = psfDesktop->GetDisplayNameOf(m_idlist, SHGDN_FORPARSING, &strret);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return StrRetToBSTR(&strret, NULL, pbs);
}

class CShellLinkObject :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IShellLinkDual2, &IID_IShellLinkDual2>
{
private:
    CComPtr<IShellLinkW> m_pShellLink;
    WCHAR m_szFile[MAX_PATH];

    static HRESULT ToBSTR(PCWSTR psz, BSTR *pbs)
    {
        if (!pbs)
            return E_POINTER;
        *pbs = SysAllocString(psz);
        return *pbs ? S_OK : E_OUTOFMEMORY;
    }

public:
    CShellLinkObject()
    {
        m_szFile[0] = UNICODE_NULL;
    }

    HRESULT Initialize(LPCITEMIDLIST pidl)
    {
        if (!SHGetPathFromIDListW(pidl, m_szFile))
            return E_FAIL;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellLinkW, &m_pShellLink));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        CComPtr<IPersistFile> pPersistFile;
        hr = m_pShellLink->QueryInterface(IID_PPV_ARG(IPersistFile, &pPersistFile));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        return pPersistFile->Load(m_szFile, STGM_READ);
    }

    STDMETHOD(get_Path)(BSTR *pbs) override
    {
        WCHAR szPath[MAX_PATH];
        HRESULT hr = m_pShellLink->GetPath(szPath, _countof(szPath), NULL, 0);
        if (FAILED(hr))
            return hr;
        return ToBSTR(hr == S_OK ? szPath : L"", pbs);
    }

    STDMETHOD(put_Path)(BSTR bs) override
    {
        return m_pShellLink->SetPath(bs);
    }

    STDMETHOD(get_Description)(BSTR *pbs) override
    {
        WCHAR szText[INFOTIPSIZE];
        HRESULT hr = m_pShellLink->GetDescription(szText, _countof(szText));
        return FAILED(hr) ? hr : ToBSTR(szText, pbs);
    }

    STDMETHOD(put_Description)(BSTR bs) override
    {
        return m_pShellLink->SetDescription(bs);
    }

    STDMETHOD(get_WorkingDirectory)(BSTR *pbs) override
    {
        WCHAR szPath[MAX_PATH];
        HRESULT hr = m_pShellLink->GetWorkingDirectory(szPath, _countof(szPath));
        return FAILED(hr) ? hr : ToBSTR(szPath, pbs);
    }

    STDMETHOD(put_WorkingDirectory)(BSTR bs) override
    {
        return m_pShellLink->SetWorkingDirectory(bs);
    }

    STDMETHOD(get_Arguments)(BSTR *pbs) override
    {
        WCHAR szArgs[INFOTIPSIZE];
        HRESULT hr = m_pShellLink->GetArguments(szArgs, _countof(szArgs));
        return FAILED(hr) ? hr : ToBSTR(szArgs, pbs);
    }

    STDMETHOD(put_Arguments)(BSTR bs) override
    {
        return m_pShellLink->SetArguments(bs);
    }

    STDMETHOD(get_Hotkey)(int *piHK) override
    {
        WORD wHotkey = 0;
        if (!piHK)
            return E_POINTER;
        HRESULT hr = m_pShellLink->GetHotkey(&wHotkey);
        *piHK = wHotkey;
        return hr;
    }

    STDMETHOD(put_Hotkey)(int iHK) override
    {
        return m_pShellLink->SetHotkey((WORD)iHK);
    }

    STDMETHOD(get_ShowCommand)(int *piShowCommand) override
    {
        if (!piShowCommand)
            return E_POINTER;
        return m_pShellLink->GetShowCmd(piShowCommand);
    }

    STDMETHOD(put_ShowCommand)(int iShowCommand) override
    {
        return m_pShellLink->SetShowCmd(iShowCommand);
    }

    STDMETHOD(Resolve)(int fFlags) override
    {
        return m_pShellLink->Resolve(NULL, fFlags);
    }

    STDMETHOD(GetIconLocation)(BSTR *pbs, int *piIcon) override
    {
        WCHAR szPath[MAX_PATH];
        int iIcon = 0;
        if (!piIcon)
            return E_POINTER;
        HRESULT hr = m_pShellLink->GetIconLocation(szPath, _countof(szPath), &iIcon);
        if (FAILED(hr))
            return hr;
        *piIcon = iIcon;
        return ToBSTR(szPath, pbs);
    }

    STDMETHOD(SetIconLocation)(BSTR bs, int iIcon) override
    {
        return m_pShellLink->SetIconLocation(bs, iIcon);
    }

    STDMETHOD(Save)(VARIANT vWhere) override
    {
        CComPtr<IPersistFile> pPersistFile;
        HRESULT hr = m_pShellLink->QueryInterface(IID_PPV_ARG(IPersistFile, &pPersistFile));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        PCWSTR pszFile = (V_VT(&vWhere) == VT_BSTR && !StrIsNullOrEmpty(V_BSTR(&vWhere))) ? V_BSTR(&vWhere) : m_szFile;
        return pPersistFile->Save(pszFile, TRUE);
    }

    STDMETHOD(get_Target)(FolderItem **ppfi) override
    {
        CComHeapPtr<ITEMIDLIST> pidl;
        if (!ppfi)
            return E_POINTER;
        *ppfi = NULL;
        HRESULT hr = m_pShellLink->GetIDList(&pidl);
        if (hr != S_OK)
            return FAILED(hr) ? hr : E_FAIL;
        CComPtr<IPersistFolder> pPersistFolder;
        hr = ShellObjectCreator<CFolderItem>(IID_PPV_ARG(IPersistFolder, &pPersistFolder));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        hr = pPersistFolder->Initialize(pidl);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        return pPersistFolder->QueryInterface(IID_PPV_ARG(FolderItem, ppfi));
    }

BEGIN_COM_MAP(CShellLinkObject)
    COM_INTERFACE_ENTRY_IID(IID_IShellLinkDual2, IShellLinkDual2)
    COM_INTERFACE_ENTRY_IID(IID_IShellLinkDual, IShellLinkDual)
    COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
END_COM_MAP()
};

HRESULT STDMETHODCALLTYPE CFolderItem::get_GetLink(IDispatch **ppid)
{
    TRACE("(%p, %p)\n", this, ppid);

    if (!ppid)
        return E_POINTER;
    *ppid = NULL;

    if (!SHGetAttributes(NULL, GetAbsoluteIDList(), SFGAO_LINK))
        return E_NOTIMPL;

    return ShellObjectCreatorInit<CShellLinkObject>(GetAbsoluteIDList(), IID_PPV_ARG(IDispatch, ppid));
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_GetFolder(IDispatch **ppid)
{
    return CFolder::CreateInstance(GetAbsoluteIDList(), *static_cast<Folder*>(m_Folder), IID_PPV_ARG(IDispatch, ppid));
}

HRESULT CFolderItem::HasAttribute(DWORD sfgaof, VARIANT_BOOL *pB)
{
    *pB = SHGetAttributes(NULL, GetAbsoluteIDList(), sfgaof) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_IsLink(VARIANT_BOOL *pb)
{
    TRACE("(%p, %p)\n", this, pb);
    return HasAttribute(SFGAO_LINK, pb);
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_IsFolder(VARIANT_BOOL *pb)
{
    TRACE("(%p, %p)\n", this, pb);
    return HasAttribute(SFGAO_FOLDER, pb);
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_IsFileSystem(VARIANT_BOOL *pb)
{
    TRACE("(%p, %p)\n", this, pb);
    return HasAttribute(SFGAO_FILESYSTEM, pb);
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_IsBrowsable(VARIANT_BOOL *pb)
{
    TRACE("(%p, %p)\n", this, pb);
    return HasAttribute(SFGAO_BROWSABLE, pb);
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_ModifyDate(DATE *pdt)
{
    TRACE("(%p, %p)\n", this, pdt);

    WIN32_FIND_DATA wfd;
    if (SUCCEEDED(GetFindDataFromIDList(wfd)))
    {
        FILETIME ft;
        FileTimeToLocalFileTime(&wfd.ftLastWriteTime, &ft);
        WORD dd, dt;
        if (FileTimeToDosDateTime(&ft, &dd, &dt) && DosDateTimeToVariantTime(dd, dt, pdt))
            return S_OK;
    }
    *pdt = 0;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CFolderItem::put_ModifyDate(DATE dt)
{
    TRACE("(%p, %f)\n", this, dt);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_Size(LONG *pul)
{
    TRACE("(%p, %p)\n", this, pul);

    WIN32_FIND_DATA wfd;
    if (SUCCEEDED(GetFindDataFromIDList(wfd)))
    {
        *pul = wfd.nFileSizeLow;
        return S_OK;
    }
    *pul = 0;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CFolderItem::get_Type(BSTR *pbs)
{
    TRACE("(%p, %p)\n", this, pbs);

    VARIANT v;
    V_VT(&v) = VT_EMPTY;
    if (SUCCEEDED(GetExtendedProperty(PKEY_ItemTypeText, &v)))
    {
        if (SUCCEEDED(VariantChangeType(&v, &v, 0, VT_BSTR)))
        {
            *pbs = V_BSTR(&v);
            return S_OK;
        }
        VariantClear(&v);
    }
    HRESULT hr = SHELL_SysAllocString(L"", pbs);
    return hr == S_OK ? S_FALSE : hr;
}

HRESULT STDMETHODCALLTYPE CFolderItem::Verbs(FolderItemVerbs **ppfic)
{
    if (!ppfic)
        return E_POINTER;

    CComPtr<CFolderItemVerbs> pVerbs;
    HRESULT hr = CFolderItemVerbs::CreateInstance(pVerbs);
    if (SUCCEEDED(hr))
        hr = pVerbs->Init(GetAbsoluteIDList());
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    *ppfic = pVerbs.Detach();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItem::InvokeVerb(VARIANT vVerb)
{
    VARIANT empty;
    V_VT(&empty) = VT_EMPTY;
    return InvokeVerbEx(vVerb, empty);
}

HRESULT STDMETHODCALLTYPE CFolderItem::InvokeVerbEx(VARIANT vVerb, VARIANT vArgs)
{
    TRACE("(%p, %s)\n", this, wine_dbgstr_variant(&vVerb));

    HWND hWnd = GetHwnd();
    CComPtr<IShellFolder> psf;
    PCUITEMID_CHILD pidlItem;
    HRESULT hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder, &psf), pidlItem);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComPtr<IContextMenu> pCM;
    if (SUCCEEDED(hr = psf->GetUIObjectOf(hWnd, 1, &pidlItem, IID_NULL_PPV_ARG(IContextMenu, &pCM))))
        hr = CFolderItems::InvokeVerbHelper(hWnd, *pCM, vVerb, vArgs, GetSite());
    return hr;
}

HRESULT CFolderItem::GetExtendedProperty(REFPROPERTYKEY pkey, VARIANT *pv)
{
    CComPtr<IShellFolder2> psf;
    PCUITEMID_CHILD pidlLeaf;
    HRESULT hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder2, &psf), pidlLeaf);
    return FAILED(hr) ? hr : psf->GetDetailsEx(pidlLeaf, reinterpret_cast<const SHCOLUMNID*>(&pkey), pv);
}

HRESULT STDMETHODCALLTYPE CFolderItem::ExtendedProperty(BSTR bsPropName, VARIANT *pv)
{
    if (!pv)
        return E_INVALIDARG;

    if (!lstrcmpiW(bsPropName, L"InfoTip"))
    {
        VARIANT v;
        V_VT(&v) = VT_DISPATCH;
        V_DISPATCH(&v) = static_cast<IDispatch*>(this);
        if (SUCCEEDED(m_Folder->GetDetailsOf(v, CFolder::INFOTIPCOLUMN, &V_BSTR(pv))))
        {
            V_VT(pv) = VT_BSTR;
            return S_OK;
        }
    }

    PROPERTYKEY pkeybuf;
    const PROPERTYKEY *pPK = SHELL_GetPropertyKeyFromString(bsPropName, &pkeybuf);
    if (pPK && GetExtendedProperty(*pPK, pv) == S_OK)
        return S_OK;

    V_VT(pv) = VT_EMPTY;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CFolderItem::GetParentAndItem(PIDLIST_ABSOLUTE *ppidlParent, IShellFolder **ppsf, PITEMID_CHILD *ppidlChild)
{
    if (ppidlParent)
        *ppidlParent = NULL;
    if (ppsf)
        *ppsf = NULL;
    if (ppidlChild)
        *ppidlChild = NULL;

    HRESULT hr = S_FALSE;
    CComPtr<IShellFolder> psf;
    PCUITEMID_CHILD pidlChild;
    if (ppsf || ppidlChild)
    {
        if (FAILED(hr = GetParentShellFolderAndItem(IID_PPV_ARG(IShellFolder, &psf), pidlChild)))
            return hr;
    }

    if (ppidlParent && FAILED(hr = SHILCloneParent(GetAbsoluteIDList(), ppidlParent)))
        return hr;
    if (ppidlChild && FAILED(hr = SHILClone(pidlChild, ppidlChild)))
    {
        if (ppidlParent)
        {
            ILFree(*ppidlParent);
            *ppidlParent = NULL;
        }
        return hr;
    }
    if (ppsf)
        *ppsf = psf.Detach();
    return hr;
}


CFolderItems::CFolderItems()
    :m_Count(-1)
{
}

CFolderItems::~CFolderItems()
{
    SysFreeString(m_bstrFilter);
}

HRESULT CFolderItems::Initialize(LPCITEMIDLIST idlist, Folder* parent)
{
    m_Folder = parent;
    return SHILClone(idlist, &m_idlist);
}

void CFolderItems::ResetEnum()
{
    m_EnumIDList = NULL;
    m_Count = -1;
}

BOOL CFolderItems::IncludeItem(LPCITEMIDLIST pidl)
{
    if (!m_bstrFilter)
        return TRUE;

    CComHeapPtr<ITEMIDLIST> pidlFull;
    if (FAILED(SHILCombine(m_idlist, pidl, &pidlFull)))
        return FALSE;
    CComHeapPtr<WCHAR> pszPath;
    if (FAILED(SHELL_DisplayNameOf(NULL, pidlFull, SHGDN_FORPARSING | SHGDN_INFOLDER, &pszPath)))
        return FALSE;
    return PathMatchSpecW(pszPath, m_bstrFilter);
}

HRESULT CALLBACK CFolderItems::ItemsEnumFilter(void *Cookie, LPCITEMIDLIST pidl)
{
    return ((CFolderItems*)Cookie)->IncludeItem(pidl) ? S_OK : S_FALSE;
}

HRESULT CFolderItems::EnumObjects()
{
    if (m_EnumIDList)
        return S_OK;

    CComPtr<IShellFolder> psf;
    HRESULT hr = SHBindToObject(NULL, m_idlist, NULL, IID_PPV_ARG(IShellFolder, &psf));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComPtr<IEnumIDList> pEnum;
    if (FAILED(hr = psf->EnumObjects(NULL, m_Contf, &pEnum)))
        return hr;
    hr = pEnum ? S_OK : E_UNEXPECTED;
    if (SUCCEEDED(hr) && m_bstrFilter)
    {
        CComPtr<IEnumIDList> pFilteredEnum;
        hr = CEnumIDListBase::CreateInstance(*pEnum, ItemsEnumFilter, this, &pFilteredEnum);
        if (SUCCEEDED(hr))
            pEnum = pFilteredEnum;
    }

    if (FAILED(hr))
        return hr;
    m_EnumIDList = pEnum;
    return S_OK;
}

// *** FolderItems methods ***
HRESULT STDMETHODCALLTYPE CFolderItems::get_Count(long *plCount)
{
    if (!plCount)
        return E_POINTER;

    if (m_Count == -1)
    {
        long count = 0;

        if (FAILED(EnumObjects()))
            return E_FAIL;
        HRESULT hr = m_EnumIDList->Reset();
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        CComHeapPtr<ITEMIDLIST> Pidl;
        while ((hr = GetNext(&Pidl)) == S_OK)
        {
            count++;
            Pidl.Free();
        }
        m_Count = count;
    }
    *plCount = m_Count;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItems::get_Application(IDispatch **ppid)
{
    TRACE("(%p, %p)\n", this, ppid);
    return m_Folder->get_Application(ppid);
}

HRESULT STDMETHODCALLTYPE CFolderItems::get_Parent(IDispatch **ppid)
{
    TRACE("(%p, %p)\n", this, ppid);

    if (ppid)
        *ppid = NULL;

    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CFolderItems::Item(VARIANT var, FolderItem **ppid)
{
    CComVariant index;
    HRESULT hr;

    if (!ppid)
        return E_POINTER;
    *ppid = NULL;

    hr = VariantCopyInd(&index, &var);
    if (FAILED(hr))
        return hr;

    if (V_VT(&index) == VT_I2)
        VariantChangeType(&index, &index, 0, VT_I4);

    if (V_VT(&index) == VT_I4)
    {
        if (FAILED(EnumObjects()))
            return E_FAIL;

        if (V_I4(&index) < 0)
            return S_FALSE;
        ULONG count = V_UI4(&index);

        hr = m_EnumIDList->Reset();
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        hr = m_EnumIDList->Skip(count);

        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        CComHeapPtr<ITEMIDLIST> pidlChild;
        hr = GetNext(&pidlChild);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        if (hr != S_OK)
            return S_FALSE;

        CComHeapPtr<ITEMIDLIST> pidlFull;
        if (FAILED_UNEXPECTEDLY(hr = SHILCombine(m_idlist, pidlChild, &pidlFull)))
            return hr;
        hr = ShellObjectCreatorInit<CFolderItem>(m_Folder, pidlFull, IID_PPV_ARG(FolderItem, ppid));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        return hr;
    }
    else if (V_VT(&index) == VT_BSTR)
    {
        if (!V_BSTR(&index))
            return S_FALSE;

        hr = m_Folder->ParseName(V_BSTR(&index), ppid);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        return hr;
    }

    else if (V_VT(&index) == VT_ERROR)
    {
        return ShellObjectCreatorInit<CFolderItem>(m_Folder, static_cast<LPCITEMIDLIST>(m_idlist), IID_PPV_ARG(FolderItem, ppid));
    }

    FIXME("Index type %d not handled.\n", V_VT(&index));
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CFolderItems::_NewEnum(IUnknown **ppunk)
{
    return ShellObjectCreatorInit<CFolderItems>(static_cast<LPITEMIDLIST>(m_idlist), m_Folder, IID_FolderItems, reinterpret_cast<void**>(ppunk));
}

HRESULT STDMETHODCALLTYPE CFolderItems::InvokeVerbEx(VARIANT vVerb, VARIANT vArgs)
{
    CComPtr<IContextMenu> pCM;
    HRESULT hr = GetUIObjectOfFolderItems(this, IID_PPV_ARG(IContextMenu, &pCM));
    if (FAILED(hr))
        return hr == HRESULT_FROM_WIN32(ERROR_NO_DATA) ? S_FALSE : hr;
    return InvokeVerbHelper(GetHwnd(), *pCM, vVerb, vArgs, GetSite());
}

HRESULT STDMETHODCALLTYPE CFolderItems::Filter(LONG grfFlags, BSTR bstrFilter)
{
    if (bstrFilter)
    {
        if (!*bstrFilter)
            bstrFilter = NULL;
        else if ((bstrFilter = SysAllocString(bstrFilter)) == NULL)
            return E_OUTOFMEMORY;
    }
    ResetEnum();
    SysFreeString(m_bstrFilter);
    m_bstrFilter = bstrFilter;
    m_Contf = grfFlags;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItems::get_Verbs(FolderItemVerbs **ppfic)
{
    if (!ppfic)
        return E_POINTER;
    *ppfic = NULL;

    long count;
    HRESULT hr = get_Count(&count);
    if (FAILED(hr))
        return hr;
    if (!count)
        return S_FALSE;

    CComPtr<CFolderItemVerbs> pVerbs;
    hr = CFolderItemVerbs::CreateInstance(pVerbs);
    if (FAILED(hr))
        return hr;
    {
        CComPtr<IContextMenu> pCM;
        if (SUCCEEDED(hr = GetUIObjectOfFolderItems(this, IID_PPV_ARG(IContextMenu, &pCM))))
            hr = pVerbs->Init(*static_cast<IContextMenu*>(pCM));
    }
    *ppfic = pVerbs.Detach();
    return S_OK;
}

HRESULT CFolderItems::InvokeVerbHelper(HWND hWnd, IContextMenu &cm, VARIANT &vVerb, VARIANT &vArgs, IUnknown *pSite)
{
    PCWSTR pszVerb = V_VT(&vVerb) == VT_BSTR && !StrIsNullOrEmpty(V_BSTR(&vVerb)) ? V_BSTR(&vVerb) : NULL;
    PCWSTR pszArgs = V_VT(&vArgs) == VT_BSTR && !StrIsNullOrEmpty(V_BSTR(&vArgs)) ? V_BSTR(&vArgs) : NULL;
    HRESULT hr = SHELL_InvokeCommandOnContextMenu(hWnd, &cm, pszVerb, !!pszVerb, pszArgs, 0, 0, pSite);
    return SUCCEEDED(hr) ? hr : S_FALSE;
}

CFolderItemsFDF::CFolderItemsFDF()
{
}

CFolderItemsFDF::~CFolderItemsFDF()
{
    for (int i = 0; i < m_Items.GetSize(); i++)
        ILFree(m_Items[i]);
    SysFreeString(m_bstrFilter);
}

HRESULT CFolderItemsFDF::SetItems(PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
{
    HRESULT hr = Initialize(pidlFolder);
    if (FAILED(hr))
        return hr;

    for (int i = 0; i < m_Items.GetSize(); i++)
        ILFree(m_Items[i]);
    m_Items.RemoveAll();

    for (UINT i = 0; i < cidl; i++)
    {
        PITEMID_CHILD pidl = ILClone(apidl[i]);
        if (!pidl)
            return E_OUTOFMEMORY;
        if (!m_Items.Add(pidl))
        {
            ILFree(pidl);
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}

BOOL CFolderItemsFDF::IncludeItem(PCUITEMID_CHILD pidl)
{
    CComHeapPtr<ITEMIDLIST> pidlFull;
    if (FAILED(SHILCombine(m_idlist, pidl, &pidlFull)))
        return FALSE;

    if ((m_Contf & (SHCONTF_FOLDERS | SHCONTF_NONFOLDERS)) != (SHCONTF_FOLDERS | SHCONTF_NONFOLDERS))
    {
        BOOL folder = (SHGetAttributes(NULL, pidlFull, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
        if (folder ? !(m_Contf & SHCONTF_FOLDERS) : !(m_Contf & SHCONTF_NONFOLDERS))
            return FALSE;
    }

    if (!m_bstrFilter)
        return TRUE;

    CComHeapPtr<WCHAR> pszPath;
    if (FAILED(SHELL_DisplayNameOf(NULL, pidlFull, SHGDN_FORPARSING | SHGDN_INFOLDER, &pszPath)))
        return FALSE;
    return PathMatchSpecW(pszPath, m_bstrFilter);
}

long CFolderItemsFDF::GetVisibleCount()
{
    long count = 0;

    for (int i = 0; i < m_Items.GetSize(); i++)
    {
        if (IncludeItem(m_Items[i]))
            count++;
    }
    return count;
}

PCUITEMID_CHILD CFolderItemsFDF::GetVisibleItem(long index)
{
    for (int i = 0; i < m_Items.GetSize(); i++)
    {
        if (!IncludeItem(m_Items[i]))
            continue;
        if (index-- == 0)
            return m_Items[i];
    }
    return NULL;
}

HRESULT CFolderItemsFDF::GetContextMenu(IContextMenu **ppcm)
{
    CSimpleArray<PCUITEMID_CHILD> items;
    CComPtr<IShellFolder> psf;
    HRESULT hr;

    for (int i = 0; i < m_Items.GetSize(); i++)
    {
        if (IncludeItem(m_Items[i]) && !items.Add(m_Items[i]))
            return E_OUTOFMEMORY;
    }
    if (!items.GetSize())
        return S_FALSE;

    hr = SHBindToObject(NULL, m_idlist, NULL, IID_PPV_ARG(IShellFolder, &psf));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return psf->GetUIObjectOf(NULL, items.GetSize(), items.GetData(), IID_IContextMenu, NULL, (void **)ppcm);
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::get_Count(long *plCount)
{
    if (!plCount)
        return E_POINTER;
    if (!m_Folder)
        return E_FAIL;

    *plCount = GetVisibleCount();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::get_Application(IDispatch **ppid)
{
    if (!ppid)
        return E_POINTER;
    *ppid = NULL;
    if (!m_Folder)
        return E_FAIL;
    return m_Folder->get_Application(ppid);
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::get_Parent(IDispatch **ppid)
{
    if (ppid)
        *ppid = NULL;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::Item(VARIANT var, FolderItem **ppid)
{
    CComVariant index;
    HRESULT hr;

    if (!ppid)
        return E_POINTER;
    *ppid = NULL;
    if (!m_Folder)
        return E_FAIL;

    hr = VariantCopyInd(&index, &var);
    if (FAILED(hr))
        return hr;

    if (V_VT(&index) == VT_I2)
        VariantChangeType(&index, &index, 0, VT_I4);

    if (V_VT(&index) == VT_I4)
    {
        PCUITEMID_CHILD pidlChild = V_I4(&index) >= 0 ? GetVisibleItem(V_I4(&index)) : NULL;
        if (!pidlChild)
            return S_FALSE;

        CComHeapPtr<ITEMIDLIST> pidlFull;
        if (FAILED_UNEXPECTEDLY(hr = SHILCombine(m_idlist, pidlChild, &pidlFull)))
            return hr;
        return ShellObjectCreatorInit<CFolderItem>(m_Folder, pidlFull, IID_PPV_ARG(FolderItem, ppid));
    }
    else if (V_VT(&index) == VT_BSTR)
    {
        if (!V_BSTR(&index))
            return S_FALSE;

        for (int i = 0; i < m_Items.GetSize(); i++)
        {
            CComHeapPtr<ITEMIDLIST> pidlFull;
            CComHeapPtr<WCHAR> pszName;

            if (!IncludeItem(m_Items[i]))
                continue;
            if (FAILED_UNEXPECTEDLY(hr = SHILCombine(m_idlist, m_Items[i], &pidlFull)))
                return hr;
            if (FAILED(SHELL_DisplayNameOf(NULL, pidlFull, SHGDN_FORPARSING | SHGDN_INFOLDER, &pszName)))
                continue;
            if (!lstrcmpiW(pszName, V_BSTR(&index)))
                return ShellObjectCreatorInit<CFolderItem>(m_Folder, pidlFull, IID_PPV_ARG(FolderItem, ppid));
        }
        return S_OK;
    }

    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::_NewEnum(IUnknown **ppunk)
{
    CComObject<CFolderItemsFDF> *pObj;
    CComPtr<IUnknown> pItems;
    CSimpleArray<PCUITEMID_CHILD> items;
    HRESULT hr;

    if (!ppunk)
        return E_POINTER;
    *ppunk = NULL;
    if (!m_Folder)
        return E_FAIL;

    for (int i = 0; i < m_Items.GetSize(); i++)
    {
        if (IncludeItem(m_Items[i]) && !items.Add(m_Items[i]))
            return E_OUTOFMEMORY;
    }

    hr = CComObject<CFolderItemsFDF>::CreateInstance(&pObj);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    pItems = static_cast<IPersistFolder *>(pObj);
    hr = pObj->SetItems(m_idlist, items.GetSize(), items.GetData());
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pItems->QueryInterface(IID_FolderItems, (void **)ppunk);
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::InvokeVerbEx(VARIANT vVerb, VARIANT vArgs)
{
    CComPtr<IContextMenu> pCM;
    HRESULT hr = GetContextMenu(&pCM);
    if (hr != S_OK)
        return SUCCEEDED(hr) ? S_FALSE : hr;
    return CFolderItems::InvokeVerbHelper(NULL, *pCM, vVerb, vArgs, NULL);
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::Filter(LONG grfFlags, BSTR bstrFilter)
{
    if (bstrFilter)
    {
        if (!*bstrFilter)
            bstrFilter = NULL;
        else if ((bstrFilter = SysAllocString(bstrFilter)) == NULL)
            return E_OUTOFMEMORY;
    }
    SysFreeString(m_bstrFilter);
    m_bstrFilter = bstrFilter;
    m_Contf = grfFlags;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::get_Verbs(FolderItemVerbs **ppfic)
{
    CComPtr<IContextMenu> pCM;
    CComPtr<CFolderItemVerbs> pVerbs;
    HRESULT hr;

    if (!ppfic)
        return E_POINTER;
    *ppfic = NULL;

    hr = GetContextMenu(&pCM);
    if (hr != S_OK)
        return SUCCEEDED(hr) ? S_FALSE : hr;

    hr = CFolderItemVerbs::CreateInstance(pVerbs);
    if (FAILED(hr))
        return hr;
    hr = pVerbs->Init(*static_cast<IContextMenu*>(pCM));
    if (FAILED(hr))
        return hr;
    *ppfic = pVerbs.Detach();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_FolderItemsFDF;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CFolderItemsFDF::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    CComPtr<IDispatch> pApplication;
    CComPtr<Folder> pFolder;
    HRESULT hr;

    if (!pidl)
        return E_INVALIDARG;

    hr = CShellDispatch_Constructor(IID_PPV_ARG(IDispatch, &pApplication));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    hr = CFolder::CreateInstance(pidl, pApplication, IID_PPV_ARG(Folder, &pFolder));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    m_idlist.Free();
    hr = SHILClone(pidl, &m_idlist);
    if (FAILED(hr))
        return hr;
    m_Folder = pFolder;
    return S_OK;
}
