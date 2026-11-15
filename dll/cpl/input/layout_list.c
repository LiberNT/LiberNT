/*
 * PROJECT:         input.dll
 * FILE:            dll/cpl/input/layout_list.c
 * PURPOSE:         input.dll
 * PROGRAMMER:      Dmitry Chapyshev (dmitry@reactos.org)
 *                  Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include "layout_list.h"

static LAYOUT_LIST_NODE *_LayoutList = NULL;

static LAYOUT_LIST_NODE*
LayoutList_AppendNode(DWORD dwKLID, WORD wSpecialId, LPCWSTR pszFile, LPCWSTR pszName,
                      LPCWSTR pszImeFile)
{
    LAYOUT_LIST_NODE *pCurrent;
    LAYOUT_LIST_NODE *pNew;

    if (pszName == NULL)
        return NULL;

    pCurrent = _LayoutList;

    pNew = (LAYOUT_LIST_NODE*)malloc(sizeof(LAYOUT_LIST_NODE));
    if (pNew == NULL)
        return NULL;

    ZeroMemory(pNew, sizeof(LAYOUT_LIST_NODE));

    pNew->dwKLID = dwKLID;
    pNew->wSpecialId = wSpecialId;

    pNew->pszName = _wcsdup(pszName);
    pNew->pszFile = _wcsdup(pszFile);
    pNew->pszImeFile = _wcsdup(pszImeFile);
    if (pNew->pszName == NULL || pNew->pszFile == NULL ||
        (pszImeFile && pNew->pszImeFile == NULL))
    {
        free(pNew->pszName);
        free(pNew->pszFile);
        free(pNew->pszImeFile);
        free(pNew);
        return NULL;
    }

    if (pCurrent == NULL)
    {
        _LayoutList = pNew;
    }
    else
    {
        while (pCurrent->pNext != NULL)
        {
            pCurrent = pCurrent->pNext;
        }

        pNew->pPrev = pCurrent;
        pCurrent->pNext = pNew;
    }

    return pNew;
}


VOID
LayoutList_Destroy(VOID)
{
    LAYOUT_LIST_NODE *pCurrent;
    LAYOUT_LIST_NODE *pNext;

    if (_LayoutList == NULL)
        return;

    for (pCurrent = _LayoutList; pCurrent; pCurrent = pNext)
    {
        pNext = pCurrent->pNext;

        free(pCurrent->pszName);
        free(pCurrent->pszFile);
        free(pCurrent->pszImeFile);
        free(pCurrent);
    }

    _LayoutList = NULL;
}

typedef HRESULT (WINAPI *FN_SHLoadRegUIStringW)(HKEY, LPCWSTR, LPWSTR, DWORD);

HRESULT FakeSHLoadRegUIStringW(HKEY hkey, LPCWSTR value, LPWSTR buf, DWORD size)
{
    HRESULT hr = E_FAIL;
    HINSTANCE hSHLWAPI = LoadLibraryW(L"shlwapi");
    FN_SHLoadRegUIStringW fn;
    fn = (FN_SHLoadRegUIStringW)GetProcAddress(hSHLWAPI, (LPCSTR)(INT_PTR)439);
    if (fn)
        hr = fn(hkey, value, buf, size);
    FreeLibrary(hSHLWAPI);
    return hr;
}

static BOOL
LayoutList_ReadLayout(HKEY hLayoutKey, LPCWSTR szKLID, LPCWSTR szSystemDirectory)
{
    WCHAR szFile[80], szImeFile[80], szBuffer[MAX_PATH], szFilePath[MAX_PATH];
    DWORD dwSize, dwKLID = DWORDfromString(szKLID);
    WORD wSpecialId = 0;
    LPWSTR pszImeFile = NULL;

    dwSize = sizeof(szFile);
    if (RegQueryValueExW(hLayoutKey, L"Layout File", NULL, NULL,
                         (LPBYTE)szFile, &dwSize) != ERROR_SUCCESS)
    {
        return FALSE; /* No "Layout File" value */
    }

    if (IS_IME_KLID(dwKLID))
    {
        WCHAR szPath[MAX_PATH];
        dwSize = sizeof(szImeFile);
        if (RegQueryValueExW(hLayoutKey, L"IME File", NULL, NULL,
                             (LPBYTE)szImeFile, &dwSize) != ERROR_SUCCESS)
        {
            return FALSE; /* No "IME File" value */
        }

        if (wcschr(szImeFile, L'\\') != NULL)
            return FALSE; /* Invalid character */

        GetSystemLibraryPath(szPath, ARRAYSIZE(szPath), szImeFile);
        if (GetFileAttributesW(szPath) == INVALID_FILE_ATTRIBUTES)
            return FALSE; /* Does not exist */

        pszImeFile = szImeFile;
    }

    /* Build the "Layout File" full path and check existence */
    StringCchPrintfW(szFilePath, ARRAYSIZE(szFilePath), L"%s\\%s", szSystemDirectory, szFile);
    if (GetFileAttributesW(szFilePath) == INVALID_FILE_ATTRIBUTES)
        return FALSE; /* No layout file found */

    /* Get the special ID */
    dwSize = sizeof(szBuffer);
    if (RegQueryValueExW(hLayoutKey, L"Layout Id", NULL, NULL,
                         (LPBYTE)szBuffer, &dwSize) == ERROR_SUCCESS)
    {
        wSpecialId = LOWORD(DWORDfromString(szBuffer));
    }

    /* If there is a valid "Layout Display Name", then use it as the entry name */
    if (FakeSHLoadRegUIStringW(hLayoutKey, L"Layout Display Name",
                               szBuffer, ARRAYSIZE(szBuffer)) == S_OK)
    {
        LayoutList_AppendNode(dwKLID, wSpecialId, szFile, szBuffer, pszImeFile);
        return TRUE;
    }

    /* Otherwise, use "Layout Text" value as the entry name */
    dwSize = sizeof(szBuffer);
    if (RegQueryValueExW(hLayoutKey, L"Layout Text", NULL, NULL,
                         (LPBYTE)szBuffer, &dwSize) == ERROR_SUCCESS)
    {
        LayoutList_AppendNode(dwKLID, wSpecialId, szFile, szBuffer, pszImeFile);
        return TRUE;
    }

    return FALSE;
}

static VOID
LayoutList_ReadTips(VOID)
{
    ITfInputProcessorProfiles *pProfiles;
    WCHAR szClsid[39], szLang[16], szKey[MAX_PATH];
    LANGID alangid[64];
    UINT clangid = 0, i;
    HMODULE hMsctf;
    HKEY hTips, hProfiles;
    DWORD dwTip, dwLang, cch;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\CTF\\TIP", 0, KEY_READ, &hTips) != ERROR_SUCCESS)
        return;
    for (dwTip = 0; clangid < ARRAYSIZE(alangid); ++dwTip)
    {
        cch = ARRAYSIZE(szClsid);
        if (RegEnumKeyExW(hTips, dwTip, szClsid, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szKey, ARRAYSIZE(szKey), L"%s\\LanguageProfile", szClsid);
        if (RegOpenKeyExW(hTips, szKey, 0, KEY_READ, &hProfiles) != ERROR_SUCCESS)
            continue;
        for (dwLang = 0; clangid < ARRAYSIZE(alangid); ++dwLang)
        {
            LANGID wLangId;

            cch = ARRAYSIZE(szLang);
            if (RegEnumKeyExW(hProfiles, dwLang, szLang, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            wLangId = LOWORD(wcstoul(szLang, NULL, 16));
            for (i = 0; i < clangid && alangid[i] != wLangId; ++i)
                ;
            if (wLangId && i == clangid)
                alangid[clangid++] = wLangId;
        }
        RegCloseKey(hProfiles);
    }
    RegCloseKey(hTips);

    pProfiles = CreateProfiles(&hMsctf);
    if (!pProfiles)
        return;
    for (i = 0; i < clangid; ++i)
    {
        IEnumTfLanguageProfiles *pEnum = NULL;
        TF_LANGUAGEPROFILE profile;
        ULONG cFetched;

        if (FAILED(ITfInputProcessorProfiles_EnumLanguageProfiles(pProfiles, alangid[i], &pEnum)) || !pEnum)
            continue;
        while (IEnumTfLanguageProfiles_Next(pEnum, 1, &profile, &cFetched) == S_OK && cFetched)
        {
            LAYOUT_LIST_NODE *pNode;
            BSTR bstrName = NULL;

            if (!IsEqualGUID(&profile.catid, &GUID_TFCAT_TIP_KEYBOARD) ||
                LayoutList_GetTip(profile.langid, &profile.clsid, &profile.guidProfile) ||
                FAILED(ITfInputProcessorProfiles_GetLanguageProfileDescription(pProfiles, &profile.clsid,
                                                                               profile.langid,
                                                                               &profile.guidProfile, &bstrName)) ||
                !bstrName)
            {
                continue;
            }
            pNode = LayoutList_AppendNode(0, 0, L"", bstrName, NULL);
            SysFreeString(bstrName);
            if (!pNode)
                continue;
            pNode->bTip = TRUE;
            pNode->wTipLangId = profile.langid;
            pNode->clsidTip = profile.clsid;
            pNode->guidTipProfile = profile.guidProfile;
        }
        IEnumTfLanguageProfiles_Release(pEnum);
    }
    ReleaseProfiles(pProfiles, hMsctf);
}

VOID
LayoutList_Create(VOID)
{
    WCHAR szSystemDirectory[MAX_PATH], szKLID[KL_NAMELENGTH];
    DWORD dwSize, dwIndex;
    HKEY hKey, hLayoutKey;

    if (!GetSystemDirectoryW(szSystemDirectory, ARRAYSIZE(szSystemDirectory)))
        return;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return;
    }

    for (dwIndex = 0; ; ++dwIndex)
    {
        dwSize = ARRAYSIZE(szKLID);
        if (RegEnumKeyExW(hKey, dwIndex, szKLID, &dwSize, NULL, NULL,
                          NULL, NULL) != ERROR_SUCCESS)
        {
            break;
        }

        if (RegOpenKeyExW(hKey, szKLID, 0, KEY_QUERY_VALUE, &hLayoutKey) == ERROR_SUCCESS)
        {
            LayoutList_ReadLayout(hLayoutKey, szKLID, szSystemDirectory);
            RegCloseKey(hLayoutKey);
        }
    }

    RegCloseKey(hKey);
    LayoutList_ReadTips();
}


LAYOUT_LIST_NODE*
LayoutList_GetByHkl(HKL hkl)
{
    LAYOUT_LIST_NODE *pCurrent;

    if (IS_SPECIAL_HKL(hkl))
    {
        WORD wSpecialId = SPECIALIDFROMHKL(hkl);

        for (pCurrent = _LayoutList; pCurrent != NULL; pCurrent = pCurrent->pNext)
        {
            if (wSpecialId == pCurrent->wSpecialId)
            {
                return pCurrent;
            }
        }
    }
    else if (IS_IME_HKL(hkl))
    {
        for (pCurrent = _LayoutList; pCurrent != NULL; pCurrent = pCurrent->pNext)
        {
            if (hkl == UlongToHandle(pCurrent->dwKLID))
            {
                return pCurrent;
            }
        }
    }
    else
    {
        for (pCurrent = _LayoutList; pCurrent != NULL; pCurrent = pCurrent->pNext)
        {
            if (HIWORD(hkl) == LOWORD(pCurrent->dwKLID))
            {
                return pCurrent;
            }
        }
    }

    return NULL;
}


LAYOUT_LIST_NODE*
LayoutList_GetFirst(VOID)
{
    return _LayoutList;
}

LAYOUT_LIST_NODE*
LayoutList_GetTip(LANGID wLangId, const CLSID *pclsid, const GUID *pguidProfile)
{
    LAYOUT_LIST_NODE *pCurrent;

    for (pCurrent = _LayoutList; pCurrent != NULL; pCurrent = pCurrent->pNext)
    {
        if (pCurrent->bTip && pCurrent->wTipLangId == wLangId && IsEqualCLSID(&pCurrent->clsidTip, pclsid) &&
            IsEqualGUID(&pCurrent->guidTipProfile, pguidProfile))
        {
            return pCurrent;
        }
    }
    return NULL;
}
