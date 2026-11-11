/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfInputProcessorProfileMgr
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <cguid.h>

static const CLSID CLSID_PmTip = {0x6b3a3fa0, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_PmProfile = {0x6b3a3fa1, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_PmProfile2 = {0x6b3a3fa2, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const WCHAR s_szTipKey[] = L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3FA0-51D2-4C1E-9A37-1F528C0E4D71}";

static const WCHAR s_szProfileKey1[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3FA0-51D2-4C1E-9A37-1F528C0E4D71}\\LanguageProfile\\0x00000411\\"
    L"{6B3A3FA1-51D2-4C1E-9A37-1F528C0E4D71}";
static const WCHAR s_szProfileKey2[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3FA0-51D2-4C1E-9A37-1F528C0E4D71}\\LanguageProfile\\0x00000411\\"
    L"{6B3A3FA2-51D2-4C1E-9A37-1F528C0E4D71}";

#define check_sz(k, n, e) check_sz_(__LINE__, k, n, e)
static void check_sz_(int line, PCWSTR pszKey, PCWSTR pszName, PCWSTR pszExpected)
{
    WCHAR sz[MAX_PATH] = { 0 };
    DWORD cb = sizeof(sz) - sizeof(WCHAR), dwType = 0;
    LSTATUS error = RegGetValueW(HKEY_LOCAL_MACHINE, pszKey, pszName, RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY, &dwType,
                                 sz, &cb);
    if (!pszExpected)
    {
        ok_(__FILE__, line)(error == ERROR_FILE_NOT_FOUND, "%ls: error %ld\n", pszName, error);
        return;
    }
    ok_(__FILE__, line)(error == ERROR_SUCCESS && dwType == REG_SZ && !wcscmp(sz, pszExpected),
                        "%ls: error %ld type %lu '%ls'\n", pszName, error, dwType, sz);
}

#define check_dword(k, n, e) check_dword_(__LINE__, k, n, e)
static void check_dword_(int line, PCWSTR pszKey, PCWSTR pszName, LONG lExpected)
{
    DWORD dwValue = 0xdeadbeef, cb = sizeof(dwValue), dwType = 0;
    LSTATUS error = RegGetValueW(HKEY_LOCAL_MACHINE, pszKey, pszName, RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY, &dwType,
                                 &dwValue, &cb);
    if (lExpected < 0)
    {
        ok_(__FILE__, line)(error == ERROR_FILE_NOT_FOUND, "%ls: error %ld\n", pszName, error);
        return;
    }
    ok_(__FILE__, line)(error == ERROR_SUCCESS && dwType == REG_DWORD && dwValue == (DWORD)lExpected,
                        "%ls: error %ld type %lu value %lu\n", pszName, error, dwType, dwValue);
}

#define check_tip_profile(p, g, f) check_tip_profile_(__LINE__, p, g, f)
static void check_tip_profile_(int line, const TF_INPUTPROCESSORPROFILE *p, REFGUID guidProfile, DWORD dwFlags)
{
    ok_(__FILE__, line)(p->dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR, "type %lu\n", p->dwProfileType);
    ok_(__FILE__, line)(p->langid == 0x0411, "langid %04x\n", p->langid);
    ok_(__FILE__, line)(IsEqualGUID(p->clsid, CLSID_PmTip), "wrong clsid\n");
    ok_(__FILE__, line)(IsEqualGUID(p->guidProfile, guidProfile), "wrong profile\n");
    ok_(__FILE__, line)(IsEqualGUID(p->catid, GUID_NULL), "wrong catid\n");
    ok_(__FILE__, line)(p->hklSubstitute == NULL, "substitute %p\n", p->hklSubstitute);
    ok_(__FILE__, line)(p->dwCaps == 0, "caps 0x%lx\n", p->dwCaps);
    ok_(__FILE__, line)(p->hkl == NULL, "hkl %p\n", p->hkl);
    ok_(__FILE__, line)(p->dwFlags == dwFlags, "flags 0x%lx, expected 0x%lx\n", p->dwFlags, dwFlags);
}

#define check_layout_profile(p, h) check_layout_profile_(__LINE__, p, h)
static void check_layout_profile_(int line, const TF_INPUTPROCESSORPROFILE *p, HKL hkl)
{
    ok_(__FILE__, line)(p->dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT, "type %lu\n", p->dwProfileType);
    ok_(__FILE__, line)(p->langid == LOWORD(HandleToUlong(hkl)), "langid %04x\n", p->langid);
    ok_(__FILE__, line)(IsEqualGUID(p->clsid, GUID_NULL), "wrong clsid\n");
    ok_(__FILE__, line)(IsEqualGUID(p->guidProfile, GUID_NULL), "wrong profile\n");
    ok_(__FILE__, line)(IsEqualGUID(p->catid, GUID_TFCAT_TIP_KEYBOARD), "wrong catid\n");
    ok_(__FILE__, line)(p->hkl == hkl, "hkl %p, expected %p\n", p->hkl, hkl);
    ok_(__FILE__, line)(p->hklSubstitute == NULL, "substitute %p\n", p->hklSubstitute);
}

static void CheckEnum(ITfInputProcessorProfileMgr *pMgr, LANGID langid, ULONG cExpectedOurs)
{
    IEnumTfInputProcessorProfiles *pEnum = NULL;
    TF_INPUTPROCESSORPROFILE profile;
    ULONG cFetched, cOurs = 0, cTotal = 0;
    BOOL fSeenTip = FALSE;
    HRESULT hr;

    winetest_push_context("EnumProfiles(%04x)", langid);
    hr = pMgr->EnumProfiles(langid, &pEnum);
    ok_hr(hr, S_OK);
    if (!pEnum)
    {
        winetest_pop_context();
        return;
    }
    while (pEnum->Next(1, &profile, &cFetched) == S_OK && cFetched == 1)
    {
        ++cTotal;
        if (profile.dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT)
        {
            ok(!fSeenTip, "a layout follows a text service\n");
            if (langid)
                ok(profile.langid == langid, "layout language %04x\n", profile.langid);
            check_layout_profile(&profile, profile.hkl);
            continue;
        }
        fSeenTip = TRUE;
        ok(!langid || profile.langid == langid || profile.langid == 0, "profile language %04x\n", profile.langid);
        if (!IsEqualGUID(profile.clsid, CLSID_PmTip))
            continue;
        ++cOurs;
        if (IsEqualGUID(profile.guidProfile, GUID_PmProfile))
            check_tip_profile(&profile, GUID_PmProfile, TF_IPP_FLAG_ACTIVE | TF_IPP_FLAG_ENABLED);
        else
            check_tip_profile(&profile, GUID_PmProfile2, 0);
    }
    ok_long(cOurs, cExpectedOurs);
    ok_hr(pEnum->Next(1, NULL, &cFetched), S_FALSE);
    ok_hr(pEnum->Reset(), S_OK);
    ok_hr(pEnum->Skip(1), cTotal ? S_OK : S_FALSE);
    pEnum->Release();
    winetest_pop_context();
}

static HKL GetMissingLayout(void)
{
    static const ULONG s_candidates[] = { 0x04150415, 0x041f041f, 0x04080408, 0x04190419 };
    HKL hkls[64];
    INT cLayouts = GetKeyboardLayoutList(ARRAYSIZE(hkls), hkls);
    for (UINT i = 0; i < ARRAYSIZE(s_candidates); i++)
    {
        BOOL fFound = FALSE;
        for (INT j = 0; j < cLayouts; j++)
            fFound = fFound || HandleToUlong(hkls[j]) == s_candidates[i];
        if (!fFound)
            return (HKL)(ULONG_PTR)s_candidates[i];
    }
    return NULL;
}

START_TEST(profilemgr)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    ITfInputProcessorProfileMgr *pMgr = NULL;
    TF_INPUTPROCESSORPROFILE profile;
    HKL hklCurrent, hklMissing;
    BSTR bstr = NULL;
    BOOL fEnabled;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                          (void **)&pProfiles);
    ok_hr(hr, S_OK);
    if (!pProfiles)
        return;
    ok_hr(pProfiles->QueryInterface(IID_ITfInputProcessorProfileMgr, (void **)&pMgr), S_OK);
    if (!pMgr)
    {
        pProfiles->Release();
        return;
    }

    hklCurrent = GetKeyboardLayout(0);
    ZeroMemory(&profile, sizeof(profile));
    hr = pMgr->GetProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, LOWORD(HandleToUlong(hklCurrent)), CLSID_NULL, GUID_NULL,
                          hklCurrent, &profile);
    ok_hr(hr, S_OK);
    check_layout_profile(&profile, hklCurrent);
    hklMissing = GetMissingLayout();
    if (hklMissing)
    {
        hr = pMgr->GetProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, LOWORD(HandleToUlong(hklMissing)), CLSID_NULL, GUID_NULL,
                              hklMissing, &profile);
        ok_hr(hr, E_INVALIDARG);
    }
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, &profile), E_FAIL);
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, NULL),
          E_INVALIDARG);
    ok_hr(pMgr->GetProfile(3, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, &profile), E_INVALIDARG);

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, s_szTipKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szTipKey);
    hr = pMgr->RegisterProfile(CLSID_PmTip, 0x0411, GUID_PmProfile, L"PmDescXYZ", 6, L"C:\\icon.dllXYZ", 11, 3, NULL, 0,
                               TRUE, 0);
    if (hr == E_ACCESSDENIED)
    {
        skip("Cannot register a profile; run as administrator\n");
        pMgr->Release();
        pProfiles->Release();
        CoUninitialize();
        return;
    }
    ok_hr(hr, S_OK);
    hr = pMgr->RegisterProfile(CLSID_PmTip, 0x0411, GUID_PmProfile2, L"Second", 6, NULL, 0, 0,
                               (HKL)(ULONG_PTR)0x04110411, 0, FALSE, 0);
    ok_hr(hr, S_OK);
    check_sz(s_szProfileKey1, L"Description", L"PmDesc");
    check_sz(s_szProfileKey1, L"IconFile", L"C:\\icon.dll");
    check_dword(s_szProfileKey1, L"IconIndex", 3);
    check_dword(s_szProfileKey1, L"Enable", 1);
    check_sz(s_szProfileKey1, L"SubstituteLayout", NULL);
    check_sz(s_szProfileKey2, L"Description", L"Second");
    check_sz(s_szProfileKey2, L"IconFile", NULL);
    check_dword(s_szProfileKey2, L"IconIndex", -1);
    check_dword(s_szProfileKey2, L"Enable", 0);
    check_sz(s_szProfileKey2, L"SubstituteLayout", L"0x04110411");

    fEnabled = 2;
    ok_hr(pProfiles->IsEnabledLanguageProfile(CLSID_PmTip, 0x0411, GUID_PmProfile, &fEnabled), S_OK);
    ok_int(fEnabled, TRUE);
    fEnabled = 2;
    ok_hr(pProfiles->IsEnabledLanguageProfile(CLSID_PmTip, 0x0411, GUID_PmProfile2, &fEnabled), S_OK);
    ok_int(fEnabled, FALSE);
    ok_hr(pProfiles->GetLanguageProfileDescription(CLSID_PmTip, 0x0411, GUID_PmProfile, &bstr), S_OK);
    ok(bstr && !wcscmp(bstr, L"PmDesc"), "description '%ls'\n", bstr ? bstr : L"(null)");
    SysFreeString(bstr);

    ZeroMemory(&profile, sizeof(profile));
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, &profile), S_OK);
    check_tip_profile(&profile, GUID_PmProfile, TF_IPP_FLAG_ACTIVE | TF_IPP_FLAG_ENABLED);
    ZeroMemory(&profile, sizeof(profile));
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile2, NULL, &profile), S_OK);
    check_tip_profile(&profile, GUID_PmProfile2, 0);
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0409, CLSID_PmTip, GUID_PmProfile, NULL, &profile), E_FAIL);

    CheckEnum(pMgr, 0x0411, 2);
    CheckEnum(pMgr, 0x0409, 0);
    CheckEnum(pMgr, 0, 2);

    if (hklMissing)
    {
        hr = pMgr->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, LOWORD(HandleToUlong(hklMissing)), CLSID_NULL,
                                   GUID_NULL, hklMissing, TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        ok_hr(hr, E_FAIL);
    }
    ok_hr(pMgr->ActivateProfile(4, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, 0), E_INVALIDARG);
    ok(GetKeyboardLayout(0) == hklCurrent, "layout %p, was %p\n", GetKeyboardLayout(0), hklCurrent);
    ok_hr(pMgr->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, 0), S_OK);
    ok_hr(pMgr->ReleaseInputProcessor(CLSID_PmTip, 0), S_OK);
    ok_hr(pMgr->ReleaseInputProcessor(CLSID_PmTip, 1), S_OK);

    ok_hr(pMgr->UnregisterProfile(CLSID_PmTip, 0x0411, GUID_PmProfile2, 0), S_OK);
    ok_hr(pMgr->UnregisterProfile(CLSID_PmTip, 0x0411, GUID_PmProfile2, 0), S_OK);
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile2, NULL, &profile), E_FAIL);
    ok_hr(pMgr->UnregisterProfile(CLSID_PmTip, 0x0411, GUID_PmProfile, 0), S_OK);
    ok_hr(pMgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, CLSID_PmTip, GUID_PmProfile, NULL, &profile), E_FAIL);
    CheckEnum(pMgr, 0x0411, 0);

    pProfiles->Unregister(CLSID_PmTip);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, s_szTipKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szTipKey);
    pMgr->Release();
    pProfiles->Release();
    CoUninitialize();
}
