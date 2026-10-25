/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Preview handler for plain text files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <richedit.h>

CTextPreviewHandler::CTextPreviewHandler() :
    m_hwndParent(NULL),
    m_hwndPreview(NULL)
{
    ::SetRectEmpty(&m_rc);
}

CTextPreviewHandler::~CTextPreviewHandler()
{
    Unload();
}

static HRESULT
ReadStreamText(IStream *pStream, CStringW &strText)
{
    STATSTG stat;
    HRESULT hr = pStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr))
        return hr;
    if (stat.cbSize.HighPart || stat.cbSize.LowPart > MAXLONG - sizeof(WCHAR))
        return E_OUTOFMEMORY;

    CHeapPtr<BYTE> pData;
    if (!pData.Allocate(stat.cbSize.LowPart + sizeof(WCHAR)))
        return E_OUTOFMEMORY;

    LARGE_INTEGER li = {};
    hr = pStream->Seek(li, STREAM_SEEK_SET, NULL);
    if (FAILED(hr))
        return hr;

    ULONG cbRead = 0;
    hr = pStream->Read(pData, stat.cbSize.LowPart, &cbRead);
    if (FAILED(hr))
        return hr;

    if (cbRead >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
    {
        strText.SetString((PCWSTR)(pData + 2), (cbRead - 2) / sizeof(WCHAR));
        return S_OK;
    }

    UINT CodePage = CP_ACP;
    ULONG cbSkip = 0;
    if (cbRead >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF)
    {
        CodePage = CP_UTF8;
        cbSkip = 3;
    }

    int cch = MultiByteToWideChar(CodePage, 0, (LPCSTR)(pData + cbSkip), cbRead - cbSkip, NULL, 0);
    PWSTR pszText = strText.GetBuffer(cch);
    if (!pszText)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CodePage, 0, (LPCSTR)(pData + cbSkip), cbRead - cbSkip, pszText, cch);
    strText.ReleaseBuffer(cch);
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::SetWindow(HWND hwnd, const RECT *prc)
{
    if (!hwnd || !prc)
        return E_INVALIDARG;

    m_hwndParent = hwnd;
    m_rc = *prc;
    if (m_hwndPreview)
    {
        ::SetParent(m_hwndPreview, m_hwndParent);
        ::MoveWindow(m_hwndPreview, m_rc.left, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top, TRUE);
    }
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::SetRect(const RECT *prc)
{
    if (!prc)
        return E_INVALIDARG;

    m_rc = *prc;
    if (m_hwndPreview)
        ::MoveWindow(m_hwndPreview, m_rc.left, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top, TRUE);
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::DoPreview()
{
    if (!m_Stream || !m_hwndParent)
        return E_FAIL;
    if (m_hwndPreview)
        return S_OK;

    CStringW strText;
    HRESULT hr = ReadStreamText(m_Stream, strText);
    if (FAILED(hr))
        return hr;

    if (!GetModuleHandleW(L"msftedit.dll") && !LoadLibraryW(L"msftedit.dll"))
        return HRESULT_FROM_WIN32(GetLastError());

    m_hwndPreview = ::CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                                      WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE,
                                      m_rc.left, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top,
                                      m_hwndParent, NULL, shell32_hInstance, NULL);
    if (!m_hwndPreview)
        return HRESULT_FROM_WIN32(GetLastError());

    ::SetWindowTextW(m_hwndPreview, strText);
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::Unload()
{
    if (m_hwndPreview)
    {
        ::DestroyWindow(m_hwndPreview);
        m_hwndPreview = NULL;
    }
    m_Stream.Release();
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::SetFocus()
{
    if (!m_hwndPreview)
        return S_FALSE;
    ::SetFocus(m_hwndPreview);
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::QueryFocus(HWND *phwnd)
{
    if (!phwnd)
        return E_INVALIDARG;
    *phwnd = ::GetFocus();
    return *phwnd ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CTextPreviewHandler::TranslateAccelerator(MSG *pmsg)
{
    CComPtr<IPreviewHandlerFrame> pFrame;
    if (m_Site && SUCCEEDED(m_Site->QueryInterface(IID_PPV_ARG(IPreviewHandlerFrame, &pFrame))))
        return pFrame->TranslateAccelerator(pmsg);
    return S_FALSE;
}

STDMETHODIMP CTextPreviewHandler::Initialize(IStream *pstream, DWORD grfMode)
{
    if (!pstream)
        return E_INVALIDARG;
    if (m_Stream)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    m_Stream = pstream;
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::SetSite(IUnknown *pUnkSite)
{
    m_Site = pUnkSite;
    return S_OK;
}

STDMETHODIMP CTextPreviewHandler::GetSite(REFIID riid, void **ppvSite)
{
    if (!ppvSite)
        return E_POINTER;
    *ppvSite = NULL;
    if (!m_Site)
        return E_FAIL;
    return m_Site->QueryInterface(riid, ppvSite);
}

STDMETHODIMP CTextPreviewHandler::GetWindow(HWND *phwnd)
{
    if (!phwnd)
        return E_INVALIDARG;
    *phwnd = m_hwndPreview;
    return m_hwndPreview ? S_OK : E_FAIL;
}

STDMETHODIMP CTextPreviewHandler::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}
