/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Control that routes the events of a shell folder view to its container
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

class CShellFolderViewOC;

class CShellFolderViewOCSink :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatch
{
    CShellFolderViewOC *m_pOwner;

public:
    CShellFolderViewOCSink() : m_pOwner(NULL) {}

    HRESULT Initialize(CShellFolderViewOC *pOwner)
    {
        m_pOwner = pOwner;
        return S_OK;
    }

    void Detach()
    {
        m_pOwner = NULL;
    }

    STDMETHOD(GetTypeInfoCount)(UINT *pctinfo) override
    {
        if (!pctinfo)
            return E_POINTER;
        *pctinfo = 0;
        return S_OK;
    }

    STDMETHOD(GetTypeInfo)(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                      VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr) override;

    BEGIN_COM_MAP(CShellFolderViewOCSink)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID_DShellFolderViewEvents, IDispatch)
    END_COM_MAP()
};

class CShellFolderViewOC :
    public CComCoClass<CShellFolderViewOC, &CLSID_ShellFolderViewOC>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IFolderViewOC, &IID_IFolderViewOC>,
    public IProvideClassInfo2,
    public IObjectSafety,
    public IConnectionPointContainerImpl<CShellFolderViewOC>,
    public IConnectionPointImpl<CShellFolderViewOC, &DIID_DShellFolderViewEvents>,
    public IPersistStreamInit,
    public IOleControl,
    public IOleObject,
    public IOleInPlaceActiveObject,
    public IOleInPlaceObjectWindowless
{
    CComPtr<IDispatch> m_pFolderView;
    CComPtr<IDispatch> m_pSink;
    CShellFolderViewOCSink *m_pSinkObject;
    DWORD m_dwCookie;
    DWORD m_dwSafety;
    CComPtr<IOleClientSite> m_pClientSite;
    CComPtr<IOleAdviseHolder> m_pAdviseHolder;
    SIZEL m_sizeExtent;

    void _Disconnect()
    {
        if (m_pFolderView && m_dwCookie)
            AtlUnadvise(m_pFolderView, DIID_DShellFolderViewEvents, m_dwCookie);
        m_dwCookie = 0;
        m_pFolderView.Release();
    }

public:
    CShellFolderViewOC() : m_pSinkObject(NULL), m_dwCookie(0), m_dwSafety(0)
    {
        m_sizeExtent.cx = 0;
        m_sizeExtent.cy = 0;
    }

    HRESULT FinalConstruct()
    {
        CComObject<CShellFolderViewOCSink> *pSink;
        HRESULT hr = CComObject<CShellFolderViewOCSink>::CreateInstance(&pSink);
        if (FAILED(hr))
            return hr;
        pSink->Initialize(this);
        m_pSinkObject = pSink;
        return pSink->QueryInterface(IID_PPV_ARG(IDispatch, &m_pSink));
    }

    void FinalRelease()
    {
        _Disconnect();
        if (m_pSinkObject)
            m_pSinkObject->Detach();
    }

    void FireEvent(DISPID dispid, DISPPARAMS *pDispParams)
    {
        DISPPARAMS dpEmpty = { NULL, NULL, 0, 0 };
        CComDynamicUnkArray &vec = IConnectionPointImpl<CShellFolderViewOC, &DIID_DShellFolderViewEvents>::m_vec;
        for (IUnknown **pp = vec.begin(); pp < vec.end(); pp++)
        {
            CComPtr<IDispatch> pDispatch;
            if (!*pp || FAILED((*pp)->QueryInterface(IID_PPV_ARG(IDispatch, &pDispatch))))
                continue;
            pDispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
                              pDispParams ? pDispParams : &dpEmpty, NULL, NULL, NULL);
        }
    }

    // IFolderViewOC
    STDMETHOD(SetFolderView)(IDispatch *pdisp) override
    {
        _Disconnect();
        if (!pdisp)
            return S_OK;
        m_pFolderView = pdisp;
        if (FAILED(AtlAdvise(m_pFolderView, m_pSink, DIID_DShellFolderViewEvents, &m_dwCookie)))
            m_dwCookie = 0;
        return S_OK;
    }

    // IProvideClassInfo
    STDMETHOD(GetClassInfo)(ITypeInfo **ppTI) override
    {
        CComPtr<ITypeLib> pTypeLib;
        WCHAR szModule[MAX_PATH];
        HRESULT hr;

        if (!ppTI)
            return E_POINTER;
        *ppTI = NULL;
        if (!GetModuleFileNameW(shell32_hInstance, szModule, _countof(szModule)))
            return HRESULT_FROM_WIN32(GetLastError());
        hr = LoadTypeLibEx(szModule, REGKIND_NONE, &pTypeLib);
        if (FAILED(hr))
            return hr;
        return pTypeLib->GetTypeInfoOfGuid(CLSID_ShellFolderView, ppTI);
    }

    // IProvideClassInfo2
    STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID *pGUID) override
    {
        if (pGUID)
            *pGUID = GUID_NULL;
        return E_FAIL;
    }

    // IObjectSafety
    STDMETHOD(GetInterfaceSafetyOptions)(REFIID riid, DWORD *pdwSupportedOptions, DWORD *pdwEnabledOptions) override
    {
        CComPtr<IUnknown> punk;
        if (!pdwSupportedOptions || !pdwEnabledOptions)
            return E_POINTER;
        if (FAILED(QueryInterface(riid, (void **)&punk)))
        {
            *pdwSupportedOptions = 0;
            *pdwEnabledOptions = 0;
            return E_NOINTERFACE;
        }
        *pdwSupportedOptions = INTERFACESAFE_FOR_UNTRUSTED_CALLER;
        *pdwEnabledOptions = m_dwSafety;
        return S_OK;
    }

    STDMETHOD(SetInterfaceSafetyOptions)(REFIID riid, DWORD dwOptionSetMask, DWORD dwEnabledOptions) override
    {
        CComPtr<IUnknown> punk;
        if (FAILED(QueryInterface(riid, (void **)&punk)))
            return E_NOINTERFACE;
        if (dwOptionSetMask & ~INTERFACESAFE_FOR_UNTRUSTED_CALLER)
            return E_FAIL;
        m_dwSafety = (m_dwSafety & ~dwOptionSetMask) | (dwEnabledOptions & dwOptionSetMask);
        return S_OK;
    }

    // IPersist
    STDMETHOD(GetClassID)(CLSID *pClassID) override
    {
        if (!pClassID)
            return E_POINTER;
        *pClassID = CLSID_ShellFolderViewOC;
        return S_OK;
    }

    // IPersistStreamInit
    STDMETHOD(IsDirty)() override
    {
        return S_FALSE;
    }

    STDMETHOD(Load)(LPSTREAM pStm) override
    {
        return S_OK;
    }

    STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) override
    {
        return S_OK;
    }

    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pCbSize) override
    {
        if (!pCbSize)
            return E_POINTER;
        pCbSize->QuadPart = 0;
        return S_OK;
    }

    STDMETHOD(InitNew)() override
    {
        return S_OK;
    }

    // IOleControl
    STDMETHOD(GetControlInfo)(CONTROLINFO *pCI) override
    {
        if (!pCI)
            return E_POINTER;
        pCI->hAccel = NULL;
        pCI->cAccel = 0;
        pCI->dwFlags = 0;
        return S_OK;
    }

    STDMETHOD(OnMnemonic)(MSG *pMsg) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(OnAmbientPropertyChange)(DISPID dispID) override
    {
        return S_OK;
    }

    STDMETHOD(FreezeEvents)(BOOL bFreeze) override
    {
        return S_OK;
    }

    // IOleObject
    STDMETHOD(SetClientSite)(IOleClientSite *pClientSite) override
    {
        m_pClientSite = pClientSite;
        return S_OK;
    }

    STDMETHOD(GetClientSite)(IOleClientSite **ppClientSite) override
    {
        if (!ppClientSite)
            return E_POINTER;
        *ppClientSite = m_pClientSite;
        if (*ppClientSite)
            (*ppClientSite)->AddRef();
        return S_OK;
    }

    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj) override
    {
        return S_OK;
    }

    STDMETHOD(Close)(DWORD dwSaveOption) override
    {
        _Disconnect();
        if (m_pAdviseHolder)
            m_pAdviseHolder->SendOnClose();
        return S_OK;
    }

    STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, IMoniker *pmk) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(InitFromData)(IDataObject *pDataObject, BOOL fCreation, DWORD dwReserved) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetClipboardData)(DWORD dwReserved, IDataObject **ppDataObject) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, IOleClientSite *pActiveSite, LONG lindex, HWND hwndParent, LPCRECT lprcPosRect) override
    {
        switch (iVerb)
        {
            case OLEIVERB_PRIMARY:
            case OLEIVERB_SHOW:
            case OLEIVERB_OPEN:
            case OLEIVERB_HIDE:
            case OLEIVERB_UIACTIVATE:
            case OLEIVERB_INPLACEACTIVATE:
                return S_OK;
        }
        return OLEOBJ_S_INVALIDVERB;
    }

    STDMETHOD(EnumVerbs)(IEnumOLEVERB **ppEnumOleVerb) override
    {
        return OleRegEnumVerbs(CLSID_ShellFolderViewOC, ppEnumOleVerb);
    }

    STDMETHOD(Update)() override
    {
        return S_OK;
    }

    STDMETHOD(IsUpToDate)() override
    {
        return S_OK;
    }

    STDMETHOD(GetUserClassID)(CLSID *pClsid) override
    {
        return GetClassID(pClsid);
    }

    STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR *pszUserType) override
    {
        return OleRegGetUserType(CLSID_ShellFolderViewOC, dwFormOfType, pszUserType);
    }

    STDMETHOD(SetExtent)(DWORD dwDrawAspect, SIZEL *psizel) override
    {
        if (!psizel)
            return E_POINTER;
        if (dwDrawAspect != DVASPECT_CONTENT)
            return DV_E_DVASPECT;
        m_sizeExtent = *psizel;
        return S_OK;
    }

    STDMETHOD(GetExtent)(DWORD dwDrawAspect, SIZEL *psizel) override
    {
        if (!psizel)
            return E_POINTER;
        if (dwDrawAspect != DVASPECT_CONTENT)
            return DV_E_DVASPECT;
        *psizel = m_sizeExtent;
        return S_OK;
    }

    STDMETHOD(Advise)(IAdviseSink *pAdvSink, DWORD *pdwConnection) override
    {
        if (!m_pAdviseHolder)
        {
            HRESULT hr = CreateOleAdviseHolder(&m_pAdviseHolder);
            if (FAILED(hr))
                return hr;
        }
        return m_pAdviseHolder->Advise(pAdvSink, pdwConnection);
    }

    STDMETHOD(Unadvise)(DWORD dwConnection) override
    {
        if (!m_pAdviseHolder)
            return OLE_E_NOCONNECTION;
        return m_pAdviseHolder->Unadvise(dwConnection);
    }

    STDMETHOD(EnumAdvise)(IEnumSTATDATA **ppenumAdvise) override
    {
        if (!ppenumAdvise)
            return E_POINTER;
        *ppenumAdvise = NULL;
        if (!m_pAdviseHolder)
            return S_OK;
        return m_pAdviseHolder->EnumAdvise(ppenumAdvise);
    }

    STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD *pdwStatus) override
    {
        return OleRegGetMiscStatus(CLSID_ShellFolderViewOC, dwAspect, pdwStatus);
    }

    STDMETHOD(SetColorScheme)(LOGPALETTE *pLogpal) override
    {
        return E_NOTIMPL;
    }

    // IOleWindow
    STDMETHOD(GetWindow)(HWND *phwnd) override
    {
        if (!phwnd)
            return E_POINTER;
        *phwnd = NULL;
        return E_FAIL;
    }

    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override
    {
        return E_NOTIMPL;
    }

    // IOleInPlaceActiveObject
    STDMETHOD(TranslateAccelerator)(LPMSG lpmsg) override
    {
        return S_FALSE;
    }

    STDMETHOD(OnFrameWindowActivate)(BOOL fActivate) override
    {
        return S_OK;
    }

    STDMETHOD(OnDocWindowActivate)(BOOL fActivate) override
    {
        return S_OK;
    }

    STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow *pUIWindow, BOOL fFrameWindow) override
    {
        return S_OK;
    }

    STDMETHOD(EnableModeless)(BOOL fEnable) override
    {
        return S_OK;
    }

    // IOleInPlaceObject
    STDMETHOD(InPlaceDeactivate)() override
    {
        return S_OK;
    }

    STDMETHOD(UIDeactivate)() override
    {
        return S_OK;
    }

    STDMETHOD(SetObjectRects)(LPCRECT lprcPosRect, LPCRECT lprcClipRect) override
    {
        return S_OK;
    }

    STDMETHOD(ReactivateAndUndo)() override
    {
        return E_NOTIMPL;
    }

    // IOleInPlaceObjectWindowless
    STDMETHOD(OnWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult) override
    {
        return S_FALSE;
    }

    STDMETHOD(GetDropTarget)(IDropTarget **ppDropTarget) override
    {
        if (ppDropTarget)
            *ppDropTarget = NULL;
        return E_NOTIMPL;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_SHELLFOLDERVIEWOC)
    DECLARE_NOT_AGGREGATABLE(CShellFolderViewOC)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CShellFolderViewOC)
        COM_INTERFACE_ENTRY_IID(IID_IFolderViewOC, IFolderViewOC)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
        COM_INTERFACE_ENTRY_IID(IID_IProvideClassInfo2, IProvideClassInfo2)
        COM_INTERFACE_ENTRY_IID(IID_IProvideClassInfo, IProvideClassInfo)
        COM_INTERFACE_ENTRY_IID(IID_IObjectSafety, IObjectSafety)
        COM_INTERFACE_ENTRY_IID(IID_IConnectionPointContainer, IConnectionPointContainer)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStreamInit, IPersistStreamInit)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersistStreamInit)
        COM_INTERFACE_ENTRY_IID(IID_IOleControl, IOleControl)
        COM_INTERFACE_ENTRY_IID(IID_IOleObject, IOleObject)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceActiveObject, IOleInPlaceActiveObject)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceObjectWindowless, IOleInPlaceObjectWindowless)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceObject, IOleInPlaceObjectWindowless)
        COM_INTERFACE_ENTRY2_IID(IID_IOleWindow, IOleWindow, IOleInPlaceObjectWindowless)
    END_COM_MAP()

    BEGIN_CONNECTION_POINT_MAP(CShellFolderViewOC)
        CONNECTION_POINT_ENTRY(DIID_DShellFolderViewEvents)
    END_CONNECTION_POINT_MAP()
};

HRESULT STDMETHODCALLTYPE CShellFolderViewOCSink::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                                                        DISPPARAMS *pDispParams, VARIANT *pVarResult,
                                                        EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    if (m_pOwner)
        m_pOwner->FireEvent(dispIdMember, pDispParams);
    return S_OK;
}

OBJECT_ENTRY_AUTO(CLSID_ShellFolderViewOC, CShellFolderViewOC)
