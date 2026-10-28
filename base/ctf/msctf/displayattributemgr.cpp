/*
 * PROJECT:     ReactOS CTF
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     ITfDisplayAttributeMgr implementation
 * COPYRIGHT:   Copyright 2010 CodeWeavers, Aric Stewart
 *              Copyright 2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "precomp.h"
#include "displayattributemgr.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(msctf);

////////////////////////////////////////////////////////////////////////////

class CEnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo
{
public:
    CEnumDisplayAttributeInfo() : m_refs(1), m_infos(NULL), m_count(0), m_capacity(0), m_index(0) { }

    HRESULT Add(ITfDisplayAttributeInfo *info)
    {
        if (m_count == m_capacity)
        {
            ULONG capacity = m_capacity ? m_capacity * 2 : 8;
            ITfDisplayAttributeInfo **infos =
                static_cast<ITfDisplayAttributeInfo **>(cicMemReAlloc(m_infos, capacity * sizeof(*infos)));
            if (!infos)
                return E_OUTOFMEMORY;
            m_infos = infos;
            m_capacity = capacity;
        }
        info->AddRef();
        m_infos[m_count++] = info;
        return S_OK;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_IEnumTfDisplayAttributeInfo)
            return E_NOINTERFACE;
        *ppvObj = static_cast<IEnumTfDisplayAttributeInfo *>(this);
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

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo **ppEnum) override
    {
        if (!ppEnum)
            return E_INVALIDARG;
        *ppEnum = NULL;
        CEnumDisplayAttributeInfo *pEnum = new(cicNoThrow) CEnumDisplayAttributeInfo();
        if (!pEnum)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < m_count; ++i)
        {
            if (FAILED(pEnum->Add(m_infos[i])))
            {
                pEnum->Release();
                return E_OUTOFMEMORY;
            }
        }
        pEnum->m_index = m_index;
        *ppEnum = pEnum;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) override
    {
        if (!rgInfo || (ulCount > 1 && !pcFetched))
            return E_INVALIDARG;
        ULONG fetched = 0;
        while (fetched < ulCount && m_index < m_count)
        {
            rgInfo[fetched] = m_infos[m_index++];
            rgInfo[fetched]->AddRef();
            ++fetched;
        }
        if (pcFetched)
            *pcFetched = fetched;
        return (fetched == ulCount) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        m_index = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG ulCount) override
    {
        if (ulCount > m_count - m_index)
        {
            m_index = m_count;
            return S_FALSE;
        }
        m_index += ulCount;
        return S_OK;
    }

private:
    ~CEnumDisplayAttributeInfo()
    {
        for (ULONG i = 0; i < m_count; ++i)
            m_infos[i]->Release();
        cicMemFree(m_infos);
    }

    LONG m_refs;
    ITfDisplayAttributeInfo **m_infos;
    ULONG m_count;
    ULONG m_capacity;
    ULONG m_index;
};

CDisplayAttributeMgr::CDisplayAttributeMgr()
    : m_cRefs(1)
    , m_fProvidersLoaded(FALSE)
    , m_providers(NULL)
    , m_cProviders(0)
{
}

CDisplayAttributeMgr::~CDisplayAttributeMgr()
{
    TRACE("destroying %p\n", this);
    _ReleaseProviders();
}

void CDisplayAttributeMgr::_ReleaseProviders()
{
    for (ULONG i = 0; i < m_cProviders; ++i)
        m_providers[i].provider->Release();
    cicMemFree(m_providers);
    m_providers = NULL;
    m_cProviders = 0;
    m_fProvidersLoaded = FALSE;
}

HRESULT CDisplayAttributeMgr::_LoadProviders()
{
    if (m_fProvidersLoaded)
        return S_OK;

    HKEY hTipKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szwSystemTIPKey, 0, KEY_READ, &hTipKey) != ERROR_SUCCESS)
    {
        m_fProvidersLoaded = TRUE;
        return S_OK;
    }

    WCHAR szCatid[39];
    StringFromGUID2(GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, szCatid, _countof(szCatid));

    WCHAR szClsid[39];
    for (DWORD index = 0;; ++index)
    {
        DWORD cch = _countof(szClsid);
        if (RegEnumKeyExW(hTipKey, index, szClsid, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        WCHAR szKey[MAX_PATH];
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\Category\\Category\\%s", szClsid, szCatid);
        HKEY hCatKey;
        if (RegOpenKeyExW(hTipKey, szKey, 0, KEY_READ, &hCatKey) != ERROR_SUCCESS)
            continue;
        RegCloseKey(hCatKey);

        CLSID clsid;
        if (FAILED(CLSIDFromString(szClsid, &clsid)))
            continue;

        ITfDisplayAttributeProvider *provider = NULL;
        HRESULT hr = cicCoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_ITfDisplayAttributeProvider,
                                         (void **)&provider);
        if (FAILED(hr))
        {
            WARN("provider %s: 0x%lx\n", debugstr_guid(&clsid), hr);
            continue;
        }

        Provider *providers = static_cast<Provider *>(cicMemReAlloc(m_providers, (m_cProviders + 1) * sizeof(*providers)));
        if (!providers)
        {
            provider->Release();
            break;
        }
        m_providers = providers;
        m_providers[m_cProviders].clsid = clsid;
        m_providers[m_cProviders].provider = provider;
        ++m_cProviders;
    }

    RegCloseKey(hTipKey);
    m_fProvidersLoaded = TRUE;
    return S_OK;
}

BOOL CDisplayAttributeMgr::_IsInCollection(REFGUID rguid)
{
    FIXME("(%p)\n", wine_dbgstr_guid(&rguid));
    return FALSE;
}

void CDisplayAttributeMgr::_AdviseMarkupCollection(ITfTextInputProcessor *pProcessor, DWORD dwCookie)
{
    FIXME("(%p, %u)\n", pProcessor, dwCookie);
}

void CDisplayAttributeMgr::_UnadviseMarkupCollection(DWORD dwCookie)
{
    FIXME("(%u)\n", dwCookie);
}

void CDisplayAttributeMgr::_SetThis()
{
    FIXME("()\n");
}

STDMETHODIMP
CDisplayAttributeMgr::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_INVALIDARG;

    *ppvObj = NULL;

    if (riid == IID_IUnknown ||
        riid == IID_ITfDisplayAttributeMgr ||
        riid == IID_CDisplayAttributeMgr)
    {
        *ppvObj = this;
    }
    else if (riid == IID_ITfDisplayAttributeCollectionMgr)
    {
        *ppvObj = static_cast<ITfDisplayAttributeCollectionMgr *>(this);
    }

    if (!*ppvObj)
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG)
CDisplayAttributeMgr::AddRef()
{
    return ::InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG)
CDisplayAttributeMgr::Release()
{
    ULONG ret = ::InterlockedDecrement(&m_cRefs);
    if (!ret)
        delete this;
    return ret;
}

STDMETHODIMP CDisplayAttributeMgr::OnUpdateInfo()
{
    _ReleaseProviders();
    return S_OK;
}

STDMETHODIMP
CDisplayAttributeMgr::EnumDisplayAttributeInfo(_Out_ IEnumTfDisplayAttributeInfo **ppEnum)
{
    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;

    HRESULT hr = _LoadProviders();
    if (FAILED(hr))
        return hr;

    CEnumDisplayAttributeInfo *pEnum = new(cicNoThrow) CEnumDisplayAttributeInfo();
    if (!pEnum)
        return E_OUTOFMEMORY;

    for (ULONG i = 0; i < m_cProviders; ++i)
    {
        IEnumTfDisplayAttributeInfo *pProviderEnum = NULL;
        if (FAILED(m_providers[i].provider->EnumDisplayAttributeInfo(&pProviderEnum)) || !pProviderEnum)
            continue;
        ITfDisplayAttributeInfo *info;
        while (pProviderEnum->Next(1, &info, NULL) == S_OK)
        {
            hr = pEnum->Add(info);
            info->Release();
            if (FAILED(hr))
                break;
        }
        pProviderEnum->Release();
        if (FAILED(hr))
        {
            pEnum->Release();
            return hr;
        }
    }

    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP
CDisplayAttributeMgr::GetDisplayAttributeInfo(
    _In_ REFGUID guid,
    _Out_ ITfDisplayAttributeInfo **ppInfo,
    _Out_ CLSID *pclsidOwner)
{
    TRACE("(%s, %p, %p)\n", wine_dbgstr_guid(&guid), ppInfo, pclsidOwner);

    if (ppInfo)
        *ppInfo = NULL;

    HRESULT hr = _LoadProviders();
    if (FAILED(hr))
        return hr;

    for (ULONG i = 0; i < m_cProviders; ++i)
    {
        ITfDisplayAttributeInfo *info = NULL;
        if (FAILED(m_providers[i].provider->GetDisplayAttributeInfo(guid, &info)) || !info)
            continue;
        if (pclsidOwner)
            *pclsidOwner = m_providers[i].clsid;
        if (ppInfo)
            *ppInfo = info;
        else
            info->Release();
        return S_OK;
    }
    return E_FAIL;
}

STDMETHODIMP
CDisplayAttributeMgr::UnknownMethod(_In_ DWORD unused)
{
    FIXME("(0x%lX)\n", unused);
    return E_NOTIMPL;
}

////////////////////////////////////////////////////////////////////////////

EXTERN_C
HRESULT DisplayAttributeMgr_Constructor(IUnknown *pUnkOuter, IUnknown **ppOut)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    CDisplayAttributeMgr *This = new(cicNoThrow) CDisplayAttributeMgr();
    if (!This)
        return E_OUTOFMEMORY;

    HRESULT hr = This->QueryInterface(IID_ITfDisplayAttributeMgr, (void **)ppOut);
    This->Release();
    return hr;
}
