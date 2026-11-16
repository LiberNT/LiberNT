/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the Text Services Framework interfaces can be used from another apartment
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static const CLSID CLSID_TsfPSFactoryBuffer =
    { 0xb5f8fb3b, 0x393f, 0x4f7c, { 0x84, 0xcb, 0x50, 0x49, 0x24, 0xc2, 0x70, 0x5a } };

static void TestPSClsid(REFIID riid, const char *pszName)
{
    CLSID clsid = { 0 };
    HRESULT hr = CoGetPSClsid(riid, &clsid);

    ok(hr == S_OK, "%s: CoGetPSClsid returned 0x%lx\n", pszName, hr);
    ok(IsEqualCLSID(clsid, CLSID_TsfPSFactoryBuffer), "%s: proxy %s\n", pszName, wine_dbgstr_guid(&clsid));
}

static DWORD WINAPI MtaThread(LPVOID pv)
{
    ITfThreadMgr *pThreadMgr = NULL;
    ITfDocumentMgr *pDocMgr = NULL;
    TfClientId tid = 0;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    ok(hr == S_OK, "CoCreateInstance(CLSID_TF_ThreadMgr) from a multithreaded apartment returned 0x%lx\n", hr);
    if (pThreadMgr)
    {
        ok_hr(pThreadMgr->Activate(&tid), S_OK);
        ok(tid != TF_CLIENTID_NULL, "no client id\n");
        ok_hr(pThreadMgr->CreateDocumentMgr(&pDocMgr), S_OK);
        if (pDocMgr)
            pDocMgr->Release();
        ok_hr(pThreadMgr->Deactivate(), S_OK);
        pThreadMgr->Release();
    }
    CoUninitialize();
    return 0;
}

START_TEST(mtaproxy)
{
    HANDLE hThread;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    TestPSClsid(IID_ITfThreadMgr, "ITfThreadMgr");
    TestPSClsid(IID_ITfDocumentMgr, "ITfDocumentMgr");
    TestPSClsid(IID_ITfContext, "ITfContext");
    TestPSClsid(IID_ITextStoreACP, "ITextStoreACP");
    CoUninitialize();

    hThread = CreateThread(NULL, 0, MtaThread, NULL, 0, NULL);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        ok_long(WaitForSingleObject(hThread, 30000), WAIT_OBJECT_0);
        CloseHandle(hThread);
    }
}
