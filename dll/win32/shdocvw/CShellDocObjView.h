/*
 * PROJECT:     LiberNT shdocvw
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell folder for documents that are browsed in place
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

class CShellDocObjView
    : public CComCoClass<CShellDocObjView, &CLSID_ShellDocObjView>
    , public CComObjectRootEx<CComMultiThreadModelNoCS>
    , public IShellFolder
    , public IPersistFolder2
{
    CComHeapPtr<ITEMIDLIST> m_pidl;

public:
    DECLARE_REGISTRY_RESOURCEID(IDR_SHELLDOCOBJVIEW)
    DECLARE_NOT_AGGREGATABLE(CShellDocObjView)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CShellDocObjView)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    END_COM_MAP()

    STDMETHOD(ParseDisplayName)(HWND hwndOwner, LPBC pbc, LPOLESTR pszDisplayName, ULONG *pchEaten,
                                PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes) override;
    STDMETHOD(EnumObjects)(HWND hwndOwner, SHCONTF grfFlags, IEnumIDList **ppenumIDList) override;
    STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv) override;
    STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv) override;
    STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override;
    STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, void **ppv) override;
    STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut) override;
    STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid,
                             UINT *rgfReserved, void **ppv) override;
    STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *pName) override;
    STDMETHOD(SetNameOf)(HWND hwnd, PCUITEMID_CHILD pidl, LPCOLESTR pszName, SHGDNF uFlags,
                         PITEMID_CHILD *ppidlOut) override;

    STDMETHOD(GetClassID)(CLSID *pClassID) override;
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE *ppidl) override;
};
