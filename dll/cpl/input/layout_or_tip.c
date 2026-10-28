/*
 * PROJECT:     LiberNT input.dll
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Keyboard layout and text service installation API
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "input.h"

#define COBJMACROS
#include <objbase.h>
#include <msctf.h>

#define LOT_DEFAULT                     0x00000001
#define LOT_DISABLED                    0x00000002

#define LOTP_INPUTPROCESSOR             1
#define LOTP_KEYBOARDLAYOUT             2

#define ILOT_UNINSTALL                  0x00000001
#define ILOT_DEFPROFILE                 0x00000002
#define ILOT_NOAPPLYTOCURRENTSESSION    0x00000020
#define ILOT_CLEANINSTALL               0x00000040
#define ILOT_DISABLED                   0x00000080

#define SDLOT_NOAPPLYTOCURRENTSESSION   0x00000001
#define SDLOT_APPLYTOCURRENTTHREAD      0x00000002

typedef struct tagLAYOUTORTIPPROFILE
{
    DWORD dwProfileType;
    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    GUID catid;
    DWORD dwSubstituteLayout;
    DWORD dwFlags;
    WCHAR szId[MAX_PATH];
} LAYOUTORTIPPROFILE;

typedef struct
{
    LANGID langid;
    BOOL fEmpty;
    BOOL fTip;
    CLSID clsid;
    GUID guidProfile;
    DWORD dwKlid;
} LAYOUT_OR_TIP;

typedef HRESULT (WINAPI *FN_TF_CreateInputProcessorProfiles)(ITfInputProcessorProfiles **);

static const struct
{
    LANGID langid;
    LPCWSTR pszTag;
} s_LanguageTags[] =
{
    { 0x0402, L"bg" },
    { 0x0404, L"zh-Hant-TW" },
    { 0x0405, L"cs" },
    { 0x0406, L"da" },
    { 0x0408, L"el" },
    { 0x040B, L"fi" },
    { 0x040D, L"he" },
    { 0x040E, L"hu" },
    { 0x040F, L"is" },
    { 0x0411, L"ja" },
    { 0x0412, L"ko" },
    { 0x0414, L"nb" },
    { 0x0415, L"pl" },
    { 0x0419, L"ru" },
    { 0x041B, L"sk" },
    { 0x041E, L"th" },
    { 0x041F, L"tr" },
    { 0x0422, L"uk" },
    { 0x0423, L"be" },
    { 0x0424, L"sl" },
    { 0x0425, L"et" },
    { 0x0426, L"lv" },
    { 0x0427, L"lt" },
    { 0x0429, L"fa" },
    { 0x042A, L"vi" },
    { 0x0439, L"hi" },
    { 0x043F, L"kk" },
    { 0x0444, L"tt-Cyrl" },
    { 0x0804, L"zh-Hans-CN" },
    { 0x0C04, L"zh-Hant-HK" },
    { 0x1404, L"zh-Hant-MO" },
};

static const WCHAR s_szUserProfileKey[] = L"Control Panel\\International\\User Profile";
static const WCHAR s_szPreloadKey[] = L"Keyboard Layout\\Preload";

static BOOL
ParseGuid(_In_ LPCWSTR psz, _Out_ GUID *pGuid)
{
    WCHAR sz[39];
    if (psz[0] != L'{' || psz[37] != L'}')
        return FALSE;
    CopyMemory(sz, psz, 38 * sizeof(WCHAR));
    sz[38] = UNICODE_NULL;
    return SUCCEEDED(CLSIDFromString(sz, pGuid));
}

static BOOL
IsAtEnd(_In_ LPCWSTR psz)
{
    return !psz[0] || (psz[0] == L';' && !psz[1]);
}

static LPCWSTR
ParseHex(_In_ LPCWSTR psz, _Out_ ULONG *pValue)
{
    ULONGLONG value = 0;
    LPCWSTR pszStart;

    if (psz[0] == L'0' && (psz[1] == L'x' || psz[1] == L'X'))
        psz += 2;
    for (pszStart = psz; iswxdigit(*psz); ++psz)
    {
        value = value * 16 + (iswdigit(*psz) ? *psz - L'0' : (towlower(*psz) - L'a' + 10));
        if (value > ULONG_MAX)
            return NULL;
    }
    if (psz == pszStart)
        return NULL;
    *pValue = (ULONG)value;
    return psz;
}

static LPCWSTR
ParseEntry(_In_ LPCWSTR psz, _Out_ LAYOUT_OR_TIP *pEntry)
{
    LPCWSTR pszEnd;
    ULONG langid;

    ZeroMemory(pEntry, sizeof(*pEntry));

    if (*psz == L';')
        ++psz;
    if (!*psz)
        return NULL;

    pszEnd = ParseHex(psz, &langid);
    if (!pszEnd || *pszEnd != L':' || langid > 0xFFFF)
        return NULL;
    pEntry->langid = (LANGID)langid;
    psz = pszEnd + 1;

    if (!*psz || *psz == L';')
    {
        pEntry->fEmpty = TRUE;
        return psz;
    }

    if (*psz == L'{')
    {
        if (wcslen(psz) < 76 || !ParseGuid(psz, &pEntry->clsid) || !ParseGuid(psz + 38, &pEntry->guidProfile))
            return NULL;
        pEntry->fTip = TRUE;
        psz += 76;
    }
    else
    {
        psz = ParseHex(psz, &pEntry->dwKlid);
        if (!psz)
            return NULL;
    }

    if (*psz && *psz != L';')
        return NULL;
    return psz;
}

static void
FormatEntryName(_In_ const LAYOUT_OR_TIP *pEntry, _In_ BOOL bLower, _Out_writes_(cch) LPWSTR psz, _In_ SIZE_T cch)
{
    WCHAR szClsid[39], szProfile[39];

    if (!pEntry->fTip)
    {
        StringCchPrintfW(psz, cch, bLower ? L"%04x:%08x" : L"%04X:%08X", pEntry->langid, pEntry->dwKlid);
        return;
    }

    StringFromGUID2(&pEntry->clsid, szClsid, _countof(szClsid));
    StringFromGUID2(&pEntry->guidProfile, szProfile, _countof(szProfile));
    StringCchPrintfW(psz, cch, bLower ? L"%04x:%s%s" : L"%04X:%s%s", pEntry->langid, szClsid, szProfile);
}

static DWORD
EntryPreloadKlid(_In_ const LAYOUT_OR_TIP *pEntry)
{
    return pEntry->fTip ? pEntry->langid : pEntry->dwKlid;
}

static ITfInputProcessorProfiles *
CreateProfiles(_Out_ HMODULE *phMsctf)
{
    FN_TF_CreateInputProcessorProfiles fnCreate;
    ITfInputProcessorProfiles *pProfiles = NULL;

    *phMsctf = LoadLibraryW(L"msctf.dll");
    if (!*phMsctf)
        return NULL;

    fnCreate = (FN_TF_CreateInputProcessorProfiles)GetProcAddress(*phMsctf, "TF_CreateInputProcessorProfiles");
    if (!fnCreate || FAILED(fnCreate(&pProfiles)))
    {
        FreeLibrary(*phMsctf);
        *phMsctf = NULL;
        return NULL;
    }
    return pProfiles;
}

static void
ReleaseProfiles(_In_opt_ ITfInputProcessorProfiles *pProfiles, _In_opt_ HMODULE hMsctf)
{
    if (pProfiles)
        ITfInputProcessorProfiles_Release(pProfiles);
    if (hMsctf)
        FreeLibrary(hMsctf);
}

typedef struct
{
    DWORD adwKlid[64];
    UINT cKlid;
} PRELOAD_LIST;

static void
ReadPreload(_Out_ PRELOAD_LIST *pList)
{
    HKEY hPreload;
    UINT i;

    pList->cKlid = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szPreloadKey, 0, KEY_READ, &hPreload) != ERROR_SUCCESS)
        return;
    for (i = 1; pList->cKlid < _countof(pList->adwKlid); ++i)
    {
        WCHAR szName[CCH_ULONG_DEC + 1], szValue[CCH_LAYOUT_ID + 1];
        DWORD cbValue = sizeof(szValue), dwType;
        LONG lError;

        StringCchPrintfW(szName, _countof(szName), L"%u", i);
        ZeroMemory(szValue, sizeof(szValue));
        lError = RegQueryValueExW(hPreload, szName, NULL, &dwType, (LPBYTE)szValue, &cbValue);
        if (lError == ERROR_FILE_NOT_FOUND)
            break;
        if (lError == ERROR_SUCCESS && dwType == REG_SZ)
            pList->adwKlid[pList->cKlid++] = wcstoul(szValue, NULL, 16);
    }
    RegCloseKey(hPreload);
}

static BOOL
WritePreload(_In_ PRELOAD_LIST *pList)
{
    HKEY hPreload;
    UINT i, j;

    for (i = 2; i < pList->cKlid; ++i)
    {
        DWORD dwKlid = pList->adwKlid[i];
        for (j = i; j > 1 && pList->adwKlid[j - 1] > dwKlid; --j)
            pList->adwKlid[j] = pList->adwKlid[j - 1];
        pList->adwKlid[j] = dwKlid;
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, s_szPreloadKey);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, s_szPreloadKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hPreload,
                        NULL) != ERROR_SUCCESS)
        return FALSE;
    for (i = 0; i < pList->cKlid; ++i)
    {
        WCHAR szName[CCH_ULONG_DEC + 1], szKlid[CCH_LAYOUT_ID + 1];
        StringCchPrintfW(szName, _countof(szName), L"%u", i + 1);
        StringCchPrintfW(szKlid, _countof(szKlid), L"%08lx", pList->adwKlid[i]);
        RegSetValueExW(hPreload, szName, 0, REG_SZ, (const BYTE *)szKlid, (DWORD)(wcslen(szKlid) + 1) * sizeof(WCHAR));
    }
    RegCloseKey(hPreload);
    return TRUE;
}

static INT
FindPreload(_In_ const PRELOAD_LIST *pList, _In_ DWORD dwKlid, _In_ BOOL bLanguageOnly)
{
    UINT i;
    for (i = 0; i < pList->cKlid; ++i)
    {
        if (pList->adwKlid[i] == dwKlid || (bLanguageOnly && LOWORD(pList->adwKlid[i]) == LOWORD(dwKlid)))
            return (INT)i;
    }
    return -1;
}

static void
RemovePreloadAt(_Inout_ PRELOAD_LIST *pList, _In_ UINT index)
{
    MoveMemory(&pList->adwKlid[index], &pList->adwKlid[index + 1], (pList->cKlid - index - 1) * sizeof(DWORD));
    --pList->cKlid;
}

static void
MakePreloadDefault(_Inout_ PRELOAD_LIST *pList, _In_ UINT index)
{
    DWORD dwKlid = pList->adwKlid[index];
    MoveMemory(&pList->adwKlid[1], &pList->adwKlid[0], index * sizeof(DWORD));
    pList->adwKlid[0] = dwKlid;
}

static BOOL
FindEntryValue(_In_ HKEY hUserProfile, _In_ const LAYOUT_OR_TIP *pEntry, _Out_writes_(cchSubKey) LPWSTR pszSubKey,
               _In_ DWORD cchSubKey)
{
    WCHAR szName[MAX_PATH];
    DWORD dwIndex;

    FormatEntryName(pEntry, FALSE, szName, _countof(szName));
    for (dwIndex = 0;; ++dwIndex)
    {
        HKEY hLang;
        DWORD cch = cchSubKey;
        LSTATUS error;

        if (RegEnumKeyExW(hUserProfile, dwIndex, pszSubKey, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            return FALSE;
        if (RegOpenKeyExW(hUserProfile, pszSubKey, 0, KEY_QUERY_VALUE, &hLang) != ERROR_SUCCESS)
            continue;
        error = RegQueryValueExW(hLang, szName, NULL, NULL, NULL, NULL);
        RegCloseKey(hLang);
        if (error == ERROR_SUCCESS)
            return TRUE;
    }
}

static void
GetLanguageSubKey(_In_ HKEY hUserProfile, _In_ LANGID langid, _Out_writes_(cchSubKey) LPWSTR pszSubKey,
                  _In_ DWORD cchSubKey)
{
    WCHAR szPrefix[6];
    DWORD dwIndex;

    StringCchPrintfW(szPrefix, _countof(szPrefix), L"%04X:", langid);

    for (dwIndex = 0;; ++dwIndex)
    {
        HKEY hLang;
        DWORD cch = cchSubKey, dwValue;
        BOOL bFound = FALSE;

        if (RegEnumKeyExW(hUserProfile, dwIndex, pszSubKey, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(hUserProfile, pszSubKey, 0, KEY_QUERY_VALUE, &hLang) != ERROR_SUCCESS)
            continue;
        for (dwValue = 0; !bFound; ++dwValue)
        {
            WCHAR szName[MAX_PATH];
            DWORD cchName = _countof(szName);
            if (RegEnumValueW(hLang, dwValue, szName, &cchName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            bFound = !_wcsnicmp(szName, szPrefix, 5);
        }
        RegCloseKey(hLang);
        if (bFound)
            return;
    }

    for (dwIndex = 0; dwIndex < _countof(s_LanguageTags); ++dwIndex)
    {
        if (s_LanguageTags[dwIndex].langid == langid)
        {
            StringCchCopyW(pszSubKey, cchSubKey, s_LanguageTags[dwIndex].pszTag);
            return;
        }
    }

    if (!LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), pszSubKey, cchSubKey, 0))
        StringCchPrintfW(pszSubKey, cchSubKey, L"%04X", langid);
}

static BOOL
ReadLanguages(_In_ HKEY hUserProfile, _Out_writes_(cch) LPWSTR pszMulti, _In_ DWORD cch)
{
    DWORD cb = (cch - 2) * sizeof(WCHAR), dwType;

    ZeroMemory(pszMulti, cch * sizeof(WCHAR));
    if (RegQueryValueExW(hUserProfile, L"Languages", NULL, &dwType, (LPBYTE)pszMulti, &cb) != ERROR_SUCCESS ||
        dwType != REG_MULTI_SZ)
    {
        ZeroMemory(pszMulti, cch * sizeof(WCHAR));
        return FALSE;
    }
    return TRUE;
}

static void
WriteLanguages(_In_ HKEY hUserProfile, _In_ LPCWSTR pszMulti)
{
    LPCWSTR p = pszMulti;
    while (*p)
        p += wcslen(p) + 1;
    RegSetValueExW(hUserProfile, L"Languages", 0, REG_MULTI_SZ, (const BYTE *)pszMulti,
                   (DWORD)(p - pszMulti + 1) * sizeof(WCHAR));
}

static void
AddLanguage(_In_ HKEY hUserProfile, _In_ LPCWSTR pszTag)
{
    WCHAR szMulti[1024];
    LPWSTR p;

    ReadLanguages(hUserProfile, szMulti, _countof(szMulti));
    for (p = szMulti; *p; p += wcslen(p) + 1)
    {
        if (!_wcsicmp(p, pszTag))
            return;
    }
    if ((SIZE_T)(p - szMulti) + wcslen(pszTag) + 2 > _countof(szMulti))
        return;
    StringCchCopyW(p, _countof(szMulti) - (p - szMulti), pszTag);
    p[wcslen(pszTag) + 1] = UNICODE_NULL;
    WriteLanguages(hUserProfile, szMulti);
}

static void
RemoveLanguage(_In_ HKEY hUserProfile, _In_ LPCWSTR pszTag)
{
    WCHAR szMulti[1024], szNew[1024];
    LPWSTR p, q = szNew;

    if (!ReadLanguages(hUserProfile, szMulti, _countof(szMulti)))
        return;
    ZeroMemory(szNew, sizeof(szNew));
    for (p = szMulti; *p; p += wcslen(p) + 1)
    {
        if (!_wcsicmp(p, pszTag))
            continue;
        StringCchCopyW(q, _countof(szNew) - (q - szNew), p);
        q += wcslen(p) + 1;
    }
    WriteLanguages(hUserProfile, szNew);
}

static BOOL
LanguageHasEntries(_In_ HKEY hUserProfile, _In_ LPCWSTR pszSubKey)
{
    HKEY hLang;
    DWORD cValues = 0;

    if (RegOpenKeyExW(hUserProfile, pszSubKey, 0, KEY_QUERY_VALUE, &hLang) != ERROR_SUCCESS)
        return FALSE;
    RegQueryInfoKeyW(hLang, NULL, NULL, NULL, NULL, NULL, NULL, &cValues, NULL, NULL, NULL, NULL);
    RegCloseKey(hLang);
    return cValues != 0;
}

static BOOL
IsTipRegistered(_In_ ITfInputProcessorProfiles *pProfiles, _In_ const LAYOUT_OR_TIP *pEntry)
{
    IEnumTfLanguageProfiles *pEnum = NULL;
    TF_LANGUAGEPROFILE profile;
    ULONG cFetched;
    BOOL bFound = FALSE;

    if (FAILED(ITfInputProcessorProfiles_EnumLanguageProfiles(pProfiles, pEntry->langid, &pEnum)) || !pEnum)
        return FALSE;
    while (!bFound && IEnumTfLanguageProfiles_Next(pEnum, 1, &profile, &cFetched) == S_OK && cFetched)
    {
        bFound = IsEqualCLSID(&profile.clsid, &pEntry->clsid) &&
                 IsEqualGUID(&profile.guidProfile, &pEntry->guidProfile) &&
                 IsEqualGUID(&profile.catid, &GUID_TFCAT_TIP_KEYBOARD);
    }
    IEnumTfLanguageProfiles_Release(pEnum);
    return bFound;
}

static BOOL
InstallEntry(_In_opt_ ITfInputProcessorProfiles *pProfiles, _In_ HKEY hUserProfile, _Inout_ PRELOAD_LIST *pPreload,
             _In_ const LAYOUT_OR_TIP *pEntry, _In_ DWORD dwFlags, _In_ BOOL bDefault)
{
    WCHAR szName[MAX_PATH], szSubKey[LOCALE_NAME_MAX_LENGTH + 1];
    BOOL bEnable = !(dwFlags & (ILOT_UNINSTALL | ILOT_DISABLED));
    DWORD dwOne = 1, dwKlid = EntryPreloadKlid(pEntry);
    HKEY hLang;
    INT index;

    if (pEntry->fTip)
    {
        if (!pProfiles)
            return FALSE;
        if (!IsTipRegistered(pProfiles, pEntry))
            return TRUE;
        if (FAILED(ITfInputProcessorProfiles_EnableLanguageProfile(pProfiles, &pEntry->clsid, pEntry->langid,
                                                                    &pEntry->guidProfile, bEnable)))
            return FALSE;
        if (bEnable && bDefault)
        {
            ITfInputProcessorProfiles_SetDefaultLanguageProfile(pProfiles, pEntry->langid, &pEntry->clsid,
                                                                &pEntry->guidProfile);
        }
    }

    FormatEntryName(pEntry, FALSE, szName, _countof(szName));

    if (dwFlags & ILOT_UNINSTALL)
    {
        if (dwFlags & ILOT_CLEANINSTALL)
        {
            GetLanguageSubKey(hUserProfile, pEntry->langid, szSubKey, _countof(szSubKey));
            if (RegCreateKeyExW(hUserProfile, szSubKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hLang, NULL) == ERROR_SUCCESS)
                RegCloseKey(hLang);
            AddLanguage(hUserProfile, szSubKey);
        }
        if (FindEntryValue(hUserProfile, pEntry, szSubKey, _countof(szSubKey)))
        {
            if (RegOpenKeyExW(hUserProfile, szSubKey, 0, KEY_SET_VALUE, &hLang) == ERROR_SUCCESS)
            {
                RegDeleteValueW(hLang, szName);
                RegCloseKey(hLang);
            }
            if (!(dwFlags & ILOT_CLEANINSTALL) && !LanguageHasEntries(hUserProfile, szSubKey))
            {
                RegDeleteTreeW(hUserProfile, szSubKey);
                RemoveLanguage(hUserProfile, szSubKey);
            }
        }
        if (!pEntry->fTip)
        {
            index = FindPreload(pPreload, dwKlid, FALSE);
            if (index >= 0)
                RemovePreloadAt(pPreload, (UINT)index);
        }
        return TRUE;
    }

    if (!FindEntryValue(hUserProfile, pEntry, szSubKey, _countof(szSubKey)))
        GetLanguageSubKey(hUserProfile, pEntry->langid, szSubKey, _countof(szSubKey));
    if (RegCreateKeyExW(hUserProfile, szSubKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hLang, NULL) != ERROR_SUCCESS)
        return FALSE;
    AddLanguage(hUserProfile, szSubKey);

    if (dwFlags & ILOT_DISABLED)
    {
        RegCloseKey(hLang);
        return TRUE;
    }

    if (RegSetValueExW(hLang, szName, 0, REG_DWORD, (const BYTE *)&dwOne, sizeof(dwOne)) != ERROR_SUCCESS)
    {
        RegCloseKey(hLang);
        return FALSE;
    }
    RegCloseKey(hLang);

    index = FindPreload(pPreload, dwKlid, pEntry->fTip);
    if (index < 0 && pPreload->cKlid < _countof(pPreload->adwKlid))
    {
        pPreload->adwKlid[pPreload->cKlid] = dwKlid;
        index = (INT)pPreload->cKlid++;
    }
    if (bDefault && index >= 0)
        MakePreloadDefault(pPreload, (UINT)index);
    return TRUE;
}

static HKL
LoadLayoutForKlid(_In_ DWORD dwKlid)
{
    WCHAR szKlid[CCH_LAYOUT_ID + 1];

    StringCchPrintfW(szKlid, _countof(szKlid), L"%08lx", dwKlid);
    return LoadKeyboardLayoutW(szKlid, KLF_NOTELLSHELL);
}

static void
ApplyToSession(_In_ LPCWSTR psz, _In_ const PRELOAD_LIST *pPreload, _In_ DWORD dwFlags)
{
    LAYOUT_OR_TIP entry;
    LPCWSTR pszNext;
    UINT i;

    if (dwFlags & ILOT_UNINSTALL)
    {
        for (pszNext = ParseEntry(psz, &entry); pszNext; pszNext = ParseEntry(pszNext, &entry))
        {
            HKL hkl;
            if (entry.fEmpty || entry.fTip)
                continue;
            hkl = LoadLayoutForKlid(entry.dwKlid);
            if (hkl)
                UnloadKeyboardLayout(hkl);
        }
        return;
    }

    for (i = 0; i < pPreload->cKlid; ++i)
        LoadLayoutForKlid(pPreload->adwKlid[i]);
}

static void
RemoveAllEntries(_In_opt_ ITfInputProcessorProfiles *pProfiles, _In_ HKEY hUserProfile)
{
    WCHAR szSubKey[LOCALE_NAME_MAX_LENGTH + 1];
    DWORD dwIndex = 0;

    for (;;)
    {
        DWORD cch = _countof(szSubKey), dwValue;
        HKEY hLang;

        if (RegEnumKeyExW(hUserProfile, dwIndex, szSubKey, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (!LanguageHasEntries(hUserProfile, szSubKey))
        {
            ++dwIndex;
            continue;
        }
        if (pProfiles && RegOpenKeyExW(hUserProfile, szSubKey, 0, KEY_QUERY_VALUE, &hLang) == ERROR_SUCCESS)
        {
            for (dwValue = 0;; ++dwValue)
            {
                WCHAR szName[MAX_PATH];
                DWORD cchName = _countof(szName);
                LAYOUT_OR_TIP entry;
                if (RegEnumValueW(hLang, dwValue, szName, &cchName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;
                if (ParseEntry(szName, &entry) && entry.fTip)
                {
                    ITfInputProcessorProfiles_EnableLanguageProfile(pProfiles, &entry.clsid, entry.langid,
                                                                    &entry.guidProfile, FALSE);
                }
            }
            RegCloseKey(hLang);
        }
        if (RegDeleteTreeW(hUserProfile, szSubKey) != ERROR_SUCCESS)
            ++dwIndex;
        RemoveLanguage(hUserProfile, szSubKey);
    }
}

BOOL WINAPI
InstallLayoutOrTipUserReg(
    _In_opt_ LPCWSTR pszUserReg,
    _In_opt_ LPCWSTR pszSystemReg,
    _In_opt_ LPCWSTR pszSoftwareReg,
    _In_ LPCWSTR psz,
    _In_ DWORD dwFlags)
{
    ITfInputProcessorProfiles *pProfiles;
    HMODULE hMsctf;
    LAYOUT_OR_TIP entry;
    LPCWSTR pszNext;
    PRELOAD_LIST preload;
    HKEY hUserProfile;
    BOOL bResult = TRUE, bDefault;

    if (!psz)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (pszUserReg || pszSystemReg || pszSoftwareReg)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    for (pszNext = psz; !IsAtEnd(pszNext);)
    {
        pszNext = ParseEntry(pszNext, &entry);
        if (!pszNext)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, s_szUserProfileKey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL,
                        &hUserProfile, NULL) != ERROR_SUCCESS)
        return FALSE;

    pProfiles = CreateProfiles(&hMsctf);
    ReadPreload(&preload);

    if (dwFlags & ILOT_CLEANINSTALL)
    {
        RemoveAllEntries(pProfiles, hUserProfile);
        preload.cKlid = 0;
    }

    bDefault = !!(dwFlags & ILOT_DEFPROFILE);
    for (pszNext = ParseEntry(psz, &entry); pszNext; pszNext = ParseEntry(pszNext, &entry))
    {
        if (entry.fEmpty)
            continue;
        if (!InstallEntry(pProfiles, hUserProfile, &preload, &entry, dwFlags, bDefault))
            bResult = FALSE;
        bDefault = FALSE;
    }

    WritePreload(&preload);
    if (!(dwFlags & ILOT_NOAPPLYTOCURRENTSESSION))
        ApplyToSession(psz, &preload, dwFlags);
    RegCloseKey(hUserProfile);
    ReleaseProfiles(pProfiles, hMsctf);
    return bResult;
}

BOOL WINAPI
InstallLayoutOrTip(_In_ LPCWSTR psz, _In_ DWORD dwFlags)
{
    return InstallLayoutOrTipUserReg(NULL, NULL, NULL, psz, dwFlags);
}

BOOL WINAPI
SetDefaultLayoutOrTip(_In_ LPCWSTR psz, _In_ DWORD dwFlags)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    WCHAR szSubKey[LOCALE_NAME_MAX_LENGTH + 1];
    HMODULE hMsctf = NULL;
    LAYOUT_OR_TIP entry;
    PRELOAD_LIST preload;
    HKEY hUserProfile;
    BOOL bFound;
    INT index;

    if (!psz || !ParseEntry(psz, &entry) || entry.fEmpty)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserProfileKey, 0, KEY_READ, &hUserProfile) != ERROR_SUCCESS)
        return FALSE;
    bFound = FindEntryValue(hUserProfile, &entry, szSubKey, _countof(szSubKey));
    RegCloseKey(hUserProfile);
    if (!bFound)
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    ReadPreload(&preload);
    index = FindPreload(&preload, EntryPreloadKlid(&entry), entry.fTip);
    if (index < 0 && preload.cKlid < _countof(preload.adwKlid))
    {
        preload.adwKlid[preload.cKlid] = EntryPreloadKlid(&entry);
        index = (INT)preload.cKlid++;
    }
    if (index >= 0)
        MakePreloadDefault(&preload, (UINT)index);
    WritePreload(&preload);

    if (entry.fTip)
    {
        pProfiles = CreateProfiles(&hMsctf);
        if (pProfiles)
        {
            ITfInputProcessorProfiles_SetDefaultLanguageProfile(pProfiles, entry.langid, &entry.clsid,
                                                                &entry.guidProfile);
        }
        ReleaseProfiles(pProfiles, hMsctf);
    }

    return TRUE;
}

UINT WINAPI
EnumEnabledLayoutOrTip(
    _In_opt_ LPCWSTR pszUserReg,
    _In_opt_ LPCWSTR pszSystemReg,
    _In_opt_ LPCWSTR pszSoftwareReg,
    _Out_writes_opt_(uBufLength) LAYOUTORTIPPROFILE *pLayoutOrTipProfile,
    _In_ UINT uBufLength)
{
    WCHAR szSubKey[LOCALE_NAME_MAX_LENGTH + 1];
    LAYOUT_OR_TIP entries[64];
    PRELOAD_LIST preload;
    DWORD dwSubKey;
    HKEY hUserProfile;
    UINT cEntries = 0, i, j, cCopied;
    INT iDefault = -1;

    if (pszUserReg || pszSystemReg || pszSoftwareReg)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserProfileKey, 0, KEY_READ, &hUserProfile) != ERROR_SUCCESS)
        return 0;

    for (dwSubKey = 0; cEntries < _countof(entries); ++dwSubKey)
    {
        DWORD cchSubKey = _countof(szSubKey), dwValue;
        HKEY hLang;

        if (RegEnumKeyExW(hUserProfile, dwSubKey, szSubKey, &cchSubKey, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(hUserProfile, szSubKey, 0, KEY_QUERY_VALUE, &hLang) != ERROR_SUCCESS)
            continue;

        for (dwValue = 0; cEntries < _countof(entries); ++dwValue)
        {
            WCHAR szName[MAX_PATH];
            DWORD cchName = _countof(szName), dwType;
            LAYOUT_OR_TIP entry;

            if (RegEnumValueW(hLang, dwValue, szName, &cchName, NULL, &dwType, NULL, NULL) != ERROR_SUCCESS)
                break;
            if (dwType != REG_DWORD || !ParseEntry(szName, &entry) || entry.fEmpty)
                continue;

            for (j = cEntries; j > 0 && entries[j - 1].langid > entry.langid; --j)
                entries[j] = entries[j - 1];
            entries[j] = entry;
            ++cEntries;
        }
        RegCloseKey(hLang);
    }
    RegCloseKey(hUserProfile);

    ReadPreload(&preload);
    if (preload.cKlid)
    {
        for (i = 0; i < cEntries && iDefault < 0; ++i)
        {
            if (!entries[i].fTip && entries[i].dwKlid == preload.adwKlid[0])
                iDefault = (INT)i;
        }
        for (i = 0; i < cEntries && iDefault < 0; ++i)
        {
            if (entries[i].fTip && entries[i].langid == LOWORD(preload.adwKlid[0]))
                iDefault = (INT)i;
        }
    }

    if (!pLayoutOrTipProfile)
        return cEntries;

    cCopied = min(cEntries, uBufLength);
    for (i = 0; i < cCopied; ++i)
    {
        LAYOUTORTIPPROFILE *pProfile = &pLayoutOrTipProfile[i];
        ZeroMemory(pProfile, sizeof(*pProfile));
        pProfile->dwProfileType = entries[i].fTip ? LOTP_INPUTPROCESSOR : LOTP_KEYBOARDLAYOUT;
        pProfile->langid = entries[i].langid;
        pProfile->clsid = entries[i].clsid;
        pProfile->guidProfile = entries[i].guidProfile;
        if (entries[i].fTip)
            pProfile->catid = GUID_TFCAT_TIP_KEYBOARD;
        pProfile->dwFlags = ((INT)i == iDefault) ? LOT_DEFAULT : 0;
        FormatEntryName(&entries[i], TRUE, pProfile->szId, _countof(pProfile->szId));
    }
    return cCopied;
}
