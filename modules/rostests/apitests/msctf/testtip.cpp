/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A keyboard text service hosted by the test process
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "testtip.h"
#include <cguid.h>

const CLSID CLSID_TestTip = {0x6b3a3f40, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
const GUID GUID_TestDisplayAttribute = {0x6b3a3f50, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
const GUID GUID_TestTipProfile = {0x6b3a3f41, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const WCHAR s_szClsidKey[] = L"Software\\Classes\\CLSID\\{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}";
static const WCHAR s_szTipKey[] = L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}";

TEST_TIP_STATE *g_pTip;
FN_TESTTIP_COMPOSE g_pfnCompose;
static LANGID g_langid = 0x0411;
static WCHAR s_szLayoutOrTip[128];
static HMODULE s_hTipDll;


BOOL TestTip_Register(void)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    ITfCategoryMgr *pCategoryMgr = NULL;
    WCHAR szPath[MAX_PATH];
    HKEY hKey;
    HRESULT hr;

    TestTip_Unregister();

    typedef TEST_TIP_STATE *(WINAPI *FN_GETSTATE)(void);
    GetModuleFileNameW(NULL, szPath, _countof(szPath));
    *(wcsrchr(szPath, L'\\') + 1) = UNICODE_NULL;
    StringCchCatW(szPath, _countof(szPath), L"msctf_testtip.dll");
    s_hTipDll = LoadLibraryW(szPath);
    if (!s_hTipDll)
        return FALSE;
    g_pTip = ((FN_GETSTATE)GetProcAddress(s_hTipDll, "TestTip_GetState"))();
    g_pfnCompose = (FN_TESTTIP_COMPOSE)GetProcAddress(s_hTipDll, "TestTip_Compose");
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, s_szClsidKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL))
        return FALSE;
    RegCloseKey(hKey);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}\\InprocServer32",
                        0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)szPath, (DWORD)(wcslen(szPath) + 1) * sizeof(WCHAR));
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hKey);
    }

    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                          (void **)&pProfiles);
    if (FAILED(hr))
        return FALSE;
    pProfiles->GetCurrentLanguage(&g_langid);
    hr = pProfiles->Register(CLSID_TestTip);
    if (SUCCEEDED(hr))
        hr = pProfiles->AddLanguageProfile(CLSID_TestTip, g_langid, GUID_TestTipProfile, L"Test TIP", 8, NULL, 0, 0);
    if (SUCCEEDED(hr))
        pProfiles->EnableLanguageProfile(CLSID_TestTip, g_langid, GUID_TestTipProfile, TRUE);
    pProfiles->Release();
    if (FAILED(hr))
        return FALSE;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void **)&pCategoryMgr);
    if (SUCCEEDED(hr))
    {
        pCategoryMgr->RegisterCategory(CLSID_TestTip, GUID_TFCAT_TIP_KEYBOARD, CLSID_TestTip);
        pCategoryMgr->Release();
    }

    MSG msg;
    Sleep(2000);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
    return TRUE;
}

void TestTip_Unregister(void)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    ITfCategoryMgr *pCategoryMgr = NULL;

    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   (void **)&pCategoryMgr)))
    {
        pCategoryMgr->UnregisterCategory(CLSID_TestTip, GUID_TFCAT_TIP_KEYBOARD, CLSID_TestTip);
        pCategoryMgr->Release();
    }
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void **)&pProfiles)))
    {
        pProfiles->Unregister(CLSID_TestTip);
        pProfiles->Release();
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, s_szTipKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, s_szClsidKey);
}

BOOL TestTip_Activate(void)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                          (void **)&pProfiles);
    if (FAILED(hr))
        return FALSE;
    typedef BOOL (WINAPI *FN_InstallLayoutOrTip)(LPCWSTR, DWORD);
    HMODULE hInput = LoadLibraryW(L"input.dll");
    FN_InstallLayoutOrTip fnInstall = hInput ? (FN_InstallLayoutOrTip)GetProcAddress(hInput, "InstallLayoutOrTip") : NULL;
    StringCchPrintfW(s_szLayoutOrTip, _countof(s_szLayoutOrTip),
                     L"%04X:{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}{6B3A3F41-51D2-4C1E-9A37-1F528C0E4D71}", g_langid);
    if (fnInstall)
        fnInstall(s_szLayoutOrTip, 0);
    hr = pProfiles->ActivateLanguageProfile(CLSID_TestTip, g_langid, GUID_TestTipProfile);
    if (SUCCEEDED(hr))
    {
        ITfInputProcessorProfileMgr *pMgr = NULL;
        if (SUCCEEDED(pProfiles->QueryInterface(IID_ITfInputProcessorProfileMgr, (void **)&pMgr)))
        {
            hr = pMgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, g_langid, CLSID_TestTip, GUID_TestTipProfile, NULL,
                                       TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
            pMgr->Release();
        }
    }
    for (int i = 0; i < 40 && !g_pTip->cActivate; i++)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(50);
    }
    pProfiles->Release();
    return SUCCEEDED(hr);
}

void TestTip_Deactivate(void)
{
    ITfInputProcessorProfiles *pProfiles = NULL;
    TF_LANGUAGEPROFILE profile;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void **)&pProfiles)))
    {
        pProfiles->ActivateLanguageProfile(CLSID_NULL, g_langid, GUID_NULL);
        pProfiles->Release();
    }
    typedef BOOL (WINAPI *FN_InstallLayoutOrTip)(LPCWSTR, DWORD);
    HMODULE hInput = LoadLibraryW(L"input.dll");
    FN_InstallLayoutOrTip fnInstall = hInput ? (FN_InstallLayoutOrTip)GetProcAddress(hInput, "InstallLayoutOrTip") : NULL;
    if (fnInstall)
        fnInstall(s_szLayoutOrTip, 0x00000001);
    (void)profile;
}
