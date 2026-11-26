/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfThreadFocusSink when the keyboard focus enters and leaves the thread
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <olectl.h>

class CFocusSink : public ITfThreadFocusSink
{
public:
    CFocusSink() : m_cRefs(1) { }
    virtual ~CFocusSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadFocusSink))
            *ppv = static_cast<ITfThreadFocusSink *>(this);
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
    STDMETHODIMP OnSetThreadFocus() override
    {
        Log_Add("set");
        return S_OK;
    }
    STDMETHODIMP OnKillThreadFocus() override
    {
        Log_Add("kill");
        return S_OK;
    }

private:
    LONG m_cRefs;
};

struct OTHER_THREAD
{
    HANDLE hReady;
    HWND hwnd;
    DWORD dwThreadId;
};

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

static HWND CreateTestWindow(LPCWSTR pszTitle, int x)
{
    return CreateWindowW(L"MsctfThreadFocusSinkTest", pszTitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 0, 200, 100, NULL,
                         NULL, GetModuleHandleW(NULL), NULL);
}

static DWORD WINAPI OtherThreadProc(LPVOID pParam)
{
    OTHER_THREAD *pOther = (OTHER_THREAD *)pParam;
    MSG msg;

    pOther->hwnd = CreateTestWindow(L"other", 400);
    SetEvent(pOther->hReady);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(pOther->hwnd);
    return 0;
}

static void ForegroundAndWait(HWND hwnd)
{
    for (int i = 0; i < 50 && GetForegroundWindow() != hwnd; i++)
    {
        SetForegroundWindow(hwnd);
        PumpMessages();
    }
    ok(GetForegroundWindow() == hwnd, "foreground window %p, expected %p\n", GetForegroundWindow(), hwnd);
}

#define check_log(expected) check_log_(__LINE__, expected)
static void check_log_(int line, const char *pszExpected)
{
    ok_(__FILE__, line)(lstrcmpA(Log_Get(), pszExpected) == 0, "log '%s', expected '%s'\n", Log_Get(), pszExpected);
    Log_Reset();
}

static void check_thread_focus_(int line, ITfThreadMgr *pThreadMgr, BOOL fExpected)
{
    BOOL fFocus = 2;
    HRESULT hr = pThreadMgr->IsThreadFocus(&fFocus);
    ok_(__FILE__, line)(hr == S_OK, "IsThreadFocus returned 0x%lx\n", hr);
    ok_(__FILE__, line)(!fFocus == !fExpected && fFocus != 2, "IsThreadFocus %d, expected %d\n", fFocus, fExpected);
}
#define check_thread_focus(mgr, expected) check_thread_focus_(__LINE__, mgr, expected)

static void TestThreadFocusSink(TSF_FIXTURE *pFixture)
{
    OTHER_THREAD other = { 0 };
    CFocusSink *pSink = new CFocusSink();
    ITfSource *pSource = NULL;
    DWORD dwCookie = TF_INVALID_COOKIE;
    HANDLE hThread;
    HWND hwnd, hwnd2;
    HRESULT hr;

    hr = pFixture->pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (!pSource)
    {
        pSink->Release();
        return;
    }
    Log_Reset();
    hr = pSource->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink *>(pSink), &dwCookie);
    ok_hr(hr, S_OK);
    PumpMessages();
    check_log("");

    hwnd = CreateTestWindow(L"main", 0);
    ok(hwnd != NULL, "CreateWindow failed\n");
    ForegroundAndWait(hwnd);
    SetFocus(hwnd);
    PumpMessages();
    check_log("set");
    check_thread_focus(pFixture->pThreadMgr, TRUE);

    other.hReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    hThread = CreateThread(NULL, 0, OtherThreadProc, &other, 0, &other.dwThreadId);
    ok(hThread != NULL, "CreateThread failed\n");
    WaitForSingleObject(other.hReady, 5000);
    ok(other.hwnd != NULL, "no window on the other thread\n");
    PumpMessages();
    check_log("kill");

    ForegroundAndWait(other.hwnd);
    PumpMessages();
    check_log("");
    check_thread_focus(pFixture->pThreadMgr, FALSE);

    ForegroundAndWait(hwnd);
    PumpMessages();
    check_log("set");
    check_thread_focus(pFixture->pThreadMgr, TRUE);

    SetFocus(NULL);
    PumpMessages();
    check_log("kill");
    check_thread_focus(pFixture->pThreadMgr, FALSE);

    SetFocus(hwnd);
    PumpMessages();
    check_log("set");
    check_thread_focus(pFixture->pThreadMgr, TRUE);

    hwnd2 = CreateTestWindow(L"main2", 200);
    ForegroundAndWait(hwnd2);
    SetFocus(hwnd2);
    PumpMessages();
    check_log("");
    check_thread_focus(pFixture->pThreadMgr, TRUE);

    ForegroundAndWait(other.hwnd);
    PumpMessages();
    check_log("kill");
    ForegroundAndWait(hwnd);
    PumpMessages();
    check_log("set");

    hr = pSource->UnadviseSink(dwCookie);
    ok_hr(hr, S_OK);
    ForegroundAndWait(other.hwnd);
    PumpMessages();
    check_log("");

    hr = pSource->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink *>(pSink), &dwCookie);
    ok_hr(hr, S_OK);
    PumpMessages();
    check_log("");
    ForegroundAndWait(hwnd);
    PumpMessages();
    check_log("set");
    hr = pSource->UnadviseSink(dwCookie);
    ok_hr(hr, S_OK);

    hr = pSource->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink *>(pSink), &dwCookie);
    ok_hr(hr, S_OK);
    ForegroundAndWait(other.hwnd);
    PumpMessages();
    check_log("kill");
    ForegroundAndWait(hwnd);
    PumpMessages();
    check_log("set");
    hr = pSource->UnadviseSink(dwCookie);
    ok_hr(hr, S_OK);

    PostThreadMessageW(other.dwThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    CloseHandle(other.hReady);
    DestroyWindow(hwnd2);
    DestroyWindow(hwnd);
    PumpMessages();
    pSource->Release();
    pSink->Release();
}

START_TEST(threadfocus)
{
    TSF_FIXTURE fixture;
    WNDCLASSW wc = { 0 };

    CoInitialize(NULL);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfThreadFocusSinkTest";
    RegisterClassW(&wc);
    if (Fixture_Init(&fixture, L"abc"))
        TestThreadFocusSink(&fixture);
    Fixture_Done(&fixture);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
}
