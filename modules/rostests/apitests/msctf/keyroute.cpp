/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for keystrokes reaching a text service in a TSF-aware thread
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "testtip.h"

static LONG s_cWndKeyDown, s_cWndChar;

class CRealKeySink : public ITfKeyEventSink
{
public:
    CRealKeySink() : m_vkEat(0) { Reset(); }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfKeyEventSink))
        {
            *ppv = static_cast<ITfKeyEventSink *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP OnSetFocus(BOOL fForeground) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        return Count(&m_cTestKeyDown, pic, wParam, pfEaten);
    }
    STDMETHODIMP OnTestKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        return Count(&m_cTestKeyUp, pic, wParam, pfEaten);
    }
    STDMETHODIMP OnKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        return Count(&m_cKeyDown, pic, wParam, pfEaten);
    }
    STDMETHODIMP OnKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override
    {
        return Count(&m_cKeyUp, pic, wParam, pfEaten);
    }
    STDMETHODIMP OnPreservedKey(ITfContext *pic, REFGUID rguid, BOOL *pfEaten) override
    {
        *pfEaten = FALSE;
        return S_OK;
    }
    void Reset()
    {
        m_cTestKeyDown = m_cTestKeyUp = m_cKeyDown = m_cKeyUp = m_cNoContext = 0;
        s_cWndKeyDown = s_cWndChar = 0;
    }

    WPARAM m_vkEat;
    LONG m_cTestKeyDown, m_cTestKeyUp, m_cKeyDown, m_cKeyUp, m_cNoContext;

private:
    HRESULT Count(LONG *pc, ITfContext *pic, WPARAM wParam, BOOL *pfEaten)
    {
        ++*pc;
        if (!pic)
            ++m_cNoContext;
        *pfEaten = (wParam == m_vkEat);
        return S_OK;
    }
};

static LRESULT CALLBACK RealKeyWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN)
        ++s_cWndKeyDown;
    else if (uMsg == WM_CHAR)
        ++s_cWndChar;
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

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
        Sleep(10);
    }
}

static void SendRealKey(WORD vk)
{
    INPUT input[2];
    ZeroMemory(input, sizeof(input));
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = vk;
    input[0].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
    PumpFor(300);
}

static void CheckRealKey(CRealKeySink *pSink, WORD vk, BOOL fEaten)
{
    pSink->Reset();
    SendRealKey(vk);
    winetest_push_context("key %c", vk);
    ok_long(pSink->m_cTestKeyDown, 0);
    ok_long(pSink->m_cKeyDown, 1);
    ok_long(pSink->m_cTestKeyUp, 0);
    ok_long(pSink->m_cKeyUp, 1);
    ok_long(pSink->m_cNoContext, 0);
    ok_long(s_cWndKeyDown, fEaten ? 0 : 1);
    ok_long(s_cWndChar, fEaten ? 0 : 1);
    winetest_pop_context();
}

START_TEST(keyroute)
{
    WNDCLASSW wc = { 0 };
    ITfDocumentMgr *pPrev = NULL;
    ITfKeystrokeMgr *pKeyMgr = NULL;
    CRealKeySink sink;
    TSF_FIXTURE fixture;
    HWND hwnd;

    CoInitialize(NULL);
    wc.lpfnWndProc = RealKeyWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfKeyRouteTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, L"key route", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 100, NULL, NULL,
                         wc.hInstance, NULL);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    PumpFor(300);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        DestroyWindow(hwnd);
        CoUninitialize();
        return;
    }
    if (Fixture_Init(&fixture, L"abc"))
    {
        fixture.pThreadMgr->AssociateFocus(hwnd, fixture.pDocMgr, &pPrev);
        if (pPrev)
            pPrev->Release();
        fixture.pThreadMgr->SetFocus(fixture.pDocMgr);
        TestTip_Activate();
        for (int i = 0; i < 50 && !g_pTip->tid; i++)
            PumpFor(100);
        PumpFor(300);
        ok(GetFocus() == hwnd, "the test window has no focus\n");
        ok(g_pTip->tid != 0, "the test text service is not active\n");
        ok_hr(fixture.pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeyMgr), S_OK);
        if (pKeyMgr && g_pTip->tid)
        {
            ok_hr(pKeyMgr->AdviseKeyEventSink(g_pTip->tid, &sink, TRUE), S_OK);
            sink.m_vkEat = 'Q';
            CheckRealKey(&sink, 'A', FALSE);
            CheckRealKey(&sink, 'Q', TRUE);
            ok_hr(pKeyMgr->UnadviseKeyEventSink(g_pTip->tid), S_OK);
        }
        if (pKeyMgr)
            pKeyMgr->Release();
        fixture.pThreadMgr->AssociateFocus(hwnd, NULL, &pPrev);
        if (pPrev)
            pPrev->Release();
        TestTip_Deactivate();
    }
    Fixture_Done(&fixture);
    TestTip_Unregister();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
}
