/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for installing a text service with the input.dll layout or TIP functions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define COBJMACROS
#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <objbase.h>
#include <initguid.h>
#include <msctf.h>
#include <strsafe.h>

#define ILOT_UNINSTALL               0x00000001
#define ILOT_NOAPPLYTOCURRENTSESSION 0x00000020

typedef BOOL (WINAPI *FN_InstallLayoutOrTip)(LPCWSTR, DWORD);

DEFINE_GUID(CLSID_TipProfileTest, 0x6b3a3f60, 0x51d2, 0x4c1e, 0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71);
DEFINE_GUID(GUID_TipProfileTestProfile, 0x6b3a3f61, 0x51d2, 0x4c1e, 0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71);
DEFINE_GUID(GUID_TipProfileTestProfile2, 0x6b3a3f62, 0x51d2, 0x4c1e, 0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71);
DEFINE_GUID(CLSID_TipProfiles, 0x33c53a50, 0xf456, 0x4884, 0xb0, 0x49, 0x85, 0xfd, 0x64, 0x3e, 0xcf, 0xed);
DEFINE_GUID(CLSID_TipCategoryMgr, 0xa4b544a1, 0x438d, 0x4b41, 0x93, 0x25, 0x86, 0x95, 0x23, 0xe2, 0xd6, 0xc7);
DEFINE_GUID(GUID_TipCatKeyboard, 0x34745c63, 0xb2f0, 0x4784, 0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31);

#define TIP_CLSID L"{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}"
#define TIP_PROFILE L"{6B3A3F61-51D2-4C1E-9A37-1F528C0E4D71}"
#define TIP_PROFILE2 L"{6B3A3F62-51D2-4C1E-9A37-1F528C0E4D71}"
#define TIP_ENTRY L"0411:" TIP_CLSID TIP_PROFILE
#define TIP_ENTRY2 L"0411:" TIP_CLSID TIP_PROFILE2
#define TIP_SORT "{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F61-51D2-4C1E-9A37-1F528C0E4D71}/0"
#define TIP_SORT2 "{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F62-51D2-4C1E-9A37-1F528C0E4D71}/0"

static FN_InstallLayoutOrTip pInstallLayoutOrTip;

static const WCHAR s_szUserProfile[] = L"Control Panel\\International\\User Profile";
static const WCHAR s_szPreload[] = L"Keyboard Layout\\Preload";
static const WCHAR s_szSortOrder[] = L"Software\\Microsoft\\CTF\\SortOrder\\AssemblyItem\\0x00000411\\{34745C63-B2F0-4784-8B67-5E12C8701A31}";
static const WCHAR s_szUserTip[] = L"Software\\Microsoft\\CTF\\TIP\\{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}";
static const WCHAR s_szBackup[] = L"Software\\LiberNTTipProfileTest";

static void CopyKey(LPCWSTR pszFrom, LPCWSTR pszTo)
{
    HKEY hSrc, hDst;

    RegDeleteTreeW(HKEY_CURRENT_USER, pszTo);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pszFrom, 0, KEY_READ, &hSrc))
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, pszTo, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hDst, NULL))
    {
        RegCopyTreeW(hSrc, NULL, hDst);
        RegCloseKey(hDst);
    }
    RegCloseKey(hSrc);
}

static void Backup(LPCWSTR pszKey, LPCWSTR pszName, BOOL fRestore)
{
    WCHAR sz[MAX_PATH];

    StringCchPrintfW(sz, _countof(sz), L"%s\\%s", s_szBackup, pszName);
    if (fRestore)
        CopyKey(sz, pszKey);
    else
        CopyKey(pszKey, sz);
}

static void Append(char *psz, SIZE_T cch, const char *pszItem)
{
    if (psz[0])
        StringCchCatA(psz, cch, "|");
    StringCchCatA(psz, cch, pszItem);
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
        StringCchPrintfA(sz, _countof(sz), "%ls", p);
        Append(psz, cch, sz);
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
        StringCchPrintfA(sz, _countof(sz), "%ls=%lu", szName, dwType == REG_DWORD ? dwData : 0xffff);
        Append(psz, cch, sz);
    }
    RegCloseKey(hKey);
}

static void GetJaEntries(char *psz, SIZE_T cch)
{
    GetEntries(L"ja", psz, cch);
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
        StringCchPrintfA(sz, _countof(sz), "%ls", szValue);
        Append(psz, cch, sz);
    }
}

static void GetSortOrder(char *psz, SIZE_T cch)
{
    DWORD i;

    psz[0] = 0;
    for (i = 0; i < 32; i++)
    {
        WCHAR szKey[MAX_PATH], szClsid[64], szProfile[64];
        DWORD cb, dwLayout = 0xffff;
        char sz[MAX_PATH];

        StringCchPrintfW(szKey, _countof(szKey), L"%s\\%08lu", s_szSortOrder, i);
        cb = sizeof(szClsid);
        if (RegGetValueW(HKEY_CURRENT_USER, szKey, L"CLSID", RRF_RT_REG_SZ, NULL, szClsid, &cb))
            break;
        cb = sizeof(szProfile);
        if (RegGetValueW(HKEY_CURRENT_USER, szKey, L"Profile", RRF_RT_REG_SZ, NULL, szProfile, &cb))
            szProfile[0] = 0;
        cb = sizeof(dwLayout);
        RegGetValueW(HKEY_CURRENT_USER, szKey, L"KeyboardLayout", RRF_RT_REG_DWORD, NULL, &dwLayout, &cb);
        StringCchPrintfA(sz, _countof(sz), "%ls%ls/%lu", szClsid, szProfile, dwLayout);
        Append(psz, cch, sz);
    }
}

static void GetUserEnable(char *psz, SIZE_T cch)
{
    WCHAR szKey[MAX_PATH];
    DWORD dw, cb = sizeof(dw);
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserTip, 0, KEY_READ, &hKey))
    {
        StringCchCopyA(psz, cch, "(none)");
        return;
    }
    RegCloseKey(hKey);
    StringCchPrintfW(szKey, _countof(szKey), L"%s\\LanguageProfile\\0x00000411\\%s", s_szUserTip, TIP_PROFILE);
    if (RegGetValueW(HKEY_CURRENT_USER, szKey, L"Enable", RRF_RT_REG_DWORD, NULL, &dw, &cb))
        StringCchCopyA(psz, cch, "(no value)");
    else
        StringCchPrintfA(psz, cch, "%lu", dw);
}

static void GetEnabled(char *psz, SIZE_T cch)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    BOOL fEnabled = -1;
    HRESULT hr = E_FAIL;

    if (SUCCEEDED(CoCreateInstance(&CLSID_TipProfiles, NULL, CLSCTX_INPROC_SERVER, &IID_ITfInputProcessorProfiles,
                                   (void **)&pProfiles)))
    {
        hr = ITfInputProcessorProfiles_IsEnabledLanguageProfile(pProfiles, &CLSID_TipProfileTest, 0x0411,
                                                                &GUID_TipProfileTestProfile, &fEnabled);
        ITfInputProcessorProfiles_Release(pProfiles);
    }
    StringCchPrintfA(psz, cch, "%08lx/%d", hr, fEnabled);
}

#define check_str(get, expected) check_str_(__LINE__, #get, get, expected)
static void check_str_(int line, const char *pszName, void (*pfn)(char *, SIZE_T), const char *pszExpected)
{
    char sz[1024];
    pfn(sz, sizeof(sz));
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "%s is \"%s\", expected \"%s\"\n", pszName, sz, pszExpected);
}

#define check_install(psz, flags) check_install_(__LINE__, psz, flags)
static void check_install_(int line, LPCWSTR psz, DWORD dwFlags)
{
    BOOL ret = pInstallLayoutOrTip(psz, dwFlags | ILOT_NOAPPLYTOCURRENTSESSION);
    ok_(__FILE__, line)(ret == TRUE, "InstallLayoutOrTip(%ls, 0x%lx) returned %d\n", psz, dwFlags, ret);
}

static BOOL RegisterTip(BOOL fRegister)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    ITfCategoryMgr *pCategoryMgr = NULL;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_TipProfiles, NULL, CLSCTX_INPROC_SERVER, &IID_ITfInputProcessorProfiles,
                          (void **)&pProfiles);
    if (FAILED(hr))
        return FALSE;
    CoCreateInstance(&CLSID_TipCategoryMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfCategoryMgr, (void **)&pCategoryMgr);
    if (fRegister)
    {
        hr = ITfInputProcessorProfiles_Register(pProfiles, &CLSID_TipProfileTest);
        if (SUCCEEDED(hr))
            hr = ITfInputProcessorProfiles_AddLanguageProfile(pProfiles, &CLSID_TipProfileTest, 0x0411,
                                                              &GUID_TipProfileTestProfile, L"Tip Profile Test", 16,
                                                              NULL, 0, 0);
        if (SUCCEEDED(hr))
            hr = ITfInputProcessorProfiles_AddLanguageProfile(pProfiles, &CLSID_TipProfileTest, 0x0411,
                                                              &GUID_TipProfileTestProfile2, L"Tip Profile Test 2", 18,
                                                              NULL, 0, 0);
        if (SUCCEEDED(hr))
            hr = pCategoryMgr ? ITfCategoryMgr_RegisterCategory(pCategoryMgr, &CLSID_TipProfileTest,
                                                                &GUID_TipCatKeyboard, &CLSID_TipProfileTest) : E_FAIL;
    }
    else
    {
        if (pCategoryMgr)
            ITfCategoryMgr_UnregisterCategory(pCategoryMgr, &CLSID_TipProfileTest, &GUID_TipCatKeyboard,
                                              &CLSID_TipProfileTest);
        hr = ITfInputProcessorProfiles_Unregister(pProfiles, &CLSID_TipProfileTest);
    }
    if (pCategoryMgr)
        ITfCategoryMgr_Release(pCategoryMgr);
    ITfInputProcessorProfiles_Release(pProfiles);
    return SUCCEEDED(hr);
}

static void SetUser(LPCWSTR pszLanguage, LPCWSTR pszEntry, LPCWSTR pszKlid)
{
    HKEY hKey;
    WCHAR szKey[MAX_PATH], multi[64];
    DWORD dwOne = 1;

    RegDeleteTreeW(HKEY_CURRENT_USER, s_szUserProfile);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szPreload);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szSortOrder);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szUserTip);
    if (pszLanguage)
    {
        StringCchPrintfW(szKey, _countof(szKey), L"%s\\%s", s_szUserProfile, pszLanguage);
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, szKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
        {
            RegSetValueExW(hKey, pszEntry, 0, REG_DWORD, (const BYTE *)&dwOne, sizeof(dwOne));
            RegCloseKey(hKey);
        }
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserProfile, 0, KEY_ALL_ACCESS, &hKey))
        {
            ZeroMemory(multi, sizeof(multi));
            StringCchCopyW(multi, _countof(multi), pszLanguage);
            RegSetValueExW(hKey, L"Languages", 0, REG_MULTI_SZ, (const BYTE *)multi,
                           (DWORD)((wcslen(multi) + 2) * sizeof(WCHAR)));
            RegCloseKey(hKey);
        }
    }
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, s_szPreload, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, L"1", 0, REG_SZ, (const BYTE *)pszKlid, (DWORD)(wcslen(pszKlid) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }
}

static void AddStaleSortOrderEntry(void)
{
    static const WCHAR szClsid[] = L"{00000000-0000-0000-0000-000000000001}";
    static const WCHAR szProfile[] = L"{00000000-0000-0000-0000-000000000002}";
    WCHAR szKey[MAX_PATH];
    DWORD dwZero = 0;
    HKEY hKey;

    StringCchPrintfW(szKey, _countof(szKey), L"%s\\00000000", s_szSortOrder);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, szKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
        return;
    RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, (const BYTE *)szClsid, sizeof(szClsid));
    RegSetValueExW(hKey, L"Profile", 0, REG_SZ, (const BYTE *)szProfile, sizeof(szProfile));
    RegSetValueExW(hKey, L"KeyboardLayout", 0, REG_DWORD, (const BYTE *)&dwZero, sizeof(dwZero));
    RegCloseKey(hKey);
}

static BOOL IsJapaneseLoaded(void)
{
    HKL ahkl[64];
    int c, i;

    c = GetKeyboardLayoutList(_countof(ahkl), ahkl);
    for (i = 0; i < c; i++)
    {
        if (HandleToUlong(ahkl[i]) == 0x04110411)
            return TRUE;
    }
    return FALSE;
}

START_TEST(tip_profile)
{
    HMODULE hInput = LoadLibraryW(L"input.dll");
    BOOL fJapaneseLoaded = IsJapaneseLoaded();

    pInstallLayoutOrTip = hInput ? (FN_InstallLayoutOrTip)GetProcAddress(hInput, "InstallLayoutOrTip") : NULL;
    if (!pInstallLayoutOrTip)
    {
        skip("InstallLayoutOrTip is missing\n");
        return;
    }
    CoInitialize(NULL);
    if (!RegisterTip(TRUE))
    {
        skip("Registering the test text service failed; run as an administrator\n");
        RegisterTip(FALSE);
        CoUninitialize();
        return;
    }

    Backup(s_szUserProfile, L"UserProfile", FALSE);
    Backup(s_szPreload, L"Preload", FALSE);
    Backup(s_szSortOrder, L"SortOrder", FALSE);
    Backup(s_szUserTip, L"UserTip", FALSE);

    SetUser(L"en-US", L"0409:00000409", L"00000409");
    check_install(TIP_ENTRY, 0);
    check_str(GetLanguages, "en-US|ja");
    check_str(GetJaEntries, "0411:{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F61-51D2-4C1E-9A37-1F528C0E4D71}=1");
    check_str(GetPreload, "00000409|00000411");
    check_str(GetSortOrder, TIP_SORT);
    check_str(GetUserEnable, "(none)");
    check_str(GetEnabled, "00000000/1");

    check_install(TIP_ENTRY, ILOT_UNINSTALL);
    check_str(GetLanguages, "en-US");
    check_str(GetJaEntries, "(none)");
    check_str(GetPreload, "00000409|00000411");
    check_str(GetSortOrder, TIP_SORT);
    check_str(GetUserEnable, "0");
    check_str(GetEnabled, "00000000/0");

    check_install(TIP_ENTRY, 0);
    check_str(GetLanguages, "en-US|ja");
    check_str(GetSortOrder, TIP_SORT);
    check_str(GetUserEnable, "(none)");
    check_str(GetEnabled, "00000000/1");

    check_install(TIP_ENTRY, 0);
    check_str(GetSortOrder, TIP_SORT);

    check_install(TIP_ENTRY2, 0);
    check_str(GetSortOrder, TIP_SORT "|" TIP_SORT2);
    check_install(TIP_ENTRY, 0);
    check_str(GetSortOrder, TIP_SORT "|" TIP_SORT2);

    SetUser(L"en-US", L"0409:00000409", L"00000409");
    AddStaleSortOrderEntry();
    check_install(TIP_ENTRY, 0);
    check_str(GetSortOrder, TIP_SORT);

    SetUser(L"ja", L"0411:00000411", L"00000411");
    check_install(TIP_ENTRY, 0);
    check_str(GetLanguages, "ja");
    check_str(GetJaEntries, "0411:00000411=1|0411:{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F61-51D2-4C1E-9A37-1F528C0E4D71}=1");
    check_str(GetPreload, "00000411");
    check_str(GetSortOrder, TIP_SORT);

    SetUser(NULL, NULL, L"00000409");
    check_install(TIP_ENTRY, 0);
    check_str(GetLanguages, "ja");
    check_str(GetPreload, "00000409|00000411");
    check_str(GetSortOrder, TIP_SORT);

    Backup(s_szUserProfile, L"UserProfile", TRUE);
    Backup(s_szPreload, L"Preload", TRUE);
    Backup(s_szSortOrder, L"SortOrder", TRUE);
    Backup(s_szUserTip, L"UserTip", TRUE);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szBackup);
    RegisterTip(FALSE);
    if (!fJapaneseLoaded)
        UnloadKeyboardLayout((HKL)(ULONG_PTR)0x04110411);
    CoUninitialize();
}
