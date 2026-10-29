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

START_TEST(composition)
{
    CoInitialize(NULL);
    TestStartAndEnd();
    TestRefused();
    TestTerminate();
    TestTerminateInSession();
    TestAfterPop();
    CoUninitialize();
}
