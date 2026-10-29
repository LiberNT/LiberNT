/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the lang bar item manager
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static const GUID GUID_LBI_SYSTEM_HELP = {0xed9d5450, 0xebe6, 0x4255, {0x82, 0x89, 0xf8, 0xa3, 0x1e, 0x68, 0x72, 0x28}};
static const GUID GUID_HELP_SERVICE = {0x5d997390, 0x8320, 0x400a, {0x88, 0xe5, 0x35, 0xb0, 0xc1, 0x20, 0xf9, 0x0b}};

typedef HRESULT (WINAPI *FN_TF_CreateLangBarItemMgr)(ITfLangBarItemMgr **);

static ITfLangBarItemMgr *CreateItemMgr(void)
{
    FN_TF_CreateLangBarItemMgr fn;
    ITfLangBarItemMgr *pMgr = NULL;
    fn = (FN_TF_CreateLangBarItemMgr)GetProcAddress(GetModuleHandleW(L"msctf.dll"), "TF_CreateLangBarItemMgr");
    if (!fn)
        return NULL;
    ok_hr(fn(&pMgr), S_OK);
    return pMgr;
}

static void CheckHelpItem(ITfLangBarItemMgr *pMgr)
{
    ITfLangBarItem *pItem = NULL;
    TF_LANGBARITEMINFO info;
    DWORD dwStatus = 0xdeadbeef;
    BSTR bstrTip = NULL;
    ITfSource *pSource = NULL;
    HRESULT hr;

    hr = pMgr->GetItem(GUID_LBI_SYSTEM_HELP, &pItem);
    ok_hr(hr, S_OK);
    if (!pItem)
        return;

    hr = pItem->GetInfo(&info);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(info.clsidService, GUID_HELP_SERVICE), "unexpected clsidService\n");
    ok(IsEqualGUID(info.guidItem, GUID_LBI_SYSTEM_HELP), "unexpected guidItem\n");
    ok_long(info.dwStyle, TF_LBI_STYLE_BTN_MENU);
    ok_long((LONG)info.ulSort, -30);
    ok(info.szDescription[0] != UNICODE_NULL, "empty description\n");

    hr = pItem->GetStatus(&dwStatus);
    ok_hr(hr, S_OK);
    ok_long(dwStatus, 0);

    hr = pItem->GetTooltipString(&bstrTip);
    ok_hr(hr, S_OK);
    ok(bstrTip != NULL, "tooltip is NULL\n");
    if (bstrTip)
    {
        ok_long(SysStringLen(bstrTip), 0);
        SysFreeString(bstrTip);
    }

    hr = pItem->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (pSource)
        pSource->Release();

    pItem->Release();
}

static const GUID GUID_TEST_LBI = {0x6b3a3f70, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

class CTestLangBarItem : public ITfLangBarItem, public ITfSource
{
public:
    CTestLangBarItem() : m_cRefs(1), m_cAdvise(0), m_cUnadvise(0), m_pSink(NULL) { }
    virtual ~CTestLangBarItem() { if (m_pSink) m_pSink->Release(); }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem))
            *ppv = static_cast<ITfLangBarItem *>(this);
        else if (IsEqualIID(riid, IID_ITfSource))
            *ppv = static_cast<ITfSource *>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO *pInfo) override
    {
        ZeroMemory(pInfo, sizeof(*pInfo));
        pInfo->guidItem = GUID_TEST_LBI;
        pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON;
        StringCchCopyW(pInfo->szDescription, _countof(pInfo->szDescription), L"Test item");
        return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override
    {
        *pdwStatus = TF_LBI_STATUS_DISABLED;
        return S_OK;
    }
    STDMETHODIMP Show(BOOL fShow) override { return S_OK; }
    STDMETHODIMP GetTooltipString(BSTR *pbstr) override
    {
        *pbstr = SysAllocString(L"tip");
        return S_OK;
    }
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD *pdwCookie) override
    {
        ++m_cAdvise;
        if (m_pSink)
            m_pSink->Release();
        m_pSink = punk;
        punk->AddRef();
        *pdwCookie = 1;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override
    {
        ++m_cUnadvise;
        if (m_pSink)
        {
            m_pSink->Release();
            m_pSink = NULL;
        }
        return S_OK;
    }

    LONG m_cRefs;
    ULONG m_cAdvise, m_cUnadvise;
    IUnknown *m_pSink;
};

class CTestItemSink : public ITfLangBarItemSink
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItemSink))
        {
            *ppv = static_cast<ITfLangBarItemSink *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP OnUpdate(DWORD dwFlags) override { return S_OK; }
};

static void TestCustomItem(ITfLangBarItemMgr *pMgr)
{
    CTestLangBarItem *pItem = new CTestLangBarItem();
    CTestItemSink sink;
    ITfLangBarItem *pGot = NULL;
    ULONG cBefore = 0, cAfter = 0, cFetched = 0;
    DWORD dwCookie = 0, dwStatus = 0;
    HRESULT hr;

    hr = pMgr->GetItemNum(&cBefore);
    ok_hr(hr, S_OK);
    ok(cBefore >= 1, "no system items\n");

    ok_hr(pMgr->AddItem(pItem), S_OK);
    ok_hr(pMgr->AddItem(pItem), E_FAIL);
    ok_hr(pMgr->GetItemNum(&cAfter), S_OK);
    ok_long(cAfter, cBefore + 1);

    hr = pMgr->GetItem(GUID_TEST_LBI, &pGot);
    ok_hr(hr, S_OK);
    ok(pGot == static_cast<ITfLangBarItem *>(pItem), "got %p\n", pGot);
    if (pGot)
        pGot->Release();

    hr = pMgr->GetItemsStatus(1, &GUID_TEST_LBI, &dwStatus);
    ok_hr(hr, S_OK);
    ok_long(dwStatus, TF_LBI_STATUS_DISABLED);

    hr = pMgr->AdviseItemSink(&sink, &dwCookie, GUID_TEST_LBI);
    ok_hr(hr, S_OK);
    ok_long(pItem->m_cAdvise, 1);
    ok(pItem->m_pSink != NULL, "item has no sink\n");
    hr = pMgr->UnadviseItemSink(dwCookie);
    ok_hr(hr, S_OK);
    ok_long(pItem->m_cUnadvise, 0);

    {
        ITfLangBarItem *items[16];
        TF_LANGBARITEMINFO infos[16];
        DWORD statuses[16];
        BOOL fFound = FALSE;
        hr = pMgr->GetItems(16, items, infos, statuses, &cFetched);
        ok_hr(hr, S_FALSE);
        ok_long(cFetched, cAfter);
        for (ULONG i = 0; i < cFetched; i++)
        {
            if (IsEqualGUID(infos[i].guidItem, GUID_TEST_LBI))
            {
                fFound = TRUE;
                ok(items[i] == static_cast<ITfLangBarItem *>(pItem), "wrong item\n");
                ok_long(statuses[i], TF_LBI_STATUS_DISABLED);
            }
            items[i]->Release();
        }
        ok(fFound, "item not returned by GetItems\n");
    }

    ok_hr(pMgr->RemoveItem(pItem), S_OK);
    ok_hr(pMgr->RemoveItem(pItem), S_OK);
    pGot = (ITfLangBarItem *)(ULONG_PTR)1;
    hr = pMgr->GetItem(GUID_TEST_LBI, &pGot);
    ok_hr(hr, S_FALSE);
    ok(pGot == NULL, "got %p\n", pGot);
    ok_hr(pMgr->GetItemNum(&cAfter), S_OK);
    ok_long(cAfter, cBefore);
    ok_long(pItem->m_cRefs, 1);
    pItem->Release();
}

static ITfLangBarItemMgr *g_pThreadMgr2;

static DWORD WINAPI SecondThread(void *pv)
{
    CoInitialize(NULL);
    g_pThreadMgr2 = CreateItemMgr();
    if (g_pThreadMgr2)
    {
        CheckHelpItem(g_pThreadMgr2);
        g_pThreadMgr2->Release();
    }
    CoUninitialize();
    return 0;
}

START_TEST(langbar)
{
    ITfLangBarItemMgr *pMgr1, *pMgr2;
    HANDLE hThread;

    CoInitialize(NULL);
    LoadLibraryW(L"msctf.dll");

    pMgr1 = CreateItemMgr();
    pMgr2 = CreateItemMgr();
    ok(pMgr1 != NULL, "no lang bar item manager\n");
    if (!pMgr1)
    {
        CoUninitialize();
        return;
    }
    ok(pMgr1 == pMgr2, "two managers on one thread: %p %p\n", pMgr1, pMgr2);
    CheckHelpItem(pMgr1);
    TestCustomItem(pMgr1);

    hThread = CreateThread(NULL, 0, SecondThread, NULL, 0, NULL);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    ok(g_pThreadMgr2 != NULL && g_pThreadMgr2 != pMgr1, "second thread got %p, first %p\n", g_pThreadMgr2, pMgr1);

    if (pMgr2)
        pMgr2->Release();
    pMgr1->Release();
    CoUninitialize();
}
