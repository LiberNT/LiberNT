/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests where a text service composition is shown in an edit control
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <ole2.h>
#include <msctf.h>
#include <imm.h>
#include <apitest.h>
#include "../msctf/testtip.h"

struct COMPWND_SEARCH
{
    HWND hwndExclude1;
    HWND hwndExclude2;
    POINT pt;
    HWND hwndFound;
};

static void Pump(void)
{
    MSG msg;
    for (int i = 0; i < 10; i++)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(30);
    }
}

static BOOL CALLBACK FindCompWnd(HWND hwnd, LPARAM lParam)
{
    COMPWND_SEARCH *pSearch = (COMPWND_SEARCH *)lParam;
    WCHAR szClass[64];
    RECT rc;

    if (hwnd == pSearch->hwndExclude1 || hwnd == pSearch->hwndExclude2 || !IsWindowVisible(hwnd))
        return TRUE;
    GetClassNameW(hwnd, szClass, _countof(szClass));
    GetWindowRect(hwnd, &rc);
    trace("window %p class %ls rect %ld,%ld-%ld,%ld style 0x%08lx\n", hwnd, szClass, rc.left, rc.top, rc.right,
          rc.bottom, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE));
    if (PtInRect(&rc, pSearch->pt))
        pSearch->hwndFound = hwnd;
    return TRUE;
}

static void TestComposition(HWND hwndParent, HWND hwndEdit)
{
    COMPWND_SEARCH search = { hwndParent, hwndEdit };
    WCHAR szText[16];
    POINT ptCaret;
    RECT rc;
    HRESULT hr;

    hr = g_pfnCompose(L"\x3042\x3044", TESTTIP_START);
    Pump();
    ok_hr(hr, S_OK);

    GetWindowTextW(hwndEdit, szText, _countof(szText));
    ok(!lstrcmpW(szText, L"ab"), "text during the composition is %ls\n", szText);

    GetCaretPos(&ptCaret);
    ClientToScreen(hwndEdit, &ptCaret);
    search.pt = ptCaret;
    EnumThreadWindows(GetCurrentThreadId(), FindCompWnd, (LPARAM)&search);
    ok(search.hwndFound != NULL, "no window shows the composition at the caret %ld,%ld\n", ptCaret.x, ptCaret.y);
    if (search.hwndFound)
    {
        GetWindowRect(search.hwndFound, &rc);
        ok_long(rc.left, ptCaret.x);
        ok_long(rc.top, ptCaret.y);
        ok(rc.bottom - rc.top >= 32, "the composition window is %ld high for a 32 pixel font\n", rc.bottom - rc.top);
        ok(!(GetWindowLongPtrW(search.hwndFound, GWL_STYLE) & WS_BORDER), "the composition window has a border\n");
    }

    hr = g_pfnCompose(NULL, TESTTIP_END);
    Pump();
    ok_hr(hr, S_OK);
    GetWindowTextW(hwndEdit, szText, _countof(szText));
    ok(!lstrcmpW(szText, L"ab\x3042\x3044"), "text after the composition is %ls\n", szText);
}

START_TEST(cuascompwnd)
{
    ITfThreadMgr *pThreadMgr = NULL;
    TfClientId tid;
    HWND hwndParent, hwndEdit;
    HFONT hFont;

    CoInitialize(NULL);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        TestTip_Unregister();
        CoUninitialize();
        return;
    }

    hwndParent = CreateWindowW(L"STATIC", L"cuascompwnd", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 560, 160, NULL,
                               NULL, GetModuleHandleW(NULL), NULL);
    hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"ab", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 20, 500,
                               50, hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    hFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetForegroundWindow(hwndParent);
    SetFocus(hwndEdit);
    SendMessageW(hwndEdit, EM_SETSEL, 2, 2);
    Pump();

    CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    if (pThreadMgr)
        pThreadMgr->Activate(&tid);
    TestTip_Activate();
    if (pThreadMgr)
    {
        ITfKeystrokeMgr *pKeystrokeMgr = NULL;
        BOOL fEaten;
        pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr);
        if (pKeystrokeMgr)
        {
            pKeystrokeMgr->TestKeyDown(VK_F24, 1, &fEaten);
            pKeystrokeMgr->Release();
        }
    }
    Pump();

    if (g_pTip->tid && g_pfnCompose)
        TestComposition(hwndParent, hwndEdit);
    else
        skip("The test text service did not activate\n");

    DestroyWindow(hwndParent);
    DeleteObject(hFont);
    TestTip_Deactivate();
    if (pThreadMgr)
    {
        pThreadMgr->Deactivate();
        pThreadMgr->Release();
    }
    TestTip_Unregister();
    CoUninitialize();
}
