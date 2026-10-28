/*
 * PROJECT:     ReactOS CTF
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     ITfRangeACP implementation
 * COPYRIGHT:   Copyright 2009 Aric Stewart, CodeWeavers
 *              Copyright 2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "precomp.h"
#include "range.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(msctf);

////////////////////////////////////////////////////////////////////////////

CRange::CRange(
    _In_ ITfContext *context,
    _In_ LONG anchorStart,
    _In_ LONG anchorEnd
)
    : CRange(context, anchorStart, anchorEnd, TF_GRAVITY_BACKWARD, TF_GRAVITY_FORWARD, FALSE)
{
}

CRange::CRange(
    _In_ ITfContext *context,
    _In_ LONG anchorStart,
    _In_ LONG anchorEnd,
    _In_ TfGravity gravityStart,
    _In_ TfGravity gravityEnd,
    _In_ BOOL fWeakContext
)
    : m_cRefs(1)
    , m_context(context)
    , m_anchorStart(anchorStart)
    , m_anchorEnd(max(anchorStart, anchorEnd))
    , m_gravityStart(gravityStart)
    , m_gravityEnd(gravityEnd)
    , m_fWeakContext(fWeakContext)
{
    m_link.range = this;
    if (context)
    {
        if (!fWeakContext)
            context->AddRef();
        Context_AddRange(context, &m_link.entry);
    }
    else
        list_init(&m_link.entry);
}

CRange::~CRange()
{
    TRACE("destroying %p\n", this);
    list_remove(&m_link.entry);
    if (m_context && !m_fWeakContext)
        m_context->Release();
}

static LONG AdjustAnchor(LONG pos, TfGravity gravity, const TS_TEXTCHANGE *change)
{
    if (pos < change->acpStart)
        return pos;
    if (pos > change->acpOldEnd)
        return pos + (change->acpNewEnd - change->acpOldEnd);
    if (pos == change->acpOldEnd && change->acpOldEnd > change->acpStart)
        return change->acpNewEnd;
    return (gravity == TF_GRAVITY_FORWARD) ? change->acpNewEnd : change->acpStart;
}

void CRange::OnTextChange(const TS_TEXTCHANGE *change)
{
    LONG start = AdjustAnchor(m_anchorStart, m_gravityStart, change);
    LONG end = AdjustAnchor(m_anchorEnd, m_gravityEnd, change);
    m_anchorStart = start;
    m_anchorEnd = max(start, end);
}

void CRange::SetAnchors(LONG start, LONG end)
{
    m_anchorStart = start;
    m_anchorEnd = max(start, end);
}

CRange *CRange::FromRange(ITfRange *range)
{
    CRange *pRange = NULL;
    if (!range || FAILED(range->QueryInterface(IID_PRIV_CRANGE, (void **)&pRange)))
        return NULL;
    return pRange;
}

CRange *CRange::_Clone()
{
    return new(cicNoThrow) CRange(m_context, m_anchorStart, m_anchorEnd,
                                  m_gravityStart, m_gravityEnd, FALSE);
}

HRESULT CRange::_GetAnchor(TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *pPos)
{
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    CRange *pOther = FromRange(pWith);
    if (!pOther || pOther->m_context != m_context)
        return E_INVALIDARG;
    if (aPos == TF_ANCHOR_START)
        *pPos = pOther->m_anchorStart;
    else if (aPos == TF_ANCHOR_END)
        *pPos = pOther->m_anchorEnd;
    else
        return E_INVALIDARG;
    return S_OK;
}

HRESULT CRange::_IsEqualX(TfEditCookie ec, BOOL bEnd, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual)
{
    if (!pfEqual)
        return E_INVALIDARG;
    *pfEqual = FALSE;
    LONG pos;
    HRESULT hr = _GetAnchor(ec, pWith, aPos, &pos);
    if (FAILED(hr))
        return hr;
    *pfEqual = ((bEnd ? m_anchorEnd : m_anchorStart) == pos);
    return S_OK;
}

HRESULT CRange::_CompareX(
    TfEditCookie ec,
    BOOL bEnd,
    ITfRange *pWidth,
    TfAnchor aPos,
    LONG *plResult)
{
    if (!plResult)
        return E_INVALIDARG;
    *plResult = 0;
    LONG pos;
    HRESULT hr = _GetAnchor(ec, pWidth, aPos, &pos);
    if (FAILED(hr))
        return hr;
    LONG mine = bEnd ? m_anchorEnd : m_anchorStart;
    *plResult = (mine < pos) ? -1 : ((mine > pos) ? 1 : 0);
    return S_OK;
}

HRESULT CRange::_Shift(TfEditCookie ec, BOOL bEnd, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt)
{
    if (!pcch)
        return E_INVALIDARG;
    *pcch = 0;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    ITextStoreACP *store = Context_GetTextStore(m_context);
    if (!store)
        return E_FAIL;

    LONG docEnd = 0;
    hr = store->GetEndACP(&docEnd);
    if (FAILED(hr))
        return hr;

    LONG from = bEnd ? m_anchorEnd : m_anchorStart;
    LONG to;
    if (cchReq > 0)
        to = (cchReq > docEnd - from) ? docEnd : from + cchReq;
    else
        to = (cchReq < -from) ? 0 : from + cchReq;
    if (to > docEnd)
        to = docEnd;

    if (pHalt && pHalt->pHaltRange)
    {
        CRange *pHaltRange = FromRange(pHalt->pHaltRange);
        if (!pHaltRange || pHaltRange->m_context != m_context)
            return E_INVALIDARG;
        LONG halt = (pHalt->aHaltPos == TF_ANCHOR_END) ? pHaltRange->m_anchorEnd : pHaltRange->m_anchorStart;
        if (cchReq > 0 && halt > from && halt < to)
            to = halt;
        else if (cchReq < 0 && halt < from && halt > to)
            to = halt;
    }

    *pcch = to - from;
    if (bEnd)
    {
        m_anchorEnd = to;
        if (m_anchorStart > m_anchorEnd)
            m_anchorStart = m_anchorEnd;
    }
    else
    {
        m_anchorStart = to;
        if (m_anchorEnd < m_anchorStart)
            m_anchorEnd = m_anchorStart;
    }
    return S_OK;
}

HRESULT CRange::TF_SELECTION_to_TS_SELECTION_ACP(const TF_SELECTION *tf, TS_SELECTION_ACP *tsAcp)
{
    if (!tf || !tsAcp || !tf->range)
    {
        ERR("E_INVALIDARG: %p, %p\n", tf, tsAcp);
        return E_INVALIDARG;
    }

    CRange *This = FromRange(tf->range);
    if (!This)
        return E_INVALIDARG;
    tsAcp->acpStart = This->m_anchorStart;
    tsAcp->acpEnd = This->m_anchorEnd;
    tsAcp->style.ase = static_cast<TsActiveSelEnd>(tf->style.ase);
    tsAcp->style.fInterimChar = tf->style.fInterimChar;
    return S_OK;
}

STDMETHODIMP CRange::QueryInterface(REFIID riid, void **ppvObj)
{
    if (riid == IID_PRIV_CRANGE)
    {
        *ppvObj = this;
        return S_OK; // No AddRef
    }

    if (riid == IID_IUnknown || riid == IID_ITfRange || riid == IID_ITfRangeACP)
        *ppvObj = static_cast<ITfRangeACP *>(this);
    else if (riid == IID_ITfRangeAnchor)
        *ppvObj = static_cast<ITfRangeAnchor *>(this);
    else if (riid == IID_ITfSource)
        *ppvObj = static_cast<ITfSource *>(this);
    else
        *ppvObj = NULL;
 
    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    WARN("unsupported interface: %s\n", debugstr_guid(&riid));
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CRange::AddRef()
{
    TRACE("%p -> ()\n", this);
    return ::InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) CRange::Release()
{
    TRACE("%p -> ()\n", this);
    ULONG ret = InterlockedDecrement(&m_cRefs);
    if (!ret)
        delete this;
    return ret;
}

STDMETHODIMP CRange::GetText(
    _In_ TfEditCookie ec,
    _In_ DWORD dwFlags,
    _Out_ WCHAR *pchText,
    _In_ ULONG cchMax,
    _Out_ ULONG *pcch)
{
    if (!pcch || (!pchText && cchMax))
        return E_INVALIDARG;
    *pcch = 0;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    ITextStoreACP *store = Context_GetTextStore(m_context);
    if (!store)
        return E_FAIL;

    LONG end = (dwFlags & TF_TF_IGNOREEND) ? -1 : m_anchorEnd;
    LONG pos = m_anchorStart;
    ULONG copied = 0;
    while (copied < cchMax && (end < 0 || pos < end))
    {
        ULONG got = 0, runs = 0;
        LONG next = pos;
        hr = store->GetText(pos, end, pchText + copied, cchMax - copied, &got, NULL, 0, &runs, &next);
        if (FAILED(hr))
            return hr;
        copied += got;
        if (next <= pos)
            break;
        pos = next;
    }

    if (dwFlags & TF_TF_MOVESTART)
    {
        m_anchorStart = pos;
        if (m_anchorEnd < m_anchorStart)
            m_anchorEnd = m_anchorStart;
    }
    *pcch = copied;
    return S_OK;
}

STDMETHODIMP CRange::SetText(
    _In_ TfEditCookie ec,
    _In_ DWORD dwFlags,
    _In_ const WCHAR *pchText,
    _In_ LONG cch)
{
    if (!pchText && cch)
        return E_INVALIDARG;
    HRESULT hr = Context_CheckLock(m_context, ec, TRUE);
    if (FAILED(hr))
        return hr;
    if (cch < 0)
        cch = lstrlenW(pchText);

    TS_TEXTCHANGE change;
    hr = Context_ReplaceText(m_context, (dwFlags & TF_ST_CORRECTION) ? TS_ST_CORRECTION : 0,
                             m_anchorStart, m_anchorEnd, pchText, cch, &change);
    if (FAILED(hr))
        return hr;
    m_anchorStart = change.acpStart;
    m_anchorEnd = change.acpNewEnd;
    return S_OK;
}

STDMETHODIMP CRange::GetFormattedText(
    _In_ TfEditCookie ec,
    _Out_ IDataObject **ppDataObject)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::GetEmbedded(
    _In_ TfEditCookie ec,
    _In_ REFGUID rguidService,
    _In_ REFIID riid,
    _Out_ IUnknown **ppunk)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::InsertEmbedded(
    _In_ TfEditCookie ec,
    _In_ DWORD dwFlags,
    _In_ IDataObject *pDataObject)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::ShiftStart(
    _In_ TfEditCookie ec,
    _In_ LONG cchReq,
    _Out_ LONG *pcch,
    _In_ const TF_HALTCOND *pHalt)
{
    return _Shift(ec, FALSE, cchReq, pcch, pHalt);
}

STDMETHODIMP CRange::ShiftEnd(
    _In_ TfEditCookie ec,
    _In_ LONG cchReq,
    _Out_ LONG *pcch,
    _In_ const TF_HALTCOND *pHalt)
{
    return _Shift(ec, TRUE, cchReq, pcch, pHalt);
}

STDMETHODIMP CRange::ShiftStartToRange(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pRange,
    _In_ TfAnchor aPos)
{
    LONG pos;
    HRESULT hr = _GetAnchor(ec, pRange, aPos, &pos);
    if (FAILED(hr))
        return hr;
    m_anchorStart = pos;
    if (m_anchorEnd < m_anchorStart)
        m_anchorEnd = m_anchorStart;
    return S_OK;
}

STDMETHODIMP CRange::ShiftEndToRange(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pRange,
    _In_ TfAnchor aPos)
{
    LONG pos;
    HRESULT hr = _GetAnchor(ec, pRange, aPos, &pos);
    if (FAILED(hr))
        return hr;
    m_anchorEnd = pos;
    if (m_anchorStart > m_anchorEnd)
        m_anchorStart = m_anchorEnd;
    return S_OK;
}

STDMETHODIMP CRange::ShiftStartRegion(
    _In_ TfEditCookie ec,
    _In_ TfShiftDir dir,
    _Out_ BOOL *pfNoRegion)
{
    if (!pfNoRegion)
        return E_INVALIDARG;
    *pfNoRegion = TRUE;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    return FAILED(hr) ? hr : S_OK;
}

STDMETHODIMP CRange::ShiftEndRegion(
    _In_ TfEditCookie ec,
    _In_ TfShiftDir dir,
    _Out_ BOOL *pfNoRegion)
{
    if (!pfNoRegion)
        return E_INVALIDARG;
    *pfNoRegion = TRUE;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    return FAILED(hr) ? hr : S_OK;
}

STDMETHODIMP CRange::IsEmpty(
    _In_ TfEditCookie ec,
    _Out_ BOOL *pfEmpty)
{
    if (!pfEmpty)
        return E_INVALIDARG;
    *pfEmpty = FALSE;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    *pfEmpty = (m_anchorStart == m_anchorEnd);
    return S_OK;
}

STDMETHODIMP CRange::Collapse(
    _In_ TfEditCookie ec,
    _In_ TfAnchor aPos)
{
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    if (aPos == TF_ANCHOR_START)
        m_anchorEnd = m_anchorStart;
    else if (aPos == TF_ANCHOR_END)
        m_anchorStart = m_anchorEnd;
    else
        return E_INVALIDARG;
    return S_OK;
}

STDMETHODIMP CRange::IsEqualStart(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pWith,
    _In_ TfAnchor aPos,
    _Out_ BOOL *pfEqual)
{
    return _IsEqualX(ec, FALSE, pWith, aPos, pfEqual);
}

STDMETHODIMP CRange::IsEqualEnd(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pWith,
    _In_ TfAnchor aPos,
    _Out_ BOOL *pfEqual)
{
    return _IsEqualX(ec, TRUE, pWith, aPos, pfEqual);
}

STDMETHODIMP CRange::CompareStart(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pWith,
    _In_ TfAnchor aPos,
    _Out_ LONG *plResult)
{
    return _CompareX(ec, FALSE, pWith, aPos, plResult);
}

STDMETHODIMP CRange::CompareEnd(
    _In_ TfEditCookie ec,
    _In_ ITfRange *pWith,
    _In_ TfAnchor aPos,
    _Out_ LONG *plResult)
{
    return _CompareX(ec, TRUE, pWith, aPos, plResult);
}

STDMETHODIMP CRange::AdjustForInsert(
    _In_ TfEditCookie ec,
    _In_ ULONG cchInsert,
    _Out_ BOOL *pfInsertOk)
{
    if (!pfInsertOk)
        return E_INVALIDARG;
    *pfInsertOk = FALSE;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    ITextStoreACP *store = Context_GetTextStore(m_context);
    if (!store)
        return E_FAIL;
    LONG start = m_anchorStart, end = m_anchorEnd;
    hr = store->QueryInsert(m_anchorStart, m_anchorEnd, cchInsert, &start, &end);
    if (FAILED(hr))
        return hr;
    if (start < 0)
        return S_OK;
    SetAnchors(start, end);
    *pfInsertOk = TRUE;
    return S_OK;
}

STDMETHODIMP CRange::GetGravity(
    _Out_ TfGravity *pgStart,
    _Out_ TfGravity *pgEnd)
{
    if (!pgStart || !pgEnd)
        return E_INVALIDARG;
    *pgStart = m_gravityStart;
    *pgEnd = m_gravityEnd;
    return S_OK;
}

STDMETHODIMP CRange::SetGravity(
    _In_ TfEditCookie ec,
    _In_ TfGravity gStart,
    _In_ TfGravity gEnd)
{
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    if ((gStart != TF_GRAVITY_BACKWARD && gStart != TF_GRAVITY_FORWARD) ||
        (gEnd != TF_GRAVITY_BACKWARD && gEnd != TF_GRAVITY_FORWARD))
        return E_INVALIDARG;
    m_gravityStart = gStart;
    m_gravityEnd = gEnd;
    return S_OK;
}

STDMETHODIMP CRange::Clone(
    _Out_ ITfRange **ppClone)
{
    TRACE("%p\n", ppClone);

    if (!ppClone)
        return E_INVALIDARG;

    CRange *pCloned = _Clone();
    if (!pCloned)
        return E_OUTOFMEMORY;

    *ppClone = static_cast<ITfRangeACP *>(pCloned);
    return S_OK;
}

STDMETHODIMP CRange::GetContext(
    _Out_ ITfContext **ppContext)
{
    if (!ppContext) return E_INVALIDARG;
    *ppContext = m_context;
    if (m_context) m_context->AddRef();
    return S_OK;
}

STDMETHODIMP CRange::GetExtent(_Out_ LONG *pacpAnchor, _Out_ LONG *pcch)
{
    if (!pacpAnchor || !pcch) return E_INVALIDARG;
    *pacpAnchor = m_anchorStart;
    *pcch = m_anchorEnd - m_anchorStart;
    return S_OK;
}

STDMETHODIMP CRange::SetExtent(_In_ LONG acpAnchor, _In_ LONG cch)
{
    if (acpAnchor < 0 || cch < 0 || cch > MAXLONG - acpAnchor) return E_INVALIDARG;
    m_anchorStart = acpAnchor;
    m_anchorEnd = acpAnchor + cch;
    return S_OK;
}

STDMETHODIMP CRange::GetExtent(_Out_ IAnchor **ppStart, _Out_ IAnchor **ppEnd)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::SetExtent(_In_ IAnchor *pAnchorStart, _In_ IAnchor *pAnchorEnd)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::AdviseSink(
    _In_ REFIID riid,
    _In_ IUnknown *punk,
    _Out_ DWORD *pdwCookie)
{
    FIXME("\n");
    return E_NOTIMPL;
}

STDMETHODIMP CRange::UnadviseSink(
    _In_ DWORD dwCookie)
{
    FIXME("\n");
    return E_NOTIMPL;
}

////////////////////////////////////////////////////////////////////////////

HRESULT Range_CreateInternal(
    ITfContext *context,
    LONG anchorStart,
    LONG anchorEnd,
    TfGravity gravityStart,
    TfGravity gravityEnd,
    BOOL fWeakContext,
    CRange **ppOut)
{
    *ppOut = new(cicNoThrow) CRange(context, anchorStart, anchorEnd, gravityStart, gravityEnd, fWeakContext);
    return *ppOut ? S_OK : E_OUTOFMEMORY;
}

EXTERN_C
HRESULT
Range_Constructor(ITfContext *context, DWORD anchorStart, DWORD anchorEnd, ITfRange **ppOut)
{
    CRange *This = new(cicNoThrow) CRange(context, anchorStart, anchorEnd);
    if (!This)
        return E_OUTOFMEMORY;

    *ppOut = static_cast<ITfRangeACP *>(This);
    return S_OK;
}

/* Internal conversion functions */

EXTERN_C
HRESULT TF_SELECTION_to_TS_SELECTION_ACP(const TF_SELECTION *tf, TS_SELECTION_ACP *tsAcp)
{
    return CRange::TF_SELECTION_to_TS_SELECTION_ACP(tf, tsAcp);
}
