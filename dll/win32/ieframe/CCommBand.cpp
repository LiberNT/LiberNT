/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Explorer bar band hosting a web page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"
#include "CCommBand.h"

#include <wine/debug.h>

#define COMMBAND_CMD_REFRESH      1
#define COMMBAND_CMD_OPENWINDOW   2

class CCommBandBrowserSite :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IOleClientSite,
    public IOleInPlaceSite
{
public:
    HWND m_hwndHost;

    CCommBandBrowserSite() : m_hwndHost(NULL) {}

    STDMETHOD(SaveObject)() override { return E_NOTIMPL; }
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk) override
    {
        *ppmk = NULL;
        return E_NOTIMPL;
    }
    STDMETHOD(GetContainer)(IOleContainer **ppContainer) override
    {
        *ppContainer = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD(ShowObject)() override { return S_OK; }
    STDMETHOD(OnShowWindow)(BOOL fShow) override { return S_OK; }
    STDMETHOD(RequestNewObjectLayout)() override { return E_NOTIMPL; }

    STDMETHOD(GetWindow)(HWND *phwnd) override
    {
        *phwnd = m_hwndHost;
        return m_hwndHost ? S_OK : E_FAIL;
    }
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override { return E_NOTIMPL; }
    STDMETHOD(CanInPlaceActivate)() override { return S_OK; }
    STDMETHOD(OnInPlaceActivate)() override { return S_OK; }
    STDMETHOD(OnUIActivate)() override { return S_OK; }
    STDMETHOD(GetWindowContext)(IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect,
                                LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo) override
    {
        *ppFrame = NULL;
        *ppDoc = NULL;
        ::GetClientRect(m_hwndHost, lprcPosRect);
        *lprcClipRect = *lprcPosRect;
        lpFrameInfo->fMDIApp = FALSE;
        lpFrameInfo->hwndFrame = ::GetAncestor(m_hwndHost, GA_ROOT);
        lpFrameInfo->haccel = NULL;
        lpFrameInfo->cAccelEntries = 0;
        return S_OK;
    }
    STDMETHOD(Scroll)(SIZE scrollExtant) override { return E_NOTIMPL; }
    STDMETHOD(OnUIDeactivate)(BOOL fUndoable) override { return S_OK; }
    STDMETHOD(OnInPlaceDeactivate)() override { return S_OK; }
    STDMETHOD(DiscardUndoState)() override { return S_OK; }
    STDMETHOD(DeactivateAndUndo)() override { return S_OK; }
    STDMETHOD(OnPosRectChange)(LPCRECT lprcPosRect) override { return S_OK; }

    BEGIN_COM_MAP(CCommBandBrowserSite)
        COM_INTERFACE_ENTRY_IID(IID_IOleClientSite, IOleClientSite)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceSite, IOleInPlaceSite)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()
};

HRESULT CCommBand::CreateBrowser()
{
    CComObject<CCommBandBrowserSite> *pSite;
    CComPtr<IWebBrowser2> pWebBrowser;
    RECT rc;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IOleObject, &m_pBrowser));
    if (FAILED(hr))
        return hr;

    hr = CComObject<CCommBandBrowserSite>::CreateInstance(&pSite);
    if (FAILED(hr))
    {
        m_pBrowser.Release();
        return hr;
    }
    pSite->m_hwndHost = m_hWnd;
    m_pBrowserSite = pSite;

    hr = m_pBrowser->SetClientSite(m_pBrowserSite);
    if (SUCCEEDED(hr))
    {
        GetClientRect(&rc);
        hr = m_pBrowser->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, m_pBrowserSite, 0, m_hWnd, &rc);
    }
    if (FAILED(hr))
    {
        DestroyBrowser();
        return hr;
    }

    if (m_Url.Length() && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IWebBrowser2, &pWebBrowser))))
        pWebBrowser->Navigate(m_Url, NULL, NULL, NULL, NULL);

    return S_OK;
}

VOID CCommBand::DestroyBrowser()
{
    if (m_pBrowser)
    {
        m_pBrowser->Close(OLECLOSE_NOSAVE);
        m_pBrowser->SetClientSite(NULL);
        m_pBrowser.Release();
    }
    m_pBrowserSite.Release();
}

HRESULT CCommBand::GetBrowserUrl(BSTR *pUrl)
{
    CComPtr<IWebBrowser2> pWebBrowser;

    *pUrl = NULL;
    if (m_pBrowser && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IWebBrowser2, &pWebBrowser))) &&
        SUCCEEDED(pWebBrowser->get_LocationURL(pUrl)) && *pUrl && **pUrl)
    {
        return S_OK;
    }

    SysFreeString(*pUrl);
    *pUrl = m_Url.Copy();
    return (*pUrl && **pUrl) ? S_OK : E_FAIL;
}

STDMETHODIMP CCommBand::GetWindow(HWND *phwnd)
{
    if (!phwnd)
        return E_INVALIDARG;
    *phwnd = m_hWnd;
    return m_hWnd ? S_OK : E_FAIL;
}

STDMETHODIMP CCommBand::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::ShowDW(BOOL fShow)
{
    if (m_hWnd)
        ShowWindow(fShow ? SW_SHOW : SW_HIDE);
    return S_OK;
}

STDMETHODIMP CCommBand::CloseDW(DWORD dwReserved)
{
    DestroyBrowser();
    if (m_hWnd)
        DestroyWindow();
    return S_OK;
}

STDMETHODIMP CCommBand::ResizeBorderDW(LPCRECT prcBorder, IUnknown *punkToolbarSite, BOOL fReserved)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO *pdbi)
{
    if (!pdbi)
        return E_INVALIDARG;

    if (pdbi->dwMask & DBIM_MINSIZE)
    {
        pdbi->ptMinSize.x = 0;
        pdbi->ptMinSize.y = 16;
    }
    if (pdbi->dwMask & DBIM_MAXSIZE)
    {
        pdbi->ptMaxSize.x = 32000;
        pdbi->ptMaxSize.y = 32000;
    }
    if (pdbi->dwMask & DBIM_INTEGRAL)
        pdbi->ptIntegral.y = 1;
    if (pdbi->dwMask & DBIM_ACTUAL)
    {
        pdbi->ptActual.x = -1;
        pdbi->ptActual.y = -1;
    }
    if (pdbi->dwMask & DBIM_MODEFLAGS)
        pdbi->dwModeFlags = DBIMF_VARIABLEHEIGHT;
    pdbi->dwMask &= ~DBIM_TITLE;
    return S_OK;
}

STDMETHODIMP CCommBand::SetSite(IUnknown *pUnkSite)
{
    HWND hwndParent;
    HRESULT hr;

    CloseDW(0);
    m_pInputSite.Release();
    m_pSite = pUnkSite;
    if (!pUnkSite)
        return S_OK;

    pUnkSite->QueryInterface(IID_PPV_ARG(IInputObjectSite, &m_pInputSite));

    hr = IUnknown_GetWindow(pUnkSite, &hwndParent);
    if (FAILED(hr))
        return hr;

    if (!Create(hwndParent, rcDefault, NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN))
        return HRESULT_FROM_WIN32(GetLastError());

    return CreateBrowser();
}

STDMETHODIMP CCommBand::GetSite(REFIID riid, void **ppvSite)
{
    if (!ppvSite)
        return E_POINTER;
    *ppvSite = NULL;
    if (!m_pSite)
        return E_FAIL;
    return m_pSite->QueryInterface(riid, ppvSite);
}

STDMETHODIMP CCommBand::UIActivateIO(BOOL fActivate, LPMSG lpMsg)
{
    if (fActivate && m_hWnd)
        ::SetFocus(m_hWnd);
    return S_OK;
}

STDMETHODIMP CCommBand::HasFocusIO()
{
    HWND hwndFocus = ::GetFocus();

    if (m_hWnd && hwndFocus && (hwndFocus == m_hWnd || IsChild(hwndFocus)))
        return S_OK;
    return S_FALSE;
}

STDMETHODIMP CCommBand::TranslateAcceleratorIO(LPMSG lpMsg)
{
    CComPtr<IOleInPlaceActiveObject> pActive;

    if (m_pBrowser && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IOleInPlaceActiveObject, &pActive))))
        return pActive->TranslateAccelerator(lpMsg);
    return S_FALSE;
}

STDMETHODIMP CCommBand::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_CommBand;
    return S_OK;
}

STDMETHODIMP CCommBand::IsDirty()
{
    return S_FALSE;
}

STDMETHODIMP CCommBand::Load(IStream *pStm)
{
    return S_OK;
}

STDMETHODIMP CCommBand::Save(IStream *pStm, BOOL fClearDirty)
{
    return S_OK;
}

STDMETHODIMP CCommBand::GetSizeMax(ULARGE_INTEGER *pcbSize)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::InitNew()
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::Load(IPropertyBag *pPropBag, IErrorLog *pErrorLog)
{
    CComVariant varUrl;

    if (!pPropBag)
        return E_POINTER;

    varUrl.vt = VT_BSTR;
    if (SUCCEEDED(pPropBag->Read(L"Url", &varUrl, pErrorLog)) && varUrl.vt == VT_BSTR)
        m_Url = V_BSTR(&varUrl);
    return S_OK;
}

STDMETHODIMP CCommBand::Save(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::QueryStatus(const GUID *pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT *pCmdText)
{
    CComPtr<IOleCommandTarget> pTarget;

    if (m_pBrowser && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IOleCommandTarget, &pTarget))))
        return pTarget->QueryStatus(pguidCmdGroup, cCmds, prgCmds, pCmdText);
    return E_FAIL;
}

STDMETHODIMP CCommBand::Exec(const GUID *pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut)
{
    CComPtr<IOleCommandTarget> pTarget;

    if (m_pBrowser && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IOleCommandTarget, &pTarget))))
        return pTarget->Exec(pguidCmdGroup, nCmdID, nCmdexecopt, pvaIn, pvaOut);
    return E_FAIL;
}

STDMETHODIMP CCommBand::QueryService(REFGUID guidService, REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    *ppvObject = NULL;
    if (!m_pSite)
        return E_NOTIMPL;
    return IUnknown_QueryService(m_pSite, guidService, riid, ppvObject);
}

STDMETHODIMP CCommBand::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    WCHAR szText[MAX_PATH];

    if (idCmdFirst + COMMBAND_CMD_OPENWINDOW > idCmdLast)
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    LoadStringW(ieframe_instance, IDS_COMMBAND_REFRESH, szText, _countof(szText));
    InsertMenuW(hMenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst + COMMBAND_CMD_REFRESH, szText);
    LoadStringW(ieframe_instance, IDS_COMMBAND_OPENWINDOW, szText, _countof(szText));
    InsertMenuW(hMenu, indexMenu + 1, MF_BYPOSITION | MF_STRING, idCmdFirst + COMMBAND_CMD_OPENWINDOW, szText);

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, COMMBAND_CMD_OPENWINDOW + 1);
}

STDMETHODIMP CCommBand::InvokeCommand(LPCMINVOKECOMMANDINFO lpici)
{
    CComPtr<IWebBrowser2> pWebBrowser;
    CComBSTR url;

    if (!lpici || !IS_INTRESOURCE(lpici->lpVerb))
        return E_INVALIDARG;

    switch (LOWORD(lpici->lpVerb))
    {
        case COMMBAND_CMD_REFRESH:
            if (!m_pBrowser || FAILED(m_pBrowser->QueryInterface(IID_PPV_ARG(IWebBrowser2, &pWebBrowser))))
                return E_FAIL;
            return pWebBrowser->Refresh();

        case COMMBAND_CMD_OPENWINDOW:
            if (FAILED(GetBrowserUrl(&url)))
                return E_FAIL;
            if ((INT_PTR)ShellExecuteW(lpici->hwnd, NULL, url, NULL, NULL, SW_SHOWNORMAL) <= 32)
                return E_FAIL;
            return S_OK;
    }

    return E_INVALIDARG;
}

STDMETHODIMP CCommBand::GetCommandString(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::GetTypeInfoCount(UINT *pctinfo)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    return E_NOTIMPL;
}

STDMETHODIMP CCommBand::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                               VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    return DISP_E_MEMBERNOTFOUND;
}

LRESULT CCommBand::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    CComPtr<IOleInPlaceObject> pInPlace;
    RECT rc;

    if (m_pBrowser && SUCCEEDED(m_pBrowser->QueryInterface(IID_PPV_ARG(IOleInPlaceObject, &pInPlace))))
    {
        GetClientRect(&rc);
        pInPlace->SetObjectRects(&rc, &rc);
    }
    return 0;
}

LRESULT CCommBand::OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_pBrowser && m_pBrowserSite)
    {
        RECT rc;
        GetClientRect(&rc);
        m_pBrowser->DoVerb(OLEIVERB_UIACTIVATE, NULL, m_pBrowserSite, 0, m_hWnd, &rc);
    }
    if (m_pInputSite)
        m_pInputSite->OnFocusChangeIS(static_cast<IDeskBand *>(this), TRUE);
    return 0;
}

OBJECT_ENTRY_AUTO(CLSID_CommBand, CCommBand)
