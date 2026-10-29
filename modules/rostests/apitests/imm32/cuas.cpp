/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for text service compositions reaching IMM applications
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <ole2.h>
#include <msctf.h>
#include <imm.h>
#include <strsafe.h>
#include <apitest.h>
#include "../msctf/testtip.h"

static char s_szMessages[1024];
static WCHAR s_szComp[64], s_szResult[64];

static void AddMessage(const char *psz)
{
    if (s_szMessages[0])
        StringCchCatA(s_szMessages, _countof(s_szMessages), ",");
    StringCchCatA(s_szMessages, _countof(s_szMessages), psz);
}

static void ReadCompositionString(HWND hwnd, DWORD dwIndex, LPWSTR psz, SIZE_T cch)
{
    HIMC hIMC = ImmGetContext(hwnd);
    LONG cb;
    ZeroMemory(psz, cch * sizeof(WCHAR));
    if (!hIMC)
        return;
    cb = ImmGetCompositionStringW(hIMC, dwIndex, psz, (DWORD)((cch - 1) * sizeof(WCHAR)));
    if (cb < 0)
        psz[0] = UNICODE_NULL;
    ImmReleaseContext(hwnd, hIMC);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char sz[64];
    switch (uMsg)
    {
        case WM_IME_STARTCOMPOSITION:
            AddMessage("start");
            return 0;
        case WM_IME_ENDCOMPOSITION:
            AddMessage("end");
            return 0;
        case WM_IME_COMPOSITION:
            StringCchPrintfA(sz, _countof(sz), "comp(%s%s)", (lParam & GCS_COMPSTR) ? "C" : "",
                             (lParam & GCS_RESULTSTR) ? "R" : "");
            AddMessage(sz);
            if (lParam & GCS_COMPSTR)
                ReadCompositionString(hwnd, GCS_COMPSTR, s_szComp, _countof(s_szComp));
            if (lParam & GCS_RESULTSTR)
                ReadCompositionString(hwnd, GCS_RESULTSTR, s_szResult, _countof(s_szResult));
            return 0;
        case WM_IME_CHAR:
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

#define check_details(hwnd, attr, clause, cursor) check_details_(__LINE__, hwnd, attr, clause, cursor)
static void check_details_(int line, HWND hwnd, const char *pszAttr, const char *pszClause, LONG lCursorExpected)
{
    HIMC hIMC = ImmGetContext(hwnd);
    BYTE attr[64];
    DWORD clause[16];
    LONG cbAttr, cbClause, lCursor, i;
    char szAttr[256] = "", szClause[256] = "";

    cbAttr = ImmGetCompositionStringW(hIMC, GCS_COMPATTR, attr, sizeof(attr));
    cbClause = ImmGetCompositionStringW(hIMC, GCS_COMPCLAUSE, clause, sizeof(clause));
    lCursor = ImmGetCompositionStringW(hIMC, GCS_CURSORPOS, NULL, 0);
    for (i = 0; i < cbAttr && i < (LONG)sizeof(attr); i++)
    {
        char t[8];
        StringCchPrintfA(t, _countof(t), "%s%u", i ? "," : "", attr[i]);
        StringCchCatA(szAttr, _countof(szAttr), t);
    }
    for (i = 0; i < cbClause / (LONG)sizeof(DWORD) && i < (LONG)_countof(clause); i++)
    {
        char t[16];
        StringCchPrintfA(t, _countof(t), "%s%lu", i ? "," : "", clause[i]);
        StringCchCatA(szClause, _countof(szClause), t);
    }
    ok_(__FILE__, line)(!strcmp(szAttr, pszAttr), "GCS_COMPATTR is \"%s\", expected \"%s\"\n", szAttr, pszAttr);
    ok_(__FILE__, line)(!strcmp(szClause, pszClause), "GCS_COMPCLAUSE is \"%s\", expected \"%s\"\n", szClause, pszClause);
    ok_(__FILE__, line)(lCursor == lCursorExpected, "GCS_CURSORPOS is %ld, expected %ld\n", lCursor, lCursorExpected);
    ImmReleaseContext(hwnd, hIMC);
}

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

static void Reset(void)
{
    s_szMessages[0] = 0;
    s_szComp[0] = s_szResult[0] = UNICODE_NULL;
}

#define check_wstr(actual, expected) check_wstr_(__LINE__, actual, expected)
static void check_wstr_(int line, LPCWSTR pszActual, LPCWSTR pszExpected)
{
    char sz[256] = "";
    for (LPCWSTR p = pszActual; *p && strlen(sz) < 240; ++p)
        StringCchPrintfA(sz + strlen(sz), _countof(sz) - strlen(sz), "%04x ", *p);
    ok_(__FILE__, line)(!wcscmp(pszActual, pszExpected), "string is [%s]\n", sz);
}

#define check_messages(expected) check_messages_(__LINE__, expected)
static void check_messages_(int line, const char *pszExpected)
{
    ok_(__FILE__, line)(!strcmp(s_szMessages, pszExpected), "messages \"%s\", expected \"%s\"\n", s_szMessages, pszExpected);
}

START_TEST(cuas)
{
    WNDCLASSW wc = { 0 };
    ITfThreadMgr *pThreadMgr = NULL;
    TfClientId tid;
    HWND hwnd;
    HIMC hIMC;
    WCHAR sz[64];
    HRESULT hr;

    CoInitialize(NULL);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        TestTip_Unregister();
        CoUninitialize();
        return;
    }

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MsctfCuasTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"MsctfCuasTest", L"cuas", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 100, NULL, NULL,
                         wc.hInstance, NULL);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
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
    if (!g_pTip->tid || !g_pfnCompose)
    {
        skip("The test text service did not activate\n");
        goto done;
    }

    Reset();
    hr = g_pfnCompose(L"\x3042\x3044", TESTTIP_START);
    Pump();
    ok_hr(hr, S_OK);
    check_messages("start,comp(C)");
    check_wstr(s_szComp, L"\x3042\x3044");
    hIMC = ImmGetContext(hwnd);
    ok(hIMC != NULL, "no input context\n");
    ReadCompositionString(hwnd, GCS_COMPSTR, sz, _countof(sz));
    check_wstr(sz, L"\x3042\x3044");
    check_details(hwnd, "0,0", "0,2", 2);

    Reset();
    hr = g_pfnCompose(L"\x3042\x3044\x3046", TESTTIP_UPDATE);
    Pump();
    ok_hr(hr, S_OK);
    check_messages("comp(C)");
    check_wstr(s_szComp, L"\x3042\x3044\x3046");
    check_details(hwnd, "0,0,0", "0,3", 3);

    Reset();
    hr = g_pfnCompose(NULL, TESTTIP_END);
    Pump();
    ok_hr(hr, S_OK);
    check_messages("comp(R),end");
    check_wstr(s_szResult, L"\x3042\x3044\x3046");

    Reset();
    g_pfnCompose(L"\x304b\x304d", TESTTIP_START);
    Pump();
    Reset();
    g_pTip->cTerminated = 0;
    ok_int(ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_COMPLETE, 0), TRUE);
    Pump();
    check_messages("comp(R),end");
    check_wstr(s_szResult, L"\x304b\x304d");
    ok_long(g_pTip->cTerminated, 1);

    Reset();
    g_pfnCompose(L"\x3055\x3057", TESTTIP_START);
    Pump();
    Reset();
    g_pTip->cTerminated = 0;
    ok_int(ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CANCEL, 0), TRUE);
    Pump();
    check_messages("end");
    check_wstr(s_szResult, L"");
    ReadCompositionString(hwnd, GCS_COMPSTR, sz, _countof(sz));
    check_wstr(sz, L"");
    ok_long(g_pTip->cTerminated, 1);

    Reset();
    ok_int(ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_COMPLETE, 0), TRUE);
    Pump();
    check_messages("");

    Reset();
    g_pfnCompose(L"\x305f\x3061", TESTTIP_START);
    Pump();
    Reset();
    g_pTip->cTerminated = 0;
    g_pTip->fCancelInSession = 2;
    hr = g_pfnCompose(NULL, TESTTIP_CANCEL);
    Pump();
    ok_hr(hr, S_OK);
    ok_int(g_pTip->fCancelInSession, TRUE);
    ok_long(g_pTip->cTerminated, 0);
    check_messages("");
    Reset();
    hr = g_pfnCompose(NULL, TESTTIP_END);
    Pump();
    ok_hr(hr, S_OK);
    check_messages("comp(R),end");
    check_wstr(s_szResult, L"\x305f\x3061");
    ImmReleaseContext(hwnd, hIMC);

    ok(GetKeyboardLayout(0) == (HKL)(ULONG_PTR)0x04110411, "layout %p\n", GetKeyboardLayout(0));
    ok_long(ImmGetProperty(GetKeyboardLayout(0), IGP_PROPERTY), 0x001D000A);

done:
    DestroyWindow(hwnd);
    TestTip_Deactivate();
    if (pThreadMgr)
    {
        pThreadMgr->Deactivate();
        pThreadMgr->Release();
    }
    TestTip_Unregister();
    CoUninitialize();
}
