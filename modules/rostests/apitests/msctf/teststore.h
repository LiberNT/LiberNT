/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ITextStoreACP test object and TSF test helpers
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

enum LOCK_MODE
{
    LOCK_GRANT_SYNC,
    LOCK_GRANT_ASYNC,
    LOCK_REFUSE_SYNCHRONOUS,
    LOCK_REFUSE_SESSION_ERROR,
    LOCK_FAIL_CALL,
};

void Log_Reset(void);
void Log_Add(const char *pszEvent);
const char *Log_Get(void);

class CTestStore : public ITextStoreACP, public ITfContextOwnerCompositionSink
{
public:
    CTestStore(LPCWSTR pszText);
    virtual ~CTestStore();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD dwMask) override;
    STDMETHODIMP UnadviseSink(IUnknown *punk) override;
    STDMETHODIMP RequestLock(DWORD dwLockFlags, HRESULT *phrSession) override;
    STDMETHODIMP GetStatus(TS_STATUS *pdcs) override;
    STDMETHODIMP QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG cch, LONG *pacpResultStart, LONG *pacpResultEnd) override;
    STDMETHODIMP GetSelection(ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP *pSelection, ULONG *pcFetched) override;
    STDMETHODIMP SetSelection(ULONG ulCount, const TS_SELECTION_ACP *pSelection) override;
    STDMETHODIMP GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain, ULONG cchPlainReq, ULONG *pcchPlainRet,
                         TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq, ULONG *pcRunInfoRet, LONG *pacpNext) override;
    STDMETHODIMP SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd, const WCHAR *pchText, ULONG cch, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP GetFormattedText(LONG acpStart, LONG acpEnd, IDataObject **ppDataObject) override;
    STDMETHODIMP GetEmbedded(LONG acpPos, REFGUID rguidService, REFIID riid, IUnknown **ppunk) override;
    STDMETHODIMP QueryInsertEmbedded(const GUID *pguidService, const FORMATETC *pFormatEtc, BOOL *pfInsertable) override;
    STDMETHODIMP InsertEmbedded(DWORD dwFlags, LONG acpStart, LONG acpEnd, IDataObject *pDataObject, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP InsertTextAtSelection(DWORD dwFlags, const WCHAR *pchText, ULONG cch, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD dwFlags, IDataObject *pDataObject, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP RequestSupportedAttrs(DWORD dwFlags, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs) override;
    STDMETHODIMP RequestAttrsAtPosition(LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags) override;
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags) override;
    STDMETHODIMP FindNextAttrTransition(LONG acpStart, LONG acpHalt, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags,
                                        LONG *pacpNext, BOOL *pfFound, LONG *plFoundOffset) override;
    STDMETHODIMP RetrieveRequestedAttrs(ULONG ulCount, TS_ATTRVAL *paAttrVals, ULONG *pcFetched) override;
    STDMETHODIMP GetEndACP(LONG *pacp) override;
    STDMETHODIMP GetActiveView(TsViewCookie *pvcView) override;
    STDMETHODIMP GetACPFromPoint(TsViewCookie vcView, const POINT *ptScreen, DWORD dwFlags, LONG *pacp) override;
    STDMETHODIMP GetTextExt(TsViewCookie vcView, LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped) override;
    STDMETHODIMP GetScreenExt(TsViewCookie vcView, RECT *prc) override;
    STDMETHODIMP GetWnd(TsViewCookie vcView, HWND *phwnd) override;

    STDMETHODIMP OnStartComposition(ITfCompositionView *pComposition, BOOL *pfOk) override;
    STDMETHODIMP OnUpdateComposition(ITfCompositionView *pComposition, ITfRange *pRangeNew) override;
    STDMETHODIMP OnEndComposition(ITfCompositionView *pComposition) override;

    HRESULT GrantPending();
    LPCWSTR Text() const { return m_szText; }

    LOCK_MODE m_lockMode;
    DWORD m_dwDynamicFlags;
    BOOL m_fAcceptComposition;
    BOOL m_fOwnerSink;
    DWORD m_lastSetTextFlags;
    DWORD m_pendingLock;
    ULONG m_cLockRequests;
    ITextStoreACPSink *m_pSink;
    TS_SELECTION_ACP m_selection;
    BOOL m_fAttr;
    GUID m_attrId;
    LONG m_attrStart, m_attrEnd, m_attrValue;
    BOOL m_fAttrPending;
    VARIANT m_attrPending;

private:
    LONG m_cRefs;
    DWORD m_lockHeld;
    WCHAR m_szText[256];
};

typedef void (*EDIT_FN)(TfEditCookie ec, void *pContext);

class CTestEditSession : public ITfEditSession
{
public:
    CTestEditSession(EDIT_FN pfn, void *pContext) : m_cRefs(1), m_pfn(pfn), m_pContext(pContext), m_cCalls(0), m_ecLast(0) { }
    virtual ~CTestEditSession() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

    LONG m_cRefs;
    EDIT_FN m_pfn;
    void *m_pContext;
    ULONG m_cCalls;
    TfEditCookie m_ecLast;
};

struct TSF_FIXTURE
{
    ITfThreadMgr *pThreadMgr;
    TfClientId tid;
    ITfDocumentMgr *pDocMgr;
    ITfContext *pContext;
    TfEditCookie ecTextStore;
    CTestStore *pStore;
};

BOOL Fixture_Init(TSF_FIXTURE *pFixture, LPCWSTR pszText);
void Fixture_Done(TSF_FIXTURE *pFixture);
HRESULT Fixture_RunSync(TSF_FIXTURE *pFixture, EDIT_FN pfn, void *pContext, DWORD dwFlags);
ITfRange *Range_New(TSF_FIXTURE *pFixture, TfEditCookie ec, LONG s, LONG e);
BOOL Range_GetExtent(TfEditCookie ec, ITfRange *pRange, LONG *pStart, LONG *pEnd);
