/*
 * IShellItem implementation
 *
 * Copyright 2008 Vincent Povirk for CodeWeavers
 * Copyright 2009 Andrew Hill
 * Copyright 2013 Katayama Hirofumi MZ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "precomp.h"
#include <propvarutil.h>
#include <thumbcache.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

EXTERN_C HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi);

CShellItem::CShellItem() :
    m_pidl(NULL)
{
}

CShellItem::~CShellItem()
{
    ILFree(m_pidl);
}

HRESULT CShellItem::get_parent_pidl(LPITEMIDLIST *parent_pidl)
{
    *parent_pidl = ILClone(m_pidl);
    if (*parent_pidl)
    {
        if (ILRemoveLastID(*parent_pidl))
            return S_OK;
        else
        {
            ILFree(*parent_pidl);
            *parent_pidl = NULL;
            return E_INVALIDARG;
        }
    }
    else
    {
        *parent_pidl = NULL;
        return E_OUTOFMEMORY;
    }
}

HRESULT CShellItem::get_parent_shellfolder(IShellFolder **ppsf)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;
    CComPtr<IShellFolder>        desktop;

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHGetDesktopFolder(&desktop);
        if (SUCCEEDED(hr))
        {
            if (_ILIsDesktop(parent_pidl))
                hr = desktop->QueryInterface(IID_PPV_ARG(IShellFolder, ppsf));
            else
                hr = desktop->BindToObject(parent_pidl, NULL, IID_PPV_ARG(IShellFolder, ppsf));
        }
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT CShellItem::get_shellfolder(IBindCtx *pbc, REFIID riid, void **ppvOut)
{
    CComPtr<IShellFolder> psf;
    CComPtr<IShellFolder> psfDesktop;
    HRESULT ret;

    ret = SHGetDesktopFolder(&psfDesktop);
    if (FAILED_UNEXPECTEDLY(ret))
        return ret;

    if (_ILIsDesktop(m_pidl))
        psf = psfDesktop;
    else
    {
        ret = psfDesktop->BindToObject(m_pidl, pbc, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;
    }

    return psf->QueryInterface(riid, ppvOut);
}

class CShellItemChildEnum :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumShellItems
{
    CComPtr<IShellFolder> m_pFolder;
    CComPtr<IEnumIDList> m_pEnumIDList;
    CComHeapPtr<ITEMIDLIST> m_pidlFolder;

public:
    HRESULT Initialize(PCIDLIST_ABSOLUTE pidlFolder, IShellFolder *pFolder, IEnumIDList *pEnumIDList)
    {
        m_pidlFolder.Attach(ILClone(pidlFolder));
        if (!m_pidlFolder)
            return E_OUTOFMEMORY;
        m_pFolder = pFolder;
        m_pEnumIDList = pEnumIDList;
        return S_OK;
    }

    STDMETHODIMP Next(ULONG celt, IShellItem **rgelt, ULONG *pceltFetched) override
    {
        ULONG Fetched = 0;

        if (!rgelt || (celt > 1 && !pceltFetched))
            return E_INVALIDARG;
        if (pceltFetched)
            *pceltFetched = 0;

        while (Fetched < celt)
        {
            CComHeapPtr<ITEMID_CHILD> pidlChild;
            HRESULT hr = m_pEnumIDList->Next(1, &pidlChild, NULL);
            if (FAILED(hr))
                return hr;
            if (hr != S_OK)
                break;
            hr = SHCreateShellItem(m_pidlFolder, m_pFolder, pidlChild, &rgelt[Fetched]);
            if (FAILED(hr))
            {
                while (Fetched)
                    rgelt[--Fetched]->Release();
                return hr;
            }
            ++Fetched;
        }

        if (pceltFetched)
            *pceltFetched = Fetched;
        return (Fetched == celt) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG celt) override
    {
        return m_pEnumIDList->Skip(celt);
    }

    STDMETHODIMP Reset() override
    {
        return m_pEnumIDList->Reset();
    }

    STDMETHODIMP Clone(IEnumShellItems **ppenum) override
    {
        if (ppenum)
            *ppenum = NULL;
        return E_NOTIMPL;
    }

DECLARE_NO_REGISTRY()
DECLARE_NOT_AGGREGATABLE(CShellItemChildEnum)

BEGIN_COM_MAP(CShellItemChildEnum)
    COM_INTERFACE_ENTRY_IID(IID_IEnumShellItems, IEnumShellItems)
END_COM_MAP()
};

HRESULT WINAPI CShellItem::BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppvOut)
{
    HRESULT ret;
    TRACE("(%p, %p,%s,%p,%p)\n", this, pbc, shdebugstr_guid(&rbhid), riid, ppvOut);

    *ppvOut = NULL;
    if (IsEqualGUID(rbhid, BHID_SFObject))
    {
        return get_shellfolder(pbc, riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFUIObject))
    {
        CComPtr<IShellFolder> psf_parent;
        if (_ILIsDesktop(m_pidl))
            ret = SHGetDesktopFolder(&psf_parent);
        else
            ret = get_parent_shellfolder(&psf_parent);
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        LPCITEMIDLIST pidl = ILFindLastID(m_pidl);
        return psf_parent->GetUIObjectOf(NULL, 1, &pidl, riid, NULL, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_DataObject))
    {
        return BindToHandler(pbc, BHID_SFUIObject, IID_IDataObject, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFViewObject))
    {
        CComPtr<IShellFolder> psf;
        ret = get_shellfolder(NULL, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        return psf->CreateViewObject(NULL, riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_PropertyStore))
    {
        CComPtr<IShellFolder> psf_parent;
        if (_ILIsDesktop(m_pidl))
            return MK_E_NOOBJECT;
        ret = get_parent_shellfolder(&psf_parent);
        if (FAILED(ret))
            return ret;
        return psf_parent->BindToObject(ILFindLastID(m_pidl), pbc, riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_ThumbnailHandler))
    {
        CComPtr<IShellFolder> psf_parent;
        if (_ILIsDesktop(m_pidl))
            return MK_E_NOOBJECT;
        ret = get_parent_shellfolder(&psf_parent);
        if (FAILED(ret))
            return ret;

        LPCITEMIDLIST pidl = ILFindLastID(m_pidl);
        ret = psf_parent->GetUIObjectOf(NULL, 1, &pidl, riid, NULL, ppvOut);
        return ret == E_NOINTERFACE ? E_NOTIMPL : ret;
    }

    else if (IsEqualGUID(rbhid, BHID_EnumItems) || IsEqualGUID(rbhid, BHID_StorageEnum))
    {
        CComPtr<IShellFolder> psf;
        CComPtr<IEnumIDList> pEnumIDList;
        CComPtr<IEnumShellItems> pEnum;

        ret = get_shellfolder(pbc, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED(ret))
            return ret;
        ret = psf->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &pEnumIDList);
        if (FAILED(ret))
            return ret;
        if (ret != S_OK || !pEnumIDList)
            return MK_E_NOOBJECT;
        ret = ShellObjectCreatorInit<CShellItemChildEnum>(static_cast<PCIDLIST_ABSOLUTE>(m_pidl), static_cast<IShellFolder*>(psf),
                                                         static_cast<IEnumIDList*>(pEnumIDList), IID_PPV_ARG(IEnumShellItems, &pEnum));
        if (FAILED(ret))
            return ret;
        return pEnum->QueryInterface(riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_LinkTargetItem))
    {
        CComPtr<IShellLinkW> pShellLink;
        CComHeapPtr<ITEMIDLIST> pidlTarget;

        if (FAILED(BindToHandler(pbc, BHID_SFUIObject, IID_PPV_ARG(IShellLinkW, &pShellLink))))
            return E_NOINTERFACE;
        ret = pShellLink->GetIDList(&pidlTarget);
        if (ret != S_OK)
            return FAILED(ret) ? ret : E_FAIL;
        return SHCreateItemFromIDList(pidlTarget, riid, ppvOut);
    }

    FIXME("Unsupported BHID %s.\n", debugstr_guid(&rbhid));

    return MK_E_NOOBJECT;
}

HRESULT WINAPI CShellItem::GetParent(IShellItem **ppsi)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;

    TRACE("(%p,%p)\n", this, ppsi);

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateShellItem(NULL, NULL, parent_pidl, ppsi);
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT WINAPI CShellItem::GetDisplayName(SIGDN sigdnName, LPWSTR *ppszName)
{
    if (!m_pidl)
    {
        if (ppszName)
            *ppszName = NULL;
        return E_INVALIDARG;
    }
    return SHGetNameFromIDList(m_pidl, sigdnName, ppszName);
}

HRESULT WINAPI CShellItem::GetAttributes(SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs)
{
    CComPtr<IShellFolder>        parent_folder;
    LPCITEMIDLIST child_pidl;
    HRESULT hr;

    TRACE("(%p,%x,%p)\n", this, sfgaoMask, psfgaoAttribs);

    if (_ILIsDesktop(m_pidl))
        hr = SHGetDesktopFolder(&parent_folder);
    else
        hr = get_parent_shellfolder(&parent_folder);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    child_pidl = ILFindLastID(m_pidl);
    *psfgaoAttribs = sfgaoMask;
    hr = parent_folder->GetAttributesOf(1, &child_pidl, psfgaoAttribs);
    *psfgaoAttribs &= sfgaoMask;

    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return (sfgaoMask == *psfgaoAttribs) ? S_OK : S_FALSE;
}

HRESULT WINAPI CShellItem::Compare(IShellItem *oth, SICHINTF hint, int *piOrder)
{
    HRESULT hr;
    CComPtr<IPersistIDList>      pIDList;
    CComPtr<IShellFolder>        psfDesktop;
    LPITEMIDLIST pidl;

    TRACE("(%p,%p,%x,%p)\n", this, oth, hint, piOrder);

    if (piOrder == NULL || oth == NULL)
        return E_POINTER;

    hr = oth->QueryInterface(IID_PPV_ARG(IPersistIDList, &pIDList));
    if (SUCCEEDED(hr))
    {
        hr = pIDList->GetIDList(&pidl);
        if (SUCCEEDED(hr))
        {
            hr = SHGetDesktopFolder(&psfDesktop);
            if (SUCCEEDED(hr))
            {
                hr = psfDesktop->CompareIDs(hint, m_pidl, pidl);
                *piOrder = (int)(short)SCODE_CODE(hr);
            }
            ILFree(pidl);
        }
    }

    if(FAILED(hr))
        return hr;

    if(*piOrder)
        return S_FALSE;
    else
        return S_OK;
}

HRESULT WINAPI CShellItem::GetClassID(CLSID *pClassID)
{
    TRACE("(%p,%p)\n", this, pClassID);

    *pClassID = CLSID_ShellItem;
    return S_OK;
}

HRESULT WINAPI CShellItem::SetIDList(PCIDLIST_ABSOLUTE pidlx)
{
    LPITEMIDLIST new_pidl;

    TRACE("(%p,%p)\n", this, pidlx);

    new_pidl = ILClone(pidlx);
    if (new_pidl)
    {
        ILFree(m_pidl);
        m_pidl = new_pidl;
        return S_OK;
    }
    else
        return E_OUTOFMEMORY;
}

HRESULT WINAPI CShellItem::GetIDList(PIDLIST_ABSOLUTE *ppidl)
{
    TRACE("(%p,%p)\n", this, ppidl);

    if (!m_pidl)
    {
        *ppidl = NULL;
        return E_INVALIDARG;
    }

    *ppidl = ILClone(m_pidl);
    if (*ppidl)
        return S_OK;
    else
        return E_OUTOFMEMORY;
}

HRESULT CShellItem::GetDefaultPropertyStore(IPropertyStore **ppStore)
{
    CComPtr<IShellFolder> psf;
    CComPtr<IShellFolder2> psf2;
    CComPtr<IPropertyStore> pStore;
    PCUITEMID_CHILD pidlChild;
    SHCOLUMNID scid;
    HRESULT hr;

    *ppStore = NULL;
    if (!m_pidl)
        return E_INVALIDARG;

    hr = SHBindToParent(m_pidl, IID_PPV_ARG(IShellFolder, &psf), &pidlChild);
    if (FAILED(hr))
        return hr;
    hr = psf->QueryInterface(IID_PPV_ARG(IShellFolder2, &psf2));
    if (FAILED(hr))
        return hr;
    hr = PSCreateMemoryPropertyStore(IID_PPV_ARG(IPropertyStore, &pStore));
    if (FAILED(hr))
        return hr;

    for (UINT iColumn = 0; SUCCEEDED(psf2->MapColumnToSCID(iColumn, &scid)); ++iColumn)
    {
        VARIANT var;
        PROPVARIANT propvar;

        VariantInit(&var);
        if (FAILED(psf2->GetDetailsEx(pidlChild, &scid, &var)))
            continue;
        if (SUCCEEDED(VariantToPropVariant(&var, &propvar)))
        {
            PROPERTYKEY key = { scid.fmtid, scid.pid };
            pStore->SetValue(key, propvar);
            PropVariantClear(&propvar);
        }
        VariantClear(&var);
    }

    *ppStore = pStore.Detach();
    return S_OK;
}

HRESULT WINAPI CShellItem::GetPropertyStore(GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv)
{
    return GetPropertyStoreWithCreateObject(flags, NULL, riid, ppv);
}

HRESULT WINAPI CShellItem::GetPropertyStoreWithCreateObject(GETPROPERTYSTOREFLAGS flags, IUnknown *punkCreateObject, REFIID riid, void **ppv)
{
    CComPtr<IPropertyStoreFactory> pFactory;
    CComPtr<IPropertyStore> pStore;
    HRESULT hr;

    *ppv = NULL;
    if (SUCCEEDED(BindToHandler(NULL, BHID_PropertyStore, IID_PPV_ARG(IPropertyStoreFactory, &pFactory))))
        return pFactory->GetPropertyStore(flags, punkCreateObject, riid, ppv);
    if (flags & GPS_READWRITE)
        return STG_E_ACCESSDENIED;

    hr = GetDefaultPropertyStore(&pStore);
    if (FAILED(hr))
        return hr;
    return pStore->QueryInterface(riid, ppv);
}

HRESULT WINAPI CShellItem::GetPropertyStoreForKeys(const PROPERTYKEY *rgKeys, UINT cKeys, GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv)
{
    CComPtr<IPropertyStoreFactory> pFactory;

    if (SUCCEEDED(BindToHandler(NULL, BHID_PropertyStore, IID_PPV_ARG(IPropertyStoreFactory, &pFactory))))
    {
        *ppv = NULL;
        return pFactory->GetPropertyStoreForKeys(rgKeys, cKeys, flags, riid, ppv);
    }
    return GetPropertyStore(flags, riid, ppv);
}

HRESULT WINAPI CShellItem::GetPropertyDescriptionList(REFPROPERTYKEY keyType, REFIID riid, void **ppv)
{
    static const ASSOCKEY Keys[] = { ASSOCKEY_CLASS, ASSOCKEY_BASECLASS };
    CComHeapPtr<WCHAR> pszName;
    CComPtr<IQueryAssociations> pqa;
    PCWSTR pszValue;
    HRESULT hr;

    *ppv = NULL;
    if (!m_pidl)
        return E_INVALIDARG;

    hr = PSGetNameFromPropertyKey(keyType, &pszName);
    if (FAILED(hr))
        return hr;
    pszValue = wcsrchr(pszName, L'.');
    pszValue = pszValue ? pszValue + 1 : (PCWSTR)pszName;

    PCUITEMID_CHILD pidlLast = ILFindLastID(m_pidl);
    if (_ILIsValue(pidlLast) || _ILIsFolder(pidlLast))
    {
        HKEY hKeys[16];
        UINT cKeys = 0;
        HRESULT hrList = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        if (_ILIsValue(pidlLast))
        {
            AddFSClassKeysToArray(1, &pidlLast, hKeys, &cKeys);
        }
        else
        {
            AddClassKeyToArray(L"Directory", hKeys, &cKeys);
            AddClassKeyToArray(L"Folder", hKeys, &cKeys);
            AddClassKeyToArray(L"AllFilesystemObjects", hKeys, &cKeys);
        }
        for (UINT i = 0; i < cKeys; ++i)
        {
            WCHAR szList[2048];
            DWORD cbList = sizeof(szList);

            if (RegGetValueW(hKeys[i], NULL, pszValue, RRF_RT_REG_SZ, NULL, szList, &cbList) == ERROR_SUCCESS)
            {
                hrList = PSGetPropertyDescriptionListFromString(szList, riid, ppv);
                break;
            }
        }
        CloseRegKeyArray(hKeys, cKeys);
        return hrList;
    }

    hr = BindToHandler(NULL, BHID_SFUIObject, IID_PPV_ARG(IQueryAssociations, &pqa));
    if (FAILED(hr))
        return hr;

    for (SIZE_T i = 0; i < _countof(Keys); ++i)
    {
        HKEY hKey;
        WCHAR szList[1024];
        DWORD cbList = sizeof(szList);

        if (FAILED(pqa->GetKey(0, Keys[i], NULL, &hKey)))
            continue;
        LSTATUS error = RegGetValueW(hKey, NULL, pszValue, RRF_RT_REG_SZ, NULL, szList, &cbList);
        RegCloseKey(hKey);
        if (error == ERROR_SUCCESS)
            return PSGetPropertyDescriptionListFromString(szList, riid, ppv);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT WINAPI CShellItem::Update(IBindCtx *pbc)
{
    CComHeapPtr<WCHAR> pszName;
    PIDLIST_ABSOLUTE pidlNew;
    HRESULT hr;

    if (!m_pidl)
        return E_INVALIDARG;

    hr = SHGetNameFromIDList(m_pidl, SIGDN_DESKTOPABSOLUTEPARSING, &pszName);
    if (FAILED(hr))
        return hr;
    hr = SHParseDisplayName(pszName, pbc, &pidlNew, 0, NULL);
    if (FAILED(hr))
        return hr;
    ILFree(m_pidl);
    m_pidl = pidlNew;
    return S_OK;
}

HRESULT WINAPI CShellItem::GetProperty(REFPROPERTYKEY key, PROPVARIANT *ppropvar)
{
    CComPtr<IPropertyStore> pStore;
    HRESULT hr;

    PropVariantInit(ppropvar);
    hr = GetPropertyStore(GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &pStore));
    if (FAILED(hr))
        return hr;
    return pStore->GetValue(key, ppropvar);
}

HRESULT CShellItem::GetPresentProperty(REFPROPERTYKEY key, PROPVARIANT *ppropvar)
{
    HRESULT hr = GetProperty(key, ppropvar);
    if (SUCCEEDED(hr) && ppropvar->vt == VT_EMPTY)
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    return hr;
}

HRESULT WINAPI CShellItem::GetCLSID(REFPROPERTYKEY key, CLSID *pclsid)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToGUID(var, pclsid);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetFileTime(REFPROPERTYKEY key, FILETIME *pft)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToFileTime(var, PSTF_UTC, pft);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetInt32(REFPROPERTYKEY key, int *pi)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        LONG value = 0;
        hr = PropVariantToInt32(var, &value);
        *pi = value;
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetString(REFPROPERTYKEY key, LPWSTR *ppsz)
{
    PROPVARIANT var;
    HRESULT hr;

    *ppsz = NULL;
    hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToStringAlloc(var, ppsz);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetUInt32(REFPROPERTYKEY key, ULONG *pui)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToUInt32(var, pui);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetUInt64(REFPROPERTYKEY key, ULONGLONG *pull)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToUInt64(var, pull);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::GetBool(REFPROPERTYKEY key, BOOL *pf)
{
    PROPVARIANT var;
    HRESULT hr = GetPresentProperty(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = PropVariantToBoolean(var, pf);
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT WINAPI CShellItem::IsDirty()
{
    return S_FALSE;
}

HRESULT WINAPI CShellItem::Load(IStream *pStm)
{
    LPITEMIDLIST pidl = NULL;
    DWORD dwExtra;
    HRESULT hr;

    hr = ILLoadFromStream(pStm, &pidl);
    if (FAILED(hr))
        return hr;
    if (!pidl)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    pStm->Read(&dwExtra, sizeof(dwExtra), NULL);

    ILFree(m_pidl);
    m_pidl = pidl;
    return S_OK;
}

HRESULT WINAPI CShellItem::Save(IStream *pStm, BOOL fClearDirty)
{
    DWORD dwExtra = 0;
    HRESULT hr;

    if (!m_pidl)
        return E_INVALIDARG;
    hr = ILSaveToStream(pStm, m_pidl);
    if (FAILED(hr))
        return hr;
    return pStm->Write(&dwExtra, sizeof(dwExtra), NULL);
}

HRESULT WINAPI CShellItem::GetSizeMax(ULARGE_INTEGER *pcbSize)
{
    if (!pcbSize)
        return E_POINTER;
    if (!m_pidl)
        return E_INVALIDARG;
    pcbSize->QuadPart = sizeof(WORD) + ILGetSize(m_pidl) + sizeof(DWORD) + sizeof(CLSID);
    return S_OK;
}

HRESULT WINAPI CShellItem::SetParentAndItem(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psf, PCUITEMID_CHILD pidlChild)
{
    LPITEMIDLIST pidl;

    if (!pidlParent || !pidlChild)
        return E_INVALIDARG;
    pidl = ILCombine(pidlParent, pidlChild);
    if (!pidl)
        return E_OUTOFMEMORY;
    ILFree(m_pidl);
    m_pidl = pidl;
    return S_OK;
}

HRESULT WINAPI CShellItem::GetParentAndItem(PIDLIST_ABSOLUTE *ppidlParent, IShellFolder **ppsf, PITEMID_CHILD *ppidlChild)
{
    CComPtr<IShellFolder> psf;
    PCUITEMID_CHILD pidlLast;
    PIDLIST_ABSOLUTE pidlParent = NULL;
    PITEMID_CHILD pidlChild = NULL;
    HRESULT hr;

    if (ppidlParent)
        *ppidlParent = NULL;
    if (ppsf)
        *ppsf = NULL;
    if (ppidlChild)
        *ppidlChild = NULL;
    if (!m_pidl)
        return E_INVALIDARG;

    hr = SHBindToParent(m_pidl, IID_PPV_ARG(IShellFolder, &psf), &pidlLast);
    if (FAILED(hr))
        return hr;

    if (ppidlChild && !(pidlChild = ILClone(pidlLast)))
        return E_OUTOFMEMORY;
    if (ppidlParent)
    {
        pidlParent = ILClone(m_pidl);
        if (!pidlParent)
        {
            ILFree(pidlChild);
            return E_OUTOFMEMORY;
        }
        ILRemoveLastID(pidlParent);
    }

    if (ppidlParent)
        *ppidlParent = pidlParent;
    if (ppidlChild)
        *ppidlChild = pidlChild;
    if (ppsf)
        *ppsf = psf.Detach();
    return S_OK;
}

HRESULT WINAPI CShellItem::GetUnmarshalClass(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, CLSID *pCid)
{
    *pCid = CLSID_ShellItem;
    return S_OK;
}

HRESULT WINAPI CShellItem::GetMarshalSizeMax(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, DWORD *pSize)
{
    ULARGE_INTEGER cbSize;
    HRESULT hr = GetSizeMax(&cbSize);
    if (SUCCEEDED(hr))
        *pSize = cbSize.LowPart;
    return hr;
}

HRESULT WINAPI CShellItem::MarshalInterface(IStream *pStm, REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags)
{
    return Save(pStm, FALSE);
}

HRESULT WINAPI CShellItem::UnmarshalInterface(IStream *pStm, REFIID riid, void **ppv)
{
    HRESULT hr;

    *ppv = NULL;
    hr = Load(pStm);
    if (FAILED(hr))
        return hr;
    return QueryInterface(riid, ppv);
}

HRESULT WINAPI CShellItem::ReleaseMarshalData(IStream *pStm)
{
    LPITEMIDLIST pidl = NULL;
    DWORD dwExtra;

    if (SUCCEEDED(ILLoadFromStream(pStm, &pidl)) && pidl)
    {
        pStm->Read(&dwExtra, sizeof(dwExtra), NULL);
        ILFree(pidl);
    }
    return S_OK;
}

HRESULT WINAPI CShellItem::DisconnectObject(DWORD dwReserved)
{
    return S_OK;
}

HRESULT WINAPI CShellItem::RemoveBackReferences()
{
    return S_OK;
}

HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi)
{
    HRESULT hr;
    CComPtr<IShellItem> newShellItem;
    LPITEMIDLIST new_pidl;
    CComPtr<IPersistIDList>            newPersistIDList;

    TRACE("(%p,%p,%p,%p)\n", pidlParent, psfParent, pidl, ppsi);

    *ppsi = NULL;

    if (!pidl)
        return E_INVALIDARG;

    if (pidlParent || psfParent)
    {
        LPITEMIDLIST temp_parent = NULL;
        if (!pidlParent)
        {
            CComPtr<IPersistFolder2>    ppf2Parent;

            if (FAILED(psfParent->QueryInterface(IID_PPV_ARG(IPersistFolder2, &ppf2Parent))))
            {
                FIXME("couldn't get IPersistFolder2 interface of parent\n");
                return E_NOINTERFACE;
            }

            if (FAILED(ppf2Parent->GetCurFolder(&temp_parent)))
            {
                FIXME("couldn't get parent PIDL\n");
                return E_NOINTERFACE;
            }

            pidlParent = temp_parent;
        }

        new_pidl = ILCombine(pidlParent, pidl);
        ILFree(temp_parent);

        if (!new_pidl)
            return E_OUTOFMEMORY;
    }
    else
    {
        new_pidl = ILClone(pidl);
        if (!new_pidl)
            return E_OUTOFMEMORY;
    }

    hr = CShellItem::_CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellItem, &newShellItem));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newShellItem->QueryInterface(IID_PPV_ARG(IPersistIDList, &newPersistIDList));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newPersistIDList->SetIDList(new_pidl);
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    ILFree(new_pidl);

    *ppsi = newShellItem.Detach();

    return hr;
}

HRESULT WINAPI SHCreateItemFromIDList(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **ppv)
{
    CComPtr<IShellItem> item;
    HRESULT hr;

    if (!pidl)
        return E_INVALIDARG;

    *ppv = NULL;
    hr = SHCreateShellItem(NULL, NULL, pidl, &item);
    if (SUCCEEDED(hr))
        hr = item->QueryInterface(riid, ppv);
    return hr;
}

HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx *pbc,
    REFIID riid, void **ppv)
{
    LPITEMIDLIST pidl;
    HRESULT hr;

    *ppv = NULL;
    hr = SHParseDisplayName(pszPath, pbc, &pidl, 0, NULL);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateItemFromIDList(pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}

EXTERN_C HRESULT WINAPI SHGetPropertyStoreFromParsingName(PCWSTR pszPath, IBindCtx *pbc,
    GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv)
{
    CComPtr<IShellItem2> item;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    hr = SHCreateItemFromParsingName(pszPath, pbc, IID_PPV_ARG(IShellItem2, &item));
    if (SUCCEEDED(hr))
        hr = item->GetPropertyStore(flags, riid, ppv);
    return hr;
}

HRESULT WINAPI SHCreateItemFromRelativeName(IShellItem *parent, PCWSTR name, IBindCtx *pbc,
    REFIID riid, void **ppv)
{
    CComPtr<IShellFolder> desktop, folder;
    LPITEMIDLIST pidlFolder = NULL, pidl = NULL;
    HRESULT hr;

    TRACE("(%p, %s, %p, %s, %p)\n", parent, debugstr_w(name), pbc,
          debugstr_guid(&riid), ppv);

    if (!ppv)
        return E_INVALIDARG;
    *ppv = NULL;
    if (!name)
        return E_INVALIDARG;

    hr = SHGetIDListFromObject(parent, &pidlFolder);
    if (hr != S_OK)
        return hr;

    hr = SHGetDesktopFolder(&desktop);
    if (hr != S_OK)
        goto cleanup;

    if (!_ILIsDesktop(pidlFolder))
    {
        hr = desktop->BindToObject(pidlFolder, NULL, IID_PPV_ARG(IShellFolder, &folder));
        if (hr != S_OK)
            goto cleanup;
    }

    hr = (folder ? folder : desktop)->ParseDisplayName(NULL, pbc, const_cast<LPWSTR>(name),
                                                       NULL, &pidl, NULL);
    if (hr == S_OK)
        hr = SHCreateItemFromIDList(pidl, riid, ppv);

cleanup:
    ILFree(pidlFolder);
    ILFree(pidl);
    return hr;
}

HRESULT WINAPI SHGetItemFromObject(IUnknown *punk, REFIID riid, void **ppv)
{
    CComPtr<IShellItem> pItem;
    LPITEMIDLIST pidl;
    HRESULT hr;

    if (punk && SUCCEEDED(punk->QueryInterface(IID_PPV_ARG(IShellItem, &pItem))))
        return pItem->QueryInterface(riid, ppv);

    hr = SHGetIDListFromObject(punk, &pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateItemFromIDList(pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}

static BOOL
IsIDListInBlock(const BYTE *pbBlock, SIZE_T cbBlock, UINT uOffset)
{
    SIZE_T Offset = uOffset;

    for (;;)
    {
        USHORT cb;

        if (Offset > cbBlock || cbBlock - Offset < sizeof(USHORT))
            return FALSE;
        CopyMemory(&cb, pbBlock + Offset, sizeof(cb));
        if (!cb)
            return TRUE;
        if (cb < sizeof(USHORT) || cb > cbBlock - Offset)
            return FALSE;
        Offset += cb;
    }
}

static HRESULT
GetItemFromHIDA(IDataObject *pdtobj, DATAOBJ_GET_ITEM_FLAGS dwFlags, REFIID riid, void **ppv)
{
    STGMEDIUM medium = { 0 };
    CIDA *pcida;
    HRESULT hr = CDataObjectHIDA::CreateCIDA(pdtobj, &pcida, medium);

    if (FAILED(hr))
        return hr;

    SIZE_T cbBlock = GlobalSize(medium.hGlobal);
    hr = E_FAIL;
    if (cbBlock >= FIELD_OFFSET(CIDA, aoffset) + 2 * sizeof(UINT) &&
        pcida->cidl >= 1 &&
        (pcida->cidl == 1 || !(dwFlags & DOGIF_ONLY_IF_ONE)) &&
        IsIDListInBlock((const BYTE *)pcida, cbBlock, pcida->aoffset[0]) &&
        IsIDListInBlock((const BYTE *)pcida, cbBlock, pcida->aoffset[1]))
    {
        LPITEMIDLIST pidl = ILCombine(HIDA_GetPIDLFolder(pcida), HIDA_GetPIDLItem(pcida, 0));
        hr = pidl ? SHCreateItemFromIDList(pidl, riid, ppv) : E_OUTOFMEMORY;
        ILFree(pidl);
    }

    CDataObjectHIDA::DestroyCIDA(pcida, medium);
    return hr;
}

static HRESULT
GetItemFromHDROP(IDataObject *pdtobj, DATAOBJ_GET_ITEM_FLAGS dwFlags, REFIID riid, void **ppv)
{
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = { 0 };
    HRESULT hr = pdtobj->GetData(&fmt, &medium);

    if (FAILED(hr))
        return hr;

    hr = E_FAIL;
    DROPFILES *pDrop = (DROPFILES *)GlobalLock(medium.hGlobal);
    if (pDrop)
    {
        SIZE_T cbBlock = GlobalSize(medium.hGlobal);
        if (cbBlock >= sizeof(*pDrop) && pDrop->fWide && pDrop->pFiles >= sizeof(*pDrop) &&
            pDrop->pFiles < cbBlock && ((cbBlock - pDrop->pFiles) / sizeof(WCHAR)) >= 2)
        {
            PCWSTR pszFirst = (PCWSTR)((LPBYTE)pDrop + pDrop->pFiles);
            SIZE_T cchMax = (cbBlock - pDrop->pFiles) / sizeof(WCHAR);
            SIZE_T cchFirst = wcsnlen(pszFirst, cchMax);

            if (cchFirst && cchFirst < cchMax &&
                (!(dwFlags & DOGIF_ONLY_IF_ONE) || (cchFirst + 1 < cchMax && !pszFirst[cchFirst + 1])))
            {
                hr = SHCreateItemFromParsingName(pszFirst, NULL, riid, ppv);
            }
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return hr;
}

EXTERN_C HRESULT WINAPI
SHGetItemFromDataObject(IDataObject *pdtobj, DATAOBJ_GET_ITEM_FLAGS dwFlags, REFIID riid, void **ppv)
{
    HRESULT hr;

    if (!pdtobj || !ppv)
        return E_INVALIDARG;
    *ppv = NULL;

    hr = GetItemFromHIDA(pdtobj, dwFlags, riid, ppv);
    if (FAILED(hr) && !(dwFlags & DOGIF_NO_HDROP))
        hr = GetItemFromHDROP(pdtobj, dwFlags, riid, ppv);
    return hr;
}

class CShellItemArrayEnum :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumShellItems
{
    CComPtr<IShellItemArray> m_pArray;
    DWORD m_Index;

public:
    CShellItemArrayEnum() : m_Index(0)
    {
    }

    HRESULT Initialize(IShellItemArray *pArray)
    {
        m_pArray = pArray;
        return S_OK;
    }

    STDMETHODIMP Next(ULONG celt, IShellItem **rgelt, ULONG *pceltFetched) override
    {
        DWORD Count = 0;
        ULONG Fetched = 0;

        if (!rgelt || (celt > 1 && !pceltFetched))
            return E_INVALIDARG;
        if (pceltFetched)
            *pceltFetched = 0;

        HRESULT hr = m_pArray->GetCount(&Count);
        if (FAILED(hr))
            return hr;
        while (Fetched < celt && m_Index < Count)
        {
            hr = m_pArray->GetItemAt(m_Index, &rgelt[Fetched]);
            if (FAILED(hr))
            {
                while (Fetched)
                    rgelt[--Fetched]->Release();
                return hr;
            }
            ++m_Index;
            ++Fetched;
        }

        if (pceltFetched)
            *pceltFetched = Fetched;
        return (Fetched == celt) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG celt) override
    {
        DWORD Count = 0;
        HRESULT hr = m_pArray->GetCount(&Count);
        if (FAILED(hr))
            return hr;
        if (celt > Count - min(m_Index, Count))
        {
            m_Index = Count;
            return S_FALSE;
        }
        m_Index += celt;
        return S_OK;
    }

    STDMETHODIMP Reset() override
    {
        m_Index = 0;
        return S_OK;
    }

    STDMETHODIMP Clone(IEnumShellItems **ppenum) override
    {
        if (ppenum)
            *ppenum = NULL;
        return E_NOTIMPL;
    }

DECLARE_NO_REGISTRY()
DECLARE_NOT_AGGREGATABLE(CShellItemArrayEnum)

BEGIN_COM_MAP(CShellItemArrayEnum)
    COM_INTERFACE_ENTRY_IID(IID_IEnumShellItems, IEnumShellItems)
END_COM_MAP()
};

class CShellItemArray :
    public CComCoClass<CShellItemArray, &CLSID_NULL>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellItemArray
{
    CIDA *m_pCIDA;
    STGMEDIUM m_Medium;

public:
    CShellItemArray() : m_pCIDA(NULL)
    {
        m_Medium.tymed = TYMED_NULL;
    }

    virtual ~CShellItemArray()
    {
        CDataObjectHIDA::DestroyCIDA(m_pCIDA, m_Medium);
    }

    HRESULT Initialize(IDataObject *pdo)
    {
        return CDataObjectHIDA::CreateCIDA(pdo, &m_pCIDA, m_Medium);
    }

    inline UINT GetCount() const { return m_pCIDA->cidl; }

    // IShellItemArray
    STDMETHODIMP BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyStore(GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyDescriptionList(REFPROPERTYKEY keyType, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetAttributes(SIATTRIBFLAGS dwAttribFlags, SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs) override
    {
        SIATTRIBFLAGS Combine = (SIATTRIBFLAGS)(dwAttribFlags & SIATTRIBFLAGS_MASK);
        SFGAOF Result;

        if (!psfgaoAttribs)
            return E_INVALIDARG;
        *psfgaoAttribs = 0;
        if (Combine != SIATTRIBFLAGS_AND && Combine != SIATTRIBFLAGS_OR && Combine != SIATTRIBFLAGS_APPCOMPAT)
            return E_INVALIDARG;
        if (!m_pCIDA)
            return E_UNEXPECTED;

        Result = (Combine == SIATTRIBFLAGS_OR) ? 0 : sfgaoMask;
        for (UINT i = 0; i < GetCount(); ++i)
        {
            CComPtr<IShellItem> pItem;
            SFGAOF Attribs = 0;
            HRESULT hr = GetItemAt(i, &pItem);
            if (FAILED(hr))
                return hr;
            hr = pItem->GetAttributes(sfgaoMask, &Attribs);
            if (FAILED(hr))
                return hr;
            if (Combine == SIATTRIBFLAGS_OR)
                Result |= Attribs & sfgaoMask;
            else
                Result &= Attribs;
        }

        *psfgaoAttribs = Result;
        return (Result == sfgaoMask) ? S_OK : S_FALSE;
    }

    STDMETHODIMP GetCount(DWORD*pCount) override
    {
        *pCount = m_pCIDA ? GetCount() : 0;
        return S_OK;
    }

    STDMETHODIMP GetItemAt(DWORD nIndex, IShellItem **ppItem) override
    {
        if (!ppItem)
            return E_INVALIDARG;
        *ppItem = NULL;
        if (!m_pCIDA)
            return E_UNEXPECTED;
        if (nIndex >= GetCount())
            return E_FAIL;
        return SHCreateShellItem(HIDA_GetPIDLFolder(m_pCIDA), NULL,
                                 HIDA_GetPIDLItem(m_pCIDA, nIndex), ppItem);
    }

    STDMETHODIMP EnumItems(IEnumShellItems **ppESI) override
    {
        if (!ppESI)
            return E_INVALIDARG;
        *ppESI = NULL;
        return ShellObjectCreatorInit<CShellItemArrayEnum>(static_cast<IShellItemArray*>(this),
                                                           IID_PPV_ARG(IEnumShellItems, ppESI));
    }

DECLARE_NO_REGISTRY()
DECLARE_NOT_AGGREGATABLE(CShellItemArray)

BEGIN_COM_MAP(CShellItemArray)
    COM_INTERFACE_ENTRY_IID(IID_IShellItemArray, IShellItemArray)
END_COM_MAP()
};

EXTERN_C HRESULT WINAPI
SHCreateShellItemArrayFromDataObject(_In_ IDataObject *pdo, _In_ REFIID riid, _Out_ void **ppv)
{
    if (!pdo)
    {
        *ppv = NULL;
        return E_INVALIDARG;
    }
    return ShellObjectCreatorInit<CShellItemArray>(pdo, riid, ppv);
}

EXTERN_C HRESULT WINAPI
SHCreateShellItemArray(_In_opt_ PCIDLIST_ABSOLUTE pidlParent, _In_opt_ IShellFolder *psf,
                       _In_ UINT cidl, _In_reads_(cidl) PCUITEMID_CHILD_ARRAY ppidl,
                       _Out_ IShellItemArray **ppsiItemArray)
{
    CComPtr<IDataObject> dataObject;
    PIDLIST_ABSOLUTE allocatedParent = NULL;
    HRESULT hr;

    if (!pidlParent && !psf)
        return E_POINTER;
    if (!ppsiItemArray || !ppidl || !cidl)
        return E_INVALIDARG;
    *ppsiItemArray = NULL;

    if (!pidlParent)
    {
        hr = SHGetIDListFromObject(psf, &allocatedParent);
        if (FAILED(hr))
            return hr;
        pidlParent = allocatedParent;
    }

    hr = SHCreateDataObject(pidlParent, cidl, ppidl, NULL, IID_PPV_ARG(IDataObject, &dataObject));
    if (SUCCEEDED(hr))
        hr = SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARG(IShellItemArray, ppsiItemArray));
    ILFree(allocatedParent);
    return hr;
}

EXTERN_C HRESULT WINAPI
SHCreateShellItemArrayFromIDLists(_In_ UINT cidl, _In_reads_(cidl) PCIDLIST_ABSOLUTE_ARRAY rgpidl,
                                  _Out_ IShellItemArray **ppsiItemArray)
{
    static const USHORT EmptyIDList = 0;
    CComPtr<IDataObject> dataObject;
    CComHeapPtr<PCIDLIST_ABSOLUTE> items;
    HRESULT hr;
    UINT i;

    *ppsiItemArray = NULL;
    if (!cidl || !rgpidl)
        return E_INVALIDARG;
    if (!rgpidl[0])
        return E_OUTOFMEMORY;
    if (!items.Allocate(cidl))
        return E_OUTOFMEMORY;
    for (i = 0; i < cidl; i++)
        items[i] = rgpidl[i] ? rgpidl[i] : (PCIDLIST_ABSOLUTE)&EmptyIDList;

    hr = SHCreateDataObject(NULL, cidl, (PCUITEMID_CHILD_ARRAY)(PCIDLIST_ABSOLUTE *)items, NULL,
                            IID_PPV_ARG(IDataObject, &dataObject));
    if (SUCCEEDED(hr))
        hr = SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARG(IShellItemArray, ppsiItemArray));
    return hr;
}

EXTERN_C HRESULT WINAPI
SHCreateShellItemArrayFromShellItem(_In_ IShellItem *psi, _In_ REFIID riid, _Out_ void **ppv)
{
    CComPtr<IShellItemArray> itemArray;
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr;

    *ppv = NULL;
    hr = SHGetIDListFromObject(psi, &pidl);
    if (FAILED(hr))
        return hr;
    hr = SHCreateShellItemArrayFromIDLists(1, (PCIDLIST_ABSOLUTE_ARRAY)&pidl, &itemArray);
    ILFree(pidl);
    if (SUCCEEDED(hr))
        hr = itemArray->QueryInterface(riid, ppv);
    return hr;
}

EXTERN_C HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags,
                                             HANDLE hToken, PWSTR *ppszPath);

EXTERN_C HRESULT WINAPI SHCreateItemWithParent(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psfParent,
    PCUITEMID_CHILD pidl, REFIID riid, void **ppvItem)
{
    CComPtr<IShellItem> item;
    HRESULT hr;

    TRACE("(%p, %p, %p, %s, %p)\n", pidlParent, psfParent, pidl, debugstr_guid(&riid), ppvItem);

    if (!ppvItem)
        return E_INVALIDARG;

    *ppvItem = NULL;

    if (!pidl || (!pidlParent && !psfParent))
        return E_INVALIDARG;

    hr = SHCreateShellItem(pidlParent, psfParent, pidl, &item);
    if (SUCCEEDED(hr))
        hr = item->QueryInterface(riid, ppvItem);
    return hr;
}

EXTERN_C HRESULT WINAPI SHCreateItemInKnownFolder(REFKNOWNFOLDERID kfid, DWORD dwKFFlags,
    PCWSTR pszItem, REFIID riid, void **ppv)
{
    CComHeapPtr<WCHAR> folderPath;
    WCHAR szPath[MAX_PATH];
    HRESULT hr;

    TRACE("(%s, 0x%lx, %s, %s, %p)\n", debugstr_guid(&kfid), dwKFFlags,
          debugstr_w(pszItem), debugstr_guid(&riid), ppv);

    if (!ppv)
        return E_INVALIDARG;

    *ppv = NULL;

    hr = SHGetKnownFolderPath(kfid, dwKFFlags, NULL, &folderPath);
    if (FAILED(hr))
        return hr;

    if (!pszItem || !*pszItem)
        return SHCreateItemFromParsingName(folderPath, NULL, riid, ppv);

    if (FAILED(StringCchCopyW(szPath, _countof(szPath), folderPath)))
        return E_FAIL;
    if (!PathAppendW(szPath, pszItem))
        return E_FAIL;

    return SHCreateItemFromParsingName(szPath, NULL, riid, ppv);
}

EXTERN_C HRESULT WINAPI SHBindToFolderIDListParentEx(IShellFolder *psfRoot, PCUIDLIST_RELATIVE pidl,
    IBindCtx *ppbc, REFIID riid, void **ppv, PCUITEMID_CHILD *ppidlLast)
{
    CComPtr<IShellFolder> root;
    LPCITEMIDLIST pidlChild;
    LPITEMIDLIST pidlParent = NULL;
    HRESULT hr;

    TRACE("(%p, %p, %p, %s, %p, %p)\n", psfRoot, pidl, ppbc, debugstr_guid(&riid), ppv, ppidlLast);

    if (!ppv)
        return E_INVALIDARG;

    *ppv = NULL;
    if (ppidlLast)
        *ppidlLast = NULL;

    if (!pidl)
        return E_INVALIDARG;

    if (psfRoot)
    {
        root = psfRoot;
    }
    else
    {
        hr = SHGetDesktopFolder(&root);
        if (FAILED(hr))
            return hr;
    }

    pidlChild = ILFindLastID(pidl);
    if (pidlChild == pidl)
    {
        hr = root->QueryInterface(riid, ppv);
    }
    else
    {
        pidlParent = ILClone(pidl);
        if (!pidlParent)
            return E_OUTOFMEMORY;
        ILRemoveLastID(pidlParent);

        hr = root->BindToObject(pidlParent, ppbc, riid, ppv);
        ILFree(pidlParent);
    }

    if (SUCCEEDED(hr) && ppidlLast)
        *ppidlLast = (PCUITEMID_CHILD)pidlChild;

    return hr;
}

EXTERN_C HRESULT WINAPI SHBindToFolderIDListParent(IShellFolder *psfRoot, PCUIDLIST_RELATIVE pidl,
    REFIID riid, void **ppv, PCUITEMID_CHILD *ppidlLast)
{
    return SHBindToFolderIDListParentEx(psfRoot, pidl, NULL, riid, ppv, ppidlLast);
}

EXTERN_C HRESULT WINAPI SHGetFolderPathEx(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken,
    LPWSTR pszPath, UINT cchPath)
{
    CComHeapPtr<WCHAR> path;
    HRESULT hr;

    TRACE("(%s, 0x%lx, %p, %p, %u)\n", debugstr_guid(&rfid), dwFlags, hToken, pszPath, cchPath);

    if (!pszPath || !cchPath)
        return E_INVALIDARG;

    hr = SHGetKnownFolderPath(rfid, dwFlags, hToken, &path);
    if (FAILED(hr))
        return hr;

    if (wcslen(path) >= cchPath)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    return StringCchCopyW(pszPath, cchPath, path);
}

static HBITMAP
CreateImageBitmap(LONG cx, LONG cy, void **ppBits)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB } };
    return CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, ppBits, NULL, 0);
}

static HBITMAP
ShrinkImageBitmap(HBITMAP hbmSource, LONG cx, LONG cy)
{
    BITMAP bm;
    if (!GetObjectW(hbmSource, sizeof(bm), &bm) || bm.bmWidth <= 0 || !bm.bmHeight)
        return NULL;

    LONG cxSource = bm.bmWidth, cySource = abs(bm.bmHeight);
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), cxSource, -cySource, 1, 32, BI_RGB } };
    CHeapPtr<DWORD> pSource;
    if (!pSource.Allocate(cxSource * cySource))
        return NULL;

    HDC hdc = GetDC(NULL);
    INT lines = GetDIBits(hdc, hbmSource, 0, cySource, pSource, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    if (lines != cySource)
        return NULL;

    DWORD *pTarget;
    HBITMAP hbm = CreateImageBitmap(cx, cy, (void **)&pTarget);
    if (!hbm)
        return NULL;

    for (LONG y = 0; y < cy; y++)
    {
        LONG y0 = y * cySource / cy, y1 = max(y0 + 1, (y + 1) * cySource / cy);
        for (LONG x = 0; x < cx; x++)
        {
            LONG x0 = x * cxSource / cx, x1 = max(x0 + 1, (x + 1) * cxSource / cx);
            ULONG sum[4] = { 0, 0, 0, 0 }, count = 0;
            for (LONG sy = y0; sy < y1; sy++)
            {
                for (LONG sx = x0; sx < x1; sx++)
                {
                    DWORD pixel = pSource[sy * cxSource + sx];
                    for (UINT channel = 0; channel < 4; channel++)
                        sum[channel] += (pixel >> (channel * 8)) & 0xFF;
                    count++;
                }
            }
            DWORD value = 0;
            for (UINT channel = 0; channel < 4; channel++)
                value |= ((sum[channel] + count / 2) / count) << (channel * 8);
            pTarget[y * cx + x] = value;
        }
    }
    return hbm;
}

HRESULT CShellItem::GetIconImage(SIZE size, HBITMAP *phbm)
{
    CComPtr<IShellFolder> psf;
    HRESULT hr;

    if (_ILIsDesktop(m_pidl))
        hr = SHGetDesktopFolder(&psf);
    else
        hr = get_parent_shellfolder(&psf);
    if (FAILED(hr))
        return hr;

    LPCITEMIDLIST pidlChild = ILFindLastID(m_pidl);
    CComPtr<IExtractIconW> pei;
    hr = psf->GetUIObjectOf(NULL, 1, &pidlChild, IID_NULL_PPV_ARG(IExtractIconW, &pei));
    if (FAILED(hr))
        return hr;

    WCHAR szLocation[MAX_PATH];
    INT iIndex = 0;
    UINT uFlags = 0;
    hr = pei->GetIconLocation(GIL_FORSHELL, szLocation, _countof(szLocation), &iIndex, &uFlags);
    if (FAILED(hr))
        return hr;

    UINT cxIcon = max(size.cx, size.cy);
    HICON hIcon = NULL;
    hr = pei->Extract(szLocation, iIndex, &hIcon, NULL, MAKELONG(cxIcon, 16));
    if (hr != S_OK)
    {
        hIcon = NULL;
        if (!(uFlags & GIL_NOTFILENAME))
            PrivateExtractIconsW(szLocation, iIndex, cxIcon, cxIcon, &hIcon, NULL, 1, 0);
    }
    if (!hIcon)
        return E_FAIL;

    void *pBits;
    HBITMAP hbm = CreateImageBitmap(size.cx, size.cy, &pBits);
    if (!hbm)
    {
        DestroyIcon(hIcon);
        return E_OUTOFMEMORY;
    }

    HDC hdc = CreateCompatibleDC(NULL);
    HGDIOBJ hbmOld = SelectObject(hdc, hbm);
    DrawIconEx(hdc, 0, 0, hIcon, size.cx, size.cy, 0, NULL, DI_NORMAL);
    SelectObject(hdc, hbmOld);
    DeleteDC(hdc);
    DestroyIcon(hIcon);

    *phbm = hbm;
    return S_OK;
}

HRESULT CShellItem::GetThumbnailImage(SIZE size, SIIGBF flags, HBITMAP *phbm)
{
    UINT cxThumb = max(size.cx, size.cy);
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    HBITMAP hbm = NULL;
    HRESULT hr;

    CComPtr<IThumbnailCache> pCache;
    hr = CoCreateInstance(CLSID_LocalThumbnailCache, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IThumbnailCache, &pCache));
    if (SUCCEEDED(hr))
    {
        CComPtr<ISharedBitmap> pShared;
        hr = pCache->GetThumbnail(static_cast<IShellItem *>(this), cxThumb,
                                  (flags & SIIGBF_INCACHEONLY) ? WTS_INCACHEONLY : WTS_EXTRACT,
                                  &pShared, NULL, NULL);
        if (SUCCEEDED(hr))
            hr = pShared->Detach(&hbm);
    }
    else if (!(flags & SIIGBF_INCACHEONLY))
    {
        CComPtr<IThumbnailProvider> pProvider;
        hr = BindToHandler(NULL, BHID_ThumbnailHandler, IID_PPV_ARG(IThumbnailProvider, &pProvider));
        if (SUCCEEDED(hr))
            hr = pProvider->GetThumbnail(cxThumb, &hbm, &alpha);
    }
    if (FAILED(hr) || !hbm)
        return WTS_E_FAILEDEXTRACTION;

    BITMAP bm;
    if (!(flags & SIIGBF_BIGGERSIZEOK) && GetObjectW(hbm, sizeof(bm), &bm) &&
        (bm.bmWidth > size.cx || abs(bm.bmHeight) > size.cy))
    {
        LONG cySource = abs(bm.bmHeight);
        LONG cx = size.cx, cy = MulDiv(cySource, size.cx, bm.bmWidth);
        if (cy > size.cy)
        {
            cy = size.cy;
            cx = MulDiv(bm.bmWidth, size.cy, cySource);
        }
        HBITMAP hbmShrunk = ShrinkImageBitmap(hbm, max(cx, 1), max(cy, 1));
        DeleteObject(hbm);
        if (!hbmShrunk)
            return E_OUTOFMEMORY;
        hbm = hbmShrunk;
    }

    *phbm = hbm;
    return S_OK;
}

HRESULT WINAPI CShellItem::GetImage(SIZE size, SIIGBF flags, HBITMAP *phbm)
{
    HRESULT hr;

    if (!phbm)
        return E_POINTER;
    *phbm = NULL;
    if (size.cx <= 0 || size.cy <= 0 || !m_pidl)
        return E_INVALIDARG;

    if (!(flags & SIIGBF_ICONONLY))
    {
        hr = GetThumbnailImage(size, flags, phbm);
        if (SUCCEEDED(hr) || (flags & SIIGBF_THUMBNAILONLY))
            return hr;
    }

    return GetIconImage(size, phbm);
}
