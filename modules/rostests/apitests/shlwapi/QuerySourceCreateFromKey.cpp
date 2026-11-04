/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for QuerySourceCreateFromKey
 * COPYRIGHT:   Copyright 2026 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shlguid_undoc.h>
#include <shlobj_undoc.h>
#include <shlwapi_undoc.h>
#include <versionhelpers.h>

typedef HRESULT (WINAPI *FN_QuerySourceCreateFromKey)(HKEY, PCWSTR, BOOL, REFIID, PVOID*);
static FN_QuerySourceCreateFromKey g_pQuerySourceCreateFromKey = NULL;

static const WCHAR k_Root[]    = L"Software\\QuerySrcTest";
static const WCHAR k_SubKeyA[] = L"SubKeyA";
static const WCHAR k_SubKeyB[] = L"SubKeyB";

static void SetupRegistry(void)
{
    HKEY hRoot;
    RegCreateKeyExW(HKEY_CURRENT_USER, k_Root, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hRoot, NULL);

    static const WCHAR valA[] = L"hello";
    RegSetValueExW(hRoot, L"ValueA", 0, REG_SZ, (const BYTE*)valA, (DWORD)sizeof(valA));

    DWORD dwValue = 0xBEEFCAFE;
    RegSetValueExW(hRoot, L"ValueB", 0, REG_DWORD, (const BYTE*)&dwValue, (DWORD)sizeof(dwValue));

    HKEY hSub;
    RegCreateKeyExW(hRoot, k_SubKeyA, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hSub, NULL);
    RegCloseKey(hSub);
    RegCreateKeyExW(hRoot, k_SubKeyB, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hSub, NULL);
    RegCloseKey(hSub);

    RegCloseKey(hRoot);
}

static void CleanupRegistry(void)
{
    SHDeleteKeyW(HKEY_CURRENT_USER, k_Root);
}

static void Test_EnumValues(void)
{
    IQuerySourceOld *pSrc = NULL;
    HRESULT hr = g_pQuerySourceCreateFromKey(HKEY_CURRENT_USER, k_Root, FALSE,
                                             IID_IQuerySourceOld, (PVOID*)&pSrc);
    ok_hr(hr, S_OK);
    ok(pSrc != NULL, "pSrc was NULL\n");

    IEnumString *pEnum = NULL;
    hr = 0xDEADFACE;
    if (pSrc)
        hr = pSrc->EnumValues(&pEnum);
    ok(pSrc && hr == S_OK, "EnumValues failed: 0x%08lX\n", hr);

    LPWSTR psz = NULL;
    ULONG fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_OK);
    ok(lstrcmpiW(psz, L"ValueA") == 0, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 1);
    CoTaskMemFree(psz);

    psz = NULL;
    fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_OK);
    ok(lstrcmpiW(psz, L"ValueB") == 0, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 1);
    CoTaskMemFree(psz);

    psz = NULL;
    fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_FALSE);
    ok(psz == NULL, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 0);
    CoTaskMemFree(psz);

    if (pEnum)
        pEnum->Release();
    if (pSrc)
        pSrc->Release();
}

static void Test_EnumSources(void)
{
    IQuerySourceOld *pSrc = NULL;
    HRESULT hr = g_pQuerySourceCreateFromKey(HKEY_CURRENT_USER, k_Root, FALSE,
                                             IID_IQuerySourceOld, (PVOID*)&pSrc);
    ok_hr(hr, S_OK);
    ok(pSrc != NULL, "pSrc was NULL\n");

    IEnumString *pEnum = NULL;
    hr = pSrc->EnumSources(&pEnum);
    ok_hr(hr, S_OK);
    ok(pEnum != NULL, "pEnum was NULL\n");

    LPWSTR psz = NULL;
    ULONG fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_OK);
    ok(lstrcmpiW(psz, k_SubKeyA) == 0, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 1);
    CoTaskMemFree(psz);

    psz = NULL;
    fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_OK);
    ok(lstrcmpiW(psz, k_SubKeyB) == 0, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 1);
    CoTaskMemFree(psz);

    psz = NULL;
    fetched = 0;
    hr = 0xDEADFACE;

    if (pEnum)
        hr = pEnum->Next(1, &psz, &fetched);
    ok_hr(hr, S_FALSE);
    ok(psz == NULL, "psz was %s\n", wine_dbgstr_w(psz));
    ok_int(fetched, 0);
    CoTaskMemFree(psz);

    if (pSrc)
        pSrc->Release();
}

static void Test_CheckValues(void)
{
    IQuerySourceOld *pSrc = NULL;
    HRESULT hr = g_pQuerySourceCreateFromKey(HKEY_CURRENT_USER, k_Root, FALSE,
                                             IID_IQuerySourceOld, (PVOID*)&pSrc);
    ok_hr(hr, S_OK);
    ok(pSrc != NULL, "pSrc was NULL\n");

    PWSTR pszValue;

    // QueryValueString
    hr = 0xDEADFACE;
    pszValue = NULL;
    if (pSrc)
        hr = pSrc->QueryValueString(NULL, L"ValueA", &pszValue);
    ok_hr(hr, S_OK);
    ok(lstrcmpiW(pszValue, L"hello") == 0, "pszValue was %s\n", wine_dbgstr_w(pszValue));

    hr = 0xDEADFACE;
    pszValue = NULL;
    if (pSrc)
        hr = pSrc->QueryValueString(NULL, L"ValueB", &pszValue);
    ok_hr(hr, E_DATATYPE_MISMATCH);
    ok(pszValue == NULL, "pszValue was %s\n", wine_dbgstr_w(pszValue));

    // QueryValueExists
    hr = 0xDEADFACE;
    if (pSrc)
        hr = pSrc->QueryValueExists(NULL, L"ValueA");
    ok_hr(hr, S_OK);
    if (pSrc)
        hr = pSrc->QueryValueExists(NULL, L"ValueB");
    ok_hr(hr, S_OK);

    DWORD dwValue;

    // QueryValueDword
    dwValue = 0xDEADFACE;
    hr = 0xDEADFACE;
    if (pSrc)
        hr = pSrc->QueryValueDword(NULL, L"ValueA", &dwValue);
    ok_hr(hr, HRESULT_FROM_WIN32(ERROR_MORE_DATA));
    ok_long(dwValue, 0xDEADFACE);

    dwValue = 0xDEADFACE;
    hr = 0xDEADFACE;
    if (pSrc)
        hr = pSrc->QueryValueDword(NULL, L"ValueB", &dwValue);
    ok_hr(hr, S_OK);
    ok_long(dwValue, 0xBEEFCAFE);

    FLAGGED_BYTE_BLOB *pBlob;

    // QueryValueDirect
    hr = 0xDEADFACE;
    pBlob = NULL;
    if (pSrc)
        hr = pSrc->QueryValueDirect(NULL, L"ValueA", &pBlob);
    ok_hr(hr, S_OK);
    ok(pBlob != NULL, "pBlob was %p\n", pBlob);
    ok(pBlob && pBlob->clSize == 12, "pBlob->clSize was %ld\n", pBlob->clSize);
    ok(pBlob && !memcmp(pBlob->abData, L"hello", 12), "pBlob->abData mismatch\n");
    CoTaskMemFree(pBlob);

    hr = 0xDEADFACE;
    pBlob = NULL;
    if (pSrc)
        hr = pSrc->QueryValueDirect(NULL, L"ValueB", &pBlob);
    ok_hr(hr, S_OK);
    ok(pBlob != NULL, "pBlob was %p\n", pBlob);
    ok(pBlob && pBlob->clSize == 4, "pBlob->clSize was %ld\n", pBlob->clSize);
    dwValue = 0xBEEFCAFE;
    ok(pBlob && !memcmp(pBlob->abData, &dwValue, sizeof(dwValue)), "pBlob->abData mismatch\n");
    CoTaskMemFree(pBlob);

    if (pSrc)
        pSrc->Release();
}

static ULONG CountEnum(IEnumString *pEnum, PCWSTR Expected1, PCWSTR Expected2, PCWSTR Expected3, PULONG pFound)
{
    PWSTR psz;
    ULONG cFetched, cItems = 0;

    *pFound = 0;
    while (pEnum->Next(1, &psz, &cFetched) == S_OK)
    {
        cItems++;
        if (!lstrcmpW(psz, Expected1) || !lstrcmpW(psz, Expected2) || (Expected3 && !lstrcmpW(psz, Expected3)))
            (*pFound)++;
        CoTaskMemFree(psz);
    }
    return cItems;
}

static void Test_VistaSource(void)
{
    static const WCHAR valG[] = L"{00021401-0000-0000-C000-000000000046}";
    static const GUID guidG = { 0x00021401, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    FLAGGED_BYTE_BLOB *pBlob = NULL;
    IQuerySource *pSrc = NULL, *pSub = NULL;
    IEnumString *pEnum = NULL;
    IUnknown *pOld = NULL;
    PWSTR psz = NULL;
    DWORD dwValue = 0;
    ULONG cItems, cFound;
    GUID guid;
    HKEY hRoot;
    HRESULT hr;

    RegCreateKeyExW(HKEY_CURRENT_USER, k_Root, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hRoot, NULL);
    RegSetValueExW(hRoot, L"ValueG", 0, REG_SZ, (const BYTE*)valG, (DWORD)sizeof(valG));
    RegCloseKey(hRoot);

    hr = g_pQuerySourceCreateFromKey(HKEY_CURRENT_USER, k_Root, FALSE, IID_IQuerySourceOld, (PVOID*)&pOld);
    ok_hex(hr, E_NOINTERFACE);
    ok(pOld == NULL, "pOld was %p\n", pOld);

    hr = g_pQuerySourceCreateFromKey(HKEY_CURRENT_USER, k_Root, FALSE, IID_IQuerySource, (PVOID*)&pSrc);
    ok_hex(hr, S_OK);
    if (!pSrc)
        return;

    hr = pSrc->QueryInterface(IID_IQuerySourceOld, (PVOID*)&pOld);
    ok_hex(hr, E_NOINTERFACE);

    hr = pSrc->QueryValueString(NULL, L"ValueA", &psz);
    ok_hex(hr, S_OK);
    ok(psz && !lstrcmpW(psz, L"hello"), "psz was %s\n", wine_dbgstr_w(psz));
    CoTaskMemFree(psz);

    hr = pSrc->QueryValueDword(NULL, L"ValueB", &dwValue);
    ok_hex(hr, S_OK);
    ok_hex(dwValue, 0xBEEFCAFE);

    memset(&guid, 0, sizeof(guid));
    hr = pSrc->QueryValueGuid(NULL, L"ValueG", &guid);
    ok_hex(hr, S_OK);
    ok(IsEqualGUID(guid, guidG), "guid was %s\n", wine_dbgstr_guid(&guid));

    hr = pSrc->QueryValueGuid(NULL, L"ValueB", &guid);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE));

    hr = pSrc->QueryValueExists(NULL, L"ValueA");
    ok_hex(hr, S_OK);
    hr = pSrc->QueryValueExists(NULL, L"Nope");
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));

    hr = pSrc->QueryValueDirect(NULL, L"ValueB", &pBlob);
    ok_hex(hr, S_OK);
    ok(pBlob && pBlob->clSize == sizeof(DWORD), "pBlob->clSize was %ld\n", pBlob ? pBlob->clSize : 0);
    CoTaskMemFree(pBlob);

    hr = pSrc->EnumValues(&pEnum);
    ok_hex(hr, S_OK);
    if (pEnum)
    {
        cItems = CountEnum(pEnum, L"ValueA", L"ValueB", L"ValueG", &cFound);
        ok(cItems == 3 && cFound == 3, "values: %lu items, %lu expected ones\n", cItems, cFound);
        pEnum->Release();
        pEnum = NULL;
    }

    hr = pSrc->EnumSources(&pEnum);
    ok_hex(hr, S_OK);
    if (pEnum)
    {
        cItems = CountEnum(pEnum, k_SubKeyA, k_SubKeyB, NULL, &cFound);
        ok(cItems == 2 && cFound == 2, "sources: %lu items, %lu expected ones\n", cItems, cFound);
        pEnum->Release();
    }

    hr = pSrc->OpenSource(k_SubKeyA, &pSub);
    ok_hex(hr, S_OK);
    ok(pSub != NULL, "pSub was NULL\n");
    if (pSub)
        pSub->Release();

    pSub = NULL;
    hr = pSrc->OpenSource(L"Nope", &pSub);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));

    pSrc->Release();
}

START_TEST(QuerySourceCreateFromKey)
{
    if (IsWindowsVistaOrGreater())
    {
        g_pQuerySourceCreateFromKey = (FN_QuerySourceCreateFromKey)
            GetProcAddress(GetModuleHandleA("shlwapi"), MAKEINTRESOURCEA(544));
        ok(g_pQuerySourceCreateFromKey != NULL, "QuerySourceCreateFromKey not found\n");
        if (!g_pQuerySourceCreateFromKey)
            return;
        HRESULT hrCo = CoInitialize(NULL);
        SetupRegistry();
        Test_VistaSource();
        CleanupRegistry();
        if (SUCCEEDED(hrCo))
            CoUninitialize();
        return;
    }

    g_pQuerySourceCreateFromKey = (FN_QuerySourceCreateFromKey)
        GetProcAddress(GetModuleHandleA("shlwapi"), MAKEINTRESOURCEA(544));
    if (!g_pQuerySourceCreateFromKey)
    {
        skip("QuerySourceCreateFromKey not found\n");
        return;
    }

    HRESULT hrCoInit = CoInitialize(NULL);

    SetupRegistry();

    Test_EnumValues();
    Test_EnumSources();
    Test_CheckValues();

    CleanupRegistry();

    if (SUCCEEDED(hrCoInit))
        CoUninitialize();
}
