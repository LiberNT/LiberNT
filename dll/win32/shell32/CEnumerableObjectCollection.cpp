/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object collection used to build jump list categories and tasks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

CEnumerableObjectCollection::~CEnumerableObjectCollection()
{
    for (int i = 0; i < m_Items.GetSize(); i++)
        m_Items[i]->Release();
}

STDMETHODIMP
CEnumerableObjectCollection::GetCount(UINT *pcObjects)
{
    if (!pcObjects)
        return E_POINTER;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    *pcObjects = (UINT)m_Items.GetSize();
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::GetAt(UINT uiIndex, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    if (uiIndex >= (UINT)m_Items.GetSize())
        return E_INVALIDARG;
    return m_Items[uiIndex]->QueryInterface(riid, ppv);
}

STDMETHODIMP
CEnumerableObjectCollection::AddObject(IUnknown *punk)
{
    if (!punk)
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    if (!m_Items.Add(punk))
        return E_OUTOFMEMORY;
    punk->AddRef();
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::AddFromArray(IObjectArray *poaSource)
{
    CComPtr<IUnknown> item;
    UINT count, i;
    HRESULT hr;

    if (!poaSource)
        return E_INVALIDARG;

    hr = poaSource->GetCount(&count);
    if (FAILED(hr))
        return hr;

    for (i = 0; i < count; i++)
    {
        item.Release();
        hr = poaSource->GetAt(i, IID_PPV_ARG(IUnknown, &item));
        if (FAILED(hr))
            return hr;
        hr = AddObject(item);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::RemoveObjectAt(UINT uiIndex)
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);

    if (uiIndex >= (UINT)m_Items.GetSize())
        return E_INVALIDARG;
    m_Items[uiIndex]->Release();
    m_Items.RemoveAt(uiIndex);
    if (m_Position > uiIndex)
        m_Position--;
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::Clear()
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);

    for (int i = 0; i < m_Items.GetSize(); i++)
        m_Items[i]->Release();
    m_Items.RemoveAll();
    m_Position = 0;
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::Next(ULONG celt, REFIID riid, void **rgelt, ULONG *pceltFetched)
{
    ULONG fetched = 0;
    HRESULT hr = S_OK;

    if (pceltFetched)
        *pceltFetched = 0;
    if (!rgelt || (celt > 1 && !pceltFetched))
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    while (fetched < celt && m_Position < (ULONG)m_Items.GetSize())
    {
        hr = m_Items[m_Position]->QueryInterface(riid, &rgelt[fetched]);
        if (FAILED(hr))
            break;
        m_Position++;
        fetched++;
    }

    if (FAILED(hr))
    {
        while (fetched)
        {
            fetched--;
            ((IUnknown *)rgelt[fetched])->Release();
            rgelt[fetched] = NULL;
        }
        return hr;
    }

    if (pceltFetched)
        *pceltFetched = fetched;
    return fetched == celt ? S_OK : S_FALSE;
}

STDMETHODIMP
CEnumerableObjectCollection::Skip(ULONG celt)
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    ULONG remaining = (ULONG)m_Items.GetSize() - m_Position;

    if (celt > remaining)
    {
        m_Position = (ULONG)m_Items.GetSize();
        return S_FALSE;
    }
    m_Position += celt;
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::Reset()
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);

    m_Position = 0;
    return S_OK;
}

STDMETHODIMP
CEnumerableObjectCollection::Clone(IEnumObjects **ppenum)
{
    CComPtr<IObjectCollection> clone;
    HRESULT hr;

    if (!ppenum)
        return E_POINTER;
    *ppenum = NULL;

    hr = ShellObjectCreator<CEnumerableObjectCollection>(IID_PPV_ARG(IObjectCollection, &clone));
    if (FAILED(hr))
        return hr;
    hr = clone->AddFromArray(this);
    if (FAILED(hr))
        return hr;

    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
        static_cast<CEnumerableObjectCollection *>(clone.p)->m_Position = m_Position;
    }
    return clone->QueryInterface(IID_PPV_ARG(IEnumObjects, ppenum));
}
