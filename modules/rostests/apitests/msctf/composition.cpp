/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for TSF compositions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

class CTestCompositionSink : public ITfCompositionSink
{
public:
    CTestCompositionSink() : m_cRefs(1), m_ecLast(0) { }
    virtual ~CTestCompositionSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompositionSink))
        {
            *ppv = static_cast<ITfCompositionSink *>(this);
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
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) override
    {
        Log_Add("terminated");
        m_ecLast = ecWrite;
        return S_OK;
    }

    LONG m_cRefs;
    TfEditCookie m_ecLast;
};

struct COMPOSITION_TEST
{
    TSF_FIXTURE *pFixture;
    CTestCompositionSink *pSink;
    ITfComposition *pComposition;
    ITfComposition *pComposition2;
    HRESULT hrStart;
};

static ULONG CountCompositions(ITfContext *pContext)
{
    ITfContextComposition *pContextComposition;
    IEnumITfCompositionView *pEnum = NULL;
    ITfCompositionView *pView;
    ULONG cFetched, cViews = 0;

    if (FAILED(pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
        return (ULONG)-1;
    pContextComposition->EnumCompositions(&pEnum);
    while (pEnum && pEnum->Next(1, &pView, &cFetched) == S_OK && cFetched)
    {
        ++cViews;
        pView->Release();
    }
    if (pEnum)
        pEnum->Release();
    pContextComposition->Release();
    return cViews;
}

static void StartSession(TfEditCookie ec, void *pv)
{
    COMPOSITION_TEST *pTest = (COMPOSITION_TEST *)pv;
    ITfContextComposition *pContextComposition;
    ITfRange *pRange = Range_New(pTest->pFixture, ec, 2, 4);
    ITfComposition **ppComposition = pTest->pComposition ? &pTest->pComposition2 : &pTest->pComposition;

    pTest->hrStart = pTest->pFixture->pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition);
    if (FAILED(pTest->hrStart))
        return;
    pTest->hrStart = pContextComposition->StartComposition(ec, pRange, pTest->pSink, ppComposition);
    pContextComposition->Release();
    pRange->Release();
}

static void ShiftSession(TfEditCookie ec, void *pv)
{
    COMPOSITION_TEST *pTest = (COMPOSITION_TEST *)pv;
    ITfRange *pRange = NULL, *pNew;
    LONG s, e;
    HRESULT hr;

    hr = pTest->pComposition->GetRange(&pRange);
    ok_hr(hr, S_OK);
    Range_GetExtent(ec, pRange, &s, &e);
    ok(s == 2 && e == 4, "composition range [%ld,%ld]\n", s, e);
    pRange->Release();

    pNew = Range_New(pTest->pFixture, ec, 6, 6);
    hr = pTest->pComposition->ShiftEnd(ec, pNew);
    ok_hr(hr, S_OK);
    pNew->Release();
    pNew = Range_New(pTest->pFixture, ec, 1, 1);
    hr = pTest->pComposition->ShiftStart(ec, pNew);
    ok_hr(hr, S_OK);
    pNew->Release();

    pTest->pComposition->GetRange(&pRange);
    Range_GetExtent(ec, pRange, &s, &e);
    ok(s == 1 && e == 6, "composition range [%ld,%ld]\n", s, e);
    pRange->Release();
}

static void FindSession(TfEditCookie ec, void *pv)
{
    COMPOSITION_TEST *pTest = (COMPOSITION_TEST *)pv;
    ITfContextComposition *pContextComposition;
    IEnumITfCompositionView *pEnum = NULL;
    ITfCompositionView *pView;
    ITfRange *pRange;
    ULONG cFetched, cViews;
    HRESULT hr;

    pTest->pFixture->pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition);

    pRange = Range_New(pTest->pFixture, ec, 3, 3);
    hr = pContextComposition->FindComposition(ec, pRange, &pEnum);
    ok_hr(hr, S_OK);
    for (cViews = 0; pEnum && pEnum->Next(1, &pView, &cFetched) == S_OK && cFetched; ++cViews)
        pView->Release();
    ok_long(cViews, 1);
    if (pEnum)
        pEnum->Release();
    pRange->Release();

    pRange = Range_New(pTest->pFixture, ec, 7, 9);
    pEnum = NULL;
    hr = pContextComposition->FindComposition(ec, pRange, &pEnum);
    ok_hr(hr, S_OK);
    for (cViews = 0; pEnum && pEnum->Next(1, &pView, &cFetched) == S_OK && cFetched; ++cViews)
        pView->Release();
    ok_long(cViews, 0);
    if (pEnum)
        pEnum->Release();
    pRange->Release();

    pEnum = NULL;
    hr = pContextComposition->FindComposition(ec, NULL, &pEnum);
    ok_hr(hr, S_OK);
    for (cViews = 0; pEnum && pEnum->Next(1, &pView, &cFetched) == S_OK && cFetched; ++cViews)
        pView->Release();
    ok_long(cViews, 1);
    if (pEnum)
        pEnum->Release();

    pContextComposition->Release();
}

static void EndSession(TfEditCookie ec, void *pv)
{
    COMPOSITION_TEST *pTest = (COMPOSITION_TEST *)pv;
    pTest->hrStart = pTest->pComposition->EndComposition(ec);
}

static void TestStartAndEnd(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    HRESULT hr;

    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        return;
    }
    ZeroMemory(&test, sizeof(test));
    test.pFixture = &fixture;
    test.pSink = new CTestCompositionSink();

    Log_Reset();
    hr = Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_hr(test.hrStart, S_OK);
    ok(test.pComposition != NULL, "no composition\n");
    ok_str(Log_Get(), "start");
    ok_long(CountCompositions(fixture.pContext), 1);
    if (!test.pComposition)
    {
        test.pSink->Release();
        Fixture_Done(&fixture);
        return;
    }

    Log_Reset();
    hr = Fixture_RunSync(&fixture, ShiftSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "update,update");

    hr = Fixture_RunSync(&fixture, FindSession, &test, TF_ES_SYNC | TF_ES_READ);
    ok_hr(hr, S_OK);

    Log_Reset();
    hr = Fixture_RunSync(&fixture, EndSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_hr(test.hrStart, S_OK);
    ok_str(Log_Get(), "end");
    ok_long(CountCompositions(fixture.pContext), 0);

    Log_Reset();
    hr = Fixture_RunSync(&fixture, EndSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(test.hrStart, E_UNEXPECTED);
    ok_str(Log_Get(), "");

    test.pComposition->Release();
    test.pSink->Release();
    Fixture_Done(&fixture);
}

static void TestRefused(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    HRESULT hr;

    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        return;
    }
    ZeroMemory(&test, sizeof(test));
    test.pFixture = &fixture;
    test.pSink = new CTestCompositionSink();
    fixture.pStore->m_fAcceptComposition = FALSE;

    Log_Reset();
    hr = Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_hr(test.hrStart, S_OK);
    ok(test.pComposition == NULL, "composition %p\n", test.pComposition);
    ok_str(Log_Get(), "start-refused");
    ok_long(CountCompositions(fixture.pContext), 0);

    if (test.pComposition)
        test.pComposition->Release();
    test.pSink->Release();
    Fixture_Done(&fixture);
}

static void TestTerminate(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    ITfContextOwnerCompositionServices *pServices = NULL;
    ITfContextComposition *pContextComposition = NULL;
    IEnumITfCompositionView *pEnum = NULL;
    ITfCompositionView *pView = NULL;
    ITfRange *pRange = NULL;
    ULONG cFetched;
    HRESULT hr;

    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        return;
    }
    ZeroMemory(&test, sizeof(test));
    test.pFixture = &fixture;
    test.pSink = new CTestCompositionSink();

    Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok(test.pComposition && test.pComposition2, "compositions %p %p\n", test.pComposition, test.pComposition2);
    ok_long(CountCompositions(fixture.pContext), 2);

    hr = fixture.pContext->QueryInterface(IID_ITfContextOwnerCompositionServices, (void **)&pServices);
    ok_hr(hr, S_OK);
    fixture.pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition);
    pContextComposition->EnumCompositions(&pEnum);
    if (pEnum)
        pEnum->Next(1, &pView, &cFetched);

    Log_Reset();
    test.pSink->m_ecLast = 0;
    hr = pServices->TerminateComposition(pView);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "terminated,end");
    ok(test.pSink->m_ecLast != 0, "no write cookie\n");
    ok_long(CountCompositions(fixture.pContext), 1);

    Log_Reset();
    hr = pServices->TerminateComposition(NULL);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "terminated,end");
    ok_long(CountCompositions(fixture.pContext), 0);

    Log_Reset();
    hr = pServices->TerminateComposition(NULL);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "");

    hr = test.pComposition->GetRange(&pRange);
    ok_hr(hr, E_UNEXPECTED);
    if (pRange)
        pRange->Release();

    if (pView)
        pView->Release();
    if (pEnum)
        pEnum->Release();
    pContextComposition->Release();
    pServices->Release();
    test.pComposition->Release();
    test.pComposition2->Release();
    test.pSink->Release();
    Fixture_Done(&fixture);
}

struct TERMINATE_IN_SESSION
{
    ITfContextOwnerCompositionServices *pServices;
    HRESULT hr;
    char szLog[64];
};

static void TerminateInSession(TfEditCookie ec, void *pv)
{
    TERMINATE_IN_SESSION *pData = (TERMINATE_IN_SESSION *)pv;

    Log_Reset();
    pData->hr = pData->pServices->TerminateComposition(NULL);
    StringCchCopyA(pData->szLog, _countof(pData->szLog), Log_Get());
}

static void TestTerminateInSession(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    TERMINATE_IN_SESSION data;
    HRESULT hr;

    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        return;
    }
    ZeroMemory(&test, sizeof(test));
    ZeroMemory(&data, sizeof(data));
    test.pFixture = &fixture;
    test.pSink = new CTestCompositionSink();
    Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_long(CountCompositions(fixture.pContext), 1);

    hr = fixture.pContext->QueryInterface(IID_ITfContextOwnerCompositionServices, (void **)&data.pServices);
    ok_hr(hr, S_OK);
    data.hr = 0xdeadbeef;
    hr = Fixture_RunSync(&fixture, TerminateInSession, &data, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_hr(data.hr, S_OK);
    ok_str(data.szLog, "terminated,end");
    ok_long(CountCompositions(fixture.pContext), 0);
    Log_Reset();
    hr = Fixture_RunSync(&fixture, EndSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "");

    Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
    {
        ITfClientId *pClientId = NULL;
        TfClientId tidOther = TF_CLIENTID_NULL;
        GUID guid = {0x6b3a3f9f, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
        fixture.pThreadMgr->QueryInterface(IID_ITfClientId, (void **)&pClientId);
        if (pClientId)
        {
            pClientId->GetClientId(guid, &tidOther);
            pClientId->Release();
        }
        CTestEditSession *pSession = new CTestEditSession(TerminateInSession, &data);
        HRESULT hrSession = 0xdeadbeef;
        data.hr = 0xdeadbeef;
        hr = fixture.pContext->RequestEditSession(tidOther, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        ok_hr(hr, S_OK);
        ok_hr(hrSession, S_OK);
        ok_long(pSession->m_cCalls, 1);
        ok_hr(data.hr, TS_E_NOLOCK);
        ok_str(data.szLog, "");
        ok_long(CountCompositions(fixture.pContext), 1);
        pSession->Release();
    }

    data.pServices->Release();
    if (test.pComposition)
        test.pComposition->Release();
    if (test.pComposition2)
        test.pComposition2->Release();
    test.pSink->Release();
    Fixture_Done(&fixture);
}

static void TestAfterPop(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    ITfRange *pRange = NULL;
    HRESULT hr;

    if (!Fixture_Init(&fixture, L"0123456789"))
    {
        Fixture_Done(&fixture);
        return;
    }
    ZeroMemory(&test, sizeof(test));
    test.pFixture = &fixture;
    test.pSink = new CTestCompositionSink();
    Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);

    Log_Reset();
    hr = fixture.pDocMgr->Pop(TF_POPF_ALL);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "end");

    if (test.pComposition)
    {
        hr = test.pComposition->GetRange(&pRange);
        ok_hr(hr, E_UNEXPECTED);
        if (pRange)
            pRange->Release();
        test.pComposition->Release();
    }
    test.pSink->Release();
    Fixture_Done(&fixture);
}

#define EDIT_PROPERTY 0x1
#define EDIT_OTHER_CLIENT 0x2
#define EDIT_NO_COMPOSITION 0x4
#define EDIT_REFUSE 0x8

struct EDIT_CASE
{
    LONG lStart, lEnd;
    LPCWSTR pszText;
    DWORD dwFlags;
    HRESULT hrExpected;
    const char *pszLog;
    LPCWSTR pszResult;
    LONG lCompStart, lCompEnd;
    BOOL fUpdatedOwn;
};

static const EDIT_CASE EditCases[] =
{
    { 2, 4, L"abc", 0, S_OK, "update,edited", L"01abc456789", 2, 5, TRUE },
    { 3, 3, L"x", 0, S_OK, "update,edited", L"012x3456789", 2, 5, TRUE },
    { 8, 9, L"y", 0, TF_E_RANGE_NOT_COVERED, "edited", L"0123456789", 2, 4, FALSE },
    { 2, 4, NULL, EDIT_PROPERTY, S_OK, "edited", L"0123456789", 2, 4, FALSE },
    { 2, 4, L"", 0, S_OK, "update,edited", L"01456789", 2, 2, TRUE },
    { 4, 4, L"z", 0, S_OK, "update,edited", L"0123z456789", 2, 5, TRUE },
    { 1, 2, L"w", 0, TF_E_RANGE_NOT_COVERED, "edited", L"0123456789", 2, 4, FALSE },
    { 1, 3, L"v", 0, TF_E_RANGE_NOT_COVERED, "edited", L"0123456789", 2, 4, FALSE },
    { 8, 9, L"y", EDIT_OTHER_CLIENT, S_OK, "start,update,end,edited", L"01234567y9", 2, 4, FALSE },
    { 3, 3, L"x", EDIT_OTHER_CLIENT, S_OK, "start,update,end,edited", L"012x3456789", 2, 5, FALSE },
    { 8, 9, L"y", EDIT_NO_COMPOSITION, S_OK, "start,update,end,edited", L"01234567y9", -1, -1, FALSE },
    { 8, 9, L"y", EDIT_NO_COMPOSITION | EDIT_REFUSE, TF_E_COMPOSITION_REJECTED, "start-refused,edited", L"0123456789", -1, -1, FALSE },
    { 8, 9, L"y", EDIT_OTHER_CLIENT | EDIT_REFUSE, TF_E_COMPOSITION_REJECTED, "start-refused,terminated,end,start-refused,edited",
      L"0123456789", -1, -1, FALSE },
};

struct EDIT_TEST
{
    COMPOSITION_TEST *pTest;
    const EDIT_CASE *pCase;
    HRESULT hr;
    LONG lCompStart, lCompEnd;
};

static void EditSession(TfEditCookie ec, void *pv)
{
    EDIT_TEST *pEdit = (EDIT_TEST *)pv;
    TSF_FIXTURE *pFixture = pEdit->pTest->pFixture;
    ITfRange *pRange = Range_New(pFixture, ec, pEdit->pCase->lStart, pEdit->pCase->lEnd);
    ITfProperty *pProperty;
    VARIANT var;

    if (pEdit->pCase->dwFlags & EDIT_PROPERTY)
    {
        pEdit->hr = pFixture->pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty);
        if (SUCCEEDED(pEdit->hr))
        {
            V_VT(&var) = VT_I4;
            V_I4(&var) = 1;
            pEdit->hr = pProperty->SetValue(ec, pRange, &var);
            pProperty->Release();
        }
    }
    else
    {
        pEdit->hr = pRange->SetText(ec, 0, pEdit->pCase->pszText, lstrlenW(pEdit->pCase->pszText));
    }
    Log_Add("edited");
    pRange->Release();
    pRange = NULL;
    if (pEdit->pTest->pComposition)
        pEdit->pTest->pComposition->GetRange(&pRange);
    if (pRange)
    {
        Range_GetExtent(ec, pRange, &pEdit->lCompStart, &pEdit->lCompEnd);
        pRange->Release();
    }
}

static HRESULT RunSyncAs(TSF_FIXTURE *pFixture, EDIT_FN pfn, void *pContext)
{
    GUID guid = {0x6b3a3f9f, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
    TfClientId tidOther = TF_CLIENTID_NULL;
    ITfClientId *pClientId = NULL;
    HRESULT hrSession = E_FAIL;

    pFixture->pThreadMgr->QueryInterface(IID_ITfClientId, (void **)&pClientId);
    if (pClientId)
    {
        pClientId->GetClientId(guid, &tidOther);
        pClientId->Release();
    }
    CTestEditSession *pSession = new CTestEditSession(pfn, pContext);
    ok_hr(pFixture->pContext->RequestEditSession(tidOther, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession), S_OK);
    pSession->Release();
    return hrSession;
}

static void TestUpdateOnEdit(void)
{
    TSF_FIXTURE fixture;
    COMPOSITION_TEST test;
    EDIT_TEST edit;
    const EDIT_CASE *pCase;
    ITfCompositionView *pView = NULL;
    HRESULT hr;

    for (UINT i = 0; i < _countof(EditCases); ++i)
    {
        pCase = &EditCases[i];
        if (!Fixture_Init(&fixture, L"0123456789"))
        {
            Fixture_Done(&fixture);
            return;
        }
        ZeroMemory(&test, sizeof(test));
        test.pFixture = &fixture;
        test.pSink = new CTestCompositionSink();
        if (!(pCase->dwFlags & EDIT_NO_COMPOSITION))
        {
            Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
            ok(test.pComposition != NULL, "case %u: no composition\n", i);
            if (!test.pComposition)
            {
                test.pSink->Release();
                Fixture_Done(&fixture);
                continue;
            }
            test.pComposition->QueryInterface(IID_ITfCompositionView, (void **)&pView);
        }
        fixture.pStore->m_fAcceptComposition = !(pCase->dwFlags & EDIT_REFUSE);
        fixture.pStore->m_pLastUpdated = NULL;
        ZeroMemory(&edit, sizeof(edit));
        edit.pTest = &test;
        edit.pCase = pCase;
        edit.lCompStart = edit.lCompEnd = -1;
        Log_Reset();
        if (pCase->dwFlags & EDIT_OTHER_CLIENT)
            hr = RunSyncAs(&fixture, EditSession, &edit);
        else
            hr = Fixture_RunSync(&fixture, EditSession, &edit, TF_ES_SYNC | TF_ES_READWRITE);
        ok_hr(hr, S_OK);
        ok(edit.hr == pCase->hrExpected, "case %u: hr 0x%lx\n", i, edit.hr);
        ok(!strcmp(Log_Get(), pCase->pszLog), "case %u: log '%s'\n", i, Log_Get());
        ok(!wcscmp(fixture.pStore->Text(), pCase->pszResult), "case %u: text '%ls'\n", i, fixture.pStore->Text());
        ok(edit.lCompStart == pCase->lCompStart && edit.lCompEnd == pCase->lCompEnd,
           "case %u: composition [%ld,%ld]\n", i, edit.lCompStart, edit.lCompEnd);
        ok((pView && fixture.pStore->m_pLastUpdated == pView) == pCase->fUpdatedOwn,
           "case %u: last update on %p, composition %p\n", i, fixture.pStore->m_pLastUpdated, pView);
        if (test.pComposition)
        {
            fixture.pStore->m_fAcceptComposition = TRUE;
            Fixture_RunSync(&fixture, EndSession, &test, TF_ES_SYNC | TF_ES_READWRITE);
            test.pComposition->Release();
        }
        if (pView)
        {
            pView->Release();
            pView = NULL;
        }
        test.pSink->Release();
        Fixture_Done(&fixture);
    }
}

START_TEST(composition)
{
    CoInitialize(NULL);
    TestStartAndEnd();
    TestRefused();
    TestTerminate();
    TestTerminateInSession();
    TestAfterPop();
    TestUpdateOnEdit();
    CoUninitialize();
}
