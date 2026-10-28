/*
 * PROJECT:     ReactOS CTF
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     ITfCategoryMgr implementation
 * COPYRIGHT:   Copyright 2009 Aric Stewart, CodeWeavers
 *              Copyright 2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "precomp.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(msctf);

////////////////////////////////////////////////////////////////////////////

class CCategoryMgr
    : public ITfCategoryMgr
{
public:
    CCategoryMgr();
    virtual ~CCategoryMgr();

    // ** IUnknown methods **
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ** ITfCategoryMgr methods **
    STDMETHODIMP RegisterCategory(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rcatid,
        _In_ REFGUID rguid) override;
    STDMETHODIMP UnregisterCategory(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rcatid,
        _In_ REFGUID rguid) override;
    STDMETHODIMP EnumCategoriesInItem(
        _In_ REFGUID rguid,
        _Out_ IEnumGUID **ppEnum) override;
    STDMETHODIMP EnumItemsInCategory(
        _In_ REFGUID rcatid,
        _Out_ IEnumGUID **ppEnum) override;
    STDMETHODIMP FindClosestCategory(
        _In_ REFGUID rguid,
        _Out_ GUID *pcatid,
        _In_ const GUID **ppcatidList,
        _In_ ULONG ulCount) override;
    STDMETHODIMP RegisterGUIDDescription(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rguid,
        _In_ const WCHAR *pchDesc,
        _In_ ULONG cch) override;
    STDMETHODIMP UnregisterGUIDDescription(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rguid) override;
    STDMETHODIMP GetGUIDDescription(
        _In_ REFGUID rguid,
        _Out_ BSTR *pbstrDesc) override;
    STDMETHODIMP RegisterGUIDDWORD(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rguid,
        _In_ DWORD dw) override;
    STDMETHODIMP UnregisterGUIDDWORD(
        _In_ REFCLSID rclsid,
        _In_ REFGUID rguid) override;
    STDMETHODIMP GetGUIDDWORD(
        _In_ REFGUID rguid,
        _Out_ DWORD *pdw) override;
    STDMETHODIMP RegisterGUID(
        _In_ REFGUID rguid,
        _Out_ TfGuidAtom *pguidatom) override;
    STDMETHODIMP GetGUID(
        _In_ TfGuidAtom guidatom,
        _Out_ GUID *pguid) override;
    STDMETHODIMP IsEqualTfGuidAtom(
        _In_ TfGuidAtom guidatom,
        _In_ REFGUID rguid,
        _Out_ BOOL *pfEqual) override;

protected:
    LONG m_cRefs;
};

////////////////////////////////////////////////////////////////////////////

CCategoryMgr::CCategoryMgr()
    : m_cRefs(1)
{
}

CCategoryMgr::~CCategoryMgr()
{
    TRACE("destroying %p\n", this);
}

STDMETHODIMP CCategoryMgr::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_INVALIDARG;

    *ppvObj = NULL;

    if (riid == IID_IUnknown || riid == IID_ITfCategoryMgr)
        *ppvObj = this;

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    WARN("unsupported interface: %s\n", debugstr_guid(&riid));
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCategoryMgr::AddRef()
{
    return ::InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) CCategoryMgr::Release()
{
    ULONG ret = ::InterlockedDecrement(&m_cRefs);
    if (!ret)
        delete this;
    return ret;
}

STDMETHODIMP CCategoryMgr::RegisterCategory(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rcatid,
    _In_ REFGUID rguid)
{
    WCHAR szFullKey[110], szClsid[39], szCatid[39], szGuid[39];
    HKEY hTipKey = NULL, hCatKey = NULL, hItemKey = NULL;
    LSTATUS error;
    HRESULT hr = E_FAIL;

    TRACE("%p -> (%s, %s, %s)\n", this, debugstr_guid(&rclsid), debugstr_guid(&rcatid),
          debugstr_guid(&rguid));

    StringFromGUID2(rclsid, szClsid, _countof(szClsid));
    StringFromGUID2(rcatid, szCatid, _countof(szCatid));
    StringFromGUID2(rguid, szGuid, _countof(szGuid));

    StringCchPrintfW(szFullKey, _countof(szFullKey), L"%s\\%s", szwSystemTIPKey, szClsid);
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, szFullKey, 0, KEY_READ | KEY_WRITE, &hTipKey);
    if (error != ERROR_SUCCESS)
        return E_FAIL;

    StringCchPrintfW(szFullKey, _countof(szFullKey), L"Category\\Category\\%s\\%s", szCatid, szGuid);
    error = RegCreateKeyExW(hTipKey, szFullKey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL,
                            &hCatKey, NULL);
    if (error == ERROR_SUCCESS)
    {
        RegCloseKey(hCatKey);

        StringCchPrintfW(szFullKey, _countof(szFullKey), L"Category\\Item\\%s\\%s", szGuid, szCatid);
        error = RegCreateKeyExW(hTipKey, szFullKey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL,
                                &hItemKey, NULL);
        if (error == ERROR_SUCCESS)
        {
            RegCloseKey(hItemKey);
            hr = S_OK;
        }
    }

    RegCloseKey(hTipKey);
    return hr;
}

STDMETHODIMP CCategoryMgr::UnregisterCategory(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rcatid,
    _In_ REFGUID rguid)
{
    WCHAR szFullKey[110], szClsid[39], szCatid[39], szGuid[39];
    HKEY hTipKey = NULL;
    LSTATUS error;

    TRACE("%p -> (%s %s %s)\n", this, debugstr_guid(&rclsid), debugstr_guid(&rcatid),
          debugstr_guid(&rguid));

    StringFromGUID2(rclsid, szClsid, _countof(szClsid));
    StringFromGUID2(rcatid, szCatid, _countof(szCatid));
    StringFromGUID2(rguid, szGuid, _countof(szGuid));

    StringCchPrintfW(szFullKey, _countof(szFullKey), L"%s\\%s", szwSystemTIPKey, szClsid);
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, szFullKey, 0, KEY_READ | KEY_WRITE, &hTipKey);
    if (error != ERROR_SUCCESS)
        return E_FAIL;

    StringCchPrintfW(szFullKey, _countof(szFullKey), L"Category\\Category\\%s\\%s", szCatid, szGuid);
    RegDeleteTreeW(hTipKey, szFullKey);

    StringCchPrintfW(szFullKey, _countof(szFullKey), L"Category\\Item\\%s\\%s", szGuid, szCatid);
    RegDeleteTreeW(hTipKey, szFullKey);

    RegCloseKey(hTipKey);
    return S_OK;
}

class CEnumCategoryGuid final : public IEnumGUID
{
public:
    CEnumCategoryGuid() : m_refs(1), m_guids(NULL), m_count(0), m_index(0) { }

    HRESULT Add(REFGUID guid)
    {
        for (ULONG i = 0; i < m_count; ++i)
        {
            if (m_guids[i] == guid)
                return S_OK;
        }
        GUID *guids = static_cast<GUID *>(cicMemReAlloc(m_guids, (m_count + 1) * sizeof(*guids)));
        if (!guids)
            return E_OUTOFMEMORY;
        m_guids = guids;
        m_guids[m_count++] = guid;
        return S_OK;
    }

    HRESULT AddFromTipSubkeys(LPCWSTR pszFormat, REFGUID rguid)
    {
        WCHAR szGuid[39], szClsid[39], szKey[200];
        HKEY hTips;
        StringFromGUID2(rguid, szGuid, _countof(szGuid));
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szwSystemTIPKey, 0, KEY_READ, &hTips) != ERROR_SUCCESS)
            return S_OK;
        for (DWORD index = 0;; ++index)
        {
            DWORD cch = _countof(szClsid);
            if (RegEnumKeyExW(hTips, index, szClsid, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            StringCchPrintfW(szKey, _countof(szKey), pszFormat, szClsid, szGuid);
            HKEY hKey;
            if (RegOpenKeyExW(hTips, szKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                continue;
            for (DWORD item = 0;; ++item)
            {
                WCHAR szItem[39];
                DWORD cchItem = _countof(szItem);
                GUID guid;
                if (RegEnumKeyExW(hKey, item, szItem, &cchItem, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;
                if (SUCCEEDED(CLSIDFromString(szItem, &guid)) && FAILED(Add(guid)))
                {
                    RegCloseKey(hKey);
                    RegCloseKey(hTips);
                    return E_OUTOFMEMORY;
                }
            }
            RegCloseKey(hKey);
        }
        RegCloseKey(hTips);
        return S_OK;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_IEnumGUID)
            return E_NOINTERFACE;
        *ppvObj = static_cast<IEnumGUID *>(this);
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
    STDMETHODIMP Next(ULONG celt, GUID *rgelt, ULONG *pceltFetched) override
    {
        if (!rgelt || (celt > 1 && !pceltFetched))
            return E_INVALIDARG;
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_count)
            rgelt[fetched++] = m_guids[m_index++];
        if (pceltFetched)
            *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG celt) override
    {
        if (celt > m_count - m_index)
        {
            m_index = m_count;
            return S_FALSE;
        }
        m_index += celt;
        return S_OK;
    }
    STDMETHODIMP Reset() override
    {
        m_index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumGUID **ppenum) override
    {
        if (!ppenum)
            return E_INVALIDARG;
        *ppenum = NULL;
        CEnumCategoryGuid *pEnum = new(cicNoThrow) CEnumCategoryGuid();
        if (!pEnum)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < m_count; ++i)
        {
            if (FAILED(pEnum->Add(m_guids[i])))
            {
                pEnum->Release();
                return E_OUTOFMEMORY;
            }
        }
        pEnum->m_index = m_index;
        *ppenum = pEnum;
        return S_OK;
    }

private:
    ~CEnumCategoryGuid() { cicMemFree(m_guids); }

    LONG m_refs;
    GUID *m_guids;
    ULONG m_count;
    ULONG m_index;
};

static HRESULT
CreateCategoryEnum(LPCWSTR pszFormat, REFGUID rguid, IEnumGUID **ppEnum)
{
    if (!ppEnum)
        return E_INVALIDARG;
    *ppEnum = NULL;
    CEnumCategoryGuid *pEnum = new(cicNoThrow) CEnumCategoryGuid();
    if (!pEnum)
        return E_OUTOFMEMORY;
    HRESULT hr = pEnum->AddFromTipSubkeys(pszFormat, rguid);
    if (FAILED(hr))
    {
        pEnum->Release();
        return hr;
    }
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP CCategoryMgr::EnumCategoriesInItem(
    _In_ REFGUID rguid,
    _Out_ IEnumGUID **ppEnum)
{
    TRACE("(%p) %s %p\n", this, debugstr_guid(&rguid), ppEnum);
    return CreateCategoryEnum(L"%s\\Category\\Item\\%s", rguid, ppEnum);
}

STDMETHODIMP CCategoryMgr::EnumItemsInCategory(
    _In_ REFGUID rcatid,
    _Out_ IEnumGUID **ppEnum)
{
    TRACE("(%p) %s %p\n", this, debugstr_guid(&rcatid), ppEnum);
    return CreateCategoryEnum(L"%s\\Category\\Category\\%s", rcatid, ppEnum);
}

static BOOL
ItemHasCategory(_In_ LPCWSTR pszItem, _In_ REFGUID rcatid)
{
    WCHAR szCatid[39], szTip[39], szKey[200];
    HKEY hTips, hKey;
    BOOL bFound = FALSE;

    StringFromGUID2(rcatid, szCatid, _countof(szCatid));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szwSystemTIPKey, 0, KEY_READ, &hTips) != ERROR_SUCCESS)
        return FALSE;
    for (DWORD dwIndex = 0; !bFound; ++dwIndex)
    {
        DWORD cchTip = _countof(szTip);
        if (RegEnumKeyExW(hTips, dwIndex, szTip, &cchTip, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\Category\\Item\\%s\\%s", szTip, pszItem, szCatid);
        if (RegOpenKeyExW(hTips, szKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            bFound = TRUE;
        }
    }
    RegCloseKey(hTips);
    return bFound;
}

STDMETHODIMP CCategoryMgr::FindClosestCategory(
    _In_ REFGUID rguid,
    _Out_ GUID *pcatid,
    _In_ const GUID **ppcatidList,
    _In_ ULONG ulCount)
{
    WCHAR szGuid[39], szTip[39], szKey[200];
    HKEY hTips, hItem;

    TRACE("(%p)\n", this);

    if (!pcatid || (ulCount && !ppcatidList))
        return E_INVALIDARG;

    *pcatid = GUID_NULL;
    StringFromGUID2(rguid, szGuid, _countof(szGuid));

    if (ulCount)
    {
        for (ULONG i = 0; i < ulCount; ++i)
        {
            if (ItemHasCategory(szGuid, *ppcatidList[i]))
            {
                *pcatid = *ppcatidList[i];
                break;
            }
        }
        return S_OK;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szwSystemTIPKey, 0, KEY_READ, &hTips) != ERROR_SUCCESS)
        return S_OK;
    for (DWORD dwIndex = 0; *pcatid == GUID_NULL; ++dwIndex)
    {
        DWORD cchTip = _countof(szTip);
        if (RegEnumKeyExW(hTips, dwIndex, szTip, &cchTip, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\Category\\Item\\%s", szTip, szGuid);
        if (RegOpenKeyExW(hTips, szKey, 0, KEY_READ, &hItem) != ERROR_SUCCESS)
            continue;
        WCHAR szCatid[39];
        DWORD cchCatid = _countof(szCatid);
        if (RegEnumKeyExW(hItem, 0, szCatid, &cchCatid, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            CLSIDFromString(szCatid, pcatid);
        RegCloseKey(hItem);
    }
    RegCloseKey(hTips);
    return S_OK;
}

static void
FormatItemKey(_In_ REFCLSID rclsid, _In_ REFGUID rguid, _Out_writes_(cch) LPWSTR psz, _In_ SIZE_T cch)
{
    WCHAR szClsid[39], szGuid[39];
    StringFromGUID2(rclsid, szClsid, _countof(szClsid));
    StringFromGUID2(rguid, szGuid, _countof(szGuid));
    StringCchPrintfW(psz, cch, L"%s\\%s\\Item\\%s", szwSystemTIPKey, szClsid, szGuid);
}

STDMETHODIMP CCategoryMgr::RegisterGUIDDescription(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rguid,
    _In_ const WCHAR *pchDesc,
    _In_ ULONG cch)
{
    WCHAR szKey[200], szDesc[MAX_PATH];
    HKEY hKey;
    LSTATUS error;

    TRACE("(%p) %s %s %s\n", this, debugstr_guid(&rclsid), debugstr_guid(&rguid), debugstr_wn(pchDesc, cch));

    if (!pchDesc && cch)
        return E_INVALIDARG;
    StringCchCopyNW(szDesc, _countof(szDesc), pchDesc ? pchDesc : L"", cch);
    FormatItemKey(rclsid, rguid, szKey, _countof(szKey));
    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, szKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(hKey, L"Description", 0, REG_SZ, (const BYTE *)szDesc,
                           (DWORD)(wcslen(szDesc) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(error);
}

STDMETHODIMP CCategoryMgr::UnregisterGUIDDescription(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rguid)
{
    WCHAR szKey[200];
    HKEY hKey;
    LSTATUS error;

    TRACE("(%p) %s %s\n", this, debugstr_guid(&rclsid), debugstr_guid(&rguid));

    FormatItemKey(rclsid, rguid, szKey, _countof(szKey));
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, szKey, 0, KEY_SET_VALUE, &hKey);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    error = RegDeleteValueW(hKey, L"Description");
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(error);
}

STDMETHODIMP CCategoryMgr::GetGUIDDescription(
    _In_ REFGUID rguid,
    _Out_ BSTR *pbstrDesc)
{
    WCHAR szGuid[39], szTip[39], szKey[200], szDesc[MAX_PATH];
    HKEY hTips, hItem;
    HRESULT hr = E_FAIL;

    TRACE("(%p) %s %p\n", this, debugstr_guid(&rguid), pbstrDesc);

    if (!pbstrDesc)
        return E_INVALIDARG;
    *pbstrDesc = NULL;

    StringFromGUID2(rguid, szGuid, _countof(szGuid));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szwSystemTIPKey, 0, KEY_READ, &hTips) != ERROR_SUCCESS)
        return E_FAIL;
    for (DWORD dwIndex = 0; hr == E_FAIL; ++dwIndex)
    {
        DWORD cchTip = _countof(szTip), cbDesc = sizeof(szDesc) - sizeof(WCHAR), dwType;
        if (RegEnumKeyExW(hTips, dwIndex, szTip, &cchTip, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\Item\\%s", szTip, szGuid);
        if (RegOpenKeyExW(hTips, szKey, 0, KEY_QUERY_VALUE, &hItem) != ERROR_SUCCESS)
            continue;
        ZeroMemory(szDesc, sizeof(szDesc));
        if (RegQueryValueExW(hItem, L"Description", NULL, &dwType, (LPBYTE)szDesc, &cbDesc) == ERROR_SUCCESS &&
            dwType == REG_SZ)
        {
            *pbstrDesc = SysAllocString(szDesc);
            hr = *pbstrDesc ? S_OK : E_OUTOFMEMORY;
        }
        RegCloseKey(hItem);
    }
    RegCloseKey(hTips);
    return hr;
}

STDMETHODIMP CCategoryMgr::RegisterGUIDDWORD(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rguid,
    _In_ DWORD dw)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CCategoryMgr::UnregisterGUIDDWORD(
    _In_ REFCLSID rclsid,
    _In_ REFGUID rguid)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CCategoryMgr::GetGUIDDWORD(
    _In_ REFGUID rguid,
    _Out_ DWORD *pdw)
{
    FIXME("STUB:(%p)\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CCategoryMgr::RegisterGUID(
    _In_ REFGUID rguid,
    _Out_ TfGuidAtom *pguidatom)
{
    TRACE("%p -> (%s, %p)\n", this, debugstr_guid(&rguid), pguidatom);

    if (!pguidatom)
        return E_INVALIDARG;

    DWORD dwCookieId = 0, dwEnumIndex = 0;
    do
    {
        dwCookieId = enumerate_Cookie(COOKIE_MAGIC_GUIDATOM, &dwEnumIndex);
        if (dwCookieId && rguid == *(const GUID *)get_Cookie_data(dwCookieId))
        {
            *pguidatom = dwCookieId;
            return S_OK;
        }
    } while (dwCookieId != 0);

    GUID *pNewGuid = (GUID *)cicMemAlloc(sizeof(GUID));
    if (!pNewGuid)
        return E_OUTOFMEMORY;

    *pNewGuid = rguid;

    dwCookieId = generate_Cookie(COOKIE_MAGIC_GUIDATOM, pNewGuid);
    if (dwCookieId == 0)
    {
        cicMemFree(pNewGuid);
        return E_FAIL;
    }

    *pguidatom = dwCookieId;
    return S_OK;
}

STDMETHODIMP CCategoryMgr::GetGUID(
    _In_ TfGuidAtom guidatom,
    _Out_ GUID *pguid)
{
    TRACE("%p -> (%d, %p)\n", this, guidatom, pguid);

    if (!pguid)
        return E_INVALIDARG;

    *pguid = GUID_NULL;

    if (get_Cookie_magic(guidatom) == COOKIE_MAGIC_GUIDATOM)
        *pguid = *(const GUID *)get_Cookie_data(guidatom);

    return S_OK;
}

STDMETHODIMP CCategoryMgr::IsEqualTfGuidAtom(
    _In_ TfGuidAtom guidatom,
    _In_ REFGUID rguid,
    _Out_ BOOL *pfEqual)
{
    TRACE("%p -> (%d %s %p)\n", this, guidatom, debugstr_guid(&rguid), pfEqual);

    if (!pfEqual)
        return E_INVALIDARG;

    *pfEqual = FALSE;
    if (get_Cookie_magic(guidatom) == COOKIE_MAGIC_GUIDATOM)
    {
        if (rguid == *(const GUID *)get_Cookie_data(guidatom))
            *pfEqual = TRUE;
    }

    return S_OK;
}

////////////////////////////////////////////////////////////////////////////

EXTERN_C
HRESULT CategoryMgr_Constructor(IUnknown *pUnkOuter, IUnknown **ppOut)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    CCategoryMgr *This = new(cicNoThrow) CCategoryMgr();
    if (!This)
        return E_OUTOFMEMORY;

    *ppOut = static_cast<ITfCategoryMgr *>(This);
    TRACE("returning %p\n", *ppOut);
    return S_OK;
}
