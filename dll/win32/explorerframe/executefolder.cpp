/*
 * PROJECT:     LiberNT Explorer Frame
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Folder verb handler that opens a folder window through the shell
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

HWND FindShellProxy(LPITEMIDLIST pidl);

CExecuteFolder::CExecuteFolder() :
    m_Show(SW_SHOWNORMAL),
    m_Explore(FALSE)
{
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetKeyState(DWORD grfKeyState)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetParameters(LPCWSTR pszParameters)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetPosition(POINT pt)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetShowWindow(int nShow)
{
    m_Show = nShow;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetNoShowUI(BOOL fNoShowUI)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetDirectory(LPCWSTR pszDirectory)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::Execute()
{
    EXPLORER_CMDLINE_PARSE_RESULTS Results = { 0 };
    CComPtr<IShellItem> Item;
    CComHeapPtr<ITEMIDLIST> Pidl;
    HRESULT hr;

    if (!m_Selection)
        return E_UNEXPECTED;
    hr = m_Selection->GetItemAt(0, &Item);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    hr = SHGetIDListFromObject(Item, &Pidl);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    if (!FindShellProxy(Pidl))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    Results.dwFlags = SH_EXPLORER_CMDLINE_FLAG_IDLIST;
    if (m_Explore)
        Results.dwFlags |= SH_EXPLORER_CMDLINE_FLAG_E;
    Results.nCmdShow = m_Show;
    Results.pidlPath = Pidl;
    return SHCreateFromDesktop(&Results) ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::SetSelection(IShellItemArray *psia)
{
    m_Selection = psia;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::GetSelection(REFIID riid, void **ppv)
{
    if (!m_Selection)
    {
        *ppv = NULL;
        return E_FAIL;
    }
    return m_Selection->QueryInterface(riid, ppv);
}

HRESULT STDMETHODCALLTYPE CExecuteFolder::Initialize(LPCWSTR pszCommandName, IPropertyBag *ppb)
{
    m_Explore = pszCommandName && !_wcsicmp(pszCommandName, L"explore");
    return S_OK;
}
