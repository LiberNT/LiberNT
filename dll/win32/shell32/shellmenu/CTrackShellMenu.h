/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modal shell menu popup (TrackShellMenu)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CTrackShellMenu :
    public CComCoClass<CTrackShellMenu, &CLSID_TrackShellMenu>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public ITrackShellMenu,
    public IShellMenu2,
    public IObjectWithSite,
    public IServiceProvider
{
    CComPtr<IShellMenu> m_pShellMenu;
    CComPtr<IUnknown> m_pSite;
    BOOL m_bTracking;

public:
    CTrackShellMenu();
    ~CTrackShellMenu();

    HRESULT FinalConstruct();

    STDMETHOD(Initialize)(IShellMenuCallback *psmc, UINT uId, UINT uIdAncestor, DWORD dwFlags) override;
    STDMETHOD(GetMenuInfo)(IShellMenuCallback **ppsmc, UINT *puId, UINT *puIdAncestor, DWORD *pdwFlags) override;
    STDMETHOD(SetShellFolder)(IShellFolder *psf, LPCITEMIDLIST pidlFolder, HKEY hKey, DWORD dwFlags) override;
    STDMETHOD(GetShellFolder)(DWORD *pdwFlags, LPITEMIDLIST *ppidl, REFIID riid, void **ppv) override;
    STDMETHOD(SetMenu)(HMENU hmenu, HWND hwnd, DWORD dwFlags) override;
    STDMETHOD(GetMenu)(HMENU *phmenu, HWND *phwnd, DWORD *pdwFlags) override;
    STDMETHOD(InvalidateItem)(LPSMDATA psmd, DWORD dwFlags) override;
    STDMETHOD(GetState)(LPSMDATA psmd) override;
    STDMETHOD(SetMenuToolbar)(IUnknown *punk, DWORD dwFlags) override;

    STDMETHOD(GetSubMenu)() override;
    STDMETHOD(SetToolbar)() override;
    STDMETHOD(SetMinWidth)() override;
    STDMETHOD(SetNoBorder)() override;
    STDMETHOD(SetTheme)() override;

    STDMETHOD(SetObscured)(HWND hwndTB, IUnknown *punkBand, DWORD dwSMSetFlags) override;
    STDMETHOD(Popup)(HWND hwnd, POINTL *ppt, RECTL *prcExclude, MP_POPUPFLAGS dwFlags) override;

    STDMETHOD(SetSite)(IUnknown *pUnkSite) override;
    STDMETHOD(GetSite)(REFIID riid, void **ppvSite) override;

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_TRACKSHELLMENU)
    DECLARE_NOT_AGGREGATABLE(CTrackShellMenu)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CTrackShellMenu)
        COM_INTERFACE_ENTRY_IID(IID_ITrackShellMenu, ITrackShellMenu)
        COM_INTERFACE_ENTRY2_IID(IID_IShellMenu, IShellMenu, ITrackShellMenu)
        COM_INTERFACE_ENTRY_IID(IID_IShellMenu2, IShellMenu2)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
    END_COM_MAP()
};
