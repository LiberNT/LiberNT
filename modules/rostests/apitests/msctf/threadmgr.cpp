/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for preserved keys, UI elements and function providers
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <olectl.h>
#include <cguid.h>
#include "testtip.h"

static const GUID GUID_KEY1 = {0x6b3a3f30, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_KEY2 = {0x6b3a3f31, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_KEY3 = {0x6b3a3f32, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_PROVIDER = {0x6b3a3f33, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_ELEMENT = {0x6b3a3f34, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

class CTestKeySink : public ITfKeyEventSink
{
public:
    CTestKeySink() : m_cRefs(1), m_fEat(TRUE) { }
    virtual ~CTestKeySink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfKeyEventSink))
        {
            *ppv = static_cast<ITfKeyEventSink *>(this);
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
    STDMETHODIMP OnSetFocus(BOOL fForeground) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        Log_Add("testdown");
        *pfEaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        Log_Add("testup");
        *pfEaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        Log_Add("down");
        *pfEaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        Log_Add("up");
        *pfEaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext *pic, REFGUID rguid, BOOL *pfEaten) override
    {
        if (IsEqualGUID(rguid, GUID_KEY1))
            Log_Add("preserved1");
        else if (IsEqualGUID(rguid, GUID_KEY2))
            Log_Add("preserved2");
        else if (IsEqualGUID(rguid, GUID_KEY3))
            Log_Add("preserved3");
        else if (rguid.Data1 >= 0x6b3a3f60 && rguid.Data1 < 0x6b3a3f70)
            Log_Add("preservedM");
        else
            Log_Add("preserved?");
        *pfEaten = m_fEat;
        return S_OK;
    }

    LONG m_cRefs;
    BOOL m_fEat;
};

static BYTE s_bDown[256];

static void PressKey(WORD vk, BOOL fDown)
{
    INPUT input;
    if (!!s_bDown[vk] == !!fDown)
        return;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = fDown ? 0 : KEYEVENTF_KEYUP;
    if (vk == VK_RCONTROL)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &input, sizeof(input));
    s_bDown[vk] = (BYTE)fDown;
}

static void PumpMessages(void)
{
    MSG msg;
    for (int i = 0; i < 5; i++)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(30);
    }
}

static void PressModifier(WORD vkDown)
{
    static const WORD s_vk[] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT };
    for (UINT i = 0; i < _countof(s_vk); i++)
    {
        if (s_vk[i] != vkDown)
            PressKey(s_vk[i], FALSE);
    }
    if (vkDown)
        PressKey(vkDown, TRUE);
    PumpMessages();
}

static BOOL ModifierMatches(UINT uModifiers, WORD vkDown)
{
    UINT uDown = 0;
    switch (vkDown)
    {
        case VK_LCONTROL: uDown = TF_MOD_CONTROL | TF_MOD_LCONTROL; break;
        case VK_RCONTROL: uDown = TF_MOD_CONTROL | TF_MOD_RCONTROL; break;
        case VK_LSHIFT: uDown = TF_MOD_SHIFT | TF_MOD_LSHIFT; break;
        case VK_RSHIFT: uDown = TF_MOD_SHIFT | TF_MOD_RSHIFT; break;
    }
    return (uModifiers & uDown) == uModifiers;
}

static void TestModifiers(ITfKeystrokeMgr *pMgr, TfClientId tid)
{
    static const struct
    {
        UINT uVKey;
        UINT uModifiers;
    } s_keys[] =
    {
        { VK_HOME, TF_MOD_RCONTROL },
        { VK_F11, TF_MOD_LCONTROL },
        { VK_F6, TF_MOD_SHIFT },
        { VK_F2, TF_MOD_LSHIFT },
        { VK_F5, TF_MOD_RSHIFT },
    };
    static const WORD s_down[] = { 0, VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT };
    UINT i, j;

    for (i = 0; i < _countof(s_keys); i++)
    {
        TF_PRESERVEDKEY key = { s_keys[i].uVKey, s_keys[i].uModifiers };
        GUID guid = GUID_KEY1;
        guid.Data1 = 0x6b3a3f60 + i;
        ok_hr(pMgr->PreserveKey(tid, guid, &key, L"modifier", 8), S_OK);
        for (j = 0; j < _countof(s_down); j++)
        {
            BOOL fMatch = ModifierMatches(s_keys[i].uModifiers, s_down[j]);
            BOOL fEaten = 2;
            LPARAM lParam = 1;
            PressModifier(s_down[j]);
            Log_Reset();
            ok_hr(pMgr->KeyDown(s_keys[i].uVKey, lParam, &fEaten), S_OK);
            ok(fEaten == fMatch, "key 0x%x modifiers 0x%x with 0x%x down: eaten %d\n", s_keys[i].uVKey,
               s_keys[i].uModifiers, s_down[j], fEaten);
            ok(!strcmp(Log_Get(), fMatch ? "preservedM" : "down"), "key 0x%x modifiers 0x%x with 0x%x down: log \"%s\"\n",
               s_keys[i].uVKey, s_keys[i].uModifiers, s_down[j], Log_Get());
        }
        PressModifier(0);
        ok_hr(pMgr->UnpreserveKey(guid, &key), S_OK);
    }
}

static void SetModifiers(BOOL fLControl, BOOL fRControl, BOOL fShift)
{
    PressKey(VK_LCONTROL, fLControl);
    PressKey(VK_RCONTROL, fRControl);
    PressKey(VK_LSHIFT, fShift);
    PumpMessages();
}

#define check_key(m, down, vk, eaten, log) check_key_(__LINE__, m, down, vk, eaten, log)
static void check_key_(int line, ITfKeystrokeMgr *pMgr, BOOL fDown, WPARAM vk, BOOL fExpected, const char *pszLog)
{
    BOOL fEaten = 2;
    LPARAM lParam = fDown ? 1 : (LPARAM)0xC0000001;
    HRESULT hr;

    Log_Reset();
    hr = fDown ? pMgr->KeyDown(vk, lParam, &fEaten) : pMgr->KeyUp(vk, lParam, &fEaten);
    ok_(__FILE__, line)(hr == S_OK, "hr 0x%lx\n", hr);
    ok_(__FILE__, line)(fEaten == fExpected, "eaten %d, expected %d\n", fEaten, fExpected);
    ok_(__FILE__, line)(!strcmp(Log_Get(), pszLog), "log \"%s\", expected \"%s\"\n", Log_Get(), pszLog);
}

static void TestPreservedKeys(TSF_FIXTURE *pFixture, TfClientId tid)
{
    ITfKeystrokeMgr *pMgr = NULL;
    CTestKeySink *pSink;
    TF_PRESERVEDKEY key1 = { VK_F7, TF_MOD_CONTROL };
    TF_PRESERVEDKEY key2 = { VK_F8, TF_MOD_LCONTROL | TF_MOD_ON_KEYUP };
    TF_PRESERVEDKEY key3 = { VK_F9, TF_MOD_IGNORE_ALL_MODIFIER };
    TF_PRESERVEDKEY probe;
    BOOL fRegistered, fEaten;
    GUID guid;
    HRESULT hr;

    hr = pFixture->pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pMgr);
    ok_hr(hr, S_OK);
    if (!pMgr)
        return;

    pSink = new CTestKeySink();
    hr = pMgr->AdviseKeyEventSink(tid, static_cast<ITfKeyEventSink *>(pSink), TRUE);
    ok_hr(hr, S_OK);

    ok_hr(pMgr->PreserveKey(tid, GUID_KEY1, &key1, L"one", 3), S_OK);
    ok_hr(pMgr->PreserveKey(tid, GUID_KEY1, &key1, L"one", 3), TF_E_ALREADY_EXISTS);
    ok_hr(pMgr->PreserveKey(tid, GUID_KEY2, &key2, L"two", 3), S_OK);
    ok_hr(pMgr->PreserveKey(tid, GUID_KEY3, &key3, L"three", 5), S_OK);

    fRegistered = 2;
    ok_hr(pMgr->IsPreservedKey(GUID_KEY1, &key1, &fRegistered), S_OK);
    ok_int(fRegistered, TRUE);
    fRegistered = 2;
    ok_hr(pMgr->IsPreservedKey(GUID_KEY2, &key1, &fRegistered), S_FALSE);
    ok_int(fRegistered, FALSE);
    guid = GUID_NULL;
    ok_hr(pMgr->GetPreservedKey(pFixture->pContext, &key2, &guid), S_OK);
    ok(IsEqualGUID(guid, GUID_KEY2), "wrong guid for key2\n");
    probe.uVKey = VK_F7;
    probe.uModifiers = TF_MOD_SHIFT;
    guid = GUID_KEY3;
    ok_hr(pMgr->GetPreservedKey(pFixture->pContext, &probe, &guid), S_FALSE);
    ok(IsEqualGUID(guid, GUID_NULL), "guid not cleared\n");

    Log_Reset();
    fEaten = 2;
    ok_hr(pMgr->SimulatePreservedKey(pFixture->pContext, GUID_KEY1, &fEaten), S_OK);
    ok_int(fEaten, TRUE);
    ok_str(Log_Get(), "preserved1");

    HWND hwnd = CreateWindowW(L"STATIC", L"msctf keys", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 100, NULL, NULL, NULL, NULL);
    ITfDocumentMgr *pPrev = NULL;
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    pFixture->pThreadMgr->AssociateFocus(hwnd, pFixture->pDocMgr, &pPrev);
    if (pPrev)
        pPrev->Release();
    pFixture->pThreadMgr->SetFocus(pFixture->pDocMgr);
    {
        MSG msg;
        for (int i = 0; i < 10; i++)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
            Sleep(50);
        }
    }

    SetModifiers(TRUE, FALSE, FALSE);
    check_key(pMgr, TRUE, VK_F7, TRUE, "preserved1");
    check_key(pMgr, FALSE, VK_F7, FALSE, "up");
    SetModifiers(FALSE, TRUE, FALSE);
    check_key(pMgr, TRUE, VK_F7, TRUE, "preserved1");
    SetModifiers(FALSE, FALSE, FALSE);
    check_key(pMgr, TRUE, VK_F7, FALSE, "down");

    SetModifiers(TRUE, FALSE, FALSE);
    check_key(pMgr, TRUE, VK_F8, FALSE, "down");
    check_key(pMgr, FALSE, VK_F8, TRUE, "preserved2");
    SetModifiers(FALSE, TRUE, FALSE);
    check_key(pMgr, FALSE, VK_F8, FALSE, "up");

    SetModifiers(TRUE, FALSE, FALSE);
    check_key(pMgr, TRUE, VK_F9, TRUE, "preserved3");
    SetModifiers(FALSE, FALSE, FALSE);
    check_key(pMgr, TRUE, VK_F9, TRUE, "preserved3");

    pSink->m_fEat = FALSE;
    check_key(pMgr, TRUE, VK_F9, TRUE, "preserved3");

    Log_Reset();
    fEaten = 2;
    hr = pMgr->TestKeyDown(VK_F9, 1, &fEaten);
    ok_hr(hr, S_OK);
    ok_int(fEaten, TRUE);
    ok_str(Log_Get(), "");

    ok_hr(pMgr->UnpreserveKey(GUID_KEY3, &key3), S_OK);
    ok_hr(pMgr->UnpreserveKey(GUID_KEY3, &key3), CONNECT_E_NOCONNECTION);
    check_key(pMgr, TRUE, VK_F9, FALSE, "down");

    pSink->m_fEat = TRUE;
    TestModifiers(pMgr, tid);

    SetModifiers(FALSE, FALSE, FALSE);
    pFixture->pThreadMgr->AssociateFocus(hwnd, NULL, &pPrev);
    if (pPrev)
        pPrev->Release();
    DestroyWindow(hwnd);
    pMgr->UnpreserveKey(GUID_KEY1, &key1);
    pMgr->UnpreserveKey(GUID_KEY2, &key2);
    ok_hr(pMgr->UnadviseKeyEventSink(tid), S_OK);
    pSink->Release();
    pMgr->Release();
}

class CTestElement : public ITfUIElement
{
public:
    CTestElement() : m_cRefs(1), m_fShown(TRUE) { }
    virtual ~CTestElement() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElement))
        {
            *ppv = static_cast<ITfUIElement *>(this);
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
    STDMETHODIMP GetDescription(BSTR *pbstrDescription) override
    {
        *pbstrDescription = SysAllocString(L"element");
        return S_OK;
    }
    STDMETHODIMP GetGUID(GUID *pguid) override
    {
        *pguid = GUID_ELEMENT;
        return S_OK;
    }
    STDMETHODIMP Show(BOOL bShow) override
    {
        Log_Add(bShow ? "show" : "hide");
        m_fShown = bShow;
        return S_OK;
    }
    STDMETHODIMP IsShown(BOOL *pbShow) override
    {
        *pbShow = m_fShown;
        return S_OK;
    }

    LONG m_cRefs;
    BOOL m_fShown;
};

class CTestElementSink : public ITfUIElementSink
{
public:
    CTestElementSink() : m_cRefs(1), m_fShow(FALSE) { }
    virtual ~CTestElementSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink))
        {
            *ppv = static_cast<ITfUIElementSink *>(this);
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
    STDMETHODIMP BeginUIElement(DWORD dwUIElementId, BOOL *pbShow) override
    {
        Log_Add("begin");
        *pbShow = m_fShow;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD dwUIElementId) override
    {
        Log_Add("update");
        return S_OK;
    }
    STDMETHODIMP EndUIElement(DWORD dwUIElementId) override
    {
        Log_Add("end");
        return S_OK;
    }

    LONG m_cRefs;
    BOOL m_fShow;
};

static void TestUIElements(TSF_FIXTURE *pFixture)
{
    ITfUIElementMgr *pMgr = NULL;
    ITfSource *pSource = NULL;
    ITfUIElement *pGot = NULL;
    CTestElement *pElement = new CTestElement();
    CTestElementSink *pSink = new CTestElementSink();
    DWORD dwCookie = 0, dwId = TF_INVALID_UIELEMENTID;
    BOOL fShow;
    HRESULT hr;

    hr = pFixture->pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pMgr);
    ok_hr(hr, S_OK);
    pFixture->pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (!pMgr || !pSource)
        goto done;

    Log_Reset();
    fShow = 2;
    hr = pMgr->BeginUIElement(pElement, &fShow, &dwId);
    ok_hr(hr, S_OK);
    ok_int(fShow, TRUE);
    ok(dwId != TF_INVALID_UIELEMENTID, "invalid id\n");
    ok_str(Log_Get(), "");
    pMgr->EndUIElement(dwId);

    hr = pSource->AdviseSink(IID_ITfUIElementSink, static_cast<ITfUIElementSink *>(pSink), &dwCookie);
    ok_hr(hr, S_OK);

    Log_Reset();
    fShow = 2;
    dwId = TF_INVALID_UIELEMENTID;
    hr = pMgr->BeginUIElement(pElement, &fShow, &dwId);
    ok_hr(hr, S_OK);
    ok_int(fShow, FALSE);
    ok(dwId != TF_INVALID_UIELEMENTID, "invalid id\n");

    hr = pMgr->GetUIElement(dwId, &pGot);
    ok_hr(hr, S_OK);
    ok(pGot == static_cast<ITfUIElement *>(pElement), "got %p\n", pGot);
    if (pGot)
        pGot->Release();

    hr = pMgr->UpdateUIElement(dwId);
    ok_hr(hr, S_OK);
    hr = pMgr->EndUIElement(dwId);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "begin,update,end");

    pGot = NULL;
    hr = pMgr->GetUIElement(dwId, &pGot);
    ok_hr(hr, E_INVALIDARG);
    ok(pGot == NULL, "got %p\n", pGot);
    hr = pMgr->EndUIElement(dwId);
    ok_hr(hr, E_INVALIDARG);

    pSink->m_fShow = TRUE;
    Log_Reset();
    fShow = 2;
    pMgr->BeginUIElement(pElement, &fShow, &dwId);
    ok_int(fShow, TRUE);
    pMgr->EndUIElement(dwId);
    ok_str(Log_Get(), "begin,end");

    pSource->UnadviseSink(dwCookie);

done:
    if (pSource)
        pSource->Release();
    if (pMgr)
        pMgr->Release();
    pSink->Release();
    pElement->Release();
}

class CTestProvider : public ITfFunctionProvider
{
public:
    CTestProvider(REFGUID guidType = GUID_PROVIDER) : m_cRefs(1), m_guidType(guidType) { }
    virtual ~CTestProvider() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfFunctionProvider))
        {
            *ppv = static_cast<ITfFunctionProvider *>(this);
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
    STDMETHODIMP GetType(GUID *pguid) override
    {
        *pguid = m_guidType;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR *pbstrDesc) override
    {
        *pbstrDesc = SysAllocString(L"provider");
        return S_OK;
    }
    STDMETHODIMP GetFunction(REFGUID rguid, REFIID riid, IUnknown **ppunk) override
    {
        *ppunk = NULL;
        return E_NOINTERFACE;
    }

    LONG m_cRefs;
    GUID m_guidType;
};

static void TestFunctionProviders(TSF_FIXTURE *pFixture, TfClientId tid, REFCLSID clsid)
{
    ITfSourceSingle *pSource = NULL;
    ITfFunctionProvider *pGot = NULL, *pExpected = NULL;
    CTestProvider *pOther = new CTestProvider();
    HRESULT hr;

    ok_hr(g_pTip->hrAdviseProvider, S_OK);
    ok(g_pTip->pTip != NULL, "the test text service is not active\n");
    if (!g_pTip->pTip)
    {
        pOther->Release();
        return;
    }
    g_pTip->pTip->QueryInterface(IID_ITfFunctionProvider, (void **)&pExpected);

    hr = pFixture->pThreadMgr->QueryInterface(IID_ITfSourceSingle, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (pSource)
    {
        hr = pSource->AdviseSingleSink(tid, IID_ITfFunctionProvider, static_cast<ITfFunctionProvider *>(pOther));
        ok_hr(hr, CONNECT_E_ADVISELIMIT);
        hr = pFixture->pThreadMgr->GetFunctionProvider(clsid, &pGot);
        ok_hr(hr, S_OK);
        ok(pGot == pExpected, "got %p, expected %p\n", pGot, pExpected);
        if (pGot)
            pGot->Release();
        pSource->Release();
    }
    if (pExpected)
        pExpected->Release();
    pOther->Release();
}

static void TestClientIdChecks(TSF_FIXTURE *pFixture)
{
    ITfKeystrokeMgr *pMgr = NULL;
    ITfSourceSingle *pSource = NULL;
    CTestKeySink *pSink = new CTestKeySink();
    CTestProvider *pProvider = new CTestProvider();
    TF_PRESERVEDKEY key = { VK_F7, TF_MOD_CONTROL };
    ITfFunctionProvider *pGot = NULL;
    HRESULT hr;

    pFixture->pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pMgr);
    pFixture->pThreadMgr->QueryInterface(IID_ITfSourceSingle, (void **)&pSource);
    if (pMgr)
    {
        hr = pMgr->AdviseKeyEventSink(pFixture->tid, static_cast<ITfKeyEventSink *>(pSink), TRUE);
        ok_hr(hr, E_INVALIDARG);
        hr = pMgr->PreserveKey(pFixture->tid, GUID_KEY1, &key, L"one", 3);
        ok_hr(hr, E_INVALIDARG);
        pMgr->Release();
    }
    if (pSource)
    {
        hr = pSource->AdviseSingleSink(pFixture->tid, IID_ITfFunctionProvider, static_cast<ITfFunctionProvider *>(pProvider));
        ok_hr(hr, S_OK);
        hr = pSource->AdviseSingleSink(pFixture->tid, IID_ITfFunctionProvider, static_cast<ITfFunctionProvider *>(pProvider));
        ok_hr(hr, CONNECT_E_ADVISELIMIT);
        hr = pFixture->pThreadMgr->GetFunctionProvider(GUID_PROVIDER, &pGot);
        ok_hr(hr, TF_E_NOPROVIDER);
        if (pGot)
            pGot->Release();
        pGot = NULL;
        hr = pFixture->pThreadMgr->GetFunctionProvider(GUID_NULL, &pGot);
        ok_hr(hr, TF_E_NOPROVIDER);
        if (pGot)
            pGot->Release();
        hr = pSource->UnadviseSingleSink(pFixture->tid, IID_ITfFunctionProvider);
        ok_hr(hr, S_OK);
        hr = pSource->UnadviseSingleSink(pFixture->tid, IID_ITfFunctionProvider);
        ok_hr(hr, CONNECT_E_NOCONNECTION);
        pSource->Release();
    }
    pProvider->Release();
    pSink->Release();
}

static void TestThreadFocus(TSF_FIXTURE *pFixture)
{
    WNDCLASSW wc = { 0 };
    BOOL fFocus = 2;
    HWND hwnd;
    HRESULT hr;

    hr = pFixture->pThreadMgr->IsThreadFocus(NULL);
    ok_hr(hr, E_INVALIDARG);

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfThreadFocusTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, L"focus", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL,
                         wc.hInstance, NULL);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    PumpMessages();
    hr = pFixture->pThreadMgr->IsThreadFocus(&fFocus);
    ok_hr(hr, S_OK);
    ok(fFocus != FALSE && fFocus != 2, "IsThreadFocus with a focused window returned %d\n", fFocus);

    DestroyWindow(hwnd);
    PumpMessages();
    fFocus = 2;
    hr = pFixture->pThreadMgr->IsThreadFocus(&fFocus);
    ok_hr(hr, S_OK);
    ok_int(fFocus, FALSE);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

START_TEST(threadmgr)
{
    TSF_FIXTURE fixture;
    BOOL fRegistered;

    CoInitialize(NULL);
    fRegistered = TestTip_Register();
    if (!fRegistered)
        skip("Cannot register the test text service; run as administrator\n");
    else
        TestTip_Activate();

    if (Fixture_Init(&fixture, L"abc"))
    {
        if (fRegistered && !g_pTip->cActivate)
        {
            fixture.pThreadMgr->SetFocus(fixture.pDocMgr);
            TestTip_Activate();
        }
        TestClientIdChecks(&fixture);
        TestUIElements(&fixture);
        if (fRegistered && g_pTip->tid)
        {
            ok_long(g_pTip->cActivate, 1);
            TestFunctionProviders(&fixture, g_pTip->tid, CLSID_TestTip);
            TestPreservedKeys(&fixture, g_pTip->tid);
            TestTip_Deactivate();
        }
        else
        {
            skip("The test text service did not activate\n");
        }
        TestThreadFocus(&fixture);
    }
    Fixture_Done(&fixture);
    {
        ITfThreadMgrEx *pEx = NULL;
        DWORD dwFlags = 0xdead;
        CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgrEx, (void **)&pEx);
        if (pEx)
        {
            pEx->GetActiveFlags(&dwFlags);
            pEx->Release();
        }
        ok(dwFlags == 0, "thread manager flags 0x%lx after the last deactivation\n", dwFlags);
    }
    if (fRegistered && g_pTip && g_pTip->cActivate)
    {
        ok(g_pTip->cDeactivate == g_pTip->cActivate, "activated %lu times, deactivated %lu times\n",
           g_pTip->cActivate, g_pTip->cDeactivate);
        ok_hr(g_pTip->hrUnadviseProvider, S_OK);
    }
    TestTip_Unregister();
    CoUninitialize();
}
