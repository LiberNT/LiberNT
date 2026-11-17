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

class CItemSink : public ITfLangBarItemSink
{
public:
    CItemSink() : m_cRefs(1), m_dwFlags(0), m_dwThreadId(0) { }
    virtual ~CItemSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItemSink))
        {
            *ppv = static_cast<ITfLangBarItemSink *>(this);
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
    STDMETHODIMP OnUpdate(DWORD dwFlags) override
    {
        m_dwFlags = dwFlags;
        m_dwThreadId = GetCurrentThreadId();
        return S_OK;
    }

    LONG m_cRefs;
    DWORD m_dwFlags;
    DWORD m_dwThreadId;
};

struct SINK_CALL
{
    IStream *pStream;
    HRESULT hrUnmarshal;
    HRESULT hrCall;
};

static DWORD WINAPI SinkCallThread(LPVOID pv)
{
    SINK_CALL *pCall = (SINK_CALL *)pv;
    ITfLangBarItemSink *pSink = NULL;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    pCall->hrUnmarshal = CoGetInterfaceAndReleaseStream(pCall->pStream, IID_ITfLangBarItemSink, (void **)&pSink);
    if (pSink)
    {
        pCall->hrCall = pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT);
        pSink->Release();
    }
    CoUninitialize();
    return 0;
}

static void TestLangBarSinkCall(void)
{
    CItemSink *pSink = new CItemSink();
    SINK_CALL call = { NULL, E_FAIL, E_FAIL };
    HANDLE hThread;
    MSG msg;

    CoInitialize(NULL);
    ok_hr(CoMarshalInterThreadInterfaceInStream(IID_ITfLangBarItemSink, static_cast<ITfLangBarItemSink *>(pSink),
                                                &call.pStream), S_OK);
    hThread = CreateThread(NULL, 0, SinkCallThread, &call, 0, NULL);
    while (hThread && MsgWaitForMultipleObjects(1, &hThread, FALSE, 30000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
    }
    if (hThread)
        CloseHandle(hThread);
    ok_hr(call.hrUnmarshal, S_OK);
    ok_hr(call.hrCall, S_OK);
    ok_long(pSink->m_dwFlags, TF_LBI_ICON | TF_LBI_TEXT);
    ok(pSink->m_dwThreadId == GetCurrentThreadId(), "the call ran on thread %lu\n", pSink->m_dwThreadId);
    pSink->Release();
    CoUninitialize();
}

START_TEST(mtaproxy)
{
    HANDLE hThread;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    TestPSClsid(IID_ITfThreadMgr, "ITfThreadMgr");
    TestPSClsid(IID_ITfDocumentMgr, "ITfDocumentMgr");
    TestPSClsid(IID_ITfContext, "ITfContext");
    TestPSClsid(IID_ITextStoreACP, "ITextStoreACP");
    TestPSClsid(IID_ITfLangBarMgr, "ITfLangBarMgr");
    TestPSClsid(IID_ITfLangBarEventSink, "ITfLangBarEventSink");
    TestPSClsid(IID_ITfLangBarItemMgr, "ITfLangBarItemMgr");
    TestPSClsid(IID_IEnumTfLangBarItems, "IEnumTfLangBarItems");
    TestPSClsid(IID_ITfLangBarItem, "ITfLangBarItem");
    TestPSClsid(IID_ITfLangBarItemSink, "ITfLangBarItemSink");
    TestPSClsid(IID_ITfLangBarItemBalloon, "ITfLangBarItemBalloon");
    TestPSClsid(IID_ITfLangBarItemBitmap, "ITfLangBarItemBitmap");
    TestPSClsid(IID_ITfLangBarItemBitmapButton, "ITfLangBarItemBitmapButton");
    TestPSClsid(IID_ITfLangBarItemButton, "ITfLangBarItemButton");
    TestPSClsid(IID_ITfMenu, "ITfMenu");
    TestPSClsid(IID_ITfSystemDeviceTypeLangBarItem, "ITfSystemDeviceTypeLangBarItem");
    TestPSClsid(IID_ITfSystemLangBarItem, "ITfSystemLangBarItem");
    TestPSClsid(IID_ITfSystemLangBarItemSink, "ITfSystemLangBarItemSink");
    TestPSClsid(IID_ITfSystemLangBarItemText, "ITfSystemLangBarItemText");
    CoUninitialize();

    hThread = CreateThread(NULL, 0, MtaThread, NULL, 0, NULL);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        ok_long(WaitForSingleObject(hThread, 30000), WAIT_OBJECT_0);
        CloseHandle(hThread);
    }

    TestLangBarSinkCall();
}
