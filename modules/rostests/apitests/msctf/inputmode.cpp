/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the input mode lang bar item of a text service
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <olectl.h>
#include <ime/indicml.h>
#include <inputindicator.h>

static const GUID GUID_TEST_SERVICE = {0x6b3a3f82, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

typedef HRESULT (WINAPI *FN_TF_CreateLangBarItemMgr)(ITfLangBarItemMgr **);
typedef BOOL (WINAPI *FN_ImmDisableIME)(DWORD);

class CModeButton : public ITfLangBarItemButton, public ITfSource
{
public:
    CModeButton() : m_cRefs(1), m_pSink(NULL), m_cAdvise(0), m_cUnadvise(0), m_cInfo(0), m_cStatus(0), m_cIcon(0),
                    m_cTooltip(0), m_cText(0), m_cClick(0), m_dwOtherThread(0)
    {
    }
    virtual ~CModeButton()
    {
        if (m_pSink)
            m_pSink->Release();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
            IsEqualIID(riid, IID_ITfLangBarItemButton))
            *ppv = static_cast<ITfLangBarItemButton *>(this);
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
        Count(&m_cInfo);
        pInfo->clsidService = GUID_TEST_SERVICE;
        pInfo->guidItem = GUID_LBI_INPUTMODE;
        pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
        pInfo->ulSort = 0;
        StringCchCopyW(pInfo->szDescription, _countof(pInfo->szDescription), L"Test input mode");
        return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override
    {
        Count(&m_cStatus);
        *pdwStatus = 0;
        return S_OK;
    }
    STDMETHODIMP Show(BOOL fShow) override { return E_NOTIMPL; }
    STDMETHODIMP GetTooltipString(BSTR *pbstr) override
    {
        Count(&m_cTooltip);
        *pbstr = SysAllocString(L"Test input mode tooltip");
        return S_OK;
    }
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT *prc) override
    {
        Count(&m_cClick);
        return S_OK;
    }
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override { return E_NOTIMPL; }
    STDMETHODIMP OnMenuSelect(UINT wID) override { return E_NOTIMPL; }
    STDMETHODIMP GetIcon(HICON *phIcon) override
    {
        Count(&m_cIcon);
        *phIcon = CopyIcon(LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION));
        return S_OK;
    }
    STDMETHODIMP GetText(BSTR *pbstr) override
    {
        Count(&m_cText);
        *pbstr = SysAllocString(L"M");
        return S_OK;
    }
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD *pdwCookie) override
    {
        Count(&m_cAdvise);
        if (!IsEqualIID(riid, IID_ITfLangBarItemSink) || m_pSink)
            return CONNECT_E_CANNOTCONNECT;
        punk->QueryInterface(IID_ITfLangBarItemSink, (void **)&m_pSink);
        *pdwCookie = 1;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override
    {
        Count(&m_cUnadvise);
        if (dwCookie != 1 || !m_pSink)
            return CONNECT_E_NOCONNECTION;
        m_pSink->Release();
        m_pSink = NULL;
        return S_OK;
    }

    void Count(LONG *pc)
    {
        InterlockedIncrement(pc);
        if (GetCurrentThreadId() != g_dwMainThread)
            m_dwOtherThread = GetCurrentThreadId();
    }

    static DWORD g_dwMainThread;

    LONG m_cRefs;
    ITfLangBarItemSink *m_pSink;
    LONG m_cAdvise, m_cUnadvise, m_cInfo, m_cStatus, m_cIcon, m_cTooltip, m_cText, m_cClick;
    DWORD m_dwOtherThread;
};

DWORD CModeButton::g_dwMainThread;

static void PumpFor(DWORD dwMs)
{
    DWORD dwEnd = GetTickCount() + dwMs;
    MSG msg;
    while ((LONG)(dwEnd - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
}

static HWND
FindTipWindow(VOID)
{
    HWND hwndTip = NULL;
    while ((hwndTip = FindWindowExW(HWND_MESSAGE, hwndTip, INPUTINDICATOR_TIP_WNDCLASS, NULL)) != NULL)
    {
        if (GetWindowThreadProcessId(hwndTip, NULL) == GetCurrentThreadId())
            return hwndTip;
    }
    return NULL;
}

static void
TestPrivateChannel(CModeButton *pButton)
{
    HWND hwndTip = FindTipWindow();
    ok(hwndTip != NULL, "no input mode window in this thread\n");
    if (!hwndTip)
        return;

    LONG cClick = pButton->m_cClick;
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_LEFT, 0), "PostMessage failed\n");
    PumpFor(1000);
    ok_long(pButton->m_cClick, cClick + 1);
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_RIGHT + 1, 0), "PostMessage failed\n");
    PumpFor(1000);
    ok_long(pButton->m_cClick, cClick + 1);

    HWND hwndIndicator = FindWindowW(INDICATOR_CLASSW, NULL);
    if (!hwndIndicator)
    {
        skip("No input indicator\n");
        return;
    }
    INPUTINDICATOR_TIPSTATE state = { sizeof(state), GetCurrentThreadId(), (ULONG64)(LONG_PTR)hwndTip };
    COPYDATASTRUCT cdsNull = { INPUTINDICATOR_COPYDATA_ID, sizeof(state), NULL };
    COPYDATASTRUCT cdsShort = { INPUTINDICATOR_COPYDATA_ID, sizeof(state) - 1, &state };
    DWORD_PTR dwResult = 1;
    ok(SendMessageTimeoutW(hwndIndicator, WM_COPYDATA, (WPARAM)hwndTip, (LPARAM)&cdsNull, SMTO_ABORTIFHUNG, 5000,
                           &dwResult), "SendMessageTimeout failed with %lu\n", GetLastError());
    ok(dwResult == FALSE, "NULL data was accepted\n");
    dwResult = 1;
    ok(SendMessageTimeoutW(hwndIndicator, WM_COPYDATA, (WPARAM)hwndTip, (LPARAM)&cdsShort, SMTO_ABORTIFHUNG, 5000,
                           &dwResult), "SendMessageTimeout failed with %lu\n", GetLastError());
    ok(dwResult == FALSE, "short data was accepted\n");
    ok(IsWindow(hwndIndicator), "the input indicator is gone\n");
}

START_TEST(inputmode)
{
    FN_TF_CreateLangBarItemMgr fn;
    ITfLangBarItemMgr *pMgr = NULL;
    ITfThreadMgr *pThreadMgr = NULL;
    ITfDocumentMgr *pDocMgr = NULL;
    ITfLangBarItem *pItem = NULL;
    TF_LANGBARITEMINFO info;
    CModeButton *pButton;
    TfClientId tid;
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    HRESULT hr;
    FN_ImmDisableIME pImmDisableIME;

    CoInitialize(NULL);
    CModeButton::g_dwMainThread = GetCurrentThreadId();
    pImmDisableIME = (FN_ImmDisableIME)GetProcAddress(LoadLibraryW(L"imm32.dll"), "ImmDisableIME");
    ok(pImmDisableIME && pImmDisableIME(GetCurrentThreadId()), "ImmDisableIME failed\n");
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfInputModeTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, L"inputmode", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 200, NULL, NULL,
                         wc.hInstance, NULL);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    ok(GetForegroundWindow() == hwnd, "the test window is not in the foreground\n");

    CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    ok(pThreadMgr != NULL, "no thread manager\n");
    if (pThreadMgr)
    {
        ITfThreadMgrEx *pThreadMgrEx = NULL;
        ok_hr(pThreadMgr->QueryInterface(IID_ITfThreadMgrEx, (void **)&pThreadMgrEx), S_OK);
        if (pThreadMgrEx)
        {
            ok_hr(pThreadMgrEx->ActivateEx(&tid, TF_TMAE_NOACTIVATETIP), S_OK);
            pThreadMgrEx->Release();
        }
        if (SUCCEEDED(pThreadMgr->CreateDocumentMgr(&pDocMgr)))
            pThreadMgr->SetFocus(pDocMgr);
    }
    fn = (FN_TF_CreateLangBarItemMgr)GetProcAddress(GetModuleHandleW(L"msctf.dll"), "TF_CreateLangBarItemMgr");
    if (fn)
        fn(&pMgr);
    ok(pMgr != NULL, "no item manager\n");

    pButton = new CModeButton();
    if (pMgr)
    {
        BOOL fShell = FindWindowW(L"Shell_TrayWnd", NULL) && (!is_reactos() || FindWindowW(INDICATOR_CLASSW, NULL));
        LONG cIcon, cTooltip;

        ok_hr(pMgr->AddItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
        ok_long(pButton->m_cAdvise, 1);
        ok(pButton->m_pSink != NULL, "no item sink\n");
        PumpFor(2000);
        if (!fShell)
            skip("No taskbar shows the input mode\n");
        else
        {
            ok(pButton->m_cIcon >= 1, "GetIcon was called %ld times\n", pButton->m_cIcon);
            ok(pButton->m_cTooltip >= 1, "GetTooltipString was called %ld times\n", pButton->m_cTooltip);
        }
        ok_long(pButton->m_cClick, 0);
        cIcon = pButton->m_cIcon;
        cTooltip = pButton->m_cTooltip;
        if (pButton->m_pSink)
        {
            ok_hr(pButton->m_pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TOOLTIP), S_OK);
            PumpFor(1000);
            if (fShell)
            {
                ok(pButton->m_cIcon > cIcon, "GetIcon was not called after OnUpdate\n");
                ok(pButton->m_cTooltip > cTooltip, "GetTooltipString was not called after OnUpdate\n");
            }
        }
        ok_long(pButton->m_cAdvise, 1);

        if (is_reactos())
            TestPrivateChannel(pButton);

        hr = pMgr->GetItem(GUID_LBI_INPUTMODE, &pItem);
        ok_hr(hr, S_OK);
        if (pItem)
        {
            ITfLangBarItemButton *pAsButton = NULL;
            hr = pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pAsButton);
            ok_hr(hr, S_OK);
            if (pAsButton)
            {
                ZeroMemory(&info, sizeof(info));
                ok_hr(pAsButton->GetInfo(&info), S_OK);
                ok(IsEqualGUID(info.guidItem, GUID_LBI_INPUTMODE), "wrong item\n");
                pAsButton->Release();
            }
            pItem->Release();
        }

        ok_hr(pMgr->RemoveItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
        PumpFor(500);
        ok_long(pButton->m_cUnadvise, 1);
        ok(pButton->m_pSink == NULL, "the item sink is still advised\n");
        ok(pButton->m_dwOtherThread == 0, "the item was called from thread %lu\n", pButton->m_dwOtherThread);
        pMgr->Release();
    }
    pButton->Release();

    if (pThreadMgr)
    {
        pThreadMgr->SetFocus(NULL);
        if (pDocMgr)
            pDocMgr->Release();
        pThreadMgr->Deactivate();
        pThreadMgr->Release();
    }
    DestroyWindow(hwnd);
    CoUninitialize();
}
