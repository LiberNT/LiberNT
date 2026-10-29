/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for installing a text service through the input.dll layout or TIP functions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "testtip.h"

typedef struct
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

typedef BOOL (WINAPI *FN_InstallLayoutOrTip)(LPCWSTR, DWORD);
typedef UINT (WINAPI *FN_EnumEnabledLayoutOrTip)(LPCWSTR, LPCWSTR, LPCWSTR, LAYOUTORTIPPROFILE *, UINT);

static const WCHAR s_szUserProfile[] = L"Control Panel\\International\\User Profile";
static const WCHAR s_szPreload[] = L"Keyboard Layout\\Preload";
static const WCHAR s_szBackup[] = L"Software\\LiberNTMsctfLayoutTest";

static FN_InstallLayoutOrTip pInstallLayoutOrTip;
static FN_EnumEnabledLayoutOrTip pEnumEnabledLayoutOrTip;

static void BackupKey(LPCWSTR pszKey, LPCWSTR pszName)
{
    WCHAR szDest[MAX_PATH];
    HKEY hSrc, hDest;

    StringCchPrintfW(szDest, _countof(szDest), L"%s\\%s", s_szBackup, pszName);
    RegDeleteTreeW(HKEY_CURRENT_USER, szDest);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pszKey, 0, KEY_READ, &hSrc))
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, szDest, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hDest, NULL))
    {
        RegCopyTreeW(hSrc, NULL, hDest);
        RegCloseKey(hDest);
    }
    RegCloseKey(hSrc);
}

static void RestoreKey(LPCWSTR pszKey, LPCWSTR pszName)
{
    WCHAR szSrc[MAX_PATH];
    HKEY hSrc, hDest;

    StringCchPrintfW(szSrc, _countof(szSrc), L"%s\\%s", s_szBackup, pszName);
    RegDeleteTreeW(HKEY_CURRENT_USER, pszKey);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, szSrc, 0, KEY_READ, &hSrc))
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, pszKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hDest, NULL))
    {
        RegCopyTreeW(hSrc, NULL, hDest);
        RegCloseKey(hDest);
    }
    RegCloseKey(hSrc);
}

static void SetEnglishOnly(void)
{
    static const WCHAR szLanguages[] = L"en-US\0";
    DWORD dwOne = 1;
    HKEY hKey;

    RegDeleteTreeW(HKEY_CURRENT_USER, s_szUserProfile);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szPreload);
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International\\User Profile\\en-US", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, L"0409:00000409", 0, REG_DWORD, (const BYTE *)&dwOne, sizeof(dwOne));
        RegCloseKey(hKey);
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserProfile, 0, KEY_ALL_ACCESS, &hKey))
    {
        RegSetValueExW(hKey, L"Languages", 0, REG_MULTI_SZ, (const BYTE *)szLanguages, sizeof(szLanguages));
        RegCloseKey(hKey);
    }
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, s_szPreload, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, L"1", 0, REG_SZ, (const BYTE *)L"00000409", sizeof(L"00000409"));
        RegCloseKey(hKey);
    }
}

static void GetLanguages(char *psz, SIZE_T cch)
{
    WCHAR data[512];
    DWORD cb = sizeof(data) - 4;
    WCHAR *p;

    psz[0] = 0;
    ZeroMemory(data, sizeof(data));
    if (RegGetValueW(HKEY_CURRENT_USER, s_szUserProfile, L"Languages", RRF_RT_REG_MULTI_SZ, NULL, data, &cb))
        return;
    for (p = data; *p; p += wcslen(p) + 1)
    {
        char sz[64];
        StringCchPrintfA(sz, _countof(sz), "%s%ls", psz[0] ? "|" : "", p);
        StringCchCatA(psz, cch, sz);
    }
}

static void GetEntries(LPCWSTR pszLanguage, char *psz, SIZE_T cch)
{
    WCHAR szKey[MAX_PATH];
    HKEY hKey;
    DWORD i;

    StringCchPrintfW(szKey, _countof(szKey), L"%s\\%s", s_szUserProfile, pszLanguage);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, szKey, 0, KEY_READ, &hKey))
    {
        StringCchCopyA(psz, cch, "(none)");
        return;
    }
    psz[0] = 0;
    for (i = 0;; i++)
    {
        WCHAR szName[MAX_PATH];
        DWORD cchName = _countof(szName), dwType, dwData = 0, cbData = sizeof(dwData);
        char sz[MAX_PATH];
        if (RegEnumValueW(hKey, i, szName, &cchName, NULL, &dwType, (LPBYTE)&dwData, &cbData))
            break;
        StringCchPrintfA(sz, _countof(sz), "%s%ls=%lu", psz[0] ? "|" : "", szName, dwType == REG_DWORD ? dwData : 0xffff);
        StringCchCatA(psz, cch, sz);
    }
    RegCloseKey(hKey);
}

static void GetPreload(char *psz, SIZE_T cch)
{
    DWORD i;

    psz[0] = 0;
    for (i = 1; i < 32; i++)
    {
        WCHAR szName[16], szValue[16];
        DWORD cb = sizeof(szValue);
        char sz[32];
        StringCchPrintfW(szName, _countof(szName), L"%lu", i);
        if (RegGetValueW(HKEY_CURRENT_USER, s_szPreload, szName, RRF_RT_REG_SZ, NULL, szValue, &cb))
            break;
        StringCchPrintfA(sz, _countof(sz), "%s%ls", psz[0] ? "|" : "", szValue);
        StringCchCatA(psz, cch, sz);
    }
}

static BOOL FindEnumEntry(LPCWSTR pszId, LAYOUTORTIPPROFILE *pFound)
{
    LAYOUTORTIPPROFILE profiles[32];
    UINT c, i;

    c = pEnumEnabledLayoutOrTip(NULL, NULL, NULL, profiles, _countof(profiles));
    for (i = 0; i < c && i < _countof(profiles); i++)
    {
        if (!_wcsicmp(profiles[i].szId, pszId))
        {
            *pFound = profiles[i];
            return TRUE;
        }
    }
    return FALSE;
}

#define check_state(get, expected) check_state_(__LINE__, #get, get, expected)
static void check_state_(int line, const char *pszName, void (*pfn)(char *, SIZE_T), const char *pszExpected)
{
    char sz[1024];
    pfn(sz, sizeof(sz));
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "%s is \"%s\", expected \"%s\"\n", pszName, sz, pszExpected);
}

static void GetJaEntries(char *psz, SIZE_T cch)
{
    GetEntries(L"ja", psz, cch);
}

static void UnloadNewLayouts(const HKL *ahklOld, int cOld)
{
    HKL ahkl[64];
    int c, i, j;

    c = GetKeyboardLayoutList(_countof(ahkl), ahkl);
    for (i = 0; i < c; i++)
    {
        BOOL fOld = FALSE;
        for (j = 0; j < cOld; j++)
        {
            if (ahkl[i] == ahklOld[j])
                fOld = TRUE;
        }
        if (!fOld)
            UnloadKeyboardLayout(ahkl[i]);
    }
}

START_TEST(layouttip)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    HMODULE hInput;
    WCHAR szTip[128];
    HKL ahklOld[64];
    int cOld;

    CoInitialize(NULL);
    hInput = LoadLibraryW(L"input.dll");
    pInstallLayoutOrTip = (FN_InstallLayoutOrTip)GetProcAddress(hInput, "InstallLayoutOrTip");
    pEnumEnabledLayoutOrTip = (FN_EnumEnabledLayoutOrTip)GetProcAddress(hInput, "EnumEnabledLayoutOrTip");
    if (!pInstallLayoutOrTip || !pEnumEnabledLayoutOrTip)
    {
        skip("input.dll layout functions are missing\n");
        CoUninitialize();
        return;
    }
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        TestTip_Unregister();
        CoUninitialize();
        return;
    }
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void **)&pProfiles)))
    {
        pProfiles->AddLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestTipProfile, L"Test TIP", 8, NULL, 0, 0);
        pProfiles->EnableLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestTipProfile, TRUE);
        pProfiles->Release();
    }
    cOld = GetKeyboardLayoutList(_countof(ahklOld), ahklOld);
    BackupKey(s_szUserProfile, L"UserProfile");
    BackupKey(s_szPreload, L"Preload");

    StringCchPrintfW(szTip, _countof(szTip),
                     L"0411:{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F41-51D2-4C1E-9A37-1F528C0E4D71}");
    SetEnglishOnly();
    ok_int(pInstallLayoutOrTip(szTip, 0), TRUE);
    check_state(GetLanguages, "en-US|ja");
    check_state(GetJaEntries, "0411:{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F41-51D2-4C1E-9A37-1F528C0E4D71}=1");
    check_state(GetPreload, "00000409|00000411");

    ok_int(pInstallLayoutOrTip(szTip, 0x2), TRUE);
    check_state(GetLanguages, "en-US|ja");
    check_state(GetPreload, "00000411|00000409");
    {
        LAYOUTORTIPPROFILE entry;
        ZeroMemory(&entry, sizeof(entry));
        ok(FindEnumEntry(szTip, &entry), "the text service is not enumerated\n");
        ok_long(entry.dwProfileType, 1);
        ok_long(entry.dwFlags, 1);
        ok(entry.langid == 0x0411, "langid 0x%04x\n", entry.langid);
        ok(IsEqualGUID(entry.clsid, CLSID_TestTip), "wrong CLSID\n");
        ok(IsEqualGUID(entry.guidProfile, GUID_TestTipProfile), "wrong profile\n");
        ok(IsEqualGUID(entry.catid, GUID_TFCAT_TIP_KEYBOARD), "wrong category\n");
        ok_long(entry.dwSubstituteLayout, 0);
    }

    ok_int(pInstallLayoutOrTip(szTip, 0x1), TRUE);
    check_state(GetLanguages, "en-US");
    check_state(GetJaEntries, "(none)");
    check_state(GetPreload, "00000411|00000409");

    SetLastError(0xdeadbeef);
    ok_int(pInstallLayoutOrTip(L"0411:{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}", 0), FALSE);
    trace("a CLSID without a profile sets error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok_int(pInstallLayoutOrTip(L"0411:{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}{bad}", 0), FALSE);
    trace("a malformed profile sets error %lu\n", GetLastError());
    ok_int(pInstallLayoutOrTip(L"0411:{6B3A3F4F-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F41-51D2-4C1E-9A37-1F528C0E4D71}", 0), TRUE);
    check_state(GetLanguages, "en-US");
    check_state(GetPreload, "00000411|00000409");

    RestoreKey(s_szUserProfile, L"UserProfile");
    RestoreKey(s_szPreload, L"Preload");
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szBackup);
    UnloadNewLayouts(ahklOld, cOld);
    TestTip_Unregister();
    CoUninitialize();
}
