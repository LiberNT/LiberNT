/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfTextEditSink
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

class CTestTextEditSink : public ITfTextEditSink
{
public:
    CTestTextEditSink() : m_cRefs(1), m_cCalls(0), m_fSelectionChanged(-1), m_cRanges(0), m_lStart(-1), m_lEnd(-1) { }
    virtual ~CTestTextEditSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextEditSink))
        {
            *ppv = static_cast<ITfTextEditSink *>(this);
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
    STDMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord) override
    {
        IEnumTfRanges *pEnum = NULL;
        ITfRange *pRange;
        ULONG cFetched;

        ++m_cCalls;
        m_fSelectionChanged = -1;
        m_cRanges = 0;
        m_lStart = m_lEnd = -1;
        pEditRecord->GetSelectionStatus(&m_fSelectionChanged);
        pEditRecord->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0, &pEnum);
        while (pEnum && pEnum->Next(1, &pRange, &cFetched) == S_OK && cFetched)
        {
            Range_GetExtent(ecReadOnly, pRange, &m_lStart, &m_lEnd);
            ++m_cRanges;
            pRange->Release();
        }
        if (pEnum)
            pEnum->Release();
        return S_OK;
    }

    LONG m_cRefs;
    ULONG m_cCalls;
    BOOL m_fSelectionChanged;
    ULONG m_cRanges;
    LONG m_lStart, m_lEnd;
};

static void SetTextSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pRange = Range_New(pFixture, ec, 2, 4);
    pRange->SetText(ec, 0, L"xyz", 3);
    pRange->Release();
}

static void SelectSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    TF_SELECTION sel;
    sel.range = Range_New(pFixture, ec, 1, 3);
    sel.style.ase = TF_AE_END;
    sel.style.fInterimChar = FALSE;
    ok_hr(pFixture->pContext->SetSelection(ec, 1, &sel), S_OK);
    sel.range->Release();
}

static void NothingSession(TfEditCookie ec, void *pv)
{
}

START_TEST(textedit)
{
    TSF_FIXTURE fixture;
    CTestTextEditSink *pSink;
    ITfSource *pSource = NULL;
    DWORD dwCookie = 0;
    TS_TEXTCHANGE change;
    HRESULT hr;

    CoInitialize(NULL);
    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        CoUninitialize();
        return;
    }

    pSink = new CTestTextEditSink();
    hr = fixture.pContext->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    hr = pSource->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(pSink), &dwCookie);
    ok_hr(hr, S_OK);

    Fixture_RunSync(&fixture, NothingSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
    ok_long(pSink->m_cCalls, 0);

    Fixture_RunSync(&fixture, SetTextSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
    ok_long(pSink->m_cCalls, 1);
    ok_int(pSink->m_fSelectionChanged, FALSE);
    ok_long(pSink->m_cRanges, 1);
    ok(pSink->m_lStart == 2 && pSink->m_lEnd == 5, "changed [%ld,%ld]\n", pSink->m_lStart, pSink->m_lEnd);

    Fixture_RunSync(&fixture, SelectSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
    ok_long(pSink->m_cCalls, 2);
    ok_int(pSink->m_fSelectionChanged, TRUE);
    ok_long(pSink->m_cRanges, 0);

    Fixture_RunSync(&fixture, NothingSession, &fixture, TF_ES_SYNC | TF_ES_READ);
    ok_long(pSink->m_cCalls, 2);

    StringCchCopyW((LPWSTR)fixture.pStore->Text(), 256, L"012345678abc");
    change.acpStart = 9;
    change.acpOldEnd = 9;
    change.acpNewEnd = 12;
    hr = fixture.pStore->m_pSink->OnTextChange(0, &change);
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cCalls, 3);
    ok_long(pSink->m_cRanges, 1);
    ok(pSink->m_lStart == 9 && pSink->m_lEnd == 12, "changed [%ld,%ld]\n", pSink->m_lStart, pSink->m_lEnd);

    hr = fixture.pStore->m_pSink->OnStartEditTransaction();
    ok_hr(hr, S_OK);
    change.acpStart = 0;
    change.acpOldEnd = 1;
    change.acpNewEnd = 1;
    fixture.pStore->m_pSink->OnTextChange(0, &change);
    ok_long(pSink->m_cCalls, 4);
    hr = fixture.pStore->m_pSink->OnEndEditTransaction();
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cCalls, 4);

    hr = fixture.pStore->m_pSink->OnSelectionChange();
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cCalls, 5);
    ok_int(pSink->m_fSelectionChanged, TRUE);
    ok_long(pSink->m_cRanges, 0);

    hr = pSource->UnadviseSink(dwCookie);
    ok_hr(hr, S_OK);
    Fixture_RunSync(&fixture, SetTextSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
    ok_long(pSink->m_cCalls, 5);

    pSource->Release();
    pSink->Release();
    Fixture_Done(&fixture);
    CoUninitialize();
}
