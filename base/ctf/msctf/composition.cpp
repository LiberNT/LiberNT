/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfComposition implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "range.h"
#include "composition.h"


CComposition::CComposition(ITfContext *context, CRange *range, ITfCompositionSink *sink, REFCLSID clsidOwner)
    : m_refs(1)
    , m_context(context)
    , m_range(range)
    , m_sink(sink)
    , m_clsidOwner(clsidOwner)
    , m_ended(FALSE)
{
    m_context->AddRef();
    if (m_sink)
        m_sink->AddRef();
}

CComposition::~CComposition()
{
    MarkEnded();
    m_range->Release();
    m_context->Release();
}

void CComposition::MarkEnded()
{
    m_ended = TRUE;
    if (m_sink)
    {
        m_sink->Release();
        m_sink = NULL;
    }
}

CComposition *CComposition::FromView(IUnknown *punk)
{
    CComposition *composition = NULL;
    if (!punk || FAILED(punk->QueryInterface(IID_PRIV_CCOMPOSITION, (void **)&composition)))
        return NULL;
    return composition;
}

STDMETHODIMP CComposition::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid == IID_PRIV_CCOMPOSITION)
    {
        *ppvObj = this;
        return S_OK;
    }
    if (riid == IID_IUnknown || riid == IID_ITfComposition)
        *ppvObj = static_cast<ITfComposition *>(this);
    else if (riid == IID_ITfCompositionView)
        *ppvObj = static_cast<ITfCompositionView *>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CComposition::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CComposition::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CComposition::GetRange(ITfRange **ppRange)
{
    if (!ppRange)
        return E_INVALIDARG;
    *ppRange = NULL;
    if (m_ended)
        return E_UNEXPECTED;
    return Range_Constructor(m_context, m_range->Start(), m_range->End(), ppRange);
}

STDMETHODIMP CComposition::ShiftStart(TfEditCookie ecWrite, ITfRange *pNewStart)
{
    if (m_ended)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ecWrite, TRUE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pNewStart);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;
    LONG start = range->Start();
    m_range->SetAnchors(start, max(start, m_range->End()));
    return Context_OnCompositionUpdated(m_context, this);
}

STDMETHODIMP CComposition::ShiftEnd(TfEditCookie ecWrite, ITfRange *pNewEnd)
{
    if (m_ended)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ecWrite, TRUE);
    if (FAILED(hr))
        return hr;
    CRange *range = CRange::FromRange(pNewEnd);
    if (!range || range->Context() != m_context)
        return E_INVALIDARG;
    LONG end = range->End();
    m_range->SetAnchors(min(m_range->Start(), end), end);
    return Context_OnCompositionUpdated(m_context, this);
}

STDMETHODIMP CComposition::EndComposition(TfEditCookie ecWrite)
{
    if (m_ended)
        return E_UNEXPECTED;
    HRESULT hr = Context_CheckLock(m_context, ecWrite, TRUE);
    if (FAILED(hr))
        return hr;
    MarkEnded();
    return Context_OnCompositionEnded(m_context, this);
}

STDMETHODIMP CComposition::GetOwnerClsid(CLSID *pclsid)
{
    if (!pclsid)
        return E_INVALIDARG;
    *pclsid = m_clsidOwner;
    return S_OK;
}

CEnumCompositionView::CEnumCompositionView()
    : m_refs(1)
    , m_views(NULL)
    , m_count(0)
    , m_capacity(0)
    , m_index(0)
{
}

CEnumCompositionView::~CEnumCompositionView()
{
    for (ULONG i = 0; i < m_count; ++i)
        m_views[i]->Release();
    cicMemFree(m_views);
}

HRESULT CEnumCompositionView::Add(ITfCompositionView *view)
{
    if (m_count == m_capacity)
    {
        ULONG capacity = m_capacity ? m_capacity * 2 : 4;
        ITfCompositionView **views =
            static_cast<ITfCompositionView **>(cicMemReAlloc(m_views, capacity * sizeof(*views)));
        if (!views)
            return E_OUTOFMEMORY;
        m_views = views;
        m_capacity = capacity;
    }
    view->AddRef();
    m_views[m_count++] = view;
    return S_OK;
}

STDMETHODIMP CEnumCompositionView::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    *ppvObj = NULL;
    if (riid != IID_IUnknown && riid != IID_IEnumITfCompositionView)
        return E_NOINTERFACE;
    *ppvObj = static_cast<IEnumITfCompositionView *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CEnumCompositionView::AddRef()
{
    return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) CEnumCompositionView::Release()
{
    ULONG refs = InterlockedDecrement(&m_refs);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP CEnumCompositionView::Clone(IEnumITfCompositionView **ppEnum)
{
    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;
    CEnumCompositionView *pEnum = new(cicNoThrow) CEnumCompositionView();
    if (!pEnum)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < m_count; ++i)
    {
        HRESULT hr = pEnum->Add(m_views[i]);
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

STDMETHODIMP CEnumCompositionView::Next(ULONG ulCount, ITfCompositionView **rgCompositionView, ULONG *pcFetched)
{
    if (!rgCompositionView || (ulCount > 1 && !pcFetched))
        return E_INVALIDARG;
    ULONG fetched = 0;
    while (fetched < ulCount && m_index < m_count)
    {
        rgCompositionView[fetched] = m_views[m_index++];
        rgCompositionView[fetched]->AddRef();
        ++fetched;
    }
    if (pcFetched)
        *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumCompositionView::Reset()
{
    m_index = 0;
    return S_OK;
}

STDMETHODIMP CEnumCompositionView::Skip(ULONG ulCount)
{
    if (ulCount > m_count - m_index)
    {
        m_index = m_count;
        return S_FALSE;
    }
    m_index += ulCount;
    return S_OK;
}
