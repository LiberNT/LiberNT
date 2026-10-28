/*
 * PROJECT:     ReactOS CTF
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     ITfContext implementation
 * COPYRIGHT:   Copyright 2009 Aric Stewart, CodeWeavers
 *              Copyright 2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "precomp.h"
#include <inputscope.h>
#include "sinksnapshot.h"
#include "range.h"
#include "textstore.h"
#include "property.h"
#include "composition.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(msctf);

////////////////////////////////////////////////////////////////////////////

class CContext
    : public ITfContext
    , public ITfSource
    // , public ITfContextComposition
    , public ITfContextOwnerCompositionServices
    , public ITfContextOwnerServices
    , public ITfInsertAtSelection
    // , public ITfMouseTracker
    // , public ITfQueryEmbedded
    , public ITfSourceSingle
    , public ITextStoreACPSink
    , public ITextStoreACPServices
{
public:
    CContext();
    virtual ~CContext();

    static HRESULT CreateInstance(
        TfClientId tidOwner,
        IUnknown *punk,
        ITfDocumentMgr *mgr,
        ITfContext **ppOut,
        TfEditCookie *pecTextStore);
    HRESULT Initialize(ITfDocumentMgr *manager);
    HRESULT Uninitialize();
    HRESULT Cleanup(TfClientId tid);
    HRESULT RequestNotificationLock();
    HRESULT NotifyTextEdit();
    void AddRange(struct list *entry) { list_add_tail(&m_ranges, entry); }
    HRESULT CheckLock(TfEditCookie ec, BOOL fWrite);
    ITextStoreACP *TextStore() const { return m_pITextStoreACP; }
    HRESULT ReplaceText(DWORD dwFlags, LONG start, LONG end, const WCHAR *text, LONG cch, TS_TEXTCHANGE *change);
    HRESULT OnCompositionUpdated(CComposition *composition);
    HRESULT OnCompositionEnded(CComposition *composition);

    // ** IUnknown methods **
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ** ITfContext methods **
    STDMETHODIMP RequestEditSession(
        _In_ TfClientId tid,
        _In_ ITfEditSession *pes,
        _In_ DWORD dwFlags,
        _Out_ HRESULT *phrSession) override;
    STDMETHODIMP InWriteSession(
        _In_ TfClientId tid,
        _Out_ BOOL *pfWriteSession) override;
    STDMETHODIMP GetSelection(
        _In_ TfEditCookie ec,
        _In_ ULONG ulIndex,
        _In_ ULONG ulCount,
        _Out_ TF_SELECTION *pSelection,
        _Out_ ULONG *pcFetched) override;
    STDMETHODIMP SetSelection(
        _In_ TfEditCookie ec,
        _In_ ULONG ulCount,
        _In_ const TF_SELECTION *pSelection) override;
    STDMETHODIMP GetStart(
        _In_ TfEditCookie ec,
        _Out_ ITfRange **ppStart) override;
    STDMETHODIMP GetEnd(
        _In_ TfEditCookie ec,
        _Out_ ITfRange **ppEnd) override;
    STDMETHODIMP GetActiveView(_Out_ ITfContextView **ppView) override;
    STDMETHODIMP EnumViews(_Out_ IEnumTfContextViews **ppEnum) override;
    STDMETHODIMP GetStatus(_Out_ TF_STATUS *pdcs) override;
    STDMETHODIMP GetProperty(
        _In_ REFGUID guidProp,
        _Out_ ITfProperty **ppProp) override;
    STDMETHODIMP GetAppProperty(
        _In_ REFGUID guidProp,
        _Out_ ITfReadOnlyProperty **ppProp) override;
    STDMETHODIMP TrackProperties(
        _In_ const GUID **prgProp,
        _In_ ULONG cProp,
        _In_ const GUID **prgAppProp,
        _In_ ULONG cAppProp,
        _Out_ ITfReadOnlyProperty **ppProperty) override;
    STDMETHODIMP EnumProperties(_Out_ IEnumTfProperties **ppEnum) override;
    STDMETHODIMP GetDocumentMgr(_Out_ ITfDocumentMgr **ppDm) override;
    STDMETHODIMP CreateRangeBackup(
        _In_ TfEditCookie ec,
        _In_ ITfRange *pRange,
        _Out_ ITfRangeBackup **ppBackup) override;

    // ** ITfSource methods **
    STDMETHODIMP AdviseSink(
        _In_ REFIID riid,
        _In_ IUnknown *punk,
        _Out_ DWORD *pdwCookie) override;
    STDMETHODIMP UnadviseSink(_In_ DWORD dwCookie) override;

    // ** ITfContextOwnerCompositionServices methods **
    STDMETHODIMP StartComposition(
        _In_ TfEditCookie ecWrite,
        _In_ ITfRange *pCompositionRange,
        _In_ ITfCompositionSink *pSink,
        _Out_ ITfComposition **ppComposition) override;
    STDMETHODIMP EnumCompositions(_Out_ IEnumITfCompositionView **ppEnum) override;
    STDMETHODIMP FindComposition(
        _In_ TfEditCookie ecRead,
        _In_ ITfRange *pTestRange,
        _Out_ IEnumITfCompositionView **ppEnum) override;
    STDMETHODIMP TakeOwnership(
        _In_ TfEditCookie ecWrite,
        _In_ ITfCompositionView *pComposition,
        _In_ ITfCompositionSink *pSink,
        _Out_ ITfComposition **ppComposition) override;
    STDMETHODIMP TerminateComposition(_In_ ITfCompositionView *pComposition) override;

    // ** ITfContextOwnerServices methods **
    STDMETHODIMP OnLayoutChange() override;
    STDMETHODIMP OnAttributeChange(_In_ REFGUID rguidAttribute) override;

    // ** ITfInsertAtSelection methods **
    STDMETHODIMP InsertTextAtSelection(
        _In_ TfEditCookie ec,
        _In_ DWORD dwFlags,
        _In_ const WCHAR *pchText,
        _In_ LONG cch,
        _Out_ ITfRange **ppRange) override;
    STDMETHODIMP InsertEmbeddedAtSelection(
        _In_ TfEditCookie ec,
        _In_ DWORD dwFlags,
        _In_ IDataObject *pDataObject,
        _Out_ ITfRange **ppRange) override;

    // ** ITfSourceSingle methods **
    STDMETHODIMP AdviseSingleSink(
        _In_ TfClientId tid,
        _In_ REFIID riid,
        _In_ IUnknown *punk) override;
    STDMETHODIMP UnadviseSingleSink(
        _In_ TfClientId tid,
        _In_ REFIID riid) override;

    // ** ITextStoreACPSink methods **
    STDMETHODIMP OnTextChange(
        _In_ DWORD dwFlags,
        _In_ const TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP OnSelectionChange() override;
    STDMETHODIMP OnLayoutChange(
        _In_ TsLayoutCode lcode,
        _In_ TsViewCookie vcView) override;
    STDMETHODIMP OnStatusChange(_In_ DWORD dwFlags) override;
    STDMETHODIMP OnAttrsChange(
        _In_ LONG acpStart,
        _In_ LONG acpEnd,
        _In_ ULONG cAttrs,
        _In_ const TS_ATTRID *paAttrs) override;
    STDMETHODIMP OnLockGranted(_In_ DWORD dwLockFlags) override;
    STDMETHODIMP OnStartEditTransaction() override;
    STDMETHODIMP OnEndEditTransaction() override;

    // ** ITextStoreACPServices methods **
    STDMETHODIMP Serialize(
        _In_ ITfProperty *prop,
        _In_ ITfRange *range,
        _Out_ TF_PERSISTENT_PROPERTY_HEADER_ACP *header,
        _In_ IStream *stream) override;
    STDMETHODIMP Unserialize(
        _In_ ITfProperty *prop,
        _In_ const TF_PERSISTENT_PROPERTY_HEADER_ACP *header,
        _In_ IStream *stream,
        _In_ ITfPersistentPropertyLoaderACP *loader) override;
    STDMETHODIMP ForceLoadProperty(_In_ ITfProperty *prop) override;
    STDMETHODIMP CreateRange(
        _In_ LONG start,
        _In_ LONG end,
        _Out_ ITfRangeACP **range) override;

protected:
    struct PendingSession
    {
        ITfEditSession *session;
        TfClientId tid;
        DWORD lock;
    };

    HRESULT RunSession(ITfEditSession *session, TfClientId tid, DWORD lock);
    HRESULT QueueSession(ITfEditSession *session, TfClientId tid, DWORD lock);
    HRESULT RequestQueuedLock();
    void ScheduleQueuedLock();
    static VOID CALLBACK QueuedLockTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
    HRESULT TerminateOne(CComposition *composition);
    void ReleaseCompositions();
    void ReleaseProperties();

    LONG m_cRefs;
    BOOL m_connected;
    CDefaultTextStore *m_pDefaultStore;
    TfClientId m_currentTid;
    DWORD m_currentLock;
    DWORD m_lockFlags;
    TfClientId m_runningTid;
    PendingSession *m_pending;
    ULONG m_cPending;
    ULONG m_cPendingMax;
    UINT_PTR m_queuedLockTimer;
    CContext *m_nextTimer;
    CComposition **m_compositions;
    ULONG m_cCompositions;
    ULONG m_cCompositionsMax;
    CProperty **m_properties;
    ULONG m_cProperties;
    ULONG m_cPropertiesMax;
    DWORD m_ownerCookie;

    // Aggregation
    ITfCompartmentMgr *m_CompartmentMgr;

    TfClientId m_tidOwner;
    TfEditCookie m_defaultCookie;
    TS_STATUS m_documentStatus;
    ITfDocumentMgr *m_manager;

    ITextStoreACP *m_pITextStoreACP;
    ITfContextOwnerCompositionSink *m_pITfContextOwnerCompositionSink;
    ITfCleanupContextSink *m_pCleanupContextSink;
    TfClientId m_cleanupSinkClientId;
    ITfEditSession *m_currentEditSession;
    BOOL m_selectionChanged;
    BOOL m_textChanged;
    LONG m_changeStart, m_changeEnd;
    ULONG m_editTransactions;
    struct list m_ranges;

    // kept as separate lists to reduce unnecessary iterations
    struct list m_pContextKeyEventSink;
    struct list m_pEditTransactionSink;
    struct list m_pStatusSink;
    struct list m_pTextEditSink;
    struct list m_pTextLayoutSink;
};

////////////////////////////////////////////////////////////////////////////

typedef struct tagEditCookie
{
    DWORD lockType;
    CContext *pOwningContext;
    TfClientId tid;
} EditCookie;

#include "contextview.h"

////////////////////////////////////////////////////////////////////////////

CContext::CContext()
    : m_cRefs(1)
    , m_connected(FALSE)
    , m_pDefaultStore(NULL)
    , m_currentTid(0)
    , m_currentLock(0)
    , m_lockFlags(0)
    , m_runningTid(0)
    , m_pending(NULL)
    , m_cPending(0)
    , m_cPendingMax(0)
    , m_queuedLockTimer(0)
    , m_nextTimer(NULL)
    , m_compositions(NULL)
    , m_cCompositions(0)
    , m_cCompositionsMax(0)
    , m_properties(NULL)
    , m_cProperties(0)
    , m_cPropertiesMax(0)
    , m_ownerCookie(0)
    , m_CompartmentMgr(NULL)
    , m_manager(NULL)
    , m_pITextStoreACP(NULL)
    , m_pITfContextOwnerCompositionSink(NULL)
    , m_pCleanupContextSink(NULL)
    , m_cleanupSinkClientId(0)
    , m_currentEditSession(NULL)
    , m_selectionChanged(FALSE)
    , m_textChanged(FALSE)
    , m_changeStart(0)
    , m_changeEnd(0)
    , m_editTransactions(0)
{
    ZeroMemory(&m_documentStatus, sizeof(m_documentStatus));
    list_init(&m_ranges);
    list_init(&m_pContextKeyEventSink);
    list_init(&m_pEditTransactionSink);
    list_init(&m_pStatusSink);
    list_init(&m_pTextEditSink);
    list_init(&m_pTextLayoutSink);
}

CContext::~CContext()
{
    EditCookie *cookie;
    TRACE("destroying %p\n", this);

    ReleaseCompositions();
    ReleaseProperties();
    for (ULONG i = 0; i < m_cPending; ++i)
        m_pending[i].session->Release();
    cicMemFree(m_pending);
    if (m_currentEditSession)
        m_currentEditSession->Release();
    if (m_ownerCookie)
        remove_Cookie(m_ownerCookie);

    if (m_pITextStoreACP)
        m_pITextStoreACP->Release();
    if (m_pDefaultStore)
        m_pDefaultStore->Release();

    if (m_pITfContextOwnerCompositionSink)
        m_pITfContextOwnerCompositionSink->Release();

    if (m_pCleanupContextSink)
        m_pCleanupContextSink->Release();

    if (m_defaultCookie)
    {
        cookie = (EditCookie *)remove_Cookie(m_defaultCookie);
        cicMemFree(cookie);
        m_defaultCookie = 0;
    }

    free_sinks(&m_pContextKeyEventSink);
    free_sinks(&m_pEditTransactionSink);
    free_sinks(&m_pStatusSink);
    free_sinks(&m_pTextEditSink);
    free_sinks(&m_pTextLayoutSink);

    if (m_CompartmentMgr)
        m_CompartmentMgr->Release();
}

STDMETHODIMP CContext::QueryInterface(REFIID riid, void **ppvObj)
{
    *ppvObj = NULL;

    IUnknown *pUnk = NULL;
    if (riid == IID_IUnknown || riid == IID_ITfContext)
        pUnk = static_cast<ITfContext *>(this);
    else if (riid == IID_ITfSource)
        pUnk = static_cast<ITfSource *>(this);
    else if (riid == IID_ITfContextOwnerCompositionServices || riid == IID_ITfContextComposition)
        pUnk = static_cast<ITfContextOwnerCompositionServices *>(this);
    else if (riid == IID_ITfContextOwnerServices)
        pUnk = static_cast<ITfContextOwnerServices *>(this);
    else if (riid == IID_ITfInsertAtSelection)
        pUnk = static_cast<ITfInsertAtSelection *>(this);
    else if (riid == IID_ITfCompartmentMgr)
        pUnk = m_CompartmentMgr;
    else if (riid == IID_ITfSourceSingle)
        pUnk = static_cast<ITfSourceSingle *>(this);
    else if (riid == IID_ITextStoreACPSink)
        pUnk = static_cast<ITextStoreACPSink *>(this);
    else if (riid == IID_ITextStoreACPServices)
        pUnk = static_cast<ITextStoreACPServices *>(this);

    if (pUnk)
    {
        pUnk->AddRef();
        *ppvObj = pUnk;
        return S_OK;
    }

    WARN("unsupported interface: %s\n", debugstr_guid(&riid));
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CContext::AddRef()
{
    return ::InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) CContext::Release()
{
    ULONG ret = ::InterlockedDecrement(&m_cRefs);
    if (!ret)
        delete this;
    return ret;
}

STDMETHODIMP CContext::RequestEditSession(
    _In_ TfClientId tid,
    _In_ ITfEditSession *pes,
    _In_ DWORD dwFlags,
    _Out_ HRESULT *phrSession)
{
    TRACE("(%p) %i %p %x %p\n", this, tid, pes, dwFlags, phrSession);

    if (!phrSession)
        return E_INVALIDARG;

    if (!pes || (!(dwFlags & TF_ES_READ) && !(dwFlags & TF_ES_READWRITE)) ||
        ((dwFlags & TF_ES_SYNC) && (dwFlags & TF_ES_ASYNC)))
    {
        *phrSession = E_FAIL;
        return E_INVALIDARG;
    }

    if (!m_pITextStoreACP)
    {
        *phrSession = E_FAIL;
        return E_FAIL;
    }

    DWORD lock = ((dwFlags & TF_ES_READWRITE) == TF_ES_READWRITE) ? TS_LF_READWRITE : TS_LF_READ;

    if (!m_documentStatus.dwDynamicFlags)
        m_pITextStoreACP->GetStatus(&m_documentStatus);

    if (lock == TS_LF_READWRITE && (m_documentStatus.dwDynamicFlags & TS_SD_READONLY))
    {
        *phrSession = TS_E_READONLY;
        return S_OK;
    }

    if (m_lockFlags && (dwFlags & TF_ES_SYNC))
    {
        if ((m_lockFlags & lock) != lock)
        {
            *phrSession = TF_E_LOCKED;
            return TF_E_LOCKED;
        }
        *phrSession = RunSession(pes, tid, lock);
        return S_OK;
    }

    if (m_lockFlags || m_currentEditSession || (dwFlags & TF_ES_ASYNC))
    {
        if (dwFlags & TF_ES_SYNC)
        {
            *phrSession = TF_E_SYNCHRONOUS;
            return S_OK;
        }
        HRESULT hr = QueueSession(pes, tid, lock);
        if (FAILED(hr))
        {
            *phrSession = hr;
            return hr;
        }
        if (!m_lockFlags && !m_currentEditSession)
            ScheduleQueuedLock();
        *phrSession = TF_S_ASYNC;
        return S_OK;
    }

    HRESULT hr = pes->QueryInterface(IID_ITfEditSession, (LPVOID *)&m_currentEditSession);
    if (FAILED(hr))
    {
        *phrSession = E_FAIL;
        return E_INVALIDARG;
    }
    ITfEditSession *pSession = m_currentEditSession;
    m_currentTid = tid;
    m_currentLock = lock;

    HRESULT hrSession = E_FAIL;
    DWORD lockFlags = lock | ((dwFlags & TF_ES_ASYNC) ? 0 : TS_LF_SYNC);
    hr = m_pITextStoreACP->RequestLock(lockFlags, &hrSession);
    if (SUCCEEDED(hr) && hrSession == TS_E_SYNCHRONOUS && !(dwFlags & (TF_ES_SYNC | TF_ES_ASYNC)))
        hr = m_pITextStoreACP->RequestLock(lock, &hrSession);

    if (m_currentEditSession == pSession && (FAILED(hr) || hrSession != TS_S_ASYNC))
    {
        m_currentEditSession->Release();
        m_currentEditSession = NULL;
    }
    if (hrSession == TS_E_SYNCHRONOUS)
        hrSession = TF_E_SYNCHRONOUS;
    *phrSession = hrSession;
    return hr;
}

STDMETHODIMP CContext::InWriteSession(
    _In_ TfClientId tid,
    _Out_ BOOL *pfWriteSession)
{
    if (!pfWriteSession)
        return E_INVALIDARG;
    *pfWriteSession = (m_lockFlags == TS_LF_READWRITE && m_runningTid == tid);
    return S_OK;
}

STDMETHODIMP CContext::GetSelection(
    _In_ TfEditCookie ec,
    _In_ ULONG ulIndex,
    _In_ ULONG ulCount,
    _Out_ TF_SELECTION *pSelection,
    _Out_ ULONG *pcFetched)
{
    ULONG count, i;
    ULONG totalFetched = 0;
    HRESULT hr = S_OK;

    if (!pSelection || !pcFetched)
        return E_INVALIDARG;

    *pcFetched = 0;

    if (!m_connected)
        return TF_E_DISCONNECTED;

    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;

    if (!m_pITextStoreACP)
    {
        FIXME("Context does not have a ITextStoreACP\n");
        return E_NOTIMPL;
    }

    count = (ulIndex == (ULONG)TF_DEFAULT_SELECTION) ? 1 : ulCount;

    for (i = 0; i < count; i++)
    {
        DWORD fetched;
        TS_SELECTION_ACP acps;

        hr = m_pITextStoreACP->GetSelection(ulIndex + i, 1, &acps, &fetched);
        if (hr == TS_E_NOLOCK)
            return TF_E_NOLOCK;
        else if (FAILED(hr))
            break;

        pSelection[totalFetched].style.ase = (TfActiveSelEnd)acps.style.ase;
        pSelection[totalFetched].style.fInterimChar = acps.style.fInterimChar;
        Range_Constructor(this, acps.acpStart, acps.acpEnd, &pSelection[totalFetched].range);
        totalFetched++;
    }

    *pcFetched = totalFetched;
    return hr;
}

STDMETHODIMP
CContext::SetSelection(
    _In_ TfEditCookie ec,
    _In_ ULONG ulCount,
    _In_ const TF_SELECTION *pSelection)
{
    TS_SELECTION_ACP *acp;
    ULONG i;
    HRESULT hr;

    TRACE("(%p) %i %i %p\n", this, ec, ulCount, pSelection);

    if (!m_pITextStoreACP)
    {
        FIXME("Context does not have a ITextStoreACP\n");
        return E_NOTIMPL;
    }

    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;

    acp = (TS_SELECTION_ACP *)cicMemAlloc(sizeof(TS_SELECTION_ACP) * ulCount);
    if (!acp)
        return E_OUTOFMEMORY;

    for (i = 0; i < ulCount; i++)
    {
        if (FAILED(TF_SELECTION_to_TS_SELECTION_ACP(&pSelection[i], &acp[i])))
        {
            TRACE("Selection Conversion Failed\n");
            cicMemFree(acp);
            return E_FAIL;
        }
    }

    hr = m_pITextStoreACP->SetSelection(ulCount, acp);
    if (SUCCEEDED(hr)) m_selectionChanged = TRUE;

    cicMemFree(acp);

    return hr;
}

STDMETHODIMP
CContext::GetStart(
    _In_ TfEditCookie ec,
    _Out_ ITfRange **ppStart)
{
    TRACE("(%p) %i %p\n", this, ec, ppStart);

    if (!ppStart)
        return E_INVALIDARG;

    *ppStart = NULL;

    if (!m_connected)
        return TF_E_DISCONNECTED;

    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;

    return Range_Constructor(this, 0, 0, ppStart);
}

STDMETHODIMP
CContext::GetEnd(
    _In_ TfEditCookie ec,
    _Out_ ITfRange **ppEnd)
{
    LONG end;

    TRACE("(%p) %i %p\n", this, ec, ppEnd);

    if (!ppEnd)
        return E_INVALIDARG;

    *ppEnd = NULL;

    if (!m_connected)
        return TF_E_DISCONNECTED;

    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;

    if (!m_pITextStoreACP)
    {
        FIXME("Context does not have a ITextStoreACP\n");
        return E_NOTIMPL;
    }

    m_pITextStoreACP->GetEndACP(&end);

    return Range_Constructor(this, end, end, ppEnd);
}

STDMETHODIMP CContext::GetActiveView(_Out_ ITfContextView **ppView)
{
    if (!ppView) return E_INVALIDARG;
    *ppView = NULL;
    if (!m_connected) return TF_E_DISCONNECTED;
    if (!m_pITextStoreACP) return E_NOTIMPL;
    TsViewCookie view;
    HRESULT hr = m_pITextStoreACP->GetActiveView(&view);
    if (FAILED(hr)) return hr;
    *ppView = new(cicNoThrow) CContextView(this, m_pITextStoreACP, view);
    return *ppView ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CContext::EnumViews(_Out_ IEnumTfContextViews **ppEnum)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::GetStatus(_Out_ TF_STATUS *pdcs)
{
    TRACE("(%p) %p\n", this, pdcs);

    if (!m_connected)
        return TF_E_DISCONNECTED;

    if (!pdcs)
        return E_INVALIDARG;

    if (!m_pITextStoreACP)
    {
        FIXME("Context does not have a ITextStoreACP\n");
        return E_NOTIMPL;
    }

    m_pITextStoreACP->GetStatus(&m_documentStatus);

    *pdcs = m_documentStatus;

    return S_OK;
}

static BOOL IsRegisteredProperty(REFGUID guidProp)
{
    WCHAR szGuid[39], szTip[39], szKey[128];
    HKEY hTips, hItem;
    BOOL bFound = FALSE;

    if (!StringFromGUID2(guidProp, szGuid, _countof(szGuid)))
        return FALSE;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\CTF\\TIP", 0, KEY_READ, &hTips) != ERROR_SUCCESS)
        return FALSE;
    for (DWORD dwIndex = 0; !bFound; ++dwIndex)
    {
        DWORD cchTip = _countof(szTip);
        if (RegEnumKeyExW(hTips, dwIndex, szTip, &cchTip, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\Category\\Item\\%s", szTip, szGuid);
        if (RegOpenKeyExW(hTips, szKey, 0, KEY_READ, &hItem) == ERROR_SUCCESS)
        {
            RegCloseKey(hItem);
            bFound = TRUE;
        }
    }
    RegCloseKey(hTips);
    return bFound;
}

static BOOL IsKnownProperty(REFGUID guidProp)
{
    static const GUID *s_systemProperties[] = {
        &GUID_PROP_ATTRIBUTE, &GUID_PROP_COMPOSING, &GUID_PROP_LANGID, &GUID_PROP_READING,
        &GUID_PROP_TEXTOWNER, &GUID_PROP_INPUTSCOPE,
    };

    for (const GUID *guid : s_systemProperties)
    {
        if (guidProp == *guid)
            return TRUE;
    }
    return IsRegisteredProperty(guidProp);
}

STDMETHODIMP CContext::GetProperty(
    _In_ REFGUID guidProp,
    _Out_ ITfProperty **ppProp)
{
    TRACE("(%p) %s %p\n", this, debugstr_guid(&guidProp), ppProp);

    if (!ppProp)
        return E_INVALIDARG;
    *ppProp = NULL;

    for (ULONG i = 0; i < m_cProperties; ++i)
    {
        if (m_properties[i]->Guid() == guidProp)
        {
            *ppProp = m_properties[i];
            (*ppProp)->AddRef();
            return S_OK;
        }
    }

    if (!IsKnownProperty(guidProp))
        return E_FAIL;

    if (m_cProperties == m_cPropertiesMax)
    {
        ULONG count = m_cPropertiesMax ? m_cPropertiesMax * 2 : 8;
        CProperty **properties = static_cast<CProperty **>(cicMemReAlloc(m_properties, count * sizeof(*properties)));
        if (!properties)
            return E_OUTOFMEMORY;
        m_properties = properties;
        m_cPropertiesMax = count;
    }

    CProperty *property = new(cicNoThrow) CProperty(static_cast<ITfContext *>(this), guidProp);
    if (!property)
        return E_OUTOFMEMORY;
    m_properties[m_cProperties++] = property;
    property->AddRef();
    *ppProp = property;
    return S_OK;
}

STDMETHODIMP CContext::GetAppProperty(
    _In_ REFGUID guidProp,
    _Out_ ITfReadOnlyProperty **ppProp)
{
    TRACE("(%p) %s %p\n", this, debugstr_guid(&guidProp), ppProp);

    if (!ppProp)
        return E_INVALIDARG;
    *ppProp = NULL;
    if (!m_pITextStoreACP)
        return E_FAIL;

    CAppProperty *property = new(cicNoThrow) CAppProperty(static_cast<ITfContext *>(this), guidProp);
    if (!property)
        return E_OUTOFMEMORY;
    m_pITextStoreACP->RequestSupportedAttrs(0, 1, &guidProp);
    *ppProp = property;
    return S_OK;
}

STDMETHODIMP CContext::TrackProperties(
    _In_ const GUID **prgProp,
    _In_ ULONG cProp,
    _In_ const GUID **prgAppProp,
    _In_ ULONG cAppProp,
    _Out_ ITfReadOnlyProperty **ppProperty)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::EnumProperties(_Out_ IEnumTfProperties **ppEnum)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::GetDocumentMgr(_Out_ ITfDocumentMgr **ppDm)
{
    TRACE("(%p) %p\n", this, ppDm);

    if (!ppDm)
        return E_INVALIDARG;

    *ppDm = m_manager;
    if (!m_manager)
        return S_FALSE;

    m_manager->AddRef();
    return S_OK;
}

STDMETHODIMP CContext::CreateRangeBackup(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pRange,
    _Out_ ITfRangeBackup **ppBackup)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::AdviseSink(
    _In_ REFIID riid,
    _In_ IUnknown *punk,
    _Out_ DWORD *pdwCookie)
{
    TRACE("(%p) %s %p %p\n", this, debugstr_guid(&riid), punk, pdwCookie);

    if (cicIsNullPtr(&riid) || !punk || !pdwCookie)
        return E_INVALIDARG;

    if (riid == IID_ITfTextEditSink)
        return advise_sink(&m_pTextEditSink, IID_ITfTextEditSink, COOKIE_MAGIC_CONTEXTSINK, punk, pdwCookie);

    if (riid == IID_ITfTextLayoutSink)
        return advise_sink(&m_pTextLayoutSink, IID_ITfTextLayoutSink, COOKIE_MAGIC_CONTEXTSINK, punk, pdwCookie);

    if (riid == IID_ITfContextOwner)
    {
        if (!m_pDefaultStore)
            return E_FAIL;
        if (m_pDefaultStore->HasOwner())
            return CONNECT_E_ADVISELIMIT;
        ITfContextOwner *owner = NULL;
        HRESULT hr = punk->QueryInterface(IID_ITfContextOwner, (void **)&owner);
        if (FAILED(hr))
            return E_INVALIDARG;
        m_ownerCookie = generate_Cookie(COOKIE_MAGIC_CONTEXTOWNER, this);
        if (!m_ownerCookie)
        {
            owner->Release();
            return E_OUTOFMEMORY;
        }
        m_pDefaultStore->SetOwner(owner);
        owner->Release();
        *pdwCookie = m_ownerCookie;
        return S_OK;
    }

    FIXME("(%p) Unhandled Sink: %s\n", this, debugstr_guid(&riid));
    return E_NOTIMPL;
}

STDMETHODIMP CContext::UnadviseSink(_In_ DWORD dwCookie)
{
    TRACE("(%p) %x\n", this, dwCookie);

    if (dwCookie && dwCookie == m_ownerCookie && get_Cookie_magic(dwCookie) == COOKIE_MAGIC_CONTEXTOWNER)
    {
        remove_Cookie(dwCookie);
        m_ownerCookie = 0;
        if (m_pDefaultStore)
            m_pDefaultStore->SetOwner(NULL);
        return S_OK;
    }

    if (get_Cookie_magic(dwCookie) != COOKIE_MAGIC_CONTEXTSINK)
        return E_INVALIDARG;

    return unadvise_sink(dwCookie);
}

STDMETHODIMP CContext::StartComposition(
    _In_ TfEditCookie ecWrite,
    _In_ ITfRange *pCompositionRange,
    _In_ ITfCompositionSink *pSink,
    _Out_ ITfComposition **ppComposition)
{
    TRACE("(%p) %#x %p %p %p\n", this, ecWrite, pCompositionRange, pSink, ppComposition);

    if (!ppComposition)
        return E_INVALIDARG;
    *ppComposition = NULL;

    HRESULT hr = CheckLock(ecWrite, TRUE);
    if (FAILED(hr))
        return hr;

    CRange *range = CRange::FromRange(pCompositionRange);
    if (!range || range->Context() != static_cast<ITfContext *>(this))
        return E_INVALIDARG;

    EditCookie *cookie = static_cast<EditCookie *>(get_Cookie_data(ecWrite));
    CLSID clsid = get_textservice_clsid(cookie->tid);

    if (m_cCompositions == m_cCompositionsMax)
    {
        ULONG count = m_cCompositionsMax ? m_cCompositionsMax * 2 : 4;
        CComposition **compositions =
            static_cast<CComposition **>(cicMemReAlloc(m_compositions, count * sizeof(*compositions)));
        if (!compositions)
            return E_OUTOFMEMORY;
        m_compositions = compositions;
        m_cCompositionsMax = count;
    }

    CRange *compositionRange = NULL;
    hr = Range_CreateInternal(static_cast<ITfContext *>(this), range->Start(), range->End(),
                              TF_GRAVITY_BACKWARD, TF_GRAVITY_FORWARD, TRUE, &compositionRange);
    if (FAILED(hr))
        return hr;

    CComposition *composition =
        new(cicNoThrow) CComposition(static_cast<ITfContext *>(this), compositionRange, pSink, clsid);
    if (!composition)
    {
        compositionRange->Release();
        return E_OUTOFMEMORY;
    }

    if (m_pITfContextOwnerCompositionSink)
    {
        BOOL fOk = TRUE;
        hr = m_pITfContextOwnerCompositionSink->OnStartComposition(
            static_cast<ITfCompositionView *>(composition), &fOk);
        if (FAILED(hr) || !fOk)
        {
            composition->MarkEnded();
            composition->Release();
            return S_OK;
        }
    }

    m_compositions[m_cCompositions++] = composition;
    composition->AddRef();
    *ppComposition = static_cast<ITfComposition *>(composition);
    return S_OK;
}

STDMETHODIMP CContext::EnumCompositions(_Out_ IEnumITfCompositionView **ppEnum)
{
    TRACE("(%p) %p\n", this, ppEnum);

    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;

    CEnumCompositionView *pEnum = new(cicNoThrow) CEnumCompositionView();
    if (!pEnum)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < m_cCompositions; ++i)
    {
        HRESULT hr = pEnum->Add(static_cast<ITfCompositionView *>(m_compositions[i]));
        if (FAILED(hr))
        {
            pEnum->Release();
            return hr;
        }
    }
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP CContext::FindComposition(
    _In_ TfEditCookie ecRead,
    _In_ ITfRange *pTestRange,
    _Out_ IEnumITfCompositionView **ppEnum)
{
    TRACE("(%p) %#x %p %p\n", this, ecRead, pTestRange, ppEnum);

    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;

    HRESULT hr = CheckLock(ecRead, FALSE);
    if (FAILED(hr))
        return hr;

    CRange *test = NULL;
    if (pTestRange)
    {
        test = CRange::FromRange(pTestRange);
        if (!test || test->Context() != static_cast<ITfContext *>(this))
            return E_INVALIDARG;
    }

    CEnumCompositionView *pEnum = new(cicNoThrow) CEnumCompositionView();
    if (!pEnum)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < m_cCompositions; ++i)
    {
        CRange *range = m_compositions[i]->Range();
        if (test && (range->Start() > test->End() || range->End() < test->Start()))
            continue;
        hr = pEnum->Add(static_cast<ITfCompositionView *>(m_compositions[i]));
        if (FAILED(hr))
        {
            pEnum->Release();
            return hr;
        }
    }
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP CContext::TakeOwnership(
    _In_ TfEditCookie ecWrite,
    _In_ ITfCompositionView *pComposition,
    _In_ ITfCompositionSink *pSink,
    _Out_ ITfComposition **ppComposition)
{
    FIXME("STUB:(%p) %#x %p %p %p\n", this, ecWrite, pComposition, pSink, ppComposition);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::TerminateComposition(_In_ ITfCompositionView *pComposition)
{
    TRACE("(%p) %p\n", this, pComposition);

    if (pComposition)
    {
        CComposition *composition = CComposition::FromView(pComposition);
        if (!composition)
            return E_INVALIDARG;
        for (ULONG i = 0; i < m_cCompositions; ++i)
        {
            if (m_compositions[i] == composition)
                return TerminateOne(composition);
        }
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    while (m_cCompositions)
    {
        hr = TerminateOne(m_compositions[0]);
        if (FAILED(hr))
            break;
    }
    return hr;
}

STDMETHODIMP CContext::OnLayoutChange()
{
    if (!m_pITextStoreACP) return S_OK;
    TsViewCookie view;
    HRESULT hr = m_pITextStoreACP->GetActiveView(&view);
    return FAILED(hr) ? hr : OnLayoutChange(TS_LC_CHANGE, view);
}

STDMETHODIMP CContext::OnAttributeChange(_In_ REFGUID rguidAttribute)
{
    TRACE("(%p) %s\n", this, debugstr_guid(&rguidAttribute));
    return S_OK;
}

STDMETHODIMP CContext::InsertTextAtSelection(
    _In_ TfEditCookie ec,
    _In_ DWORD dwFlags,
    _In_ const WCHAR *pchText,
    _In_ LONG cch,
    _Out_ ITfRange **ppRange)
{
    EditCookie *cookie;
    LONG acpStart, acpEnd;
    TS_TEXTCHANGE change;
    HRESULT hr;

    TRACE("(%p) %i %x %s %p\n", this, ec, dwFlags, debugstr_wn(pchText,cch), ppRange);

    if (!m_connected)
        return TF_E_DISCONNECTED;

    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;

    cookie = (EditCookie *)get_Cookie_data(ec);

    if ((cookie->lockType & TS_LF_READWRITE) != TS_LF_READWRITE)
        return TS_E_READONLY;

    if (!m_pITextStoreACP)
    {
        FIXME("Context does not have a ITextStoreACP\n");
        return E_NOTIMPL;
    }

    hr = m_pITextStoreACP->InsertTextAtSelection(dwFlags, pchText, cch, &acpStart, &acpEnd, &change);
    if (SUCCEEDED(hr))
    {
        if (!(dwFlags & TF_IAS_QUERYONLY))
        {
            for (ULONG i = 0; i < m_cProperties; ++i)
                m_properties[i]->ClearSpan(change.acpStart, change.acpOldEnd);
            OnTextChange(0, &change);
            acpStart = change.acpStart;
            acpEnd = change.acpNewEnd;
        }
        if (ppRange) hr = Range_Constructor(this, acpStart, acpEnd, ppRange);
    }

    return hr;
}

STDMETHODIMP CContext::InsertEmbeddedAtSelection(
    _In_ TfEditCookie ec,
    _In_ DWORD dwFlags,
    _In_ IDataObject *pDataObject,
    _Out_ ITfRange **ppRange)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::AdviseSingleSink(
    _In_ TfClientId tid,
    _In_ REFIID riid,
    _In_ IUnknown *punk)
{
    TRACE("(%p) %i %s %p\n", this, tid, debugstr_guid(&riid), punk);

    if (!punk || riid != IID_ITfCleanupContextSink)
        return E_INVALIDARG;
    if (m_pCleanupContextSink)
        return CONNECT_E_ADVISELIMIT;

    HRESULT hr = punk->QueryInterface(IID_ITfCleanupContextSink, (void **)&m_pCleanupContextSink);
    if (SUCCEEDED(hr))
        m_cleanupSinkClientId = tid;
    return hr;
}

STDMETHODIMP CContext::UnadviseSingleSink(
    _In_ TfClientId tid,
    _In_ REFIID riid)
{
    TRACE("(%p) %i %s\n", this, tid, debugstr_guid(&riid));

    if (riid != IID_ITfCleanupContextSink)
        return E_INVALIDARG;
    if (!m_pCleanupContextSink || m_cleanupSinkClientId != tid)
        return CONNECT_E_NOCONNECTION;

    m_pCleanupContextSink->Release();
    m_pCleanupContextSink = NULL;
    m_cleanupSinkClientId = 0;
    return S_OK;
}

STDMETHODIMP CContext::OnTextChange(
    _In_ DWORD dwFlags,
    _In_ const TS_TEXTCHANGE *pChange)
{
    if (!pChange || pChange->acpStart < 0 || pChange->acpOldEnd < pChange->acpStart ||
        pChange->acpNewEnd < pChange->acpStart)
        return E_INVALIDARG;
    struct list *cursor;
    LIST_FOR_EACH(cursor, &m_ranges)
        LIST_ENTRY(cursor, ContextRange, entry)->range->OnTextChange(pChange);
    if (pChange->acpNewEnd > pChange->acpStart)
    {
        for (ULONG i = 0; i < m_cProperties; ++i)
            m_properties[i]->ClearSpan(pChange->acpStart, pChange->acpNewEnd);
    }
    if (m_textChanged)
    {
        if (m_changeEnd >= pChange->acpOldEnd)
            m_changeEnd += pChange->acpNewEnd - pChange->acpOldEnd;
        m_changeStart = min(m_changeStart, pChange->acpStart);
        m_changeEnd = max(m_changeEnd, pChange->acpNewEnd);
    }
    else
    {
        m_changeStart = pChange->acpStart;
        m_changeEnd = pChange->acpNewEnd;
    }
    m_textChanged = TRUE;
    return RequestNotificationLock();
}

STDMETHODIMP CContext::OnSelectionChange()
{
    m_selectionChanged = TRUE;
    return RequestNotificationLock();
}

STDMETHODIMP CContext::OnLayoutChange(
    _In_ TsLayoutCode lcode,
    _In_ TsViewCookie vcView)
{
    TfLayoutCode code;
    switch (lcode)
    {
        case TS_LC_CREATE: code = TF_LC_CREATE; break;
        case TS_LC_CHANGE: code = TF_LC_CHANGE; break;
        case TS_LC_DESTROY: code = TF_LC_DESTROY; break;
        default: return E_INVALIDARG;
    }
    if (!m_pITextStoreACP) return E_UNEXPECTED;
    if (list_empty(&m_pTextLayoutSink)) return S_OK;
    CSinkSnapshot sinks;
    HRESULT hr = sinks.Capture(&m_pTextLayoutSink);
    if (FAILED(hr)) return hr;
    ITfContextView *view = new(cicNoThrow) CContextView(this, m_pITextStoreACP, vcView);
    if (!view) return E_OUTOFMEMORY;
    for (ULONG i = 0; i < sinks.Count(); ++i)
        static_cast<ITfTextLayoutSink *>(sinks[i])->OnLayoutChange(this, code, view);
    view->Release();
    return S_OK;
}

HRESULT CContext::RequestNotificationLock()
{
    if (m_lockFlags || m_currentEditSession) return S_OK;
    if (list_empty(&m_pTextEditSink))
    {
        m_textChanged = m_selectionChanged = FALSE;
        return S_OK;
    }
    if (!m_pITextStoreACP) return E_UNEXPECTED;
    HRESULT session;
    HRESULT hr = m_pITextStoreACP->RequestLock(TS_LF_READ, &session);
    return FAILED(hr) ? hr : session;
}

HRESULT CContext::NotifyTextEdit()
{
    CSinkSnapshot sinks;
    HRESULT hr = sinks.Capture(&m_pTextEditSink);
    if (FAILED(hr)) return hr;
    ITfRange *range = NULL;
    if (m_textChanged)
    {
        hr = Range_Constructor(this, m_changeStart, m_changeEnd, &range);
        if (FAILED(hr)) return hr;
    }
    CEditRecord *record = new(cicNoThrow) CEditRecord(m_selectionChanged, range);
    if (range) range->Release();
    if (!record) return E_OUTOFMEMORY;
    EditCookie *cookie = static_cast<EditCookie *>(cicMemAlloc(sizeof(*cookie)));
    if (!cookie) { record->Release(); return E_OUTOFMEMORY; }
    cookie->lockType = TS_LF_READ;
    cookie->pOwningContext = this;
    cookie->tid = 0;
    TfEditCookie ec = generate_Cookie(COOKIE_MAGIC_EDITCOOKIE, cookie);
    if (!ec) { cicMemFree(cookie); record->Release(); return E_OUTOFMEMORY; }
    m_selectionChanged = m_textChanged = FALSE;
    AddRef();
    for (ULONG i = 0; i < sinks.Count(); ++i)
        static_cast<ITfTextEditSink *>(sinks[i])->OnEndEdit(this, ec, record);
    cicMemFree(remove_Cookie(ec));
    record->Release();
    Release();
    return S_OK;
}

STDMETHODIMP CContext::OnStatusChange(_In_ DWORD dwFlags)
{
    HRESULT hr, hrSession;

    TRACE("(%p) %x\n", this, dwFlags);

    if (!m_pITextStoreACP)
    {
        m_documentStatus.dwDynamicFlags = dwFlags;
        return S_OK;
    }

    hr = m_pITextStoreACP->RequestLock(TS_LF_READ, &hrSession);

    if(SUCCEEDED(hr) && SUCCEEDED(hrSession))
        m_documentStatus.dwDynamicFlags = dwFlags;

    return S_OK;
}

STDMETHODIMP CContext::OnAttrsChange(
    _In_ LONG acpStart,
    _In_ LONG acpEnd,
    _In_ ULONG cAttrs,
    _In_ const TS_ATTRID *paAttrs)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::OnLockGranted(_In_ DWORD dwLockFlags)
{
    TRACE("(%p) %x\n", this, dwLockFlags);

    HRESULT hr = S_OK;
    AddRef();
    m_lockFlags = dwLockFlags & TS_LF_READWRITE;

    if (m_currentEditSession && (m_lockFlags & m_currentLock) == m_currentLock)
    {
        ITfEditSession *session = m_currentEditSession;
        TfClientId tid = m_currentTid;
        m_currentEditSession = NULL;
        hr = RunSession(session, tid, m_lockFlags);
        session->Release();
    }

    while (!m_currentEditSession && m_cPending && (m_lockFlags & m_pending[0].lock) == m_pending[0].lock)
    {
        PendingSession item = m_pending[0];
        MoveMemory(&m_pending[0], &m_pending[1], (m_cPending - 1) * sizeof(*m_pending));
        --m_cPending;
        RunSession(item.session, item.tid, m_lockFlags);
        item.session->Release();
    }

    if (m_textChanged || m_selectionChanged)
        NotifyTextEdit();

    m_lockFlags = 0;
    if (m_cPending && !m_currentEditSession)
        ScheduleQueuedLock();
    Release();
    return hr;
}

STDMETHODIMP CContext::OnStartEditTransaction()
{
    ++m_editTransactions;
    return S_OK;
}

STDMETHODIMP CContext::OnEndEditTransaction()
{
    if (!m_editTransactions) return E_UNEXPECTED;
    --m_editTransactions;
    return S_OK;
}

STDMETHODIMP CContext::Serialize(
    _In_ ITfProperty *prop,
    _In_ ITfRange *range,
    _Out_ TF_PERSISTENT_PROPERTY_HEADER_ACP *header,
    _In_ IStream *stream)
{
    FIXME("stub: %p %p %p %p %p\n", this, prop, range, header, stream);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::Unserialize(
    _In_ ITfProperty *prop,
    _In_ const TF_PERSISTENT_PROPERTY_HEADER_ACP *header,
    _In_ IStream *stream,
    _In_ ITfPersistentPropertyLoaderACP *loader)
{
    FIXME("stub: %p %p %p %p %p\n", this, prop, header, stream, loader);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::ForceLoadProperty(_In_ ITfProperty *prop)
{
    FIXME("stub: %p %p\n", this, prop);
    return E_NOTIMPL;
}

STDMETHODIMP CContext::CreateRange(
    _In_ LONG start,
    _In_ LONG end,
    _Out_ ITfRangeACP **range)
{
    TRACE("(%p) %d %d %p\n", this, start, end, range);

    if (!range || start < 0 || end < start)
        return E_INVALIDARG;

    *range = NULL;
    ITfRange *pRange = NULL;
    HRESULT hr = Range_Constructor(this, start, end, &pRange);
    if (SUCCEEDED(hr))
    {
        hr = pRange->QueryInterface(IID_ITfRangeACP, (void **)range);
        pRange->Release();
    }
    return hr;
}

HRESULT CContext::CreateInstance(
    TfClientId tidOwner,
    IUnknown *punk,
    ITfDocumentMgr *mgr,
    ITfContext **ppOut,
    TfEditCookie *pecTextStore)
{
    CContext *This = new(cicNoThrow) CContext();
    if (!This)
        return E_OUTOFMEMORY;

    EditCookie *cookie = (EditCookie *)cicMemAlloc(sizeof(EditCookie));
    if (!cookie)
    {
        delete This;
        return E_OUTOFMEMORY;
    }

    TRACE("(%p) %x %p %p %p\n", This, tidOwner, punk, ppOut, pecTextStore);

    This->m_tidOwner = tidOwner;
    This->m_manager = mgr;

    CompartmentMgr_Constructor(static_cast<ITfContext *>(This), IID_IUnknown, (IUnknown **)&This->m_CompartmentMgr);

    cookie->lockType = TF_ES_READ;
    cookie->pOwningContext = This;
    cookie->tid = tidOwner;

    if (punk)
    {
        punk->QueryInterface(IID_ITextStoreACP, (LPVOID*)&This->m_pITextStoreACP);
        punk->QueryInterface(IID_ITfContextOwnerCompositionSink, (LPVOID*)&This->m_pITfContextOwnerCompositionSink);
    }

    if (!This->m_pITextStoreACP)
    {
        This->m_pDefaultStore = new(cicNoThrow) CDefaultTextStore();
        if (!This->m_pDefaultStore)
        {
            cicMemFree(cookie);
            This->Release();
            return E_OUTOFMEMORY;
        }
        This->m_pITextStoreACP = This->m_pDefaultStore;
        This->m_pITextStoreACP->AddRef();
    }

    This->m_defaultCookie = generate_Cookie(COOKIE_MAGIC_EDITCOOKIE, cookie);
    *pecTextStore = This->m_pDefaultStore ? 0 : This->m_defaultCookie;

    list_init(&This->m_pContextKeyEventSink);
    list_init(&This->m_pEditTransactionSink);
    list_init(&This->m_pStatusSink);
    list_init(&This->m_pTextEditSink);
    list_init(&This->m_pTextLayoutSink);

    *ppOut = static_cast<ITfContext *>(This);
    TRACE("returning %p\n", *ppOut);

    return S_OK;
}

HRESULT CContext::Initialize(ITfDocumentMgr *manager)
{
    if (m_pITextStoreACP)
    {
        m_pITextStoreACP->AdviseSink(IID_ITextStoreACPSink,
                                     static_cast<ITextStoreACPSink *>(this),
                                     TS_AS_ALL_SINKS);
    }
    m_connected = TRUE;
    m_manager = manager;
    return S_OK;
}

HRESULT CContext::Uninitialize()
{
    while (m_cCompositions)
    {
        CComposition *composition = m_compositions[--m_cCompositions];
        composition->MarkEnded();
        if (m_pITfContextOwnerCompositionSink)
            m_pITfContextOwnerCompositionSink->OnEndComposition(static_cast<ITfCompositionView *>(composition));
        composition->Release();
    }
    ReleaseCompositions();
    if (m_pITextStoreACP)
        m_pITextStoreACP->UnadviseSink(static_cast<ITextStoreACPSink *>(this));
    m_connected = FALSE;
    m_manager = NULL;
    return S_OK;
}

HRESULT CContext::Cleanup(TfClientId tid)
{
    ITfCleanupContextSink *CleanupSink;
    EditCookie *Cookie;
    TfEditCookie EditCookieId;
    HRESULT hr;

    if (!m_connected || !m_pCleanupContextSink || (tid && tid != m_cleanupSinkClientId))
        return S_FALSE;

    Cookie = (EditCookie *)cicMemAlloc(sizeof(*Cookie));
    if (!Cookie)
        return E_OUTOFMEMORY;
    Cookie->lockType = TS_LF_READWRITE;
    Cookie->pOwningContext = this;
    Cookie->tid = tid;
    EditCookieId = generate_Cookie(COOKIE_MAGIC_EDITCOOKIE, Cookie);
    if (!EditCookieId)
    {
        cicMemFree(Cookie);
        return E_OUTOFMEMORY;
    }

    CleanupSink = m_pCleanupContextSink;
    CleanupSink->AddRef();
    hr = CleanupSink->OnCleanupContext(EditCookieId, static_cast<ITfContext *>(this));
    CleanupSink->Release();

    Cookie = (EditCookie *)remove_Cookie(EditCookieId);
    cicMemFree(Cookie);
    return hr;
}

class CTerminateSession final : public ITfEditSession
{
public:
    CTerminateSession(CComposition *composition) : m_refs(1), m_composition(composition) { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *ppvObj = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs)
            delete this;
        return refs;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        ITfCompositionSink *sink = m_composition->Sink();
        if (sink && m_composition->IsActive())
        {
            sink->AddRef();
            sink->OnCompositionTerminated(ec, static_cast<ITfComposition *>(m_composition));
            sink->Release();
        }
        return S_OK;
    }
private:
    LONG m_refs;
    CComposition *m_composition;
};

HRESULT CContext::CheckLock(TfEditCookie ec, BOOL fWrite)
{
    if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE)
        return TF_E_NOLOCK;
    EditCookie *cookie = static_cast<EditCookie *>(get_Cookie_data(ec));
    if (!cookie || cookie->pOwningContext != this)
        return TF_E_NOLOCK;
    if (fWrite && (cookie->lockType & TS_LF_READWRITE) != TS_LF_READWRITE)
        return TF_E_NOLOCK;
    return S_OK;
}

HRESULT CContext::ReplaceText(DWORD dwFlags, LONG start, LONG end, const WCHAR *text, LONG cch, TS_TEXTCHANGE *change)
{
    if (!m_pITextStoreACP)
        return E_FAIL;
    HRESULT hr = m_pITextStoreACP->SetText(dwFlags, start, end, text, cch, change);
    if (FAILED(hr))
        return hr;
    if (!(dwFlags & TS_ST_CORRECTION))
    {
        for (ULONG i = 0; i < m_cProperties; ++i)
            m_properties[i]->ClearSpan(change->acpStart, change->acpOldEnd);
    }
    OnTextChange(0, change);
    return S_OK;
}

HRESULT CContext::RunSession(ITfEditSession *session, TfClientId tid, DWORD lock)
{
    EditCookie *cookie = static_cast<EditCookie *>(cicMemAlloc(sizeof(EditCookie)));
    if (!cookie)
        return E_OUTOFMEMORY;
    cookie->lockType = lock;
    cookie->pOwningContext = this;
    cookie->tid = tid;
    TfEditCookie ec = generate_Cookie(COOKIE_MAGIC_EDITCOOKIE, cookie);
    if (!ec)
    {
        cicMemFree(cookie);
        return E_OUTOFMEMORY;
    }

    TfClientId previousTid = m_runningTid;
    m_runningTid = tid;
    HRESULT hr = session->DoEditSession(ec);
    m_runningTid = previousTid;

    cicMemFree(remove_Cookie(ec));
    return hr;
}

HRESULT CContext::QueueSession(ITfEditSession *session, TfClientId tid, DWORD lock)
{
    if (m_cPending == m_cPendingMax)
    {
        ULONG count = m_cPendingMax ? m_cPendingMax * 2 : 4;
        PendingSession *pending = static_cast<PendingSession *>(cicMemReAlloc(m_pending, count * sizeof(*pending)));
        if (!pending)
            return E_OUTOFMEMORY;
        m_pending = pending;
        m_cPendingMax = count;
    }
    HRESULT hr = session->QueryInterface(IID_ITfEditSession, (void **)&m_pending[m_cPending].session);
    if (FAILED(hr))
        return E_INVALIDARG;
    m_pending[m_cPending].tid = tid;
    m_pending[m_cPending].lock = lock;
    ++m_cPending;
    return S_OK;
}

static SRWLOCK s_timerLock = SRWLOCK_INIT;
static CContext *s_timerContexts = NULL;

void CContext::ScheduleQueuedLock()
{
    if (m_queuedLockTimer)
        return;
    m_queuedLockTimer = SetTimer(NULL, 0, 0, QueuedLockTimerProc);
    if (!m_queuedLockTimer)
    {
        RequestQueuedLock();
        return;
    }
    AddRef();
    AcquireSRWLockExclusive(&s_timerLock);
    m_nextTimer = s_timerContexts;
    s_timerContexts = this;
    ReleaseSRWLockExclusive(&s_timerLock);
}

VOID CALLBACK CContext::QueuedLockTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    CContext *context = NULL;

    KillTimer(NULL, idEvent);
    AcquireSRWLockExclusive(&s_timerLock);
    for (CContext **ppNext = &s_timerContexts; *ppNext; ppNext = &(*ppNext)->m_nextTimer)
    {
        if ((*ppNext)->m_queuedLockTimer == idEvent)
        {
            context = *ppNext;
            *ppNext = context->m_nextTimer;
            break;
        }
    }
    ReleaseSRWLockExclusive(&s_timerLock);
    if (!context)
        return;

    context->m_queuedLockTimer = 0;
    context->m_nextTimer = NULL;
    if (!context->m_lockFlags && !context->m_currentEditSession)
        context->RequestQueuedLock();
    context->Release();
}

HRESULT CContext::RequestQueuedLock()
{
    HRESULT hr = S_OK;
    while (m_cPending && !m_currentEditSession && m_pITextStoreACP)
    {
        PendingSession item = m_pending[0];
        MoveMemory(&m_pending[0], &m_pending[1], (m_cPending - 1) * sizeof(*m_pending));
        --m_cPending;
        m_currentEditSession = item.session;
        m_currentTid = item.tid;
        m_currentLock = item.lock;
        HRESULT hrSession = E_FAIL;
        hr = m_pITextStoreACP->RequestLock(item.lock, &hrSession);
        if (m_currentEditSession != item.session || (SUCCEEDED(hr) && hrSession == TS_S_ASYNC))
            break;
        m_currentEditSession = NULL;
        MoveMemory(&m_pending[1], &m_pending[0], m_cPending * sizeof(*m_pending));
        m_pending[0] = item;
        ++m_cPending;
        break;
    }
    return hr;
}

HRESULT CContext::OnCompositionUpdated(CComposition *composition)
{
    if (!m_pITfContextOwnerCompositionSink)
        return S_OK;
    ITfRange *range = NULL;
    HRESULT hr = composition->GetRange(&range);
    if (FAILED(hr))
        return hr;
    m_pITfContextOwnerCompositionSink->OnUpdateComposition(static_cast<ITfCompositionView *>(composition), range);
    range->Release();
    return S_OK;
}

HRESULT CContext::OnCompositionEnded(CComposition *composition)
{
    ULONG i;
    for (i = 0; i < m_cCompositions; ++i)
    {
        if (m_compositions[i] == composition)
            break;
    }
    if (i == m_cCompositions)
        return S_OK;
    MoveMemory(&m_compositions[i], &m_compositions[i + 1], (m_cCompositions - i - 1) * sizeof(*m_compositions));
    --m_cCompositions;
    if (m_pITfContextOwnerCompositionSink)
        m_pITfContextOwnerCompositionSink->OnEndComposition(static_cast<ITfCompositionView *>(composition));
    composition->Release();
    return S_OK;
}

HRESULT CContext::TerminateOne(CComposition *composition)
{
    composition->AddRef();
    HRESULT hr = S_OK;
    if (composition->Sink())
    {
        CTerminateSession *session = new(cicNoThrow) CTerminateSession(composition);
        if (!session)
        {
            composition->Release();
            return E_OUTOFMEMORY;
        }
        HRESULT hrSession = S_OK;
        if (m_lockFlags == TS_LF_READWRITE && m_runningTid != m_tidOwner)
            hr = TS_E_NOLOCK;
        else if (m_lockFlags == TS_LF_READWRITE)
            hr = RunSession(session, m_runningTid, m_lockFlags);
        else
            hr = RequestEditSession(m_tidOwner, session, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        session->Release();
        if (SUCCEEDED(hr) && FAILED(hrSession))
            hr = hrSession;
        if (FAILED(hr))
        {
            composition->Release();
            return hr;
        }
    }
    if (composition->IsActive())
    {
        composition->MarkEnded();
        OnCompositionEnded(composition);
    }
    composition->Release();
    return hr;
}

void CContext::ReleaseCompositions()
{
    while (m_cCompositions)
    {
        CComposition *composition = m_compositions[--m_cCompositions];
        composition->MarkEnded();
        composition->Release();
    }
    cicMemFree(m_compositions);
    m_compositions = NULL;
    m_cCompositionsMax = 0;
}

void CContext::ReleaseProperties()
{
    while (m_cProperties)
    {
        CProperty *property = m_properties[--m_cProperties];
        property->Detach();
        property->Release();
    }
    cicMemFree(m_properties);
    m_properties = NULL;
    m_cPropertiesMax = 0;
}

////////////////////////////////////////////////////////////////////////////

EXTERN_C
HRESULT
Context_Constructor(
    TfClientId tidOwner,
    IUnknown *punk,
    ITfDocumentMgr *mgr,
    ITfContext **ppOut,
    TfEditCookie *pecTextStore)
{
    return CContext::CreateInstance(tidOwner, punk, mgr, ppOut, pecTextStore);
}

EXTERN_C
HRESULT Context_Initialize(ITfContext *iface, ITfDocumentMgr *manager)
{
    CContext *This = static_cast<CContext *>(iface);
    return This->Initialize(manager);
}

EXTERN_C
HRESULT Context_Cleanup(ITfContext *iface, TfClientId tid)
{
    CContext *This = static_cast<CContext *>(iface);
    return This->Cleanup(tid);
}

EXTERN_C
HRESULT Context_Uninitialize(ITfContext *iface)
{
    CContext *This = static_cast<CContext *>(iface);
    return This->Uninitialize();
}

EXTERN_C void Context_AddRange(ITfContext *context, struct list *entry)
{
    static_cast<CContext *>(context)->AddRange(entry);
}

EXTERN_C HRESULT Context_CheckLock(ITfContext *context, TfEditCookie ec, BOOL fWrite)
{
    return static_cast<CContext *>(context)->CheckLock(ec, fWrite);
}

EXTERN_C ITextStoreACP *Context_GetTextStore(ITfContext *context)
{
    return static_cast<CContext *>(context)->TextStore();
}

EXTERN_C HRESULT Context_ReplaceText(ITfContext *context, DWORD dwFlags, LONG start, LONG end,
                                     const WCHAR *text, LONG cch, TS_TEXTCHANGE *change)
{
    return static_cast<CContext *>(context)->ReplaceText(dwFlags, start, end, text, cch, change);
}

HRESULT Context_OnCompositionUpdated(ITfContext *context, CComposition *composition)
{
    return static_cast<CContext *>(context)->OnCompositionUpdated(composition);
}

HRESULT Context_OnCompositionEnded(ITfContext *context, CComposition *composition)
{
    return static_cast<CContext *>(context)->OnCompositionEnded(composition);
}
