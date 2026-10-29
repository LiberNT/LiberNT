/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ITextStoreACP test object and TSF test helpers
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static char s_szLog[1024];

void Log_Reset(void)
{
    s_szLog[0] = 0;
}

void Log_Add(const char *pszEvent)
{
    if (s_szLog[0])
        StringCchCatA(s_szLog, _countof(s_szLog), ",");
    StringCchCatA(s_szLog, _countof(s_szLog), pszEvent);
}

const char *Log_Get(void)
{
    return s_szLog;
}

CTestStore::CTestStore(LPCWSTR pszText)
    : m_lockMode(LOCK_GRANT_SYNC)
    , m_dwDynamicFlags(0)
    , m_fAcceptComposition(TRUE)
    , m_fOwnerSink(TRUE)
    , m_lastSetTextFlags(0)
    , m_pendingLock(0)
    , m_cLockRequests(0)
    , m_pSink(NULL)
    , m_fAttr(FALSE)
    , m_attrStart(0)
    , m_attrEnd(0)
    , m_attrValue(0)
    , m_fAttrPending(FALSE)
    , m_cRefs(1)
    , m_lockHeld(0)
{
    StringCchCopyW(m_szText, _countof(m_szText), pszText);
    ZeroMemory(&m_attrId, sizeof(m_attrId));
    VariantInit(&m_attrPending);
    m_selection.acpStart = m_selection.acpEnd = 0;
    m_selection.style.ase = TS_AE_END;
    m_selection.style.fInterimChar = FALSE;
}

CTestStore::~CTestStore()
{
    if (m_pSink)
        m_pSink->Release();
}

STDMETHODIMP CTestStore::QueryInterface(REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACP))
    {
        *ppv = static_cast<ITextStoreACP *>(this);
        AddRef();
        return S_OK;
    }
    if (m_fOwnerSink && IsEqualIID(riid, IID_ITfContextOwnerCompositionSink))
    {
        *ppv = static_cast<ITfContextOwnerCompositionSink *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP CTestStore::OnStartComposition(ITfCompositionView *pComposition, BOOL *pfOk)
{
    Log_Add(m_fAcceptComposition ? "start" : "start-refused");
    *pfOk = m_fAcceptComposition;
    return S_OK;
}

STDMETHODIMP CTestStore::OnUpdateComposition(ITfCompositionView *pComposition, ITfRange *pRangeNew)
{
    Log_Add("update");
    return S_OK;
}

STDMETHODIMP CTestStore::OnEndComposition(ITfCompositionView *pComposition)
{
    Log_Add("end");
    return S_OK;
}

STDMETHODIMP_(ULONG) CTestStore::AddRef()
{
    return InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) CTestStore::Release()
{
    ULONG cRefs = InterlockedDecrement(&m_cRefs);
    if (!cRefs)
        delete this;
    return cRefs;
}

STDMETHODIMP CTestStore::AdviseSink(REFIID riid, IUnknown *punk, DWORD dwMask)
{
    if (!IsEqualIID(riid, IID_ITextStoreACPSink))
        return E_INVALIDARG;
    if (m_pSink)
        m_pSink->Release();
    return punk->QueryInterface(IID_ITextStoreACPSink, (void **)&m_pSink);
}

STDMETHODIMP CTestStore::UnadviseSink(IUnknown *punk)
{
    if (m_pSink)
    {
        m_pSink->Release();
        m_pSink = NULL;
    }
    return S_OK;
}

STDMETHODIMP CTestStore::RequestLock(DWORD dwLockFlags, HRESULT *phrSession)
{
    ++m_cLockRequests;
    if (!m_pSink)
        return E_UNEXPECTED;

    switch (m_lockMode)
    {
        case LOCK_GRANT_ASYNC:
            if (m_lockHeld || (dwLockFlags & TS_LF_SYNC))
            {
                *phrSession = TS_E_SYNCHRONOUS;
                return S_OK;
            }
            m_pendingLock = dwLockFlags & TS_LF_READWRITE;
            *phrSession = TS_S_ASYNC;
            return S_OK;
        case LOCK_REFUSE_SYNCHRONOUS:
            *phrSession = TS_E_SYNCHRONOUS;
            return S_OK;
        case LOCK_REFUSE_SESSION_ERROR:
            *phrSession = E_FAIL;
            return S_OK;
        case LOCK_FAIL_CALL:
            return E_FAIL;
        default:
            break;
    }

    if (m_lockHeld)
    {
        if ((dwLockFlags & TS_LF_SYNC) || ((dwLockFlags & TS_LF_READWRITE) == TS_LF_READWRITE && m_lockHeld != TS_LF_READWRITE))
        {
            *phrSession = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        m_pendingLock = dwLockFlags & TS_LF_READWRITE;
        *phrSession = TS_S_ASYNC;
        return S_OK;
    }

    m_lockHeld = dwLockFlags & TS_LF_READWRITE;
    *phrSession = m_pSink->OnLockGranted(m_lockHeld);
    m_lockHeld = 0;
    if (m_pendingLock)
        GrantPending();
    return S_OK;
}

HRESULT CTestStore::GrantPending()
{
    if (!m_pendingLock || !m_pSink)
        return S_FALSE;
    m_lockHeld = m_pendingLock;
    m_pendingLock = 0;
    HRESULT hr = m_pSink->OnLockGranted(m_lockHeld);
    m_lockHeld = 0;
    return hr;
}

STDMETHODIMP CTestStore::GetStatus(TS_STATUS *pdcs)
{
    pdcs->dwDynamicFlags = m_dwDynamicFlags;
    pdcs->dwStaticFlags = 0;
    return S_OK;
}

STDMETHODIMP CTestStore::QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG cch, LONG *pacpResultStart, LONG *pacpResultEnd)
{
    *pacpResultStart = acpTestStart;
    *pacpResultEnd = acpTestEnd;
    return S_OK;
}

STDMETHODIMP CTestStore::GetSelection(ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP *pSelection, ULONG *pcFetched)
{
    if (!ulCount)
    {
        *pcFetched = 0;
        return S_OK;
    }
    *pSelection = m_selection;
    *pcFetched = 1;
    return S_OK;
}

STDMETHODIMP CTestStore::SetSelection(ULONG ulCount, const TS_SELECTION_ACP *pSelection)
{
    if (ulCount)
        m_selection = *pSelection;
    return S_OK;
}

STDMETHODIMP CTestStore::GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain, ULONG cchPlainReq, ULONG *pcchPlainRet,
                                 TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq, ULONG *pcRunInfoRet, LONG *pacpNext)
{
    LONG len = lstrlenW(m_szText);
    if (acpEnd < 0 || acpEnd > len)
        acpEnd = len;
    if (acpStart > acpEnd)
        return TS_E_INVALIDPOS;
    ULONG cch = min((ULONG)(acpEnd - acpStart), cchPlainReq);
    if (pchPlain && cch)
        CopyMemory(pchPlain, m_szText + acpStart, cch * sizeof(WCHAR));
    *pcchPlainRet = cch;
    if (prgRunInfo && cRunInfoReq)
    {
        prgRunInfo[0].uCount = cch;
        prgRunInfo[0].type = TS_RT_PLAIN;
        *pcRunInfoRet = 1;
    }
    else
    {
        *pcRunInfoRet = 0;
    }
    *pacpNext = acpStart + cch;
    return S_OK;
}

STDMETHODIMP CTestStore::SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd, const WCHAR *pchText, ULONG cch, TS_TEXTCHANGE *pChange)
{
    WCHAR szNew[256];
    LONG len = lstrlenW(m_szText);
    if (acpStart < 0 || acpEnd > len || acpStart > acpEnd)
        return TS_E_INVALIDPOS;
    m_lastSetTextFlags = dwFlags;
    StringCchCopyNW(szNew, _countof(szNew), m_szText, acpStart);
    StringCchCatNW(szNew, _countof(szNew), pchText, cch);
    StringCchCatW(szNew, _countof(szNew), m_szText + acpEnd);
    StringCchCopyW(m_szText, _countof(m_szText), szNew);
    pChange->acpStart = acpStart;
    pChange->acpOldEnd = acpEnd;
    pChange->acpNewEnd = acpStart + cch;
    return S_OK;
}

STDMETHODIMP CTestStore::GetFormattedText(LONG acpStart, LONG acpEnd, IDataObject **ppDataObject)
{
    return E_NOTIMPL;
}

STDMETHODIMP CTestStore::GetEmbedded(LONG acpPos, REFGUID rguidService, REFIID riid, IUnknown **ppunk)
{
    return E_NOTIMPL;
}

STDMETHODIMP CTestStore::QueryInsertEmbedded(const GUID *pguidService, const FORMATETC *pFormatEtc, BOOL *pfInsertable)
{
    *pfInsertable = FALSE;
    return S_OK;
}

STDMETHODIMP CTestStore::InsertEmbedded(DWORD dwFlags, LONG acpStart, LONG acpEnd, IDataObject *pDataObject, TS_TEXTCHANGE *pChange)
{
    return E_NOTIMPL;
}

STDMETHODIMP CTestStore::InsertTextAtSelection(DWORD dwFlags, const WCHAR *pchText, ULONG cch, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange)
{
    TS_TEXTCHANGE change;
    LONG start = m_selection.acpStart;
    if (dwFlags & TS_IAS_QUERYONLY)
    {
        *pacpStart = start;
        *pacpEnd = m_selection.acpEnd;
        return S_OK;
    }
    HRESULT hr = SetText(0, start, m_selection.acpEnd, pchText, cch, &change);
    if (FAILED(hr))
        return hr;
    m_selection.acpStart = m_selection.acpEnd = start + cch;
    if (pacpStart)
        *pacpStart = start;
    if (pacpEnd)
        *pacpEnd = start + cch;
    if (pChange)
        *pChange = change;
    return S_OK;
}

STDMETHODIMP CTestStore::InsertEmbeddedAtSelection(DWORD dwFlags, IDataObject *pDataObject, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange)
{
    return E_NOTIMPL;
}

static BOOL FilterHas(const GUID &id, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs)
{
    for (ULONG i = 0; i < cFilterAttrs; i++)
    {
        if (IsEqualGUID(paFilterAttrs[i], id))
            return TRUE;
    }
    return FALSE;
}

STDMETHODIMP CTestStore::RequestSupportedAttrs(DWORD dwFlags, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs)
{
    char sz[64];
    if (!m_fAttr)
        return E_NOTIMPL;
    StringCchPrintfA(sz, _countof(sz), "supported(%lx)", dwFlags);
    Log_Add(sz);
    m_fAttrPending = FilterHas(m_attrId, cFilterAttrs, paFilterAttrs);
    VariantInit(&m_attrPending);
    return S_OK;
}

STDMETHODIMP CTestStore::RequestAttrsAtPosition(LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags)
{
    char sz[64];
    if (!m_fAttr)
        return E_NOTIMPL;
    StringCchPrintfA(sz, _countof(sz), "at(%ld,%lx)", acpPos, dwFlags);
    Log_Add(sz);
    m_fAttrPending = FilterHas(m_attrId, cFilterAttrs, paFilterAttrs);
    VariantInit(&m_attrPending);
    if (acpPos >= m_attrStart && acpPos < m_attrEnd)
    {
        V_VT(&m_attrPending) = VT_I4;
        V_I4(&m_attrPending) = m_attrValue;
    }
    return S_OK;
}

STDMETHODIMP CTestStore::RequestAttrsTransitioningAtPosition(LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags)
{
    char sz[64];
    if (!m_fAttr)
        return E_NOTIMPL;
    StringCchPrintfA(sz, _countof(sz), "transitioning(%ld,%lx)", acpPos, dwFlags);
    Log_Add(sz);
    m_fAttrPending = FALSE;
    return S_OK;
}

STDMETHODIMP CTestStore::FindNextAttrTransition(LONG acpStart, LONG acpHalt, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags,
                                                LONG *pacpNext, BOOL *pfFound, LONG *plFoundOffset)
{
    char sz[64];
    LONG acpEnd = (LONG)wcslen(m_szText);
    if (!m_fAttr)
        return E_NOTIMPL;
    StringCchPrintfA(sz, _countof(sz), "next(%ld,%ld,%lx)", acpStart, acpHalt, dwFlags);
    Log_Add(sz);
    *plFoundOffset = 0;
    if (acpStart < m_attrStart)
        *pacpNext = m_attrStart;
    else if (acpStart < m_attrEnd)
        *pacpNext = m_attrEnd;
    else
        *pacpNext = acpEnd;
    *pfFound = *pacpNext < acpEnd;
    if (acpHalt >= 0 && *pacpNext > acpHalt)
    {
        *pacpNext = acpHalt;
        *pfFound = FALSE;
    }
    return S_OK;
}

STDMETHODIMP CTestStore::RetrieveRequestedAttrs(ULONG ulCount, TS_ATTRVAL *paAttrVals, ULONG *pcFetched)
{
    *pcFetched = 0;
    if (!m_fAttr || !m_fAttrPending || !ulCount)
        return S_OK;
    paAttrVals[0].idAttr = m_attrId;
    paAttrVals[0].dwOverlapId = 0;
    VariantInit(&paAttrVals[0].varValue);
    VariantCopy(&paAttrVals[0].varValue, &m_attrPending);
    *pcFetched = 1;
    m_fAttrPending = FALSE;
    return S_OK;
}

STDMETHODIMP CTestStore::GetEndACP(LONG *pacp)
{
    *pacp = lstrlenW(m_szText);
    return S_OK;
}

STDMETHODIMP CTestStore::GetActiveView(TsViewCookie *pvcView)
{
    *pvcView = 1;
    return S_OK;
}

STDMETHODIMP CTestStore::GetACPFromPoint(TsViewCookie vcView, const POINT *ptScreen, DWORD dwFlags, LONG *pacp)
{
    return E_NOTIMPL;
}

STDMETHODIMP CTestStore::GetTextExt(TsViewCookie vcView, LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped)
{
    SetRect(prc, acpStart * 10, 0, acpEnd * 10, 20);
    *pfClipped = FALSE;
    return S_OK;
}

STDMETHODIMP CTestStore::GetScreenExt(TsViewCookie vcView, RECT *prc)
{
    SetRect(prc, 0, 0, 1000, 20);
    return S_OK;
}

STDMETHODIMP CTestStore::GetWnd(TsViewCookie vcView, HWND *phwnd)
{
    *phwnd = NULL;
    return S_OK;
}

STDMETHODIMP CTestEditSession::QueryInterface(REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
    {
        *ppv = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CTestEditSession::Release()
{
    ULONG cRefs = InterlockedDecrement(&m_cRefs);
    if (!cRefs)
        delete this;
    return cRefs;
}

STDMETHODIMP CTestEditSession::DoEditSession(TfEditCookie ec)
{
    ++m_cCalls;
    m_ecLast = ec;
    if (m_pfn)
        m_pfn(ec, m_pContext);
    return S_OK;
}

BOOL Fixture_Init(TSF_FIXTURE *pFixture, LPCWSTR pszText)
{
    HRESULT hr;

    ZeroMemory(pFixture, sizeof(*pFixture));
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pFixture->pThreadMgr);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;
    hr = pFixture->pThreadMgr->Activate(&pFixture->tid);
    ok_hr(hr, S_OK);
    hr = pFixture->pThreadMgr->CreateDocumentMgr(&pFixture->pDocMgr);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;
    pFixture->pStore = new CTestStore(pszText);
    hr = pFixture->pDocMgr->CreateContext(pFixture->tid, 0, static_cast<ITextStoreACP *>(pFixture->pStore),
                                          &pFixture->pContext, &pFixture->ecTextStore);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;
    hr = pFixture->pDocMgr->Push(pFixture->pContext);
    ok_hr(hr, S_OK);
    return SUCCEEDED(hr);
}

void Fixture_Done(TSF_FIXTURE *pFixture)
{
    if (pFixture->pDocMgr)
    {
        pFixture->pDocMgr->Pop(TF_POPF_ALL);
        pFixture->pDocMgr->Release();
    }
    if (pFixture->pContext)
        pFixture->pContext->Release();
    if (pFixture->pStore)
        pFixture->pStore->Release();
    if (pFixture->pThreadMgr)
    {
        pFixture->pThreadMgr->Deactivate();
        pFixture->pThreadMgr->Release();
    }
    ZeroMemory(pFixture, sizeof(*pFixture));
}

HRESULT Fixture_RunSync(TSF_FIXTURE *pFixture, EDIT_FN pfn, void *pContext, DWORD dwFlags)
{
    CTestEditSession *pSession = new CTestEditSession(pfn, pContext);
    HRESULT hrSession = E_FAIL;
    HRESULT hr = pFixture->pContext->RequestEditSession(pFixture->tid, pSession, dwFlags, &hrSession);
    ok_hr(hr, S_OK);
    ok(pSession->m_cCalls == 1, "DoEditSession ran %lu times\n", pSession->m_cCalls);
    pSession->Release();
    return hrSession;
}

BOOL Range_GetExtent(TfEditCookie ec, ITfRange *pRange, LONG *pStart, LONG *pEnd)
{
    ITfRangeACP *pRangeACP;
    LONG len = 0;
    *pStart = *pEnd = -1;
    if (FAILED(pRange->QueryInterface(IID_ITfRangeACP, (void **)&pRangeACP)))
        return FALSE;
    HRESULT hr = pRangeACP->GetExtent(pStart, &len);
    pRangeACP->Release();
    *pEnd = *pStart + len;
    return SUCCEEDED(hr);
}

ITfRange *Range_New(TSF_FIXTURE *pFixture, TfEditCookie ec, LONG s, LONG e)
{
    ITfRange *pRange = NULL;
    LONG cch;
    pFixture->pContext->GetStart(ec, &pRange);
    if (pRange)
    {
        pRange->ShiftEnd(ec, e, &cch, NULL);
        pRange->ShiftStart(ec, s, &cch, NULL);
    }
    return pRange;
}
