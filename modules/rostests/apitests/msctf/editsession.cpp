/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfContext edit sessions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

struct NESTED
{
    TSF_FIXTURE *pFixture;
    CTestEditSession *pInner;
    DWORD dwInnerFlags;
    HRESULT hrCall;
    HRESULT hrSession;
    BOOL fWriteSession;
    HRESULT hrInWrite;
    ULONG cInnerCallsDuring;
};

static ULONG g_order[8];
static ULONG g_cOrder;

static void OrderSession(TfEditCookie ec, void *pv)
{
    if (g_cOrder < _countof(g_order))
        g_order[g_cOrder++] = (ULONG)(ULONG_PTR)pv;
}

static void NestedSession(TfEditCookie ec, void *pv)
{
    NESTED *pNested = (NESTED *)pv;
    pNested->hrInWrite = pNested->pFixture->pContext->InWriteSession(pNested->pFixture->tid, &pNested->fWriteSession);
    pNested->hrSession = 0xdeadbeef;
    pNested->hrCall = pNested->pFixture->pContext->RequestEditSession(pNested->pFixture->tid, pNested->pInner,
                                                                      pNested->dwInnerFlags, &pNested->hrSession);
    pNested->cInnerCallsDuring = pNested->pInner->m_cCalls;
}

static void PumpMessages(void)
{
    MSG msg;
    DWORD dwEnd = GetTickCount() + 200;
    while ((LONG)(dwEnd - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

static HRESULT Request(TSF_FIXTURE *pFixture, CTestEditSession *pSession, DWORD dwFlags, HRESULT *phrSession)
{
    *phrSession = 0xdeadbeef;
    return pFixture->pContext->RequestEditSession(pFixture->tid, pSession, dwFlags, phrSession);
}

static void TestSyncAndAsync(void)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pSession;
    HRESULT hr, hrSession;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }

    pSession = new CTestEditSession(NULL, NULL);
    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 1);

    hr = Request(&fixture, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 2);

    hr = Request(&fixture, pSession, TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 3);

    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_ASYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, E_INVALIDARG);

    fixture.pStore->m_lockMode = LOCK_GRANT_ASYNC;
    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_E_SYNCHRONOUS);
    ok_long(pSession->m_cCalls, 3);

    fixture.pStore->m_cLockRequests = 0;
    hr = Request(&fixture, pSession, TF_ES_ASYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_S_ASYNC);
    ok_long(pSession->m_cCalls, 3);
    ok_long(fixture.pStore->m_cLockRequests, 0);
    PumpMessages();
    ok_long(fixture.pStore->m_cLockRequests, 1);
    ok_long(pSession->m_cCalls, 3);
    hr = fixture.pStore->GrantPending();
    ok_hr(hr, S_OK);
    ok_long(pSession->m_cCalls, 4);

    hr = Request(&fixture, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_S_ASYNC);
    ok_long(pSession->m_cCalls, 4);
    PumpMessages();
    fixture.pStore->GrantPending();
    ok_long(pSession->m_cCalls, 5);

    pSession->Release();
    Fixture_Done(&fixture);
}

static void TestRefused(void)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pSession;
    HRESULT hr, hrSession;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }
    pSession = new CTestEditSession(NULL, NULL);

    fixture.pStore->m_lockMode = LOCK_REFUSE_SYNCHRONOUS;
    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_E_SYNCHRONOUS);
    ok_long(pSession->m_cCalls, 0);

    fixture.pStore->m_lockMode = LOCK_REFUSE_SESSION_ERROR;
    hr = Request(&fixture, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, E_FAIL);
    ok_long(pSession->m_cCalls, 0);

    fixture.pStore->m_lockMode = LOCK_FAIL_CALL;
    hr = Request(&fixture, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    ok_hr(hr, E_FAIL);
    ok_long(pSession->m_cCalls, 0);

    fixture.pStore->m_lockMode = LOCK_GRANT_SYNC;
    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 1);

    pSession->Release();
    Fixture_Done(&fixture);
}

static void TestQueuedRefused(LOCK_MODE mode)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pFirst, *pSecond;
    HRESULT hr, hrSession;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }
    pFirst = new CTestEditSession(NULL, NULL);
    pSecond = new CTestEditSession(NULL, NULL);

    fixture.pStore->m_lockMode = mode;
    fixture.pStore->m_cLockRequests = 0;
    hr = Request(&fixture, pFirst, TF_ES_ASYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_S_ASYNC);
    PumpMessages();
    ok_long(pFirst->m_cCalls, 0);
    ok_long(fixture.pStore->m_cLockRequests, 1);

    fixture.pStore->m_lockMode = LOCK_GRANT_SYNC;
    hr = Request(&fixture, pSecond, TF_ES_ASYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TF_S_ASYNC);
    PumpMessages();
    ok_long(pFirst->m_cCalls, 1);
    ok_long(pSecond->m_cCalls, 1);
    ok_long(fixture.pStore->m_cLockRequests, 2);

    pFirst->Release();
    pSecond->Release();
    Fixture_Done(&fixture);
}

static void TestNested(void)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pOuter;
    NESTED nested;
    HRESULT hr, hrSession;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }

    ZeroMemory(&nested, sizeof(nested));
    nested.pFixture = &fixture;
    nested.pInner = new CTestEditSession(NULL, NULL);
    pOuter = new CTestEditSession(NestedSession, &nested);

    nested.dwInnerFlags = TF_ES_SYNC | TF_ES_READ;
    hr = Request(&fixture, pOuter, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_hr(nested.hrInWrite, S_OK);
    ok_int(nested.fWriteSession, TRUE);
    ok_hr(nested.hrCall, S_OK);
    ok_hr(nested.hrSession, S_OK);
    ok_long(nested.cInnerCallsDuring, 1);

    nested.dwInnerFlags = TF_ES_SYNC | TF_ES_READWRITE;
    hr = Request(&fixture, pOuter, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_int(nested.fWriteSession, FALSE);
    ok_hr(nested.hrCall, TF_E_LOCKED);
    ok_hr(nested.hrSession, TF_E_LOCKED);
    ok_long(nested.cInnerCallsDuring, 1);

    nested.dwInnerFlags = TF_ES_ASYNC | TF_ES_READWRITE;
    hr = Request(&fixture, pOuter, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_hr(nested.hrCall, S_OK);
    ok_hr(nested.hrSession, TF_S_ASYNC);
    ok_long(nested.cInnerCallsDuring, 1);
    PumpMessages();
    fixture.pStore->GrantPending();
    ok_long(nested.pInner->m_cCalls, 2);

    pOuter->Release();
    nested.pInner->Release();
    Fixture_Done(&fixture);
}

static void TestQueueOrder(void)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pSessions[3];
    HRESULT hr, hrSession;
    ULONG i;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }

    fixture.pStore->m_lockMode = LOCK_GRANT_ASYNC;
    g_cOrder = 0;
    for (i = 0; i < 3; i++)
    {
        pSessions[i] = new CTestEditSession(OrderSession, (void *)(ULONG_PTR)(i + 1));
        hr = Request(&fixture, pSessions[i], TF_ES_ASYNC | TF_ES_READ, &hrSession);
        ok_hr(hr, S_OK);
        ok_hr(hrSession, TF_S_ASYNC);
    }
    PumpMessages();
    ok_long(fixture.pStore->m_cLockRequests, 1);
    fixture.pStore->GrantPending();
    ok_long(g_cOrder, 3);
    ok_long(g_order[0], 1);
    ok_long(g_order[1], 2);
    ok_long(g_order[2], 3);

    for (i = 0; i < 3; i++)
        pSessions[i]->Release();
    Fixture_Done(&fixture);
}

static void TestReadOnly(void)
{
    TSF_FIXTURE fixture;
    CTestEditSession *pSession;
    HRESULT hr, hrSession;

    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        return;
    }
    pSession = new CTestEditSession(NULL, NULL);

    fixture.pStore->m_dwDynamicFlags = TS_SD_READONLY;
    if (fixture.pStore->m_pSink)
        fixture.pStore->m_pSink->OnStatusChange(TS_SD_READONLY);

    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, TS_E_READONLY);
    ok_long(pSession->m_cCalls, 0);

    hr = Request(&fixture, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    ok_hr(hr, S_OK);
    ok_hr(hrSession, S_OK);
    ok_long(pSession->m_cCalls, 1);

    pSession->Release();
    Fixture_Done(&fixture);
}

START_TEST(editsession)
{
    CoInitialize(NULL);
    TestSyncAndAsync();
    TestRefused();
    TestQueuedRefused(LOCK_REFUSE_SYNCHRONOUS);
    TestQueuedRefused(LOCK_REFUSE_SESSION_ERROR);
    TestQueuedRefused(LOCK_FAIL_CALL);
    TestNested();
    TestQueueOrder();
    TestReadOnly();
    CoUninitialize();
}
