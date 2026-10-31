/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfLangBarMgr::GetThreadLangBarItemMgr
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *FN_TF_CreateLangBarItemMgr)(ITfLangBarItemMgr **);

static HANDLE g_hReady, g_hDone;

static DWORD WINAPI ItemThread(void *pv)
{
    FN_TF_CreateLangBarItemMgr fn;
    ITfLangBarItemMgr *pMgr = NULL;
    ITfThreadMgr *pThreadMgr = NULL;
    TfClientId tid;

    CoInitialize(NULL);
    CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    if (pThreadMgr)
        pThreadMgr->Activate(&tid);
    fn = (FN_TF_CreateLangBarItemMgr)GetProcAddress(GetModuleHandleW(L"msctf.dll"), "TF_CreateLangBarItemMgr");
    if (fn)
        fn(&pMgr);
    SetEvent(g_hReady);
    WaitForSingleObject(g_hDone, 10000);
    if (pMgr)
        pMgr->Release();
    if (pThreadMgr)
    {
        pThreadMgr->Deactivate();
        pThreadMgr->Release();
    }
    CoUninitialize();
    return 0;
}

static void TestThread(ITfLangBarMgr *pLangBarMgr, DWORD dwThreadId, int line)
{
    ITfLangBarItemMgr *pItemMgr = (ITfLangBarItemMgr *)(ULONG_PTR)0xdeadbeef;
    DWORD dwThread = 0xdeadbeef;
    HRESULT hr;

    hr = pLangBarMgr->GetThreadLangBarItemMgr(dwThreadId, &pItemMgr, &dwThread);
    ok_(__FILE__, line)(hr == E_FAIL, "hr 0x%lx\n", hr);
    ok_(__FILE__, line)(dwThread == 0xdeadbeef, "thread %lu\n", dwThread);
    ok_(__FILE__, line)(pItemMgr == (ITfLangBarItemMgr *)(ULONG_PTR)0xdeadbeef, "item manager %p\n", pItemMgr);
}

START_TEST(langbarmgr)
{
    ITfLangBarMgr *pLangBarMgr = NULL;
    ITfLangBarItemMgr *pItemMgr = NULL, *pOwnMgr = NULL;
    FN_TF_CreateLangBarItemMgr fn;
    DWORD dwThread = 0xdeadbeef, dwOther;
    HANDLE hThread;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_TF_LangBarMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfLangBarMgr, (void **)&pLangBarMgr);
    ok_hr(hr, S_OK);
    if (!pLangBarMgr)
    {
        CoUninitialize();
        return;
    }

    hr = pLangBarMgr->GetThreadLangBarItemMgr(GetCurrentThreadId(), &pItemMgr, &dwThread);
    ok_hr(hr, S_OK);
    ok(pItemMgr != NULL, "no item manager\n");
    ok(dwThread == GetCurrentThreadId(), "thread %lu\n", dwThread);
    fn = (FN_TF_CreateLangBarItemMgr)GetProcAddress(GetModuleHandleW(L"msctf.dll"), "TF_CreateLangBarItemMgr");
    if (fn && SUCCEEDED(fn(&pOwnMgr)))
    {
        ok(pOwnMgr == pItemMgr, "the item manager of the thread is %p, not %p\n", pOwnMgr, pItemMgr);
        pOwnMgr->Release();
    }
    if (pItemMgr)
        pItemMgr->Release();

    dwThread = 0xdeadbeef;
    ok_hr(pLangBarMgr->GetThreadLangBarItemMgr(GetCurrentThreadId(), NULL, &dwThread), E_UNEXPECTED);
    ok(dwThread == 0xdeadbeef, "thread %lu\n", dwThread);
    ok_hr(pLangBarMgr->GetThreadLangBarItemMgr(GetCurrentThreadId(), NULL, NULL), E_UNEXPECTED);
    ok_hr(pLangBarMgr->GetThreadLangBarItemMgr(0x7ffffff0, NULL, NULL), E_FAIL);
    pItemMgr = NULL;
    ok_hr(pLangBarMgr->GetThreadLangBarItemMgr(GetCurrentThreadId(), &pItemMgr, NULL), S_OK);
    ok(pItemMgr != NULL, "no item manager\n");
    if (pItemMgr)
        pItemMgr->Release();

    g_hReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    hThread = CreateThread(NULL, 0, ItemThread, NULL, 0, &dwOther);
    WaitForSingleObject(g_hReady, 10000);

    TestThread(pLangBarMgr, dwOther, __LINE__);
    TestThread(pLangBarMgr, 0x7ffffff0, __LINE__);
    TestThread(pLangBarMgr, 0, __LINE__);

    SetEvent(g_hDone);
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);
    CloseHandle(g_hReady);
    CloseHandle(g_hDone);
    pLangBarMgr->Release();
    CoUninitialize();
}
