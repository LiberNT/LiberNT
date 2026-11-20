/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     User's Files folder, the user profile folder in the shell namespace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <precomp.h>
#include "CUsersFilesFolder.h"

HRESULT CUsersFilesFolder::EnsureProfile()
{
    if (m_pProfile)
        return S_OK;
    if (!m_pidlRoot)
        return E_UNEXPECTED;
    return SHELL32_CoCreateInitSF(m_pidlRoot, &CLSID_ShellFSFolder, CSIDL_PROFILE,
                                  IID_PPV_ARG(IShellFolder2, &m_pProfile));
}

STDMETHODIMP CUsersFilesFolder::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, DWORD *pchEaten,
                                                 PIDLIST_RELATIVE *ppidl, DWORD *pdwAttributes)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->ParseDisplayName(hwndOwner, pbc, lpszDisplayName, pchEaten, ppidl, pdwAttributes);
}

STDMETHODIMP CUsersFilesFolder::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->EnumObjects(hwndOwner, dwFlags, ppEnumIDList);
}

STDMETHODIMP CUsersFilesFolder::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->BindToObject(pidl, pbcReserved, riid, ppvOut);
}

STDMETHODIMP CUsersFilesFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->BindToStorage(pidl, pbcReserved, riid, ppvOut);
}

STDMETHODIMP CUsersFilesFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->CompareIDs(lParam, pidl1, pidl2);
}

STDMETHODIMP CUsersFilesFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->CreateViewObject(hwndOwner, riid, ppvOut);
}

STDMETHODIMP CUsersFilesFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetAttributesOf(cidl, apidl, rgfInOut);
}

STDMETHODIMP CUsersFilesFolder::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid,
                                              UINT *prgfInOut, LPVOID *ppvOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, ppvOut);
}

STDMETHODIMP CUsersFilesFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    WCHAR szProfile[MAX_PATH];
    CComHeapPtr<ITEMIDLIST> pidlProfile;
    CComHeapPtr<WCHAR> pszName;
    HRESULT hr;

    if (!strRet)
        return E_INVALIDARG;

    hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    if (!_ILIsEmpty(pidl) || GET_SHGDN_FOR(dwFlags) == SHGDN_FORPARSING)
        return m_pProfile->GetDisplayNameOf(pidl, dwFlags, strRet);

    if (!SHGetSpecialFolderPathW(NULL, szProfile, CSIDL_PROFILE, FALSE))
        return E_FAIL;
    hr = SHParseDisplayName(szProfile, NULL, &pidlProfile, 0, NULL);
    if (FAILED(hr))
        return hr;
    hr = SHGetNameFromIDList(pidlProfile, SIGDN_NORMALDISPLAY, &pszName);
    if (FAILED(hr))
        return hr;
    return SHSetStrRet(strRet, pszName);
}

STDMETHODIMP CUsersFilesFolder::SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags,
                                          PITEMID_CHILD *pPidlOut)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->SetNameOf(hwndOwner, pidl, lpName, dwFlags, pPidlOut);
}

STDMETHODIMP CUsersFilesFolder::GetDefaultSearchGUID(GUID *pguid)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetDefaultSearchGUID(pguid);
}

STDMETHODIMP CUsersFilesFolder::EnumSearches(IEnumExtraSearch **ppenum)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->EnumSearches(ppenum);
}

STDMETHODIMP CUsersFilesFolder::GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetDefaultColumn(dwRes, pSort, pDisplay);
}

STDMETHODIMP CUsersFilesFolder::GetDefaultColumnState(UINT iColumn, DWORD *pcsFlags)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetDefaultColumnState(iColumn, pcsFlags);
}

STDMETHODIMP CUsersFilesFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetDetailsEx(pidl, pscid, pv);
}

STDMETHODIMP CUsersFilesFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->GetDetailsOf(pidl, iColumn, psd);
}

STDMETHODIMP CUsersFilesFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    HRESULT hr = EnsureProfile();
    if (FAILED(hr))
        return hr;
    return m_pProfile->MapColumnToSCID(column, pscid);
}

STDMETHODIMP CUsersFilesFolder::GetClassID(CLSID *lpClassId)
{
    if (!lpClassId)
        return E_POINTER;
    *lpClassId = CLSID_UsersFilesFolder;
    return S_OK;
}

STDMETHODIMP CUsersFilesFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    LPITEMIDLIST pidlRoot = ILClone(pidl);

    if (!pidlRoot)
        return E_OUTOFMEMORY;
    m_pProfile.Release();
    m_pidlRoot.Free();
    m_pidlRoot.Attach(pidlRoot);
    return S_OK;
}

STDMETHODIMP CUsersFilesFolder::GetCurFolder(PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_POINTER;
    *pidl = NULL;
    if (!m_pidlRoot)
        return E_UNEXPECTED;
    *pidl = ILClone(m_pidlRoot);
    return *pidl ? S_OK : E_OUTOFMEMORY;
}
