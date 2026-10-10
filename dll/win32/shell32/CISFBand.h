/*
 * PROJECT:     ReactOS shell extensions
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Quick Launch Toolbar (Taskbar Shell Extension)
 * COPYRIGHT:   Copyright 2017 Shriraj Sawant a.k.a SR13 <sr.official@hotmail.com>
 *              Copyright 2017-2018 Giannis Adamopoulos
 *              Copyright 2023-2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#pragma once

#define WM_ISFBAND_CHANGE_NOTIFY (WM_USER + 100)

class CISFBand :
    public CWindowImpl<CISFBand, CWindow>,
    public CComCoClass<CISFBand, &CLSID_ISFBand>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IObjectWithSite,
    public IDeskBand,
    public IPersistStream,
    public IWinEventHandler,
    public IOleCommandTarget,
    public IShellFolderBand,
    public IContextMenu,
    public IInputObject,
    public IServiceProvider,
    public IShellChangeNotify,
    public IDropTarget,
    public IFolderBandPriv
{
    // Band
    DWORD m_BandID;
    CComPtr<IUnknown> m_Site;

    // Toolbar
    CComPtr<IShellFolder> m_pISF;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> m_pidl;
    UINT m_uChangeNotify;

    // Menu
    BOOL m_bShowText;
    BOOL m_bSmallIcon;
    BOOL m_QLaunch;
    BOOL m_bNoIcons;

    CComPtr<IDropTarget> m_pDropTarget;

    BOOL RegisterChangeNotify(_In_ BOOL bRegister);
    HRESULT AddToolbarButtons();
    void DeleteToolbarButtons();
    HRESULT RefreshToolbar();
    HRESULT SetImageListIconSize(_In_ BOOL bSmall);
    HRESULT BandInfoChanged();
    HRESULT ShowHideText(_In_ BOOL bShow);

public:
    CISFBand();
    virtual ~CISFBand();

// Personal Methods
    HRESULT CreateSimpleToolbar(HWND hWndParent);

    STDMETHOD(ContainsWindow)(IN HWND hWnd);

// IObjectWithSite
    STDMETHOD(GetSite)(IN REFIID riid, OUT void  **ppvSite) override;
    STDMETHOD(SetSite)(IN IUnknown *pUnkSite) override;

// IDeskBand
    STDMETHOD(GetWindow)(OUT HWND *phwnd) override;
    STDMETHOD(ContextSensitiveHelp)(IN BOOL fEnterMode) override;
    STDMETHOD(ShowDW)(IN BOOL bShow) override;
    STDMETHOD(CloseDW)(IN DWORD dwReserved) override;

    STDMETHOD(ResizeBorderDW)(
        LPCRECT prcBorder,
        IUnknown *punkToolbarSite,
        BOOL fReserved) override;

    STDMETHOD(GetBandInfo)(
        IN DWORD dwBandID,
        IN DWORD dwViewMode,
        IN OUT DESKBANDINFO *pdbi) override;

// IPersistStream
    STDMETHOD(GetClassID)(OUT CLSID *pClassID) override;
    STDMETHOD(GetSizeMax)(OUT ULARGE_INTEGER *pcbSize) override;
    STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(IN IStream *pStm) override;
    STDMETHOD(Save)(IN IStream *pStm, IN BOOL fClearDirty) override;

// IWinEventHandler
    STDMETHOD(OnWinEvent)(
        HWND hWnd,
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        LRESULT *theResult) override;

    STDMETHOD(IsWindowOwner)(HWND hWnd) override;

// IOleCommandTarget

    STDMETHOD(Exec)(
        IN const GUID *pguidCmdGroup,
        IN DWORD nCmdID,
        IN DWORD nCmdexecopt,
        IN VARIANT *pvaIn,
        IN OUT VARIANT *pvaOut) override;

    STDMETHOD(QueryStatus)(
        IN const GUID *pguidCmdGroup,
        IN ULONG cCmds,
        IN OUT OLECMD prgCmds[],
        IN OUT OLECMDTEXT *pCmdText) override;

// IShellFolderBand
    STDMETHOD(GetBandInfoSFB)(PBANDINFOSFB pbi) override;
    STDMETHOD(InitializeSFB)(IShellFolder *psf, PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(SetBandInfoSFB)(PBANDINFOSFB pbi) override;

// IContextMenu
    STDMETHOD(GetCommandString)(
        UINT_PTR idCmd,
        UINT uFlags,
        UINT *pwReserved,
        LPSTR pszName,
        UINT cchMax) override;

    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO pici) override;

    STDMETHOD(QueryContextMenu)(
        HMENU hmenu,
        UINT indexMenu,
        UINT idCmdFirst,
        UINT idCmdLast,
        UINT uFlags) override;

    STDMETHOD(UIActivateIO)(BOOL fActivate, LPMSG lpMsg) override;
    STDMETHOD(HasFocusIO)() override;
    STDMETHOD(TranslateAcceleratorIO)(LPMSG lpMsg) override;

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    STDMETHOD(OnChange)(LONG lEvent, PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2) override;

    STDMETHOD(DragEnter)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragLeave)() override;
    STDMETHOD(Drop)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

    STDMETHOD(SetCascade)(BOOL fCascade) override;
    STDMETHOD(SetAccelerators)(BOOL fAccelerators) override;
    STDMETHOD(SetNoIcons)(BOOL fNoIcons) override;
    STDMETHOD(SetNoText)(BOOL fNoText) override;

    LRESULT OnChangeNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

//*****************************************************************************************************

    DECLARE_REGISTRY_RESOURCEID(IDR_ISFBAND)
    DECLARE_NOT_AGGREGATABLE(CISFBand)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CISFBand)
        COM_INTERFACE_ENTRY2_IID(IID_IOleWindow, IOleWindow, IDeskBand)
        COM_INTERFACE_ENTRY2_IID(IID_IDockingWindow, IDockingWindow, IDeskBand)
        COM_INTERFACE_ENTRY_IID(IID_IDeskBand, IDeskBand)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStream, IPersistStream)
        COM_INTERFACE_ENTRY_IID(IID_IWinEventHandler, IWinEventHandler)
        COM_INTERFACE_ENTRY_IID(IID_IOleCommandTarget, IOleCommandTarget)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderBand, IShellFolderBand)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IInputObject, IInputObject)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_IShellChangeNotify, IShellChangeNotify)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
        COM_INTERFACE_ENTRY_IID(IID_IFolderBandPriv, IFolderBandPriv)
    END_COM_MAP()

    BEGIN_MSG_MAP(CISFBand)
        MESSAGE_HANDLER(WM_ISFBAND_CHANGE_NOTIFY, OnChangeNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

class CQuickLinks :
    public CISFBand
{
    BOOL m_bFolderSet;

public:
    CQuickLinks();

    static const CLSID& WINAPI GetObjectCLSID() { return CLSID_QuickLinks; }

    STDMETHOD(SetSite)(IN IUnknown *pUnkSite) override;
    STDMETHOD(GetClassID)(OUT CLSID *pClassID) override;
    STDMETHOD(InitializeSFB)(IShellFolder *psf, PCIDLIST_ABSOLUTE pidl) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_QUICKLINKS)
    DECLARE_NOT_AGGREGATABLE(CQuickLinks)
};
