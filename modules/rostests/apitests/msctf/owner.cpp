/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the default text store of ITfContextOwner contexts
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

class CTestOwner : public ITfContextOwner
{
public:
    CTestOwner() : m_hwnd(NULL), m_cRefs(1), m_cTextExt(0), m_acpStart(-1), m_acpEnd(-1) { }
    virtual ~CTestOwner() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContextOwner))
        {
            *ppv = static_cast<ITfContextOwner *>(this);
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
    STDMETHODIMP GetACPFromPoint(const POINT *ptScreen, DWORD dwFlags, LONG *pacp) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped) override
    {
        ++m_cTextExt;
        m_acpStart = acpStart;
        m_acpEnd = acpEnd;
        SetRect(prc, 100 + acpStart, 200, 100 + acpEnd, 220);
        *pfClipped = TRUE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(RECT *prc) override
    {
        SetRect(prc, 0, 0, 640, 480);
        return S_OK;
    }
    STDMETHODIMP GetStatus(TF_STATUS *pdcs) override
    {
        pdcs->dwDynamicFlags = 0;
        pdcs->dwStaticFlags = 0;
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND *phwnd) override
    {
        *phwnd = m_hwnd;
        return S_OK;
    }
    STDMETHODIMP GetAttribute(REFGUID rguidAttribute, VARIANT *pvarValue) override
    {
        VariantInit(pvarValue);
        return S_OK;
    }

    HWND m_hwnd;
    LONG m_cRefs;
    ULONG m_cTextExt;
    LONG m_acpStart, m_acpEnd;
};

struct OWNER_TEST
{
    ITfContext *pContext;
    CTestOwner *pOwner;
};

static void OwnerSession(TfEditCookie ec, void *pv)
{
    OWNER_TEST *pTest = (OWNER_TEST *)pv;
    ITfRange *pRange = NULL, *pEnd = NULL, *pInserted = NULL;
    ITfInsertAtSelection *pInsert = NULL;
    TF_SELECTION sel;
    ULONG cFetched, cch;
    WCHAR sz[32];
    LONG s, e, lCount;
    HRESULT hr;

    hr = pTest->pContext->GetStart(ec, &pRange);
    ok_hr(hr, S_OK);
    hr = pTest->pContext->GetEnd(ec, &pEnd);
    ok_hr(hr, S_OK);
    Range_GetExtent(ec, pEnd, &s, &e);
    ok(s == 0 && e == 0, "empty document end [%ld,%ld]\n", s, e);
    pEnd->Release();

    hr = pRange->SetText(ec, 0, L"hello", 5);
    ok_hr(hr, S_OK);
    Range_GetExtent(ec, pRange, &s, &e);
    ok(s == 0 && e == 5, "text range [%ld,%ld]\n", s, e);

    hr = pTest->pContext->GetEnd(ec, &pEnd);
    Range_GetExtent(ec, pEnd, &s, &e);
    ok(s == 5 && e == 5, "end [%ld,%ld]\n", s, e);
    pEnd->Release();

    ZeroMemory(sz, sizeof(sz));
    hr = pRange->GetText(ec, 0, sz, _countof(sz), &cch);
    ok_hr(hr, S_OK);
    ok_wstr(sz, L"hello");

    pRange->Collapse(ec, TF_ANCHOR_START);
    pRange->ShiftEnd(ec, 2, &lCount, NULL);
    pRange->ShiftStart(ec, 2, &lCount, NULL);
    sel.range = pRange;
    sel.style.ase = TF_AE_END;
    sel.style.fInterimChar = FALSE;
    hr = pTest->pContext->SetSelection(ec, 1, &sel);
    ok_hr(hr, S_OK);
    pRange->Release();
    pRange = NULL;

    hr = pTest->pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &cFetched);
    ok_hr(hr, S_OK);
    ok_long(cFetched, 1);
    if (cFetched)
    {
        Range_GetExtent(ec, sel.range, &s, &e);
        ok(s == 2 && e == 2, "selection [%ld,%ld]\n", s, e);
        sel.range->Release();
    }

    hr = pTest->pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsert);
    ok_hr(hr, S_OK);
    if (pInsert)
    {
        hr = pInsert->InsertTextAtSelection(ec, 0, L"XY", 2, &pInserted);
        ok_hr(hr, S_OK);
        if (pInserted)
        {
            Range_GetExtent(ec, pInserted, &s, &e);
            ok(s == 2 && e == 4, "inserted [%ld,%ld]\n", s, e);
            ZeroMemory(sz, sizeof(sz));
            pInserted->GetText(ec, TF_TF_IGNOREEND, sz, 7, &cch);
            ok_wstr(sz, L"XYllo");
            pInserted->Release();
        }
        pInsert->Release();
    }

}

static void TextExtSession(TfEditCookie ec, void *pv)
{
    OWNER_TEST *pTest = (OWNER_TEST *)pv;
    ITfContextView *pView = NULL;
    ITfRange *pRange = NULL;
    RECT rc = { 0 };
    BOOL fClipped = FALSE;
    LONG lCount;
    HRESULT hr;

    hr = pTest->pContext->GetStart(ec, &pRange);
    ok_hr(hr, S_OK);
    if (!pRange)
        return;
    hr = pRange->SetText(ec, 0, L"hello", 5);
    ok_hr(hr, S_OK);
    pRange->ShiftEnd(ec, 3, &lCount, NULL);
    pRange->ShiftStart(ec, 1, &lCount, NULL);

    hr = pTest->pContext->GetActiveView(&pView);
    ok_hr(hr, S_OK);
    if (pView)
    {
        hr = pView->GetTextExt(ec, pRange, &rc, &fClipped);
        ok_hr(hr, S_OK);
        ok_long(pTest->pOwner->m_cTextExt, 1);
        ok(pTest->pOwner->m_acpStart == 1 && pTest->pOwner->m_acpEnd == 5, "owner asked for [%ld,%ld]\n",
           pTest->pOwner->m_acpStart, pTest->pOwner->m_acpEnd);
        ok(rc.left == 101 && rc.top == 200 && rc.right == 105 && rc.bottom == 220, "rect (%ld,%ld)-(%ld,%ld)\n",
           rc.left, rc.top, rc.right, rc.bottom);
        ok(fClipped, "not clipped\n");
        pView->Release();
    }
    pRange->Release();
}

static void TestAdvisedOwner(ITfThreadMgr *pThreadMgr, ITfDocumentMgr *pDocMgr, TfClientId tid)
{
    OWNER_TEST test;
    ITfSource *pSource = NULL;
    CTestEditSession *pSession;
    TfEditCookie ec;
    DWORD dwCookie = TF_INVALID_COOKIE;
    HRESULT hr, hrSession;

    ZeroMemory(&test, sizeof(test));
    test.pOwner = new CTestOwner();
    hr = pDocMgr->CreateContext(tid, 0, NULL, &test.pContext, &ec);
    ok_hr(hr, S_OK);
    if (!test.pContext)
    {
        test.pOwner->Release();
        return;
    }
    hr = test.pContext->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (pSource)
    {
        hr = pSource->AdviseSink(IID_ITfContextOwner, static_cast<ITfContextOwner *>(test.pOwner), &dwCookie);
        ok_hr(hr, S_OK);
    }
    hr = pDocMgr->Push(test.pContext);
    ok_hr(hr, S_OK);
    hr = pThreadMgr->SetFocus(pDocMgr);
    ok_hr(hr, S_OK);
    pSession = new CTestEditSession(TextExtSession, &test);
    hrSession = E_FAIL;
    hr = test.pContext->RequestEditSession(tid, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 1);
    pSession->Release();
    if (pSource)
    {
        if (dwCookie != TF_INVALID_COOKIE)
            ok_hr(pSource->UnadviseSink(dwCookie), S_OK);
        pSource->Release();
    }
    pDocMgr->Pop(TF_POPF_ALL);
    test.pContext->Release();
    test.pOwner->Release();
}

START_TEST(owner)
{
    ITfThreadMgr *pThreadMgr = NULL;
    ITfDocumentMgr *pDocMgr = NULL;
    TfClientId tid;
    TfEditCookie ecOwner;
    OWNER_TEST test;
    CTestEditSession *pSession;
    HRESULT hr, hrSession;

    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }
    pThreadMgr->Activate(&tid);
    pThreadMgr->CreateDocumentMgr(&pDocMgr);

    ZeroMemory(&test, sizeof(test));
    test.pOwner = new CTestOwner();
    hr = pDocMgr->CreateContext(tid, 0, static_cast<ITfContextOwner *>(test.pOwner), &test.pContext, &ecOwner);
    ok_hr(hr, S_OK);
    ok(ecOwner == 0, "edit cookie %lu\n", ecOwner);
    if (test.pContext)
    {
        hr = pDocMgr->Push(test.pContext);
        ok_hr(hr, S_OK);
        hr = pThreadMgr->SetFocus(pDocMgr);
        ok_hr(hr, S_OK);
        pSession = new CTestEditSession(OwnerSession, &test);
        hrSession = E_FAIL;
        hr = test.pContext->RequestEditSession(tid, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        ok_hr(hr, S_OK);
        ok_hr(hrSession, S_OK);
        ok_long(pSession->m_cCalls, 1);
        pSession->Release();
        pDocMgr->Pop(TF_POPF_ALL);
        test.pContext->Release();
    }
    test.pOwner->Release();
    TestAdvisedOwner(pThreadMgr, pDocMgr, tid);
    pDocMgr->Release();
    pThreadMgr->Deactivate();
    pThreadMgr->Release();
    CoUninitialize();
}
