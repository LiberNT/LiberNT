/*
 * PROJECT:     LiberNT shdocvw
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Attachment Services (IAttachmentExecute)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"
#include <urlmon.h>
#include <strsafe.h>
#include <wine/debug.h>

CAttachmentServices::CAttachmentServices()
    : m_ClientGuid(GUID_NULL)
    , m_HasClientGuid(FALSE)
    , m_hrLatched(S_OK)
{
}

HRESULT CAttachmentServices::Latch(HRESULT hr)
{
    if (FAILED(hr) && SUCCEEDED(m_hrLatched))
        m_hrLatched = hr;
    return hr;
}

HRESULT CAttachmentServices::SetString(CComHeapPtr<WCHAR> &Target, LPCWSTR Value, BOOL Once)
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!Value || !*Value)
        return Latch(E_INVALIDARG);
    if (Once && Target)
        return Latch(E_UNEXPECTED);

    LPWSTR pszCopy;
    HRESULT hr = SHStrDupW(Value, &pszCopy);
    if (FAILED(hr))
        return Latch(hr);
    Target.Free();
    Target.Attach(pszCopy);
    return S_OK;
}

DWORD CAttachmentServices::GetZone()
{
    if (!m_Source && !m_Referrer)
        return URLZONE_UNTRUSTED;

    CComPtr<IInternetSecurityManager> pSecurityManager;
    if (FAILED(CoInternetCreateSecurityManager(NULL, &pSecurityManager, 0)))
        return URLZONE_UNTRUSTED;

    DWORD dwResult = URLZONE_LOCAL_MACHINE;
    LPCWSTR apszEvidence[] = { m_Source, m_Referrer };
    for (UINT i = 0; i < _countof(apszEvidence); ++i)
    {
        if (!apszEvidence[i])
            continue;
        DWORD dwZone;
        if (FAILED(pSecurityManager->MapUrlToZone(apszEvidence[i], &dwZone, 0)))
            dwZone = URLZONE_UNTRUSTED;
        if (dwZone > dwResult)
            dwResult = dwZone;
    }
    return dwResult;
}

BOOL CAttachmentServices::IsDangerous()
{
    LPCWSTR pszName = m_FileName ? m_FileName : m_LocalPath;
    return AssocIsDangerous(PathFindExtensionW(PathFindFileNameW(pszName)));
}

HRESULT CAttachmentServices::EvaluatePolicy()
{
    DWORD dwZone = GetZone();
    if (dwZone <= URLZONE_TRUSTED)
        return S_OK;
    if (dwZone == URLZONE_INTERNET)
        return S_FALSE;
    return IsDangerous() ? INET_E_SECURITY_PROBLEM : S_FALSE;
}

HRESULT CAttachmentServices::WriteZoneIdentifier(DWORD Zone)
{
    CHAR szData[4096];
    HRESULT hr = StringCbPrintfA(szData, sizeof(szData), "[ZoneTransfer]\r\nZoneId=%lu\r\n", Zone);
    LPCWSTR apszKeys[] = { L"ReferrerUrl", L"HostUrl" };
    LPCWSTR apszValues[] = { m_Referrer, m_Source };
    for (UINT i = 0; SUCCEEDED(hr) && i < _countof(apszKeys); ++i)
    {
        if (!apszValues[i])
            continue;
        CHAR szKey[16], szValue[2048];
        WideCharToMultiByte(CP_ACP, 0, apszKeys[i], -1, szKey, sizeof(szKey), NULL, NULL);
        if (!WideCharToMultiByte(CP_ACP, 0, apszValues[i], -1, szValue, sizeof(szValue), NULL, NULL))
            return HRESULT_FROM_WIN32(GetLastError());
        hr = StringCbPrintfA(szData + strlen(szData), sizeof(szData) - strlen(szData), "%s=%s\r\n", szKey, szValue);
    }
    if (FAILED(hr))
        return hr;

    WCHAR szStream[MAX_PATH + 32];
    hr = StringCchPrintfW(szStream, _countof(szStream), L"%s:Zone.Identifier", (LPCWSTR)m_LocalPath);
    if (FAILED(hr))
        return hr;

    HANDLE hFile = CreateFileW(szStream, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD cbWritten;
    DWORD cbData = (DWORD)strlen(szData);
    BOOL bOk = WriteFile(hFile, szData, cbData, &cbWritten, NULL) && cbWritten == cbData;
    hr = bOk ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(hFile);
    return hr;
}

STDMETHODIMP CAttachmentServices::SetClientTitle(LPCWSTR pszTitle)
{
    return SetString(m_Title, pszTitle, FALSE);
}

STDMETHODIMP CAttachmentServices::SetClientGuid(REFGUID guid)
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    m_ClientGuid = guid;
    m_HasClientGuid = TRUE;
    return S_OK;
}

STDMETHODIMP CAttachmentServices::SetLocalPath(LPCWSTR pszLocalPath)
{
    return SetString(m_LocalPath, pszLocalPath, TRUE);
}

STDMETHODIMP CAttachmentServices::SetFileName(LPCWSTR pszFileName)
{
    return SetString(m_FileName, pszFileName, FALSE);
}

STDMETHODIMP CAttachmentServices::SetSource(LPCWSTR pszSource)
{
    return SetString(m_Source, pszSource, FALSE);
}

STDMETHODIMP CAttachmentServices::SetReferrer(LPCWSTR pszReferrer)
{
    return SetString(m_Referrer, pszReferrer, FALSE);
}

STDMETHODIMP CAttachmentServices::CheckPolicy()
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!m_FileName && !m_LocalPath)
        return Latch(E_UNEXPECTED);
    return EvaluatePolicy();
}

STDMETHODIMP CAttachmentServices::Prompt(HWND hwnd, ATTACHMENT_PROMPT prompt, ATTACHMENT_ACTION *paction)
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!paction)
        return E_POINTER;
    *paction = ATTACHMENT_ACTION_CANCEL;
    if (!m_FileName && !m_LocalPath)
        return Latch(E_UNEXPECTED);
    if (prompt == ATTACHMENT_PROMPT_NONE)
        return S_OK;

    WCHAR szTitle[128], szQuestion[256], szFormat[128], szText[1024];
    if (m_Title)
        StringCchCopyW(szTitle, _countof(szTitle), m_Title);
    else
        LoadStringW(_AtlBaseModule.GetResourceInstance(), IDS_ATTACHMENT_TITLE, szTitle, _countof(szTitle));

    HRESULT hrPolicy = EvaluatePolicy();
    UINT idQuestion = FAILED(hrPolicy) ? IDS_ATTACHMENT_BLOCKED :
                      prompt == ATTACHMENT_PROMPT_SAVE ? IDS_ATTACHMENT_SAVE :
                      prompt == ATTACHMENT_PROMPT_EXEC ? IDS_ATTACHMENT_OPEN : IDS_ATTACHMENT_OPENSAVE;
    LoadStringW(_AtlBaseModule.GetResourceInstance(), idQuestion, szQuestion, _countof(szQuestion));

    LPCWSTR pszName = PathFindFileNameW(m_FileName ? m_FileName : m_LocalPath);
    LoadStringW(_AtlBaseModule.GetResourceInstance(), IDS_ATTACHMENT_NAME, szFormat, _countof(szFormat));
    StringCchPrintfW(szText, _countof(szText), L"%s\n\n", szQuestion);
    StringCchPrintfW(szText + wcslen(szText), _countof(szText) - wcslen(szText), szFormat, pszName);
    if (m_Source)
    {
        LoadStringW(_AtlBaseModule.GetResourceInstance(), IDS_ATTACHMENT_FROM, szFormat, _countof(szFormat));
        StringCchCatW(szText, _countof(szText), L"\n");
        StringCchPrintfW(szText + wcslen(szText), _countof(szText) - wcslen(szText), szFormat, (LPCWSTR)m_Source);
    }

    if (FAILED(hrPolicy))
    {
        MessageBoxW(hwnd, szText, szTitle, MB_OK | MB_ICONERROR);
        return S_OK;
    }

    if (prompt == ATTACHMENT_PROMPT_EXEC_OR_SAVE)
    {
        int nResult = MessageBoxW(hwnd, szText, szTitle, MB_YESNOCANCEL | MB_ICONWARNING);
        if (nResult == IDYES)
            *paction = ATTACHMENT_ACTION_EXEC;
        else if (nResult == IDNO)
            *paction = ATTACHMENT_ACTION_SAVE;
        return S_OK;
    }

    if (MessageBoxW(hwnd, szText, szTitle, MB_OKCANCEL | MB_ICONWARNING) == IDOK)
        *paction = (prompt == ATTACHMENT_PROMPT_SAVE) ? ATTACHMENT_ACTION_SAVE : ATTACHMENT_ACTION_EXEC;
    return S_OK;
}

STDMETHODIMP CAttachmentServices::Save()
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!m_LocalPath)
        return Latch(E_UNEXPECTED);
    if (GetFileAttributesW(m_LocalPath) == INVALID_FILE_ATTRIBUTES)
        return Latch(HRESULT_FROM_WIN32(GetLastError()));

    DWORD dwZone = GetZone();
    if (dwZone < URLZONE_INTERNET)
        return S_OK;
    return Latch(WriteZoneIdentifier(dwZone));
}

STDMETHODIMP CAttachmentServices::Execute(HWND hwnd, LPCWSTR pszVerb, HANDLE *phProcess)
{
    if (phProcess)
        *phProcess = NULL;
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!m_LocalPath)
        return E_INVALIDARG;

    HRESULT hr = EvaluatePolicy();
    if (FAILED(hr))
        return hr;
    if (hr == S_FALSE)
    {
        ATTACHMENT_ACTION action;
        hr = Prompt(hwnd, ATTACHMENT_PROMPT_EXEC, &action);
        if (FAILED(hr))
            return hr;
        if (action != ATTACHMENT_ACTION_EXEC)
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    hr = Save();
    if (FAILED(hr))
        return hr;

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOZONECHECKS | (phProcess ? SEE_MASK_NOCLOSEPROCESS : 0);
    sei.hwnd = hwnd;
    sei.lpVerb = pszVerb;
    sei.lpFile = m_LocalPath;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        return HRESULT_FROM_WIN32(GetLastError());
    if (phProcess)
        *phProcess = sei.hProcess;
    return S_OK;
}

STDMETHODIMP CAttachmentServices::SaveWithUI(HWND hwnd)
{
    HRESULT hr = Save();
    if (SUCCEEDED(hr) || !m_LocalPath)
        return hr;

    WCHAR szTitle[128], szText[512];
    if (m_Title)
        StringCchCopyW(szTitle, _countof(szTitle), m_Title);
    else
        LoadStringW(_AtlBaseModule.GetResourceInstance(), IDS_ATTACHMENT_TITLE, szTitle, _countof(szTitle));
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, 0,
                        szText, _countof(szText), NULL))
    {
        StringCchPrintfW(szText, _countof(szText), L"0x%08lX", hr);
    }
    MessageBoxW(hwnd, szText, szTitle, MB_OK | MB_ICONERROR);
    return hr;
}

STDMETHODIMP CAttachmentServices::ClearClientState()
{
    if (FAILED(m_hrLatched))
        return m_hrLatched;
    if (!m_HasClientGuid)
        return E_UNEXPECTED;
    return S_OK;
}
