/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Text store of contexts owned through ITfContextOwner
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "textstore.h"


#define DEFAULT_TEXTSTORE_VIEW 1

CDefaultTextStore::CDefaultTextStore()
    : m_refs(1)
    , m_text(NULL)
    , m_length(0)
    , m_capacity(0)
    , m_sink(NULL)
    , m_owner(NULL)
    , m_lock(0)
    , m_pendingLock(0)
    , m_cAttrs(0)
    , m_attrs(NULL)
{
    ZeroMemory(&m_selection, sizeof(m_selection));
    m_selection.style.ase = TS_AE_END;
}

CDefaultTextStore::~CDefaultTextStore()
{
    ClearRequestedAttrs();
    if (m_sink)
        m_sink->Release();
    if (m_owner)
        m_owner->Release();
    cicMemFree(m_text);
}

void CDefaultTextStore::SetOwner(ITfContextOwner *owner)
{
    if (owner)
        owner->AddRef();
    if (m_owner)
        m_owner->Release();
    m_owner = owner;
}

void CDefaultTextStore::ClearRequestedAttrs()
{
    for (ULONG i = 0; i < m_cAttrs; ++i)
        VariantClear(&m_attrs[i].varValue);
    cicMemFree(m_attrs);
    m_attrs = NULL;
    m_cAttrs = 0;
}

STDMETHODIMP CDefaultTextStore::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid != IID_IUnknown && riid != IID_ITextStoreACP)
        return E_NOINTERFACE;
    *ppvObj = static_cast<ITextStoreACP *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CDefaultTextStore::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CDefaultTextStore::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CDefaultTextStore::AdviseSink(REFIID riid, IUnknown *punk, DWORD dwMask)
{
    if (!punk || riid != IID_ITextStoreACPSink)
        return E_INVALIDARG;
    if (m_sink)
    {
        IUnknown *unk1 = NULL, *unk2 = NULL;
        m_sink->QueryInterface(IID_IUnknown, (void **)&unk1);
        punk->QueryInterface(IID_IUnknown, (void **)&unk2);
        BOOL same = (unk1 == unk2);
        if (unk1) unk1->Release();
        if (unk2) unk2->Release();
        return same ? S_OK : CONNECT_E_ADVISELIMIT;
    }
    return punk->QueryInterface(IID_ITextStoreACPSink, (void **)&m_sink);
}

STDMETHODIMP CDefaultTextStore::UnadviseSink(IUnknown *punk)
{
    if (!m_sink)
        return CONNECT_E_NOCONNECTION;
    m_sink->Release();
    m_sink = NULL;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::RequestLock(DWORD dwLockFlags, HRESULT *phrSession)
{
    if (!phrSession)
        return E_INVALIDARG;
    if (!m_sink)
        return E_UNEXPECTED;

    DWORD lock = dwLockFlags & TS_LF_READWRITE;
    if (!lock)
        return E_INVALIDARG;

    if (m_lock)
    {
        if (dwLockFlags & TS_LF_SYNC)
        {
            *phrSession = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        m_pendingLock |= lock;
        *phrSession = TS_S_ASYNC;
        return S_OK;
    }

    ITextStoreACPSink *sink = m_sink;
    sink->AddRef();
    AddRef();
    m_lock = lock;
    *phrSession = sink->OnLockGranted(lock);
    m_lock = 0;
    while (m_pendingLock && m_sink)
    {
        lock = m_pendingLock;
        m_pendingLock = 0;
        m_lock = lock;
        m_sink->OnLockGranted(lock);
        m_lock = 0;
    }
    sink->Release();
    Release();
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetStatus(TS_STATUS *pdcs)
{
    if (!pdcs)
        return E_INVALIDARG;
    ZeroMemory(pdcs, sizeof(*pdcs));
    if (m_owner)
    {
        TF_STATUS status = { 0 };
        if (SUCCEEDED(m_owner->GetStatus(&status)))
        {
            pdcs->dwDynamicFlags = status.dwDynamicFlags;
            pdcs->dwStaticFlags = status.dwStaticFlags;
        }
    }
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG cch,
                                            LONG *pacpResultStart, LONG *pacpResultEnd)
{
    if (!pacpResultStart || !pacpResultEnd)
        return E_INVALIDARG;
    if (acpTestStart < 0 || acpTestStart > acpTestEnd || acpTestEnd > m_length)
        return E_INVALIDARG;
    *pacpResultStart = acpTestStart;
    *pacpResultEnd = acpTestEnd;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetSelection(ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP *pSelection,
                                             ULONG *pcFetched)
{
    if (!pSelection || !pcFetched)
        return E_INVALIDARG;
    *pcFetched = 0;
    if (!(m_lock & TS_LF_READ))
        return TS_E_NOLOCK;
    if (ulIndex != TS_DEFAULT_SELECTION && ulIndex != 0)
        return TS_E_NOSELECTION;
    if (!ulCount)
        return S_OK;
    pSelection[0] = m_selection;
    *pcFetched = 1;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::SetSelection(ULONG ulCount, const TS_SELECTION_ACP *pSelection)
{
    if (!pSelection || ulCount != 1)
        return E_INVALIDARG;
    if ((m_lock & TS_LF_READWRITE) != TS_LF_READWRITE)
        return TS_E_NOLOCK;
    if (pSelection->acpStart < 0 || pSelection->acpStart > pSelection->acpEnd ||
        pSelection->acpEnd > m_length)
        return TF_E_INVALIDPOS;
    m_selection = *pSelection;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain, ULONG cchPlainReq,
                                        ULONG *pcchPlainRet, TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq,
                                        ULONG *pcRunInfoRet, LONG *pacpNext)
{
    if (!pcchPlainRet || !pcRunInfoRet || !pacpNext || (cchPlainReq && !pchPlain) ||
        (cRunInfoReq && !prgRunInfo))
        return E_INVALIDARG;
    *pcchPlainRet = 0;
    *pcRunInfoRet = 0;
    *pacpNext = acpStart;
    if (!(m_lock & TS_LF_READ))
        return TS_E_NOLOCK;
    if (acpEnd == -1)
        acpEnd = m_length;
    if (acpStart < 0 || acpStart > acpEnd || acpEnd > m_length)
        return TF_E_INVALIDPOS;

    ULONG count = acpEnd - acpStart;
    if (count > cchPlainReq)
        count = cchPlainReq;
    if (count)
        CopyMemory(pchPlain, m_text + acpStart, count * sizeof(WCHAR));
    *pcchPlainRet = count;
    if (cRunInfoReq && count)
    {
        prgRunInfo[0].uCount = count;
        prgRunInfo[0].type = TS_RT_PLAIN;
        *pcRunInfoRet = 1;
    }
    *pacpNext = acpStart + count;
    return S_OK;
}

static LONG AdjustSelectionAnchor(LONG pos, BOOL fForward, const TS_TEXTCHANGE *change)
{
    if (pos < change->acpStart)
        return pos;
    if (pos > change->acpOldEnd)
        return pos + (change->acpNewEnd - change->acpOldEnd);
    if (pos == change->acpOldEnd && change->acpOldEnd > change->acpStart)
        return change->acpNewEnd;
    return fForward ? change->acpNewEnd : change->acpStart;
}

HRESULT CDefaultTextStore::Replace(LONG start, LONG end, const WCHAR *text, ULONG cch, TS_TEXTCHANGE *change)
{
    if (start < 0 || start > end || end > m_length)
        return TF_E_INVALIDPOS;
    if (cch > (ULONG)(MAXLONG - (m_length - (end - start))))
        return E_OUTOFMEMORY;

    LONG newLength = m_length - (end - start) + (LONG)cch;
    if (newLength > m_capacity)
    {
        LONG capacity = max(newLength, max(m_capacity * 2, 64));
        WCHAR *buffer = static_cast<WCHAR *>(cicMemAlloc(capacity * sizeof(WCHAR)));
        if (!buffer)
            return E_OUTOFMEMORY;
        if (m_length)
            CopyMemory(buffer, m_text, m_length * sizeof(WCHAR));
        cicMemFree(m_text);
        m_text = buffer;
        m_capacity = capacity;
    }

    MoveMemory(m_text + start + cch, m_text + end, (m_length - end) * sizeof(WCHAR));
    if (cch)
        CopyMemory(m_text + start, text, cch * sizeof(WCHAR));
    m_length = newLength;

    TS_TEXTCHANGE local;
    local.acpStart = start;
    local.acpOldEnd = end;
    local.acpNewEnd = start + cch;
    m_selection.acpStart = AdjustSelectionAnchor(m_selection.acpStart, FALSE, &local);
    m_selection.acpEnd = AdjustSelectionAnchor(m_selection.acpEnd, TRUE, &local);
    if (change)
        *change = local;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd, const WCHAR *pchText,
                                        ULONG cch, TS_TEXTCHANGE *pChange)
{
    if (!pChange || (cch && !pchText))
        return E_INVALIDARG;
    if ((m_lock & TS_LF_READWRITE) != TS_LF_READWRITE)
        return TS_E_NOLOCK;
    return Replace(acpStart, acpEnd, pchText, cch, pChange);
}

STDMETHODIMP CDefaultTextStore::GetFormattedText(LONG acpStart, LONG acpEnd, IDataObject **ppDataObject)
{
    return E_NOTIMPL;
}

STDMETHODIMP CDefaultTextStore::GetEmbedded(LONG acpPos, REFGUID rguidService, REFIID riid, IUnknown **ppunk)
{
    return E_NOTIMPL;
}

STDMETHODIMP CDefaultTextStore::QueryInsertEmbedded(const GUID *pguidService, const FORMATETC *pFormatEtc,
                                                    BOOL *pfInsertable)
{
    if (!pfInsertable)
        return E_INVALIDARG;
    *pfInsertable = FALSE;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::InsertEmbedded(DWORD dwFlags, LONG acpStart, LONG acpEnd,
                                               IDataObject *pDataObject, TS_TEXTCHANGE *pChange)
{
    return TS_E_FORMAT;
}

STDMETHODIMP CDefaultTextStore::InsertTextAtSelection(DWORD dwFlags, const WCHAR *pchText, ULONG cch,
                                                      LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange)
{
    if ((m_lock & TS_LF_READWRITE) != TS_LF_READWRITE)
        return TS_E_NOLOCK;

    LONG start = m_selection.acpStart, end = m_selection.acpEnd;
    if (dwFlags & TS_IAS_QUERYONLY)
    {
        if (!pacpStart || !pacpEnd)
            return E_INVALIDARG;
        *pacpStart = start;
        *pacpEnd = end;
        return S_OK;
    }

    if (!pChange || (cch && !pchText))
        return E_INVALIDARG;
    HRESULT hr = Replace(start, end, pchText, cch, pChange);
    if (FAILED(hr))
        return hr;
    m_selection.acpStart = m_selection.acpEnd = start + cch;
    m_selection.style.ase = TS_AE_END;
    if (!(dwFlags & TS_IAS_NOQUERY))
    {
        if (pacpStart)
            *pacpStart = start;
        if (pacpEnd)
            *pacpEnd = start + cch;
    }
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::InsertEmbeddedAtSelection(DWORD dwFlags, IDataObject *pDataObject,
                                                          LONG *pacpStart, LONG *pacpEnd,
                                                          TS_TEXTCHANGE *pChange)
{
    return TS_E_FORMAT;
}

STDMETHODIMP CDefaultTextStore::RequestSupportedAttrs(DWORD dwFlags, ULONG cFilterAttrs,
                                                      const TS_ATTRID *paFilterAttrs)
{
    return RequestAttrsAtPosition(0, cFilterAttrs, paFilterAttrs, dwFlags);
}

STDMETHODIMP CDefaultTextStore::RequestAttrsAtPosition(LONG acpPos, ULONG cFilterAttrs,
                                                       const TS_ATTRID *paFilterAttrs, DWORD dwFlags)
{
    if (cFilterAttrs && !paFilterAttrs)
        return E_INVALIDARG;
    ClearRequestedAttrs();
    if (!cFilterAttrs)
        return S_OK;
    m_attrs = static_cast<TS_ATTRVAL *>(cicMemAllocClear(cFilterAttrs * sizeof(TS_ATTRVAL)));
    if (!m_attrs)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < cFilterAttrs; ++i)
    {
        TS_ATTRVAL *attr = &m_attrs[m_cAttrs];
        VariantInit(&attr->varValue);
        if (!m_owner || m_owner->GetAttribute(paFilterAttrs[i], &attr->varValue) != S_OK)
        {
            VariantClear(&attr->varValue);
            continue;
        }
        attr->idAttr = paFilterAttrs[i];
        ++m_cAttrs;
    }
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::RequestAttrsTransitioningAtPosition(LONG acpPos, ULONG cFilterAttrs,
                                                                    const TS_ATTRID *paFilterAttrs,
                                                                    DWORD dwFlags)
{
    ClearRequestedAttrs();
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::FindNextAttrTransition(LONG acpStart, LONG acpHalt, ULONG cFilterAttrs,
                                                       const TS_ATTRID *paFilterAttrs, DWORD dwFlags,
                                                       LONG *pacpNext, BOOL *pfFound, LONG *plFoundOffset)
{
    if (!pacpNext || !pfFound || !plFoundOffset)
        return E_INVALIDARG;
    *pacpNext = (acpHalt < 0) ? m_length : acpHalt;
    *pfFound = FALSE;
    *plFoundOffset = 0;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::RetrieveRequestedAttrs(ULONG ulCount, TS_ATTRVAL *paAttrVals, ULONG *pcFetched)
{
    if (!pcFetched || (ulCount && !paAttrVals))
        return E_INVALIDARG;
    ULONG count = min(ulCount, m_cAttrs);
    for (ULONG i = 0; i < count; ++i)
        paAttrVals[i] = m_attrs[i];
    for (ULONG i = count; i < m_cAttrs; ++i)
        VariantClear(&m_attrs[i].varValue);
    cicMemFree(m_attrs);
    m_attrs = NULL;
    m_cAttrs = 0;
    *pcFetched = count;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetEndACP(LONG *pacp)
{
    if (!pacp)
        return E_INVALIDARG;
    if (!(m_lock & TS_LF_READ))
        return TS_E_NOLOCK;
    *pacp = m_length;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetActiveView(TsViewCookie *pvcView)
{
    if (!pvcView)
        return E_INVALIDARG;
    *pvcView = DEFAULT_TEXTSTORE_VIEW;
    return S_OK;
}

STDMETHODIMP CDefaultTextStore::GetACPFromPoint(TsViewCookie vcView, const POINT *ptScreen, DWORD dwFlags,
                                                LONG *pacp)
{
    if (vcView != DEFAULT_TEXTSTORE_VIEW)
        return E_INVALIDARG;
    if (!m_owner)
        return E_NOTIMPL;
    return m_owner->GetACPFromPoint(ptScreen, dwFlags, pacp);
}

STDMETHODIMP CDefaultTextStore::GetTextExt(TsViewCookie vcView, LONG acpStart, LONG acpEnd, RECT *prc,
                                           BOOL *pfClipped)
{
    if (vcView != DEFAULT_TEXTSTORE_VIEW || !prc || !pfClipped)
        return E_INVALIDARG;
    if (!(m_lock & TS_LF_READ))
        return TS_E_NOLOCK;
    if (!m_owner)
        return E_NOTIMPL;
    return m_owner->GetTextExt(acpStart, acpEnd, prc, pfClipped);
}

STDMETHODIMP CDefaultTextStore::GetScreenExt(TsViewCookie vcView, RECT *prc)
{
    if (vcView != DEFAULT_TEXTSTORE_VIEW || !prc)
        return E_INVALIDARG;
    if (!m_owner)
        return E_NOTIMPL;
    return m_owner->GetScreenExt(prc);
}

STDMETHODIMP CDefaultTextStore::GetWnd(TsViewCookie vcView, HWND *phwnd)
{
    if (vcView != DEFAULT_TEXTSTORE_VIEW || !phwnd)
        return E_INVALIDARG;
    *phwnd = NULL;
    if (!m_owner)
        return S_FALSE;
    return m_owner->GetWnd(phwnd);
}
