/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A keyboard text service for the msctf API tests
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <ole2.h>
#include <msctf.h>
#include <imm.h>

#include "testtip.h"

const CLSID CLSID_TestTip = {0x6b3a3f40, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

static TEST_TIP_STATE g_tip;
static ITfComposition *g_pComposition;

class CTestCompositionSink : public ITfCompositionSink
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompositionSink))
        {
            *ppv = static_cast<ITfCompositionSink *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) override
    {
        ++g_tip.cTerminated;
        if (g_pComposition)
        {
            g_pComposition->Release();
            g_pComposition = NULL;
        }
        return S_OK;
    }
};

static CTestCompositionSink s_compositionSink;

class CComposeSession : public ITfEditSession
{
public:
    CComposeSession(ITfContext *pContext, LPCWSTR pszText, DWORD dwAction)
        : m_pContext(pContext), m_pszText(pszText), m_dwAction(dwAction), m_hr(E_FAIL) { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
        {
            *ppv = static_cast<ITfEditSession *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        ITfRange *pRange = NULL;

        if (m_dwAction == 1)
        {
            ITfInsertAtSelection *pInsert = NULL;
            ITfContextComposition *pContextComposition = NULL;
            m_hr = m_pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsert);
            if (SUCCEEDED(m_hr))
                m_hr = pInsert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0, &pRange);
            if (pInsert)
                pInsert->Release();
            if (SUCCEEDED(m_hr))
                m_hr = m_pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition);
            if (SUCCEEDED(m_hr))
                m_hr = pContextComposition->StartComposition(ec, pRange, &s_compositionSink, &g_pComposition);
            if (pContextComposition)
                pContextComposition->Release();
            if (SUCCEEDED(m_hr) && !g_pComposition)
                m_hr = E_FAIL;
            if (pRange)
            {
                pRange->Release();
                pRange = NULL;
            }
        }
        if (FAILED(m_hr) && m_dwAction == 1)
            return S_OK;
        if (!g_pComposition)
        {
            m_hr = E_UNEXPECTED;
            return S_OK;
        }
        if (m_dwAction == 1 || m_dwAction == 2)
        {
            m_hr = g_pComposition->GetRange(&pRange);
            if (SUCCEEDED(m_hr))
                m_hr = pRange->SetText(ec, 0, m_pszText, lstrlenW(m_pszText));
            if (SUCCEEDED(m_hr))
            {
                TF_SELECTION sel;
                pRange->Collapse(ec, TF_ANCHOR_END);
                sel.range = pRange;
                sel.style.ase = TF_AE_END;
                sel.style.fInterimChar = FALSE;
                m_pContext->SetSelection(ec, 1, &sel);
            }
            if (pRange)
                pRange->Release();
        }
        else if (m_dwAction == 4)
        {
            HWND hwnd = GetFocus();
            HIMC hIMC = ImmGetContext(hwnd);
            g_tip.fCancelInSession = ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(hwnd, hIMC);
            m_hr = S_OK;
        }
        else if (m_dwAction == 3)
        {
            m_hr = g_pComposition->EndComposition(ec);
            g_pComposition->Release();
            g_pComposition = NULL;
        }
        return S_OK;
    }

    ITfContext *m_pContext;
    LPCWSTR m_pszText;
    DWORD m_dwAction;
    HRESULT m_hr;
};

EXTERN_C HRESULT WINAPI TestTip_Compose(LPCWSTR pszText, DWORD dwAction)
{
    ITfDocumentMgr *pDocMgr = NULL;
    ITfContext *pContext = NULL;
    HRESULT hr, hrSession = E_FAIL;

    if (!g_tip.pThreadMgr)
        return E_UNEXPECTED;
    hr = g_tip.pThreadMgr->GetFocus(&pDocMgr);
    if (FAILED(hr) || !pDocMgr)
        return FAILED(hr) ? hr : TF_E_NOLOCK;
    hr = pDocMgr->GetTop(&pContext);
    pDocMgr->Release();
    if (FAILED(hr) || !pContext)
        return FAILED(hr) ? hr : TF_E_NOLOCK;

    CComposeSession session(pContext, pszText, dwAction);
    hr = pContext->RequestEditSession(g_tip.tid, &session, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    pContext->Release();
    if (FAILED(hr))
        return hr;
    if (FAILED(hrSession))
        return hrSession;
    return session.m_hr;
}

const GUID GUID_TestDisplayAttribute = {0x6b3a3f50, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

class CTestDisplayAttributeInfo : public ITfDisplayAttributeInfo
{
public:
    CTestDisplayAttributeInfo() : m_cRefs(1) { Reset(); }
    virtual ~CTestDisplayAttributeInfo() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo))
        {
            *ppv = static_cast<ITfDisplayAttributeInfo *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP GetGUID(GUID *pguid) override
    {
        *pguid = GUID_TestDisplayAttribute;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR *pbstrDesc) override
    {
        *pbstrDesc = SysAllocString(L"Test attribute");
        return S_OK;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE *pda) override
    {
        *pda = m_da;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE *pda) override
    {
        m_da = *pda;
        return S_OK;
    }
    STDMETHODIMP Reset() override
    {
        ZeroMemory(&m_da, sizeof(m_da));
        m_da.lsStyle = TF_LS_DOT;
        m_da.bAttr = TF_ATTR_TARGET_CONVERTED;
        return S_OK;
    }

    LONG m_cRefs;
    TF_DISPLAYATTRIBUTE m_da;
};

class CTestEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo
{
public:
    CTestEnumDisplayAttributeInfo() : m_cRefs(1), m_index(0) { }
    virtual ~CTestEnumDisplayAttributeInfo() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo))
        {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo **ppEnum) override
    {
        CTestEnumDisplayAttributeInfo *pEnum = new CTestEnumDisplayAttributeInfo();
        pEnum->m_index = m_index;
        *ppEnum = pEnum;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) override
    {
        ULONG cFetched = 0;
        if (ulCount && m_index == 0)
        {
            rgInfo[0] = new CTestDisplayAttributeInfo();
            m_index = 1;
            cFetched = 1;
        }
        if (pcFetched)
            *pcFetched = cFetched;
        return cFetched == ulCount ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        m_index = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG ulCount) override
    {
        m_index += ulCount;
        return S_OK;
    }

    LONG m_cRefs;
    ULONG m_index;
};

class CTestTip : public ITfTextInputProcessor, public ITfFunctionProvider, public ITfDisplayAttributeProvider
{
public:
    CTestTip() : m_cRefs(1) { }
    virtual ~CTestTip() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor))
        {
            *ppv = static_cast<ITfTextInputProcessor *>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfFunctionProvider))
        {
            *ppv = static_cast<ITfFunctionProvider *>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
        {
            *ppv = static_cast<ITfDisplayAttributeProvider *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP Activate(ITfThreadMgr *ptim, TfClientId tid) override
    {
        ++g_tip.cActivate;
        g_tip.pTip = static_cast<ITfTextInputProcessor *>(this);
        g_tip.pThreadMgr = ptim;
        ITfSourceSingle *pSource = NULL;
        g_tip.hrAdviseProvider = ptim->QueryInterface(IID_ITfSourceSingle, (void **)&pSource);
        if (pSource)
        {
            g_tip.hrAdviseProvider = pSource->AdviseSingleSink(tid, IID_ITfFunctionProvider,
                                                               static_cast<ITfFunctionProvider *>(this));
            pSource->Release();
        }
        g_tip.tid = tid;
        return S_OK;
    }
    STDMETHODIMP Deactivate() override
    {
        ++g_tip.cDeactivate;
        ITfSourceSingle *pSource = NULL;
        if (g_tip.pThreadMgr && SUCCEEDED(g_tip.pThreadMgr->QueryInterface(IID_ITfSourceSingle, (void **)&pSource)))
        {
            g_tip.hrUnadviseProvider = pSource->UnadviseSingleSink(g_tip.tid, IID_ITfFunctionProvider);
            pSource->Release();
        }
        g_tip.pTip = NULL;
        g_tip.pThreadMgr = NULL;
        g_tip.tid = 0;
        return S_OK;
    }

    STDMETHODIMP GetType(GUID *pguid) override
    {
        *pguid = CLSID_TestTip;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR *pbstrDesc) override
    {
        *pbstrDesc = SysAllocString(L"Test TIP");
        return S_OK;
    }
    STDMETHODIMP GetFunction(REFGUID rguid, REFIID riid, IUnknown **ppunk) override
    {
        *ppunk = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **ppEnum) override
    {
        ++g_tip.cEnumDisplayAttributes;
        *ppEnum = new CTestEnumDisplayAttributeInfo();
        return S_OK;
    }
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo **ppInfo) override
    {
        ++g_tip.cGetDisplayAttribute;
        if (!IsEqualGUID(guid, GUID_TestDisplayAttribute))
        {
            *ppInfo = NULL;
            return E_INVALIDARG;
        }
        *ppInfo = new CTestDisplayAttributeInfo();
        return S_OK;
    }

    LONG m_cRefs;
};

class CTestTipFactory : public IClassFactory
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        {
            *ppv = static_cast<IClassFactory *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) override
    {
        if (pUnkOuter)
            return CLASS_E_NOAGGREGATION;
        CTestTip *pTip = new CTestTip();
        HRESULT hr = pTip->QueryInterface(riid, ppv);
        pTip->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override { return S_OK; }
};

static CTestTipFactory s_factory;

EXTERN_C TEST_TIP_STATE *WINAPI TestTip_GetState(void)
{
    return &g_tip;
}

EXTERN_C HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!IsEqualCLSID(rclsid, CLSID_TestTip))
        return CLASS_E_CLASSNOTAVAILABLE;
    return s_factory.QueryInterface(riid, ppv);
}

EXTERN_C HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
