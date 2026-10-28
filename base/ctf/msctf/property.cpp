/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfProperty implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "range.h"
#include "property.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(msctf);

CProperty::CProperty(ITfContext *context, REFGUID guid)
    : m_refs(1)
    , m_context(context)
    , m_guid(guid)
    , m_spans(NULL)
    , m_count(0)
    , m_capacity(0)
{
}

CProperty::~CProperty()
{
    Detach();
}

void CProperty::Detach()
{
    while (m_count)
        RemoveSpan(m_count - 1);
    cicMemFree(m_spans);
    m_spans = NULL;
    m_capacity = 0;
    m_context = NULL;
}

void CProperty::RemoveSpan(ULONG index)
{
    m_spans[index].range->Release();
    VariantClear(&m_spans[index].value);
    MoveMemory(&m_spans[index], &m_spans[index + 1], (m_count - index - 1) * sizeof(*m_spans));
    --m_count;
}

void CProperty::Compact()
{
    for (ULONG i = m_count; i > 0; --i)
    {
        CRange *range = m_spans[i - 1].range;
        if (range->Start() >= range->End())
            RemoveSpan(i - 1);
    }
}

HRESULT CProperty::AddSpan(LONG start, LONG end, const VARIANT *value)
{
    if (start >= end)
        return S_OK;
    if (m_count == m_capacity)
    {
        ULONG capacity = m_capacity ? m_capacity * 2 : 8;
        PropertySpan *spans = static_cast<PropertySpan *>(cicMemReAlloc(m_spans, capacity * sizeof(*spans)));
        if (!spans)
            return E_OUTOFMEMORY;
        m_spans = spans;
        m_capacity = capacity;
    }

    PropertySpan *span = &m_spans[m_count];
    VariantInit(&span->value);
    HRESULT hr = VariantCopy(&span->value, value);
    if (FAILED(hr))
        return hr;
    hr = Range_CreateInternal(m_context, start, end, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD, TRUE, &span->range);
    if (FAILED(hr))
    {
        VariantClear(&span->value);
        return hr;
    }
    ++m_count;
    return S_OK;
}

HRESULT CProperty::ClearSpan(LONG start, LONG end)
{
    if (!m_context)
        return E_UNEXPECTED;
    Compact();
    if (start >= end)
        return S_OK;
    for (ULONG i = m_count; i > 0; --i)
    {
        PropertySpan *span = &m_spans[i - 1];
        LONG spanStart = span->range->Start(), spanEnd = span->range->End();
        if (spanStart >= end || spanEnd <= start)
            continue;
        if (spanStart >= start && spanEnd <= end)
        {
            RemoveSpan(i - 1);
        }
        else if (spanStart < start && spanEnd > end)
        {
            VARIANT value;
            VariantInit(&value);
            HRESULT hr = VariantCopy(&value, &span->value);
            if (FAILED(hr))
                return hr;
            span->range->SetAnchors(spanStart, start);
            hr = AddSpan(end, spanEnd, &value);
            VariantClear(&value);
            if (FAILED(hr))
                return hr;
        }
        else if (spanStart < start)
        {
            span->range->SetAnchors(spanStart, start);
        }
        else
        {
            span->range->SetAnchors(end, spanEnd);
        }
    }
    return S_OK;
}

STDMETHODIMP CProperty::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid != IID_IUnknown && riid != IID_ITfReadOnlyProperty && riid != IID_ITfProperty)
        return E_NOINTERFACE;
    *ppvObj = static_cast<ITfProperty *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CProperty::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CProperty::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CProperty::GetType(GUID *pguid)
{
    if (!pguid)
        return E_INVALIDARG;
    *pguid = m_guid;
    return S_OK;
}

STDMETHODIMP CProperty::EnumRanges(TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange)
{
    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;
    if (!m_context)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;

    LONG targetStart = 0, targetEnd = MAXLONG;
    if (pTargetRange)
    {
        CRange *target = CRange::FromRange(pTargetRange);
        if (!target || target->Context() != m_context)
            return E_INVALIDARG;
        targetStart = target->Start();
        targetEnd = target->End();
    }

    Compact();
    CEnumRanges *pEnum = new(cicNoThrow) CEnumRanges();
    if (!pEnum)
        return E_OUTOFMEMORY;
    ULONG *order = static_cast<ULONG *>(cicMemAlloc(m_count * sizeof(ULONG) + 1));
    if (!order)
    {
        pEnum->Release();
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < m_count; ++i)
    {
        ULONG j = i;
        while (j && m_spans[order[j - 1]].range->Start() > m_spans[i].range->Start())
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = i;
    }
    LONG previousEnd = -1;
    for (ULONG k = 0; k < 2 * m_count; ++k)
    {
        LONG start, end;
        if (k & 1)
        {
            start = max(m_spans[order[k / 2]].range->Start(), targetStart);
            end = min(m_spans[order[k / 2]].range->End(), targetEnd);
            if (start < end)
                previousEnd = end;
        }
        else
        {
            if (previousEnd < 0)
                continue;
            start = previousEnd;
            end = min(m_spans[order[k / 2]].range->Start(), targetEnd);
        }
        if (start >= end)
            continue;
        ITfRange *range = NULL;
        hr = Range_Constructor(m_context, start, end, &range);
        if (SUCCEEDED(hr))
        {
            hr = pEnum->Add(range);
            range->Release();
        }
        if (FAILED(hr))
        {
            cicMemFree(order);
            pEnum->Release();
            return hr;
        }
    }
    cicMemFree(order);
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP CProperty::GetValue(TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue)
{
    if (!pvarValue)
        return E_INVALIDARG;
    VariantInit(pvarValue);
    if (!m_context)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pRange);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;

    Compact();
    LONG start = range->Start(), end = range->End();
    for (ULONG i = 0; i < m_count; ++i)
    {
        LONG spanStart = m_spans[i].range->Start(), spanEnd = m_spans[i].range->End();
        if (spanStart <= start && end <= spanEnd && (start < end || start < spanEnd))
            return VariantCopy(pvarValue, &m_spans[i].value);
    }
    return S_FALSE;
}

STDMETHODIMP CProperty::GetContext(ITfContext **ppContext)
{
    if (!ppContext)
        return E_INVALIDARG;
    *ppContext = m_context;
    if (!m_context)
        return E_UNEXPECTED;
    m_context->AddRef();
    return S_OK;
}

STDMETHODIMP CProperty::FindRange(TfEditCookie ec, ITfRange *pRange, ITfRange **ppRange, TfAnchor aPos)
{
    if (ppRange)
        *ppRange = NULL;
    if (!m_context)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pRange);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;

    LONG pos;
    if (aPos == TF_ANCHOR_START)
        pos = range->Start();
    else if (aPos == TF_ANCHOR_END)
        pos = range->End();
    else
        return E_INVALIDARG;

    Compact();
    for (ULONG i = 0; i < m_count; ++i)
    {
        LONG spanStart = m_spans[i].range->Start(), spanEnd = m_spans[i].range->End();
        BOOL found = (aPos == TF_ANCHOR_START) ? (spanStart <= pos && pos < spanEnd)
                                               : (spanStart < pos && pos <= spanEnd);
        if (!found)
            continue;
        if (!ppRange)
            return S_OK;
        return Range_Constructor(m_context, spanStart, spanEnd, ppRange);
    }
    return S_FALSE;
}

STDMETHODIMP CProperty::SetValueStore(TfEditCookie ec, ITfRange *pRange, ITfPropertyStore *pPropStore)
{
    FIXME("(%p) %x %p %p\n", this, ec, pRange, pPropStore);
    return E_NOTIMPL;
}

STDMETHODIMP CProperty::SetValue(TfEditCookie ec, ITfRange *pRange, const VARIANT *pvarValue)
{
    if (!pvarValue)
        return E_INVALIDARG;
    if (!m_context)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ec, TRUE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pRange);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;

    LONG start = range->Start(), end = range->End();
    hr = ClearSpan(start, end);
    if (FAILED(hr))
        return hr;
    if (V_VT(pvarValue) == VT_EMPTY)
        return S_OK;
    return AddSpan(start, end, pvarValue);
}

STDMETHODIMP CProperty::Clear(TfEditCookie ec, ITfRange *pRange)
{
    if (!m_context)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ec, TRUE);
    if (FAILED(hr))
        return hr;
    if (!pRange)
    {
        while (m_count)
            RemoveSpan(m_count - 1);
        return S_OK;
    }
    CRange *range = CRange::FromRange(pRange);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;
    return ClearSpan(range->Start(), range->End());
}

CEnumRanges::CEnumRanges()
    : m_refs(1)
    , m_ranges(NULL)
    , m_count(0)
    , m_capacity(0)
    , m_index(0)
{
}

CEnumRanges::~CEnumRanges()
{
    for (ULONG i = 0; i < m_count; ++i)
        m_ranges[i]->Release();
    cicMemFree(m_ranges);
}

HRESULT CEnumRanges::Add(ITfRange *range)
{
    if (m_count == m_capacity)
    {
        ULONG capacity = m_capacity ? m_capacity * 2 : 8;
        ITfRange **ranges = static_cast<ITfRange **>(cicMemReAlloc(m_ranges, capacity * sizeof(*ranges)));
        if (!ranges)
            return E_OUTOFMEMORY;
        m_ranges = ranges;
        m_capacity = capacity;
    }
    range->AddRef();
    m_ranges[m_count++] = range;
    return S_OK;
}

STDMETHODIMP CEnumRanges::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid != IID_IUnknown && riid != IID_IEnumTfRanges)
        return E_NOINTERFACE;
    *ppvObj = static_cast<IEnumTfRanges *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CEnumRanges::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CEnumRanges::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CEnumRanges::Clone(IEnumTfRanges **ppEnum)
{
    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;
    CEnumRanges *pEnum = new(cicNoThrow) CEnumRanges();
    if (!pEnum)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < m_count; ++i)
    {
        ITfRange *range = NULL;
        HRESULT hr = m_ranges[i]->Clone(&range);
        if (SUCCEEDED(hr))
        {
            hr = pEnum->Add(range);
            range->Release();
        }
        if (FAILED(hr))
        {
            pEnum->Release();
            return hr;
        }
    }
    pEnum->m_index = m_index;
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP CEnumRanges::Next(ULONG ulCount, ITfRange **ppRange, ULONG *pcFetched)
{
    if (!ppRange || (ulCount > 1 && !pcFetched))
        return E_INVALIDARG;
    ULONG fetched = 0;
    while (fetched < ulCount && m_index < m_count)
    {
        HRESULT hr = m_ranges[m_index]->Clone(&ppRange[fetched]);
        if (FAILED(hr))
        {
            while (fetched)
                ppRange[--fetched]->Release();
            return hr;
        }
        ++fetched;
        ++m_index;
    }
    if (pcFetched)
        *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumRanges::Reset()
{
    m_index = 0;
    return S_OK;
}

STDMETHODIMP CEnumRanges::Skip(ULONG ulCount)
{
    if (ulCount > m_count - m_index)
    {
        m_index = m_count;
        return S_FALSE;
    }
    m_index += ulCount;
    return S_OK;
}

CAppProperty::CAppProperty(ITfContext *context, REFGUID guid)
    : m_refs(1)
    , m_context(context)
    , m_guid(guid)
{
    m_context->AddRef();
}

CAppProperty::~CAppProperty()
{
    m_context->Release();
}

STDMETHODIMP CAppProperty::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid != IID_IUnknown && riid != IID_ITfReadOnlyProperty)
        return E_NOINTERFACE;
    *ppvObj = static_cast<ITfReadOnlyProperty *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CAppProperty::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CAppProperty::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CAppProperty::GetType(GUID *pguid)
{
    if (!pguid)
        return E_INVALIDARG;
    *pguid = m_guid;
    return S_OK;
}

STDMETHODIMP CAppProperty::EnumRanges(TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange)
{
    FIXME("(%p) %x %p %p\n", this, ec, ppEnum, pTargetRange);
    if (!ppEnum || !pTargetRange)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

STDMETHODIMP CAppProperty::GetValue(TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue)
{
    if (!pvarValue)
        return E_INVALIDARG;
    VariantInit(pvarValue);
    HRESULT hr = Context_CheckLock(m_context, ec, FALSE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pRange);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;
    ITextStoreACP *store = Context_GetTextStore(m_context);
    if (!store)
        return E_FAIL;

    hr = store->RequestAttrsAtPosition(range->Start(), 1, &m_guid, 0);
    if (FAILED(hr))
        return hr;
    TS_ATTRVAL value;
    ULONG fetched = 0;
    VariantInit(&value.varValue);
    hr = store->RetrieveRequestedAttrs(1, &value, &fetched);
    if (FAILED(hr))
        return hr;
    if (fetched == 1)
        *pvarValue = value.varValue;
    return S_OK;
}

STDMETHODIMP CAppProperty::GetContext(ITfContext **ppContext)
{
    if (!ppContext)
        return E_INVALIDARG;
    *ppContext = m_context;
    m_context->AddRef();
    return S_OK;
}
