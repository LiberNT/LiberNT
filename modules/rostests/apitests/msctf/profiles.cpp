/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for language profile enabling
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static const CLSID CLSID_TestTip = {0x6b3a3f10, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_TestProfile = {0x6b3a3f11, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const WCHAR s_szTipKey[] = L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3F10-51D2-4C1E-9A37-1F528C0E4D71}";
static const WCHAR s_szProfileKey[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3F10-51D2-4C1E-9A37-1F528C0E4D71}\\LanguageProfile\\0x00000411\\"
    L"{6B3A3F11-51D2-4C1E-9A37-1F528C0E4D71}";

#define ENABLE_ABSENT   (-1)
#define ENABLE_NOKEY    (-2)

static LONG GetEnable(HKEY hRoot)
{
    HKEY hKey;
    DWORD dwValue, cb = sizeof(dwValue);
    LSTATUS error;

    if (RegOpenKeyExW(hRoot, s_szProfileKey, 0, KEY_READ | (hRoot == HKEY_LOCAL_MACHINE ? KEY_WOW64_64KEY : 0), &hKey))
        return ENABLE_NOKEY;
    error = RegQueryValueExW(hKey, L"Enable", NULL, NULL, (LPBYTE)&dwValue, &cb);
    RegCloseKey(hKey);
    return error ? ENABLE_ABSENT : (LONG)dwValue;
}

static void SetEnable(HKEY hRoot, LONG lValue)
{
    HKEY hKey;
    if (RegCreateKeyExW(hRoot, s_szProfileKey, 0, NULL, 0,
                        KEY_ALL_ACCESS | (hRoot == HKEY_LOCAL_MACHINE ? KEY_WOW64_64KEY : 0), NULL, &hKey, NULL))
    {
        skip("Cannot open the profile key\n");
        return;
    }
    if (lValue == ENABLE_ABSENT)
    {
        RegDeleteValueW(hKey, L"Enable");
    }
    else
    {
        DWORD dwValue = lValue;
        RegSetValueExW(hKey, L"Enable", 0, REG_DWORD, (const BYTE *)&dwValue, sizeof(dwValue));
    }
    RegCloseKey(hKey);
}

#define check_enabled(p, e, lm, cu) check_enabled_(__LINE__, p, e, lm, cu)
static void check_enabled_(int line, ITfInputProcessorProfiles *pProfiles, BOOL fExpected, LONG lMachine, LONG lUser)
{
    BOOL fEnabled = 2;
    HRESULT hr = pProfiles->IsEnabledLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestProfile, &fEnabled);
    ok_(__FILE__, line)(hr == S_OK, "IsEnabledLanguageProfile returned 0x%lx\n", hr);
    ok_(__FILE__, line)(fEnabled == fExpected, "enabled is %d, expected %d\n", fEnabled, fExpected);
    ok_(__FILE__, line)(GetEnable(HKEY_LOCAL_MACHINE) == lMachine, "HKLM Enable is %ld, expected %ld\n",
                        GetEnable(HKEY_LOCAL_MACHINE), lMachine);
    ok_(__FILE__, line)(GetEnable(HKEY_CURRENT_USER) == lUser, "HKCU Enable is %ld, expected %ld\n",
                        GetEnable(HKEY_CURRENT_USER), lUser);
}

static void Cleanup(ITfInputProcessorProfiles *pProfiles)
{
    pProfiles->Unregister(CLSID_TestTip);
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szTipKey);
}

class CLanguageSink : public ITfLanguageProfileNotifySink
{
public:
    CLanguageSink() : m_cRefs(1), m_fAccept(TRUE), m_cChange(0), m_cChanged(0), m_langid(0) { }
    virtual ~CLanguageSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLanguageProfileNotifySink))
        {
            *ppv = static_cast<ITfLanguageProfileNotifySink *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP OnLanguageChange(LANGID langid, BOOL *pfAccept) override
    {
        ++m_cChange;
        m_langid = langid;
        *pfAccept = m_fAccept;
        return S_OK;
    }
    STDMETHODIMP OnLanguageChanged() override
    {
        ++m_cChanged;
        return S_OK;
    }

    LONG m_cRefs;
    BOOL m_fAccept;
    ULONG m_cChange, m_cChanged;
    LANGID m_langid;
};

static void TestChangeLanguage(ITfInputProcessorProfiles *pProfiles)
{
    ITfSource *pSource = NULL;
    CLanguageSink *pSink;
    DWORD dwCookie = TF_INVALID_COOKIE;
    LANGID langOrig = 0, langOther, lang = 0;
    HKL hklOther, ahkl[64];
    BOOL fLoaded = FALSE;
    int c, i;
    HRESULT hr;

    hr = pProfiles->GetCurrentLanguage(&langOrig);
    ok_hr(hr, S_OK);
    langOther = (langOrig == 0x0409) ? 0x0407 : 0x0409;
    c = GetKeyboardLayoutList(_countof(ahkl), ahkl);
    for (i = 0; i < c; i++)
    {
        if (LOWORD(HandleToUlong(ahkl[i])) == langOther)
            fLoaded = TRUE;
    }
    hklOther = LoadKeyboardLayoutW(langOther == 0x0409 ? L"00000409" : L"00000407", 0);
    ok(hklOther != NULL, "LoadKeyboardLayoutW failed\n");

    hr = pProfiles->QueryInterface(IID_ITfSource, (void **)&pSource);
    ok_hr(hr, S_OK);
    if (!pSource)
        return;
    pSink = new CLanguageSink();
    hr = pSource->AdviseSink(IID_ITfLanguageProfileNotifySink, static_cast<ITfLanguageProfileNotifySink *>(pSink),
                             &dwCookie);
    ok_hr(hr, S_OK);

    hr = pProfiles->ChangeCurrentLanguage(langOther);
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cChange, 0);
    ok_long(pSink->m_cChanged, 1);
    pProfiles->GetCurrentLanguage(&lang);
    ok(lang == langOther, "current language 0x%04x\n", lang);
    ok(LOWORD(HandleToUlong(GetKeyboardLayout(0))) == langOther, "layout %p\n", GetKeyboardLayout(0));

    pSink->m_cChange = pSink->m_cChanged = 0;
    hr = pProfiles->ChangeCurrentLanguage(langOther);
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cChange, 0);
    ok_long(pSink->m_cChanged, 0);

    pSink->m_fAccept = FALSE;
    hr = pProfiles->ChangeCurrentLanguage(langOrig);
    ok_hr(hr, S_OK);
    ok_long(pSink->m_cChange, 0);
    ok_long(pSink->m_cChanged, 1);
    pProfiles->GetCurrentLanguage(&lang);
    ok(lang == langOrig, "current language 0x%04x after restoring\n", lang);

    if (dwCookie != TF_INVALID_COOKIE)
        ok_hr(pSource->UnadviseSink(dwCookie), S_OK);
    pSink->m_cChanged = 0;
    pProfiles->ChangeCurrentLanguage(langOther);
    ok_long(pSink->m_cChanged, 0);
    pProfiles->ChangeCurrentLanguage(langOrig);
    pSink->Release();
    pSource->Release();
    if (!fLoaded && hklOther)
        UnloadKeyboardLayout(hklOther);
}

START_TEST(profiles)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                          (void **)&pProfiles);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }

    Cleanup(pProfiles);
    hr = pProfiles->Register(CLSID_TestTip);
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
    {
        skip("Registering a text service needs administrator rights\n");
        pProfiles->Release();
        CoUninitialize();
        return;
    }
    ok_hr(hr, S_OK);
    hr = pProfiles->AddLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestProfile, L"Test", 4, NULL, 0, 0);
    ok_hr(hr, S_OK);
    check_enabled(pProfiles, TRUE, ENABLE_ABSENT, ENABLE_NOKEY);

    hr = pProfiles->EnableLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestProfile, TRUE);
    ok_hr(hr, S_OK);
    check_enabled(pProfiles, TRUE, ENABLE_ABSENT, 1);

    hr = pProfiles->EnableLanguageProfile(CLSID_TestTip, 0x0411, GUID_TestProfile, FALSE);
    ok_hr(hr, S_OK);
    check_enabled(pProfiles, FALSE, ENABLE_ABSENT, 0);

    hr = pProfiles->EnableLanguageProfileByDefault(CLSID_TestTip, 0x0411, GUID_TestProfile, TRUE);
    ok_hr(hr, S_OK);
    check_enabled(pProfiles, FALSE, 1, 0);

    SetEnable(HKEY_CURRENT_USER, ENABLE_ABSENT);
    SetEnable(HKEY_LOCAL_MACHINE, ENABLE_ABSENT);
    check_enabled(pProfiles, TRUE, ENABLE_ABSENT, ENABLE_ABSENT);
    SetEnable(HKEY_LOCAL_MACHINE, 0);
    check_enabled(pProfiles, FALSE, 0, ENABLE_ABSENT);
    SetEnable(HKEY_LOCAL_MACHINE, 1);
    check_enabled(pProfiles, TRUE, 1, ENABLE_ABSENT);
    SetEnable(HKEY_CURRENT_USER, 0);
    check_enabled(pProfiles, FALSE, 1, 0);
    SetEnable(HKEY_LOCAL_MACHINE, 0);
    SetEnable(HKEY_CURRENT_USER, 1);
    check_enabled(pProfiles, TRUE, 0, 1);
    SetEnable(HKEY_LOCAL_MACHINE, ENABLE_ABSENT);
    check_enabled(pProfiles, TRUE, ENABLE_ABSENT, 1);
    SetEnable(HKEY_CURRENT_USER, 0);
    check_enabled(pProfiles, FALSE, ENABLE_ABSENT, 0);

    Cleanup(pProfiles);
    TestChangeLanguage(pProfiles);
    pProfiles->Release();
    CoUninitialize();
}
