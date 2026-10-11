/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Background thumbnail generator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

class CThumbnail :
    public CComCoClass<CThumbnail, &CLSID_Thumbnail>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IThumbnail3,
    public CObjectWithSiteBase
{
    HWND m_hwnd;
    UINT m_uMsg;

public:
    CThumbnail() : m_hwnd(NULL), m_uMsg(0) {}

    STDMETHOD(Init)(HWND hwnd, UINT uMsg) override
    {
        m_hwnd = hwnd;
        m_uMsg = uMsg;
        return S_OK;
    }

    STDMETHOD(GetBitmap)(LPCWSTR pszFile, DWORD dwItemID, LONG lWidth, LONG lHeight) override
    {
        CComHeapPtr<ITEMIDLIST> pidl;
        HRESULT hr;

        if (!pszFile)
            return E_INVALIDARG;
        if (GetFileAttributesW(pszFile) == INVALID_FILE_ATTRIBUTES)
            return HRESULT_FROM_WIN32(GetLastError());
        hr = SHParseDisplayName(pszFile, NULL, &pidl, 0, NULL);
        if (FAILED(hr))
            return hr;
        return GetBitmapFromIDList(pidl, dwItemID, lWidth, lHeight);
    }

    STDMETHOD(GetBitmapFromIDList)(PCIDLIST_ABSOLUTE pidl, DWORD dwItemID, LONG lWidth, LONG lHeight) override
    {
        CComPtr<IShellFolder> psf;
        CComPtr<IExtractImage> pei;
        PCUITEMID_CHILD pidlChild;
        HRESULT hr;

        if (!pidl)
            return E_INVALIDARG;
        hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psf), &pidlChild);
        if (FAILED(hr))
            return hr;
        hr = psf->GetUIObjectOf(NULL, 1, &pidlChild, IID_NULL_PPV_ARG(IExtractImage, &pei));
        if (FAILED(hr))
            return E_FAIL;
        FIXME("Thumbnail extraction for item %lu is not implemented\n", dwItemID);
        return E_NOTIMPL;
    }

    STDMETHOD(GetBitmapFromShellItem)(IShellItem *psi, DWORD dwItemID, LONG lWidth, LONG lHeight) override
    {
        CComHeapPtr<ITEMIDLIST> pidl;
        HRESULT hr;

        if (!psi)
            return E_INVALIDARG;
        hr = SHGetIDListFromObject(psi, &pidl);
        if (FAILED(hr))
            return hr;
        return GetBitmapFromIDList(pidl, dwItemID, lWidth, lHeight);
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_THUMBNAIL)
    DECLARE_NOT_AGGREGATABLE(CThumbnail)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CThumbnail)
        COM_INTERFACE_ENTRY_IID(IID_IThumbnail, IThumbnail3)
        COM_INTERFACE_ENTRY_IID(IID_IThumbnail2, IThumbnail3)
        COM_INTERFACE_ENTRY_IID(IID_IThumbnail3, IThumbnail3)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_Thumbnail, CThumbnail)
