/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a text service is activated on the thread manager of every thread
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "testtip.h"

struct THREAD_RESULT
{
    DWORD dwCoInit;
    BOOL fFixture;
    BOOL fOnThisThread;
    HRESULT hrApartment;
    APTTYPE AptType;
    APTTYPEQUALIFIER AptQualifier;
    ULONG cActivateAfterWindow;
    ULONG cActivateAfterFocus;
    ULONG cActivateAfterProfile;
    BOOL fOnThisManager;
    ULONG cDeactivateAfterDone;
};

#define NO_COINIT 0xffffffff

static ULONG g_cExpectedActivations;
static HANDLE g_hMtaStop;

static DWORD WINAPI MtaThread(LPVOID pv)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    SetEvent((HANDLE)pv);
    WaitForSingleObject(g_hMtaStop, INFINITE);
    CoUninitialize();
    return 0;
}

static void WaitForActivations(ULONG cActivate)
{
    for (int i = 0; i < 40 && g_pTip->cActivate < cActivate; i++)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(50);
    }
}

static DWORD WINAPI SecondThread(LPVOID pv)
{
    THREAD_RESULT *pResult = (THREAD_RESULT *)pv;
    TSF_FIXTURE fixture;
    HWND hwnd;

    if (pResult->dwCoInit != NO_COINIT)
        CoInitializeEx(NULL, pResult->dwCoInit);
    hwnd = CreateWindowExW(0, L"EDIT", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL,
                           GetModuleHandleW(NULL), NULL);
    if (hwnd)
    {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
    WaitForActivations(g_cExpectedActivations);
    pResult->cActivateAfterWindow = g_pTip->cActivate;
    pResult->fFixture = Fixture_Init(&fixture, L"abc");
    if (pResult->fFixture)
    {
        fixture.pThreadMgr->SetFocus(fixture.pDocMgr);
        WaitForActivations(g_cExpectedActivations);
        pResult->cActivateAfterFocus = g_pTip->cActivate;
        if (g_pTip->cActivate < g_cExpectedActivations)
            TestTip_Activate();
        WaitForActivations(g_cExpectedActivations);
        pResult->cActivateAfterProfile = g_pTip->cActivate;
        pResult->fOnThisManager = (g_pTip->pThreadMgr == fixture.pThreadMgr);
        pResult->fOnThisThread = (g_pTip->dwActivateThreadId == GetCurrentThreadId());
        pResult->hrApartment = CoGetApartmentType(&pResult->AptType, &pResult->AptQualifier);
    }
    Fixture_Done(&fixture);
    if (hwnd)
        DestroyWindow(hwnd);
    pResult->cDeactivateAfterDone = g_pTip->cDeactivate;
    if (pResult->dwCoInit != NO_COINIT)
        CoUninitialize();
    return 0;
}

static void TestSecondThread(DWORD dwCoInit)
{
    THREAD_RESULT result = { dwCoInit };
    ULONG cActivate = g_pTip->cActivate, cDeactivate = g_pTip->cDeactivate;
    HANDLE hThread;

    winetest_push_context("%s", dwCoInit == NO_COINIT ? "no COM" : "STA");
    g_cExpectedActivations = cActivate + 1;
    hThread = CreateThread(NULL, 0, SecondThread, &result, 0, NULL);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        while (MsgWaitForMultipleObjects(1, &hThread, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }
        CloseHandle(hThread);
    }
    trace("activations %lu after the window, %lu after the focus, %lu after the profile, deactivations %lu, "
          "apartment 0x%lx %d %d\n",
          result.cActivateAfterWindow, result.cActivateAfterFocus, result.cActivateAfterProfile,
          result.cDeactivateAfterDone, result.hrApartment, result.AptType, result.AptQualifier);
    ok(result.fFixture, "the thread has no thread manager\n");
    ok_long(result.cActivateAfterProfile, cActivate + 1);
    ok(result.fOnThisManager, "the text service was not activated on the thread's manager\n");
    ok(result.fOnThisThread, "the text service was not activated on the thread\n");
    ok_long(result.cDeactivateAfterDone, cDeactivate + 1);
    ok_hr(result.hrApartment, S_OK);
    ok_int(result.AptType, APTTYPE_STA);
    winetest_pop_context();
}

START_TEST(tipthread)
{
    TSF_FIXTURE fixture;
    ITfThreadMgr *pActiveMgr = NULL;
    TfClientId tidActive = 0;
    BOOL fActivated;

    CoInitialize(NULL);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        CoUninitialize();
        return;
    }
    CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pActiveMgr);
    if (pActiveMgr)
        pActiveMgr->Activate(&tidActive);
    fActivated = TestTip_Activate();
    trace("first activation %d, %lu activations\n", fActivated, g_pTip->cActivate);
    if (!Fixture_Init(&fixture, L"abc"))
    {
        Fixture_Done(&fixture);
        TestTip_Unregister();
        CoUninitialize();
        return;
    }
    if (!g_pTip->cActivate)
    {
        fixture.pThreadMgr->SetFocus(fixture.pDocMgr);
        fActivated = TestTip_Activate();
        trace("second activation %d, %lu activations\n", fActivated, g_pTip->cActivate);
    }
    ok_long(g_pTip->cActivate, 1);
    ok(g_pTip->pThreadMgr == fixture.pThreadMgr, "the text service is not on the first thread's manager\n");

    TestSecondThread(COINIT_APARTMENTTHREADED);
    {
        HANDLE hReady = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE hMta;

        g_hMtaStop = CreateEventW(NULL, TRUE, FALSE, NULL);
        hMta = CreateThread(NULL, 0, MtaThread, hReady, 0, NULL);
        WaitForSingleObject(hReady, 5000);
        TestSecondThread(NO_COINIT);
        SetEvent(g_hMtaStop);
        if (hMta)
        {
            WaitForSingleObject(hMta, 5000);
            CloseHandle(hMta);
        }
        CloseHandle(hReady);
        CloseHandle(g_hMtaStop);
    }

    TestTip_Deactivate();
    Fixture_Done(&fixture);
    if (pActiveMgr)
    {
        pActiveMgr->Deactivate();
        pActiveMgr->Release();
    }
    ok(g_pTip->cDeactivate == g_pTip->cActivate, "activated %lu times, deactivated %lu times\n", g_pTip->cActivate,
       g_pTip->cDeactivate);
    TestTip_Unregister();
    CoUninitialize();
}
