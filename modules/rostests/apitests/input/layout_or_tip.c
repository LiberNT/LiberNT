/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the input.dll layout or TIP functions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <strsafe.h>

#define ILOT_UNINSTALL               0x00000001
#define ILOT_DEFPROFILE              0x00000002
#define ILOT_NOAPPLYTOCURRENTSESSION 0x00000020
#define ILOT_CLEANINSTALL            0x00000040
#define ILOT_DISABLED                0x00000080


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
typedef BOOL (WINAPI *FN_InstallLayoutOrTipUserReg)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI *FN_SetDefaultLayoutOrTip)(LPCWSTR, DWORD);
typedef UINT (WINAPI *FN_EnumEnabledLayoutOrTip)(LPCWSTR, LPCWSTR, LPCWSTR, LAYOUTORTIPPROFILE *, UINT);

static FN_InstallLayoutOrTip pInstallLayoutOrTip;
static FN_InstallLayoutOrTipUserReg pInstallLayoutOrTipUserReg;
static FN_SetDefaultLayoutOrTip pSetDefaultLayoutOrTip;
static FN_EnumEnabledLayoutOrTip pEnumEnabledLayoutOrTip;

static const WCHAR s_szUserProfile[] = L"Control Panel\\International\\User Profile";
static const WCHAR s_szPreload[] = L"Keyboard Layout\\Preload";
static const WCHAR s_szBackup[] = L"Software\\LiberNTInputTest";

static void BackupKey(LPCWSTR pszKey, LPCWSTR pszName)
{
    HKEY hSrc, hDst;
    WCHAR szDst[MAX_PATH];
    StringCchPrintfW(szDst, _countof(szDst), L"%s\\%s", s_szBackup, pszName);
    RegDeleteTreeW(HKEY_CURRENT_USER, szDst);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pszKey, 0, KEY_READ, &hSrc))
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, szDst, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hDst, NULL))
    {
        RegCopyTreeW(hSrc, NULL, hDst);
        RegCloseKey(hDst);
    }
    RegCloseKey(hSrc);
}

static void RestoreKey(LPCWSTR pszKey, LPCWSTR pszName)
{
    HKEY hSrc, hDst;
    WCHAR szSrc[MAX_PATH];
    StringCchPrintfW(szSrc, _countof(szSrc), L"%s\\%s", s_szBackup, pszName);
    RegDeleteTreeW(HKEY_CURRENT_USER, pszKey);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, szSrc, 0, KEY_READ, &hSrc))
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, pszKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hDst, NULL))
    {
        RegCopyTreeW(hSrc, NULL, hDst);
        RegCloseKey(hDst);
    }
    RegCloseKey(hSrc);
}

static BOOL IsTestLanguage(LANGID langid)
{
    static const LANGID s_langids[] = { 0x0407, 0x0409, 0x040C, 0x0410, 0x0413, 0x0416 };
    UINT i;
    for (i = 0; i < _countof(s_langids); i++)
    {
        if (s_langids[i] == langid)
            return TRUE;
    }
    return FALSE;
}

static void GetLanguages(char *psz, SIZE_T cch)
{
    HKEY hKey;
    WCHAR data[512];
    DWORD cb = sizeof(data) - 4, dwType;
    WCHAR *p;

    psz[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szUserProfile, 0, KEY_READ, &hKey))
        return;
    ZeroMemory(data, sizeof(data));
    if (!RegQueryValueExW(hKey, L"Languages", NULL, &dwType, (LPBYTE)data, &cb) && dwType == REG_MULTI_SZ)
    {
        for (p = data; *p; p += wcslen(p) + 1)
        {
            char sz[64];
            StringCchPrintfA(sz, _countof(sz), "%s%ls", psz[0] ? "|" : "", p);
            StringCchCatA(psz, cch, sz);
        }
    }
    RegCloseKey(hKey);
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
        DWORD cchName = _countof(szName), dwType, dwData, cbData = sizeof(dwData);
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
    HKEY hKey;
    DWORD i;

    psz[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, s_szPreload, 0, KEY_READ, &hKey))
        return;
    for (i = 1; i < 32; i++)
    {
        WCHAR szName[16], szValue[16];
        DWORD cb = sizeof(szValue);
        char sz[32];
        StringCchPrintfW(szName, _countof(szName), L"%lu", i);
        if (RegQueryValueExW(hKey, szName, NULL, NULL, (LPBYTE)szValue, &cb))
            break;
        if (!IsTestLanguage(LOWORD(wcstoul(szValue, NULL, 16))))
            continue;
        StringCchPrintfA(sz, _countof(sz), "%s%ls", psz[0] ? "|" : "", szValue);
        StringCchCatA(psz, cch, sz);
    }
    RegCloseKey(hKey);
}

static void GetEnum(char *psz, SIZE_T cch)
{
    LAYOUTORTIPPROFILE profiles[16];
    UINT c, i;

    psz[0] = 0;
    c = pEnumEnabledLayoutOrTip(NULL, NULL, NULL, profiles, _countof(profiles));
    for (i = 0; i < c && i < _countof(profiles); i++)
    {
        char sz[MAX_PATH];
        if (!IsTestLanguage(profiles[i].langid))
            continue;
        StringCchPrintfA(sz, _countof(sz), "%s%ls/%lu/%lu", psz[0] ? "|" : "", profiles[i].szId,
                         profiles[i].dwProfileType, profiles[i].dwFlags);
        StringCchCatA(psz, cch, sz);
    }
}

static BOOL IsLoaded(DWORD dwHkl);
static HKL s_ahklLoaded[64];
static int s_cLoaded;

static void RestoreLoadedLayouts(void)
{
    HKL ahkl[64];
    int c, i, j;

    c = GetKeyboardLayoutList(_countof(ahkl), ahkl);
    for (i = 0; i < c; i++)
    {
        BOOL fKeep = FALSE;
        for (j = 0; j < s_cLoaded; j++)
        {
            if (ahkl[i] == s_ahklLoaded[j])
                fKeep = TRUE;
        }
        if (!fKeep)
            UnloadKeyboardLayout(ahkl[i]);
    }
    for (j = 0; j < s_cLoaded; j++)
    {
        WCHAR szKlid[KL_NAMELENGTH];
        if (!IsLoaded(HandleToUlong(s_ahklLoaded[j])) && !(HandleToUlong(s_ahklLoaded[j]) & 0xF0000000))
        {
            StringCchPrintfW(szKlid, _countof(szKlid), L"%08lx", LOWORD(HandleToUlong(s_ahklLoaded[j])));
            LoadKeyboardLayoutW(szKlid, KLF_NOTELLSHELL);
        }
    }
}

static void GetOverride(char *psz, SIZE_T cch)
{
    WCHAR sz[MAX_PATH];
    DWORD cb = sizeof(sz);

    if (RegGetValueW(HKEY_CURRENT_USER, s_szUserProfile, L"InputMethodOverride", RRF_RT_REG_SZ, NULL, sz, &cb))
        StringCchCopyA(psz, cch, "(none)");
    else
        StringCchPrintfA(psz, cch, "%ls", sz);
}

#define check_str(get, expected) check_str_(__LINE__, #get, get, expected)
static void check_str_(int line, const char *pszName, void (*pfn)(char *, SIZE_T), const char *pszExpected)
{
    char sz[1024];
    pfn(sz, sizeof(sz));
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "%s is \"%s\", expected \"%s\"\n", pszName, sz, pszExpected);
}

#define check_entries(lang, expected) check_entries_(__LINE__, lang, expected)
static void check_entries_(int line, LPCWSTR pszLanguage, const char *pszExpected)
{
    char sz[1024];
    GetEntries(pszLanguage, sz, sizeof(sz));
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "entries of %ls are \"%s\", expected \"%s\"\n", pszLanguage, sz, pszExpected);
}

static void SetState(LPCWSTR pszLanguage, LPCWSTR pszEntry, LPCWSTR pszKlid)
{
    HKEY hKey;
    WCHAR szKey[MAX_PATH], multi[64];
    DWORD dwOne = 1;
    SIZE_T cch;

    RegDeleteTreeW(HKEY_CURRENT_USER, s_szUserProfile);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szPreload);
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
        cch = wcslen(multi) + 2;
        RegSetValueExW(hKey, L"Languages", 0, REG_MULTI_SZ, (const BYTE *)multi, (DWORD)(cch * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, s_szPreload, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, L"1", 0, REG_SZ, (const BYTE *)pszKlid, (DWORD)(wcslen(pszKlid) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }
}

#define check_install(psz, flags, ret) check_install_(__LINE__, psz, flags, ret)
static void check_install_(int line, LPCWSTR psz, DWORD dwFlags, BOOL fExpected)
{
    BOOL ret = pInstallLayoutOrTip(psz, dwFlags);
    ok_(__FILE__, line)(ret == fExpected, "InstallLayoutOrTip(%ls, 0x%lx) returned %d\n", psz, dwFlags, ret);
}

static BOOL IsLoaded(DWORD dwHkl)
{
    HKL ahkl[64];
    int c, i;

    c = GetKeyboardLayoutList(_countof(ahkl), ahkl);
    for (i = 0; i < c; i++)
    {
        if (HandleToUlong(ahkl[i]) == dwHkl)
            return TRUE;
    }
    return FALSE;
}

static void TestParsing(void)
{
    static const struct
    {
        LPCWSTR psz;
        BOOL fExpected;
    } s_cases[] =
    {
        { L"0407: 00000407", FALSE },
        { L"0407:0x407", TRUE },
        { L"0407:407", TRUE },
        { L" 0407:00000407", FALSE },
        { L"0407:00000407 ", FALSE },
        { L"407:00000407", TRUE },
        { L"0407:00000407;", TRUE },
        { L";0407:00000407", TRUE },
        { L"0407:00000407;;040C:0000040C", FALSE },
        { L"0407:+0000407", FALSE },
        { L"0x407:00000407", TRUE },
    };
    UINT i;

    for (i = 0; i < _countof(s_cases); i++)
    {
        char sz[256];
        SetState(L"en-US", L"0409:00000409", L"00000409");
        ok(pInstallLayoutOrTip(s_cases[i].psz, ILOT_NOAPPLYTOCURRENTSESSION) == s_cases[i].fExpected,
           "InstallLayoutOrTip(\"%ls\") did not return %d\n", s_cases[i].psz, s_cases[i].fExpected);
        GetPreload(sz, sizeof(sz));
        ok(!strcmp(sz, s_cases[i].fExpected ? "00000409|00000407" : "00000409"), "Preload after \"%ls\" is \"%s\"\n",
           s_cases[i].psz, sz);
    }

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_install(L"0411:00000411;0412:00000412;0804:00000804;0404:00000404", ILOT_NOAPPLYTOCURRENTSESSION, TRUE);
    check_str(GetLanguages, "en-US|ja|ko|zh-Hans-CN|zh-Hant-TW");
}

static void TestMore(void)
{
    TestParsing();
    LAYOUTORTIPPROFILE buf[16];
    UINT c, i;

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_install(L"", ILOT_CLEANINSTALL, TRUE);
    check_str(GetLanguages, "");
    check_entries(L"en-US", "(none)");
    check_str(GetPreload, "");
    check_str(GetEnum, "");
    check_str(GetOverride, "(none)");

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_install(L"0411:", ILOT_CLEANINSTALL, TRUE);
    check_str(GetLanguages, "");
    check_str(GetPreload, "");

    SetState(L"en-US", L"0409:00000409", L"00000409");
    UnloadKeyboardLayout((HKL)(ULONG_PTR)0x04130413);
    UnloadKeyboardLayout((HKL)(ULONG_PTR)0x04100410);
    check_install(L"0413:00000413", ILOT_NOAPPLYTOCURRENTSESSION, TRUE);
    check_str(GetLanguages, "en-US|nl-NL");
    check_str(GetPreload, "00000409|00000413");
    ok(!IsLoaded(0x04130413), "04130413 is loaded\n");
    check_install(L"0410:00000410", 0, TRUE);
    check_str(GetLanguages, "en-US|nl-NL|it-IT");
    check_str(GetPreload, "00000409|00000410|00000413");
    ok(IsLoaded(0x04100410), "04100410 is not loaded\n");
    ok(IsLoaded(0x04090409), "04090409 is not loaded\n");
    check_install(L"0410:00000410", ILOT_DEFPROFILE, TRUE);
    check_str(GetPreload, "00000410|00000409|00000413");
    check_str(GetEnum, "0409:00000409/2/0|0410:00000410/2/1|0413:00000413/2/0");
    check_str(GetOverride, "(none)");
    check_install(L"0410:00000410", ILOT_UNINSTALL, TRUE);
    check_install(L"0413:00000413", ILOT_UNINSTALL, TRUE);
    check_str(GetLanguages, "en-US");
    check_str(GetPreload, "00000409");
    ok(!IsLoaded(0x04100410), "04100410 is still loaded\n");

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_install(L"0411:00000411;0412:00000412;0419:00000419;0408:00000408;0809:00000809;0804:00000804",
                  ILOT_NOAPPLYTOCURRENTSESSION, TRUE);
    check_str(GetLanguages, "en-US|ja|ko|ru|el|en-GB|zh-Hans-CN");
    check_entries(L"ru", "0419:00000419=1");
    check_entries(L"el", "0408:00000408=1");
    check_entries(L"ja-JP", "(none)");
    check_entries(L"ru-RU", "(none)");

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_install(L"0407:00000407;040C:0000040C;0410:00000410", 0, TRUE);
    ok_int(pEnumEnabledLayoutOrTip(NULL, NULL, NULL, NULL, 0), 4);
    ok_int(pEnumEnabledLayoutOrTip(NULL, NULL, NULL, NULL, 2), 4);
    for (i = 0; i <= 5; i++)
    {
        ZeroMemory(buf, sizeof(buf));
        c = pEnumEnabledLayoutOrTip(NULL, NULL, NULL, buf, i);
        ok(c == min(i, 4), "EnumEnabledLayoutOrTip with %u entries returned %u\n", i, c);
        ok(!wcscmp(buf[0].szId, i ? L"0407:00000407" : L""), "first entry \"%ls\" with %u entries\n", buf[0].szId, i);
        if (i < _countof(buf))
            ok(buf[i].szId[0] == 0, "entry %u written with %u entries\n", i, i);
    }
}

START_TEST(layout_or_tip)
{
    HMODULE hInput = LoadLibraryW(L"input.dll");
    LAYOUTORTIPPROFILE one[1];
    BOOL ret;

    pInstallLayoutOrTip = (FN_InstallLayoutOrTip)GetProcAddress(hInput, "InstallLayoutOrTip");
    pInstallLayoutOrTipUserReg = (FN_InstallLayoutOrTipUserReg)GetProcAddress(hInput, "InstallLayoutOrTipUserReg");
    pSetDefaultLayoutOrTip = (FN_SetDefaultLayoutOrTip)GetProcAddress(hInput, "SetDefaultLayoutOrTip");
    pEnumEnabledLayoutOrTip = (FN_EnumEnabledLayoutOrTip)GetProcAddress(hInput, "EnumEnabledLayoutOrTip");
    if (!pInstallLayoutOrTip || !pSetDefaultLayoutOrTip || !pEnumEnabledLayoutOrTip || !pInstallLayoutOrTipUserReg)
    {
        skip("input.dll layout functions are missing\n");
        return;
    }

    BackupKey(s_szUserProfile, L"UserProfile");
    BackupKey(s_szPreload, L"Preload");
    s_cLoaded = GetKeyboardLayoutList(_countof(s_ahklLoaded), s_ahklLoaded);

    SetState(L"en-US", L"0409:00000409", L"00000409");
    check_str(GetEnum, "0409:00000409/2/1");
    check_str(GetPreload, "00000409");

    check_install(L"", 0, TRUE);
    check_install(L"0407:", 0, TRUE);
    check_install(L"zzzz", 0, FALSE);
    check_install(L"0407", 0, FALSE);
    check_install(L"0407:xyz", 0, FALSE);
    check_install(L"0407:00000407;bad", 0, FALSE);
    check_str(GetLanguages, "en-US");
    check_str(GetPreload, "00000409");

    check_install(L"0407:00000407", 0, TRUE);
    check_str(GetLanguages, "en-US|de-DE");
    check_entries(L"de-DE", "0407:00000407=1");
    check_str(GetPreload, "00000409|00000407");
    check_str(GetEnum, "0407:00000407/2/0|0409:00000409/2/1");

    check_install(L"0407:00000407", 0, TRUE);
    check_str(GetLanguages, "en-US|de-DE");
    check_str(GetPreload, "00000409|00000407");

    check_install(L"040C:0000040C;0410:00000410", 0x2, TRUE);
    check_str(GetLanguages, "en-US|de-DE|fr-FR|it-IT");
    check_entries(L"fr-FR", "040C:0000040C=1");
    check_entries(L"it-IT", "0410:00000410=1");
    check_str(GetPreload, "0000040c|00000407|00000409|00000410");
    check_str(GetEnum, "0407:00000407/2/0|0409:00000409/2/0|040c:0000040c/2/1|0410:00000410/2/0");

    ret = pSetDefaultLayoutOrTip(L"0407:00000407", 0);
    ok_int(ret, TRUE);
    check_str(GetPreload, "00000407|00000409|0000040c|00000410");
    ret = pSetDefaultLayoutOrTip(L"0416:00000416", 0);
    ok_int(ret, FALSE);
    ret = pSetDefaultLayoutOrTip(L"bad", 0);
    ok_int(ret, FALSE);
    check_str(GetPreload, "00000407|00000409|0000040c|00000410");
    check_str(GetEnum, "0407:00000407/2/1|0409:00000409/2/0|040c:0000040c/2/0|0410:00000410/2/0");

    ok(pEnumEnabledLayoutOrTip(NULL, NULL, NULL, NULL, 0) >= 4, "too few entries\n");
    ZeroMemory(one, sizeof(one));
    ok_int(pEnumEnabledLayoutOrTip(NULL, NULL, NULL, one, 1), 1);
    ok_wstr(one[0].szId, L"0407:00000407");
    ok_long(one[0].dwFlags, 1);

    check_install(L"040C:0000040C", 0x1, TRUE);
    check_str(GetLanguages, "en-US|de-DE|it-IT");
    check_entries(L"fr-FR", "(none)");
    check_str(GetPreload, "00000407|00000409|00000410");

    check_install(L"0416:00000416", 0x80, TRUE);
    check_str(GetLanguages, "en-US|de-DE|it-IT|pt-BR");
    check_entries(L"pt-BR", "");
    check_str(GetPreload, "00000407|00000409|00000410");
    check_str(GetEnum, "0407:00000407/2/1|0409:00000409/2/0|0410:00000410/2/0");

    check_install(L"0413:00000413", 0x20, TRUE);
    check_str(GetLanguages, "en-US|de-DE|it-IT|pt-BR|nl-NL");
    check_entries(L"nl-NL", "0413:00000413=1");
    check_str(GetPreload, "00000407|00000409|00000410|00000413");

    check_install(L"0407:00000407", 0x40, TRUE);
    check_str(GetLanguages, "pt-BR|de-DE");
    check_entries(L"de-DE", "0407:00000407=1");
    check_entries(L"en-US", "(none)");
    check_entries(L"pt-BR", "");
    check_str(GetPreload, "00000407");
    check_str(GetEnum, "0407:00000407/2/1");

    check_install(L"0407:00000407", 0x41, TRUE);
    check_str(GetLanguages, "pt-BR|de-DE");
    check_entries(L"de-DE", "");
    check_str(GetPreload, "");
    check_str(GetEnum, "");

    ret = pInstallLayoutOrTipUserReg(NULL, NULL, NULL, L"0407:00000407", 0);
    ok_int(ret, TRUE);
    check_entries(L"de-DE", "0407:00000407=1");
    check_str(GetPreload, "00000407");
    ret = pInstallLayoutOrTipUserReg(L"C:\\nonexistent\\ntuser.dat", NULL, NULL, L"0409:00000409", 0);
    ok_int(ret, FALSE);
    check_str(GetPreload, "00000407");

    TestMore();

    RestoreKey(s_szUserProfile, L"UserProfile");
    RestoreKey(s_szPreload, L"Preload");
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szBackup);
    RestoreLoadedLayouts();
}
