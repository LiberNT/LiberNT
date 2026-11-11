/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the lang bar item manager
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <olectl.h>

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

static const GUID GUID_SYSTEM_ITEM_SERVICE = {0xbebacc94, 0x5cd3, 0x4662, {0xa1, 0xe0, 0xf3, 0x31, 0x99, 0x49, 0x36, 0x69}};
static const GUID GUID_LBI_LANGUAGE = {0x58c99d96, 0x2f9b, 0x42ce, {0x91, 0xbe, 0x37, 0xef, 0x18, 0x60, 0xf8, 0x82}};
static const GUID GUID_LBI_CORRECTION = {0x190f67a2, 0x024f, 0x40d9, {0xaa, 0xba, 0x43, 0x9f, 0x5c, 0x80, 0x8f, 0x67}};
static const GUID GUID_LBI_KEYBOARD = {0x34745c63, 0xb2f0, 0x4784, {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}};

class CTestMenu : public ITfMenu
{
public:
    CTestMenu() : m_cItems(0) { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfMenu))
        {
            *ppv = static_cast<ITfMenu *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP AddMenuItem(UINT uId, DWORD dwFlags, HBITMAP hbmp, HBITMAP hbmpMask, const WCHAR *pch, ULONG cch,
                             ITfMenu **ppMenu) override
    {
        ++m_cItems;
        if (ppMenu)
            *ppMenu = NULL;
        return S_OK;
    }

    ULONG m_cItems;
};

class CTestSystemItemSink : public ITfSystemLangBarItemSink
{
public:
    CTestSystemItemSink() : m_cInitMenu(0), m_cMenuSelect(0) { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfSystemLangBarItemSink))
        {
            *ppv = static_cast<ITfSystemLangBarItemSink *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override
    {
        ++m_cInitMenu;
        return S_OK;
    }
    STDMETHODIMP OnMenuSelect(UINT wID) override
    {
        ++m_cMenuSelect;
        return S_OK;
    }

    ULONG m_cInitMenu, m_cMenuSelect;
};

static void ok_bstr_(HRESULT hr, BSTR bstr, LPCWSTR pszExpected, int line)
{
    ok_(__FILE__, line)(hr == S_OK, "hr %lx\n", hr);
    ok_(__FILE__, line)(bstr && !lstrcmpW(bstr, pszExpected), "got '%ls', expected '%ls'\n", bstr ? bstr : L"(null)",
                        pszExpected);
    SysFreeString(bstr);
}

static void CheckTexts(ITfLangBarItem *pItem, LPCWSTR pszText, LPCWSTR pszTooltip, int line)
{
    ITfLangBarItemButton *pButton = NULL;
    BSTR bstr = NULL;
    HRESULT hr;

    hr = pItem->GetTooltipString(&bstr);
    ok_bstr_(hr, bstr, pszTooltip, line);
    if (FAILED(pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pButton)))
        return;
    bstr = NULL;
    hr = pButton->GetText(&bstr);
    ok_bstr_(hr, bstr, pszText, line);
    pButton->Release();
}

static void TestSystemItemButton(ITfLangBarItem *pItem, BOOL fSystem)
{
    ITfLangBarItemButton *pButton = NULL;
    CTestMenu menu;
    HICON hIcon = (HICON)(ULONG_PTR)1;
    POINT pt = {10, 10};
    RECT rc = {0, 0, 20, 20};
    HRESULT hr;

    hr = pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pButton);
    ok_hr(hr, S_OK);
    if (!pButton)
        return;
    ok_hr(pButton->GetIcon(&hIcon), S_OK);
    ok(hIcon == NULL, "icon %p\n", hIcon);
    ok_hr(pButton->GetIcon(NULL), E_INVALIDARG);
    ok_hr(pButton->GetText(NULL), E_INVALIDARG);
    ok_hr(pButton->InitMenu(&menu), S_OK);
    ok_long(menu.m_cItems, 0);
    ok_hr(pButton->InitMenu(NULL), S_OK);
    ok_hr(pButton->OnClick(TF_LBI_CLK_RIGHT, pt, &rc), S_OK);
    ok_hr(pButton->OnMenuSelect(0), fSystem ? E_FAIL : S_OK);
    ok_hr(pButton->OnMenuSelect(1000), fSystem ? E_FAIL : S_OK);
    pButton->Release();
}

static void TestSystemItemSinks(ITfLangBarItem *pItem, BOOL fSystem)
{
    ITfSource *pSource = NULL;
    ITfLangBarItemButton *pButton = NULL;
    CTestSystemItemSink sysSink;
    CTestItemSink itemSink;
    CTestMenu menu;
    DWORD dwCookie, dwCookie2;
    HRESULT hr;

    hr = pItem->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (!pSource)
        return;
    hr = pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pButton);
    if (FAILED(hr))
    {
        pSource->Release();
        return;
    }

    dwCookie = 0;
    hr = pSource->AdviseSink(IID_ITfLangBarItemSink, &itemSink, &dwCookie);
    ok_hr(hr, CONNECT_E_CANNOTCONNECT);
    ok_long(dwCookie, fSystem ? (DWORD)-1 : 0);
    dwCookie = 0;
    hr = pSource->AdviseSink(IID_IUnknown, &sysSink, &dwCookie);
    ok_hr(hr, CONNECT_E_CANNOTCONNECT);
    ok_hr(pSource->UnadviseSink(0x7777), fSystem ? CONNECT_E_NOCONNECTION : E_FAIL);

    dwCookie = 0;
    hr = pSource->AdviseSink(IID_ITfSystemLangBarItemSink, &sysSink, &dwCookie);
    if (!fSystem)
    {
        ok_hr(hr, CONNECT_E_CANNOTCONNECT);
        ok_long(dwCookie, 0);
        pButton->Release();
        pSource->Release();
        return;
    }
    ok_hr(hr, S_OK);
    ok_long(dwCookie, 1);
    ok_hr(pButton->InitMenu(&menu), S_OK);
    ok_long(sysSink.m_cInitMenu, 1);
    ok_long(menu.m_cItems, 0);
    ok_hr(pButton->OnMenuSelect(5), E_FAIL);
    ok_long(sysSink.m_cMenuSelect, 0);
    ok_hr(pSource->UnadviseSink((DWORD)-1), E_INVALIDARG);
    ok_hr(pSource->UnadviseSink(dwCookie), S_OK);
    ok_hr(pSource->UnadviseSink(dwCookie), CONNECT_E_NOCONNECTION);
    ok_hr(pButton->InitMenu(&menu), S_OK);
    ok_long(sysSink.m_cInitMenu, 1);

    dwCookie2 = 0;
    hr = pSource->AdviseSink(IID_ITfSystemLangBarItemSink, &sysSink, &dwCookie2);
    ok_hr(hr, S_OK);
    ok_long(dwCookie2, 1);
    ok_hr(pSource->UnadviseSink(dwCookie2), S_OK);
    pButton->Release();
    pSource->Release();
}

static void TestSystemItemSetters(ITfLangBarItem *pItem)
{
    ITfSystemLangBarItem *pSys = NULL;
    ITfSystemDeviceTypeLangBarItem *pDev = NULL;
    ITfSystemLangBarItemText *pText = NULL;
    ITfLangBarItemButton *pButton = NULL;
    HICON hIcon, hSetIcon;
    BSTR bstr = NULL;
    DWORD dwMode;
    HRESULT hr;

    hr = pItem->QueryInterface(IID_ITfSystemLangBarItem, (void **)&pSys);
    ok_hr(hr, S_OK);
    hr = pItem->QueryInterface(IID_ITfSystemDeviceTypeLangBarItem, (void **)&pDev);
    ok_hr(hr, S_OK);
    hr = pItem->QueryInterface(IID_ITfSystemLangBarItemText, (void **)&pText);
    ok_hr(hr, S_OK);
    pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pButton);
    if (pSys && pDev && pText && pButton)
    {
        hr = pText->GetItemText(&bstr);
        ok_bstr_(hr, bstr, L"", __LINE__);
        ok_hr(pText->GetItemText(NULL), E_INVALIDARG);
        ok_hr(pText->SetItemText(L"XyZ", 3), S_OK);
        bstr = NULL;
        hr = pText->GetItemText(&bstr);
        ok_bstr_(hr, bstr, L"Xy", __LINE__);
        ok_hr(pSys->SetTooltipString(const_cast<WCHAR *>(L"TT"), 2), S_OK);
        CheckTexts(pItem, L"Xy", L"T", __LINE__);
        ok_hr(pSys->SetTooltipString(const_cast<WCHAR *>(L"ABCD"), 0), S_OK);
        CheckTexts(pItem, L"Xy", L"", __LINE__);
        ok_hr(pText->SetItemText(L"", 1), S_OK);
        CheckTexts(pItem, L"", L"", __LINE__);

        hSetIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
        ok_hr(pSys->SetIcon(hSetIcon), S_OK);
        hIcon = NULL;
        ok_hr(pButton->GetIcon(&hIcon), S_OK);
        ok(hIcon != NULL && hIcon != hSetIcon, "icon %p, set %p\n", hIcon, hSetIcon);
        if (hIcon)
            ok(DestroyIcon(hIcon), "DestroyIcon failed\n");
        ok_hr(pDev->SetIconMode(TF_DTLBI_USEPROFILEICON), S_OK);
        hIcon = (HICON)(ULONG_PTR)1;
        ok_hr(pButton->GetIcon(&hIcon), S_OK);
        ok(hIcon == NULL, "icon %p\n", hIcon);
        ok_hr(pDev->SetIconMode(0), S_OK);
        hIcon = NULL;
        ok_hr(pButton->GetIcon(&hIcon), S_OK);
        ok(hIcon != NULL, "no icon\n");
        if (hIcon)
            DestroyIcon(hIcon);
        ok_hr(pSys->SetIcon(NULL), S_OK);
        hIcon = (HICON)(ULONG_PTR)1;
        ok_hr(pButton->GetIcon(&hIcon), S_OK);
        ok(hIcon == NULL, "icon %p\n", hIcon);

        dwMode = 0xdead;
        ok_hr(pDev->GetIconMode(&dwMode), S_OK);
        ok_long(dwMode, 0);
        ok_hr(pDev->GetIconMode(NULL), E_INVALIDARG);
        ok_hr(pDev->SetIconMode(1), S_OK);
        dwMode = 0xdead;
        ok_hr(pDev->GetIconMode(&dwMode), S_OK);
        ok_long(dwMode, 1);
        ok_hr(pDev->SetIconMode(0), S_OK);
    }

    if (pButton)
        pButton->Release();
    if (pText)
        pText->Release();
    if (pDev)
        pDev->Release();
    if (pSys)
        pSys->Release();
}

static void TestSystemItems(ITfLangBarItemMgr *pMgr)
{
    static const struct
    {
        const GUID *pguidItem;
        const GUID *pguidService;
        DWORD dwStyle;
        LONG lSort;
        BOOL fSystem;
    } s_Expected[] = {
        { &GUID_LBI_LANGUAGE, &GUID_SYSTEM_ITEM_SERVICE, TF_LBI_STYLE_BTN_MENU, 0, FALSE },
        { &GUID_LBI_CORRECTION, &GUID_SYSTEM_ITEM_SERVICE, TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_HIDDENBYDEFAULT, 0,
          FALSE },
        { &GUID_LBI_KEYBOARD, &GUID_SYSTEM_ITEM_SERVICE, TF_LBI_STYLE_BTN_MENU, 0, TRUE },
        { &GUID_LBI_SYSTEM_HELP, &GUID_HELP_SERVICE, TF_LBI_STYLE_BTN_MENU, -30, TRUE },
    };
    ITfLangBarItem *items[16];
    TF_LANGBARITEMINFO infos[16];
    DWORD statuses[16];
    ULONG cFetched = 0, i;
    HRESULT hr;

    hr = pMgr->GetItems(16, items, infos, statuses, &cFetched);
    ok_hr(hr, S_FALSE);
    ok_long(cFetched, _countof(s_Expected));
    for (i = 0; i < cFetched; i++)
    {
        IUnknown *pUnk;
        DWORD dwStatus;
        BOOL fSystem;

        if (i >= _countof(s_Expected))
        {
            items[i]->Release();
            continue;
        }
        fSystem = s_Expected[i].fSystem;
        winetest_push_context("item %lu", i);
        ok(IsEqualGUID(infos[i].guidItem, *s_Expected[i].pguidItem), "unexpected guidItem\n");
        ok(IsEqualGUID(infos[i].clsidService, *s_Expected[i].pguidService), "unexpected clsidService\n");
        ok_long(infos[i].dwStyle, s_Expected[i].dwStyle);
        ok_long((LONG)infos[i].ulSort, s_Expected[i].lSort);
        ok(infos[i].szDescription[0] != UNICODE_NULL, "empty description\n");
        ok_long(statuses[i], 0);

        hr = items[i]->QueryInterface(IID_ITfSystemLangBarItem, (void **)&pUnk);
        ok_hr(hr, fSystem ? S_OK : E_NOINTERFACE);
        if (SUCCEEDED(hr))
            pUnk->Release();
        hr = items[i]->QueryInterface(IID_ITfSystemDeviceTypeLangBarItem, (void **)&pUnk);
        ok_hr(hr, fSystem ? S_OK : E_NOINTERFACE);
        if (SUCCEEDED(hr))
            pUnk->Release();
        hr = items[i]->QueryInterface(IID_ITfSystemLangBarItemText, (void **)&pUnk);
        ok_hr(hr, fSystem ? S_OK : E_NOINTERFACE);
        if (SUCCEEDED(hr))
            pUnk->Release();

        CheckTexts(items[i], L"", L"", __LINE__);
        TestSystemItemButton(items[i], fSystem);
        TestSystemItemSinks(items[i], fSystem);
        if (fSystem)
            TestSystemItemSetters(items[i]);

        ok_hr(items[i]->Show(FALSE), S_OK);
        dwStatus = 0xdead;
        ok_hr(items[i]->GetStatus(&dwStatus), S_OK);
        ok_long(dwStatus, TF_LBI_STATUS_HIDDEN);
        ok_hr(items[i]->Show(TRUE), S_OK);
        dwStatus = 0xdead;
        ok_hr(items[i]->GetStatus(&dwStatus), S_OK);
        ok_long(dwStatus, 0);
        winetest_pop_context();
        items[i]->Release();
    }
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
    TestSystemItems(pMgr1);
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
