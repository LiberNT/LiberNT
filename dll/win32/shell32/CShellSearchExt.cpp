/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     In-pane search command object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

class CShellSearchExt :
    public CComCoClass<CShellSearchExt, &CLSID_ShellSearchExt>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenu,
    public CObjectWithSiteBase
{
public:
    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpici) override
    {
        CComPtr<IShellBrowser> psb;
        CComPtr<IShellView> psv;
        CComPtr<IFolderView> pfv;
        CComPtr<IPersistFolder2> ppf;
        CComHeapPtr<ITEMIDLIST> pidlFolder;

        if (!lpici || lpici->cbSize < sizeof(CMINVOKECOMMANDINFO))
            return E_INVALIDARG;

        if (m_pUnkSite &&
            SUCCEEDED(IUnknown_QueryService(m_pUnkSite, SID_STopLevelBrowser, IID_PPV_ARG(IShellBrowser, &psb))) &&
            SUCCEEDED(psb->QueryActiveShellView(&psv)) &&
            SUCCEEDED(psv->QueryInterface(IID_PPV_ARG(IFolderView, &pfv))) &&
            SUCCEEDED(pfv->GetFolder(IID_PPV_ARG(IPersistFolder2, &ppf))))
        {
            ppf->GetCurFolder(&pidlFolder);
        }

        return SHFindFiles(pidlFolder, NULL) ? S_OK : E_FAIL;
    }

    STDMETHOD(GetCommandString)(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax) override
    {
        return E_NOTIMPL;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_SHELLSEARCHEXT)
    DECLARE_NOT_AGGREGATABLE(CShellSearchExt)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CShellSearchExt)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_ShellSearchExt, CShellSearchExt)
