/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Explorer bar band hosting a web page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CCommBand :
    public CComCoClass<CCommBand, &CLSID_CommBand>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public CWindowImpl<CCommBand>,
    public IDeskBand,
    public IObjectWithSite,
    public IInputObject,
    public IPersistStream,
    public IPersistPropertyBag,
    public IOleCommandTarget,
    public IServiceProvider,
    public IContextMenu,
    public IDispatch
{
    CComPtr<IUnknown> m_pSite;
    CComPtr<IInputObjectSite> m_pInputSite;
    CComPtr<IOleObject> m_pBrowser;
    CComPtr<IOleClientSite> m_pBrowserSite;
    CComBSTR m_Url;

    HRESULT CreateBrowser();
    VOID DestroyBrowser();
    HRESULT GetBrowserUrl(BSTR *pUrl);

public:
    DECLARE_WND_CLASS_EX(L"LiberNT CommBand", 0, COLOR_WINDOW)

    void FinalRelease()
    {
        CloseDW(0);
    }

    STDMETHOD(GetWindow)(HWND *phwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override;

    STDMETHOD(ShowDW)(BOOL fShow) override;
    STDMETHOD(CloseDW)(DWORD dwReserved) override;
    STDMETHOD(ResizeBorderDW)(LPCRECT prcBorder, IUnknown *punkToolbarSite, BOOL fReserved) override;

    STDMETHOD(GetBandInfo)(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO *pdbi) override;

    STDMETHOD(SetSite)(IUnknown *pUnkSite) override;
    STDMETHOD(GetSite)(REFIID riid, void **ppvSite) override;

    STDMETHOD(UIActivateIO)(BOOL fActivate, LPMSG lpMsg) override;
    STDMETHOD(HasFocusIO)() override;
    STDMETHOD(TranslateAcceleratorIO)(LPMSG lpMsg) override;

    STDMETHOD(GetClassID)(CLSID *pClassID) override;
    STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(IStream *pStm) override;
    STDMETHOD(Save)(IStream *pStm, BOOL fClearDirty) override;
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pcbSize) override;

    STDMETHOD(InitNew)() override;
    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override;
    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override;

    STDMETHOD(QueryStatus)(const GUID *pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT *pCmdText) override;
    STDMETHOD(Exec)(const GUID *pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut) override;

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpici) override;
    STDMETHOD(GetCommandString)(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax) override;

    STDMETHOD(GetTypeInfoCount)(UINT *pctinfo) override;
    STDMETHOD(GetTypeInfo)(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) override;
    STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId) override;
    STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                      VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr) override;

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CCommBand)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_MSG_MAP(CCommBand)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
    END_MSG_MAP()

    BEGIN_COM_MAP(CCommBand)
        COM_INTERFACE_ENTRY_IID(IID_IDeskBand, IDeskBand)
        COM_INTERFACE_ENTRY_IID(IID_IDockingWindow, IDockingWindow)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IInputObject, IInputObject)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStream, IPersistStream)
        COM_INTERFACE_ENTRY2_IID(IID_IPersist, IPersist, IPersistStream)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IOleCommandTarget, IOleCommandTarget)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
    END_COM_MAP()
};
