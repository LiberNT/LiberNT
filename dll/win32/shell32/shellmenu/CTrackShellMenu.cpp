/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modal shell menu popup (TrackShellMenu)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellmenu.h"
#include <shdeprecated.h>
#include "CTrackShellMenu.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

CTrackShellMenu::CTrackShellMenu()
    : m_bTracking(FALSE)
{
}

CTrackShellMenu::~CTrackShellMenu()
{
}

HRESULT CTrackShellMenu::FinalConstruct()
{
    return CMenuBand_CreateInstance(IID_PPV_ARG(IShellMenu, &m_pShellMenu));
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::Initialize(IShellMenuCallback *psmc, UINT uId, UINT uIdAncestor, DWORD dwFlags)
{
    return m_pShellMenu->Initialize(psmc, uId, uIdAncestor, dwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetMenuInfo(IShellMenuCallback **ppsmc, UINT *puId, UINT *puIdAncestor, DWORD *pdwFlags)
{
    return m_pShellMenu->GetMenuInfo(ppsmc, puId, puIdAncestor, pdwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetShellFolder(IShellFolder *psf, LPCITEMIDLIST pidlFolder, HKEY hKey, DWORD dwFlags)
{
    return m_pShellMenu->SetShellFolder(psf, pidlFolder, hKey, dwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetShellFolder(DWORD *pdwFlags, LPITEMIDLIST *ppidl, REFIID riid, void **ppv)
{
    return m_pShellMenu->GetShellFolder(pdwFlags, ppidl, riid, ppv);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetMenu(HMENU hmenu, HWND hwnd, DWORD dwFlags)
{
    return m_pShellMenu->SetMenu(hmenu, hwnd, dwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetMenu(HMENU *phmenu, HWND *phwnd, DWORD *pdwFlags)
{
    return m_pShellMenu->GetMenu(phmenu, phwnd, pdwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::InvalidateItem(LPSMDATA psmd, DWORD dwFlags)
{
    return m_pShellMenu->InvalidateItem(psmd, dwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetState(LPSMDATA psmd)
{
    return m_pShellMenu->GetState(psmd);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetMenuToolbar(IUnknown *punk, DWORD dwFlags)
{
    return m_pShellMenu->SetMenuToolbar(punk, dwFlags);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetSubMenu()
{
    CComPtr<IShellMenu2> pShellMenu2;
    HRESULT hr = m_pShellMenu->QueryInterface(IID_PPV_ARG(IShellMenu2, &pShellMenu2));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pShellMenu2->GetSubMenu();
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetToolbar()
{
    CComPtr<IShellMenu2> pShellMenu2;
    HRESULT hr = m_pShellMenu->QueryInterface(IID_PPV_ARG(IShellMenu2, &pShellMenu2));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pShellMenu2->SetToolbar();
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetMinWidth()
{
    CComPtr<IShellMenu2> pShellMenu2;
    HRESULT hr = m_pShellMenu->QueryInterface(IID_PPV_ARG(IShellMenu2, &pShellMenu2));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pShellMenu2->SetMinWidth();
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetNoBorder()
{
    CComPtr<IShellMenu2> pShellMenu2;
    HRESULT hr = m_pShellMenu->QueryInterface(IID_PPV_ARG(IShellMenu2, &pShellMenu2));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pShellMenu2->SetNoBorder();
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetTheme()
{
    CComPtr<IShellMenu2> pShellMenu2;
    HRESULT hr = m_pShellMenu->QueryInterface(IID_PPV_ARG(IShellMenu2, &pShellMenu2));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    return pShellMenu2->SetTheme();
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetObscured(HWND hwndTB, IUnknown *punkBand, DWORD dwSMSetFlags)
{
    FIXME("(%p, %p, 0x%lx)\n", hwndTB, punkBand, dwSMSetFlags);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::Popup(HWND hwnd, POINTL *ppt, RECTL *prcExclude, MP_POPUPFLAGS dwFlags)
{
    if (!ppt)
        return E_INVALIDARG;
    if (m_bTracking)
        return E_UNEXPECTED;

    CComPtr<IBandSite> pBandSite;
    HRESULT hr = CMenuSite_CreateInstance(IID_PPV_ARG(IBandSite, &pBandSite));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComPtr<IDeskBar> pDeskBar;
    hr = CMenuDeskBar_CreateInstance(IID_PPV_ARG(IDeskBar, &pDeskBar));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = pDeskBar->SetClient(pBandSite);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = pBandSite->AddBand(m_pShellMenu);
    if (FAILED_UNEXPECTEDLY(hr))
    {
        IUnknown_SetSite(pDeskBar, NULL);
        return hr;
    }

    HWND hwndBar = NULL;
    hr = IUnknown_GetWindow(pDeskBar, &hwndBar);
    if (FAILED_UNEXPECTEDLY(hr))
    {
        IUnknown_SetSite(pDeskBar, NULL);
        return hr;
    }

    if (hwnd)
        SetWindowLongPtrW(hwndBar, GWLP_HWNDPARENT, (LONG_PTR)hwnd);

    CComPtr<IMenuPopup> pMenuPopup;
    hr = pDeskBar->QueryInterface(IID_PPV_ARG(IMenuPopup, &pMenuPopup));
    if (SUCCEEDED(hr))
        hr = pMenuPopup->Popup(ppt, prcExclude, dwFlags);
    if (FAILED_UNEXPECTEDLY(hr))
    {
        IUnknown_SetSite(pDeskBar, NULL);
        return hr;
    }

    CComPtr<ITrackShellMenu> protectThis(this);
    m_bTracking = TRUE;

    MSG msg;
    while (::IsWindowVisible(hwndBar))
    {
        BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
        if (bRet <= 0)
        {
            if (bRet == 0)
                PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    m_bTracking = FALSE;

    IUnknown_SetSite(pDeskBar, NULL);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::SetSite(IUnknown *pUnkSite)
{
    m_pSite = pUnkSite;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::GetSite(REFIID riid, void **ppvSite)
{
    if (!ppvSite)
        return E_POINTER;
    *ppvSite = NULL;
    if (!m_pSite)
        return E_FAIL;
    return m_pSite->QueryInterface(riid, ppvSite);
}

HRESULT STDMETHODCALLTYPE CTrackShellMenu::QueryService(REFGUID guidService, REFIID riid, void **ppvObject)
{
    return IUnknown_QueryService(m_pSite, guidService, riid, ppvObject);
}
