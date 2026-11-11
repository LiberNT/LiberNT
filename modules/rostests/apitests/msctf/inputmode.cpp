/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the input mode lang bar item of a text service
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <ctffunc.h>
#include <olectl.h>
#include <ime/indicml.h>
#include <inputindicator.h>

static const GUID GUID_TEST_SERVICE = {0x6b3a3f82, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_TEST_OTHER_ITEM = {0x6b3a3f83, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

typedef HRESULT (WINAPI *FN_TF_CreateLangBarItemMgr)(ITfLangBarItemMgr **);
typedef BOOL (WINAPI *FN_ImmDisableIME)(DWORD);

class CModeButton : public ITfLangBarItemButton, public ITfSource
{
public:
    CModeButton() : m_cRefs(1), m_pSink(NULL), m_cAdvise(0), m_cUnadvise(0), m_cInfo(0), m_cStatus(0), m_cIcon(0),
                    m_cTooltip(0), m_cText(0), m_cClick(0), m_dwOtherThread(0), m_hIconLast(NULL),
                    m_clickLast(TF_LBI_CLK_LEFT), m_ptClickLast(), m_rcClickLast(), m_guidItem(GUID_LBI_INPUTMODE), m_fNoButton(FALSE)
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
            (IsEqualIID(riid, IID_ITfLangBarItemButton) && !m_fNoButton))
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
        pInfo->guidItem = m_guidItem;
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
        m_clickLast = click;
        m_ptClickLast = pt;
        if (prc)
            m_rcClickLast = *prc;
        else
            SetRect(&m_rcClickLast, -1, -1, -1, -1);
        return S_OK;
    }
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override { return E_NOTIMPL; }
    STDMETHODIMP OnMenuSelect(UINT wID) override { return E_NOTIMPL; }
    STDMETHODIMP GetIcon(HICON *phIcon) override
    {
        Count(&m_cIcon);
        *phIcon = CopyIcon(LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION));
        m_hIconLast = *phIcon;
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
    HICON m_hIconLast;
    TfLBIClick m_clickLast;
    POINT m_ptClickLast;
    RECT m_rcClickLast;
    GUID m_guidItem;
    BOOL m_fNoButton;
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

static DWORD_PTR
SendTipState(HWND hwndIndicator, HWND hwndSender, ULONG_PTR dwData, DWORD cbData, PVOID pData, int line)
{
    COPYDATASTRUCT cds = { dwData, cbData, pData };
    DWORD_PTR dwResult = 0xdead;
    ok_(__FILE__, line)(SendMessageTimeoutW(hwndIndicator, WM_COPYDATA, (WPARAM)hwndSender, (LPARAM)&cds,
                                            SMTO_ABORTIFHUNG, 5000, &dwResult),
                        "SendMessageTimeout failed with %lu\n", GetLastError());
    return dwResult;
}

static void
CheckClickRect(CModeButton *pButton, LONG left, LONG top, LONG right, LONG bottom, int line)
{
    const RECT *prc = &pButton->m_rcClickLast;
    ok_(__FILE__, line)(prc->left == left && prc->top == top && prc->right == right && prc->bottom == bottom,
                        "rect (%ld, %ld)-(%ld, %ld)\n", prc->left, prc->top, prc->right, prc->bottom);
}

static void
TestDeferredClick(CModeButton *pButton, HWND hwndTip, HWND hwndFocus)
{
    LONG cClick = pButton->m_cClick;
    SetFocus(NULL);
    ok(GetFocus() == NULL, "the thread still has the focus\n");
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_LEFT, MAKELPARAM(10, 20)),
       "PostMessage failed\n");
    PumpFor(100);
    ok_long(pButton->m_cClick, cClick);
    SetFocus(hwndFocus);
    ok(GetFocus() == hwndFocus, "the focus did not come back\n");
    PumpFor(200);
    ok_long(pButton->m_cClick, cClick + 1);
    ok(pButton->m_clickLast == TF_LBI_CLK_LEFT, "click %d\n", pButton->m_clickLast);
    ok(pButton->m_ptClickLast.x == 10 && pButton->m_ptClickLast.y == 20, "point (%ld, %ld)\n",
       pButton->m_ptClickLast.x, pButton->m_ptClickLast.y);

    SetFocus(NULL);
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICKRECT, MAKEWPARAM(100, 200), MAKELPARAM(132, 248)),
       "PostMessage failed\n");
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_RIGHT, MAKELPARAM(30, 40)),
       "PostMessage failed\n");
    PumpFor(1000);
    ok_long(pButton->m_cClick, cClick + 2);
    ok(pButton->m_clickLast == TF_LBI_CLK_RIGHT, "click %d\n", pButton->m_clickLast);
    CheckClickRect(pButton, 100, 200, 132, 248, __LINE__);
    SetFocus(hwndFocus);
    PumpFor(100);
    ok_long(pButton->m_cClick, cClick + 2);
}

static void
TestPrivateChannel(CModeButton *pButton, HWND hwndFocus)
{
    HWND hwndTip = FindTipWindow();
    ok(hwndTip != NULL, "no input mode window in this thread\n");
    if (!hwndTip)
        return;

    LONG cClick = pButton->m_cClick;
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_LEFT, 0), "PostMessage failed\n");
    PumpFor(1000);
    ok_long(pButton->m_cClick, cClick + 1);
    ok(pButton->m_clickLast == TF_LBI_CLK_LEFT, "click %d\n", pButton->m_clickLast);
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_RIGHT + 1, 0), "PostMessage failed\n");
    PumpFor(1000);
    ok_long(pButton->m_cClick, cClick + 1);
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_RIGHT, MAKELPARAM(-5, -7)),
       "PostMessage failed\n");
    PumpFor(500);
    ok_long(pButton->m_cClick, cClick + 2);
    ok(pButton->m_clickLast == TF_LBI_CLK_RIGHT, "click %d\n", pButton->m_clickLast);
    ok(pButton->m_ptClickLast.x == -5 && pButton->m_ptClickLast.y == -7, "point (%ld, %ld)\n",
       pButton->m_ptClickLast.x, pButton->m_ptClickLast.y);
    CheckClickRect(pButton, -5, -7, -4, -6, __LINE__);

    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICKRECT, MAKEWPARAM(-30, 720), MAKELPARAM(-2, 768)),
       "PostMessage failed\n");
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_LEFT, MAKELPARAM(-10, 744)),
       "PostMessage failed\n");
    PumpFor(500);
    ok_long(pButton->m_cClick, cClick + 3);
    ok(pButton->m_ptClickLast.x == -10 && pButton->m_ptClickLast.y == 744, "point (%ld, %ld)\n",
       pButton->m_ptClickLast.x, pButton->m_ptClickLast.y);
    CheckClickRect(pButton, -30, 720, -2, 768, __LINE__);
    ok(PostMessageW(hwndTip, WM_INPUTINDICATOR_CLICK, INPUTINDICATOR_CLICK_LEFT, MAKELPARAM(20, 30)),
       "PostMessage failed\n");
    PumpFor(500);
    ok_long(pButton->m_cClick, cClick + 4);
    CheckClickRect(pButton, 20, 30, 21, 31, __LINE__);
    TestDeferredClick(pButton, hwndTip, hwndFocus);

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

    ok(SendTipState(hwndIndicator, hwndTip, INPUTINDICATOR_COPYDATA_ID + 1, sizeof(state), &state, __LINE__) ==
       FALSE, "data with another ID was accepted\n");
    INPUTINDICATOR_TIPSTATE stateSmall = state;
    stateSmall.cbSize = sizeof(stateSmall) - 1;
    ok(SendTipState(hwndIndicator, hwndTip, INPUTINDICATOR_COPYDATA_ID, sizeof(stateSmall), &stateSmall,
                    __LINE__) == FALSE, "a state with a short size was accepted\n");
    ok(SendTipState(hwndIndicator, hwndFocus, INPUTINDICATOR_COPYDATA_ID, sizeof(state), &state, __LINE__) ==
       FALSE, "a state sent for another window was accepted\n");
    ok(SendTipState(hwndIndicator, hwndTip, INPUTINDICATOR_COPYDATA_ID, sizeof(state), &state, __LINE__) == TRUE,
       "a valid state was rejected\n");
    ok(IsWindow(hwndIndicator), "the input indicator is gone\n");
    ok_hr(pButton->m_pSink ? pButton->m_pSink->OnUpdate(TF_LBI_ICON) : E_UNEXPECTED, S_OK);
}

static ITfLangBarItemMgr *g_pRemoveMgr;
static ITfLangBarItem *g_pRemoveItem;

static DWORD WINAPI
RemoveItemThread(void *pv)
{
    HRESULT *phr = (HRESULT *)pv;
    *phr = g_pRemoveMgr->RemoveItem(g_pRemoveItem);
    return 0;
}

static void
TestRemoveFromOtherThread(ITfLangBarItemMgr *pMgr)
{
    CModeButton *pButton = new CModeButton();
    ok_hr(pMgr->AddItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
    HWND hwndTip = FindTipWindow();
    ok(hwndTip != NULL, "no input mode window in this thread\n");

    HRESULT hr = E_FAIL;
    g_pRemoveMgr = pMgr;
    g_pRemoveItem = static_cast<ITfLangBarItem *>(pButton);
    HANDLE hThread = CreateThread(NULL, 0, RemoveItemThread, &hr, 0, NULL);
    ok(WaitForSingleObject(hThread, 5000) == WAIT_OBJECT_0, "RemoveItem did not return\n");
    CloseHandle(hThread);
    ok_hr(hr, S_OK);
    ok_long(pButton->m_cUnadvise, 1);
    ok(pButton->m_pSink == NULL, "the item sink is still advised\n");
    ok(hwndTip && IsWindow(hwndTip), "the input mode window was destroyed off its thread\n");
    PumpFor(200);
    ok(!IsWindow(hwndTip), "the input mode window is still there\n");
    ok(FindTipWindow() == NULL, "an input mode window is left in this thread\n");
    pButton->Release();
}

static void
TestNoIndicator(ITfLangBarItemMgr *pMgr, BOOL fNoButton, const GUID *pguidItem)
{
    CModeButton *pButton = new CModeButton();
    pButton->m_fNoButton = fNoButton;
    pButton->m_guidItem = *pguidItem;
    ok_hr(pMgr->AddItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
    ok(FindTipWindow() == NULL, "an input mode window was created\n");
    ok_long(pButton->m_cAdvise, 0);
    ok_hr(pMgr->RemoveItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
    ok_long(pButton->m_cUnadvise, 0);
    pButton->Release();
}

static HWND g_hwndRemover, g_hwndFakeIndicator;
static BOOL g_fRemoveArmed;
static HANDLE g_hFakeReady;

static LRESULT CALLBACK
RemoverWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_APP)
        return g_pRemoveMgr->RemoveItem(g_pRemoveItem);
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK
FakeIndicatorWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg != WM_COPYDATA)
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    if (g_fRemoveArmed)
    {
        g_fRemoveArmed = FALSE;
        SendMessageW(g_hwndRemover, WM_APP, 0, 0);
    }
    return TRUE;
}

static DWORD WINAPI
FakeIndicatorThread(void *pv)
{
    MSG msg;
    g_hwndFakeIndicator = CreateWindowW(INDICATOR_CLASSW, NULL, 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL),
                                        NULL);
    SetEvent(g_hFakeReady);
    while (g_hwndFakeIndicator && GetMessageW(&msg, NULL, 0, 0) > 0)
        DispatchMessageW(&msg);
    if (g_hwndFakeIndicator)
        DestroyWindow(g_hwndFakeIndicator);
    return 0;
}

static void
TestRemoveDuringAdd(CModeButton *pButton, ITfLangBarItemMgr *pMgr)
{
    g_pRemoveMgr = pMgr;
    g_pRemoveItem = static_cast<ITfLangBarItem *>(pButton);
    g_fRemoveArmed = TRUE;
    ok_hr(pMgr->AddItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
    ok(!g_fRemoveArmed, "the item was not removed while it was added\n");
    ok(FindTipWindow() == NULL, "an input mode window is left for a removed item\n");
    ok(pButton->m_pSink == NULL, "the sink of a removed item is still advised\n");
    ok_long(pButton->m_cUnadvise, pButton->m_cAdvise);
    ITfLangBarItem *pItem = (ITfLangBarItem *)(ULONG_PTR)1;
    ok_hr(pMgr->GetItem(GUID_LBI_INPUTMODE, &pItem), S_FALSE);
    if (pItem && pItem != (ITfLangBarItem *)(ULONG_PTR)1)
        pItem->Release();
}

static void
TestRemoveDuringPublish(ITfLangBarItemMgr *pMgr, BOOL fDuringAdd)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = RemoverWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfInputModeRemover";
    RegisterClassW(&wc);
    wc.lpfnWndProc = FakeIndicatorWndProc;
    wc.lpszClassName = INDICATOR_CLASSW;
    RegisterClassW(&wc);
    g_hwndRemover = CreateWindowW(L"MsctfInputModeRemover", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                  GetModuleHandleW(NULL), NULL);
    ok(g_hwndRemover != NULL, "CreateWindow failed with %lu\n", GetLastError());

    DWORD dwFakeThread;
    g_hFakeReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE hThread = CreateThread(NULL, 0, FakeIndicatorThread, NULL, 0, &dwFakeThread);
    ok(WaitForSingleObject(g_hFakeReady, 5000) == WAIT_OBJECT_0, "the indicator thread did not start\n");
    ok(g_hwndFakeIndicator && FindWindowW(INDICATOR_CLASSW, NULL) == g_hwndFakeIndicator,
       "the test indicator %p is not the one msctf finds\n", g_hwndFakeIndicator);

    CModeButton *pButton = new CModeButton();
    if (fDuringAdd && g_hwndRemover && g_hwndFakeIndicator)
    {
        TestRemoveDuringAdd(pButton, pMgr);
    }
    else if (!fDuringAdd)
    {
        ok_hr(pMgr->AddItem(static_cast<ITfLangBarItem *>(pButton)), S_OK);
        ok(pButton->m_pSink != NULL, "no item sink\n");
    }
    if (!fDuringAdd && g_hwndRemover && g_hwndFakeIndicator && pButton->m_pSink)
    {
        g_pRemoveMgr = pMgr;
        g_pRemoveItem = static_cast<ITfLangBarItem *>(pButton);
        g_fRemoveArmed = TRUE;
        ok_hr(pButton->m_pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TOOLTIP), S_OK);
        ok(!g_fRemoveArmed, "the item was not removed during the update\n");
        ok(pButton->m_pSink == NULL, "the item sink is still advised\n");
        ok(pButton->m_hIconLast != NULL, "no icon was read during the update\n");
        HICON hIconCopy = pButton->m_hIconLast ? CopyIcon(pButton->m_hIconLast) : NULL;
        ok(hIconCopy == NULL, "the icon read during the update was not destroyed\n");
        if (hIconCopy)
            DestroyIcon(hIconCopy);
    }
    g_fRemoveArmed = FALSE;
    pMgr->RemoveItem(static_cast<ITfLangBarItem *>(pButton));
    pButton->Release();

    PostThreadMessageW(dwFakeThread, WM_QUIT, 0, 0);
    ok(WaitForSingleObject(hThread, 5000) == WAIT_OBJECT_0, "the indicator thread did not exit\n");
    CloseHandle(hThread);
    CloseHandle(g_hFakeReady);
    DestroyWindow(g_hwndRemover);
    UnregisterClassW(INDICATOR_CLASSW, GetModuleHandleW(NULL));
    UnregisterClassW(L"MsctfInputModeRemover", GetModuleHandleW(NULL));
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
            TestPrivateChannel(pButton, hwnd);

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
        if (is_reactos())
        {
            TestRemoveFromOtherThread(pMgr);
            TestNoIndicator(pMgr, TRUE, &GUID_LBI_INPUTMODE);
            TestNoIndicator(pMgr, FALSE, &GUID_TEST_OTHER_ITEM);
            TestRemoveDuringPublish(pMgr, TRUE);
            TestRemoveDuringPublish(pMgr, FALSE);
        }
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
