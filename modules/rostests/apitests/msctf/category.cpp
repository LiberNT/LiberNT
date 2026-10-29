/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfCategoryMgr and ITfDisplayAttributeMgr
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <cguid.h>
#include "testtip.h"

static const GUID GUID_TEST_ITEM = {0x6b3a3f60, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};
static const GUID GUID_TEST_CATEGORY = {0x6b3a3f61, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

static BOOL EnumContains(IEnumGUID *pEnum, REFGUID guid)
{
    GUID got;
    ULONG cFetched;
    BOOL fFound = FALSE;
    while (pEnum && pEnum->Next(1, &got, &cFetched) == S_OK && cFetched)
    {
        if (IsEqualGUID(got, guid))
            fFound = TRUE;
    }
    return fFound;
}

static void TestCategories(ITfCategoryMgr *pMgr)
{
    IEnumGUID *pEnum = NULL;
    const GUID *list[2];
    GUID found;
    TfGuidAtom atom1 = 0, atom2 = 0;
    BOOL fEqual;
    WCHAR szDesc[64];
    ULONG cch;
    HRESULT hr;

    hr = pMgr->RegisterCategory(CLSID_TestTip, GUID_TEST_CATEGORY, GUID_TEST_ITEM);
    ok_hr(hr, S_OK);

    hr = pMgr->EnumItemsInCategory(GUID_TEST_CATEGORY, &pEnum);
    ok_hr(hr, S_OK);
    ok(EnumContains(pEnum, GUID_TEST_ITEM), "item not in the category\n");
    if (pEnum)
        pEnum->Release();

    pEnum = NULL;
    hr = pMgr->EnumCategoriesInItem(GUID_TEST_ITEM, &pEnum);
    ok_hr(hr, S_OK);
    ok(EnumContains(pEnum, GUID_TEST_CATEGORY), "category not listed for the item\n");
    if (pEnum)
        pEnum->Release();

    pEnum = NULL;
    hr = pMgr->EnumCategoriesInItem(CLSID_TestTip, &pEnum);
    ok_hr(hr, S_OK);
    ok(EnumContains(pEnum, GUID_TFCAT_TIP_KEYBOARD), "keyboard category not listed for the TIP\n");
    if (pEnum)
        pEnum->Release();

    list[0] = &GUID_TFCAT_TIP_KEYBOARD;
    list[1] = &GUID_TEST_CATEGORY;
    found = GUID_NULL;
    hr = pMgr->FindClosestCategory(GUID_TEST_ITEM, &found, list, 2);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(found, GUID_TEST_CATEGORY), "wrong closest category\n");

    found = GUID_TEST_ITEM;
    hr = pMgr->FindClosestCategory(GUID_TEST_ITEM, &found, list, 1);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(found, GUID_NULL), "closest category not cleared\n");

    hr = pMgr->RegisterGUID(GUID_TEST_ITEM, &atom1);
    ok_hr(hr, S_OK);
    hr = pMgr->RegisterGUID(GUID_TEST_ITEM, &atom2);
    ok_hr(hr, S_OK);
    ok(atom1 && atom1 == atom2, "atoms %lu %lu\n", atom1, atom2);
    found = GUID_NULL;
    hr = pMgr->GetGUID(atom1, &found);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(found, GUID_TEST_ITEM), "wrong GUID for the atom\n");
    fEqual = FALSE;
    hr = pMgr->IsEqualTfGuidAtom(atom1, GUID_TEST_ITEM, &fEqual);
    ok_hr(hr, S_OK);
    ok_int(fEqual, TRUE);
    fEqual = TRUE;
    hr = pMgr->IsEqualTfGuidAtom(atom1, GUID_TEST_CATEGORY, &fEqual);
    ok_hr(hr, S_OK);
    ok_int(fEqual, FALSE);

    hr = pMgr->RegisterGUIDDescription(CLSID_TestTip, GUID_TEST_ITEM, L"item", 4);
    ok_hr(hr, S_OK);
    hr = pMgr->GetGUIDDescription(GUID_TEST_ITEM, NULL);
    ok_hr(hr, E_INVALIDARG);
    {
        BSTR bstr = NULL;
        hr = pMgr->GetGUIDDescription(GUID_TEST_ITEM, &bstr);
        ok_hr(hr, S_OK);
        ok(bstr && !wcscmp(bstr, L"item"), "description %ls\n", bstr ? bstr : L"(null)");
        SysFreeString(bstr);
    }
    hr = pMgr->UnregisterGUIDDescription(CLSID_TestTip, GUID_TEST_ITEM);
    ok_hr(hr, S_OK);
    (void)szDesc;
    (void)cch;

    hr = pMgr->UnregisterCategory(CLSID_TestTip, GUID_TEST_CATEGORY, GUID_TEST_ITEM);
    ok_hr(hr, S_OK);
    {
        HKEY hKey;
        LSTATUS error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                      L"SOFTWARE\\Microsoft\\CTF\\TIP\\{6B3A3F40-51D2-4C1E-9A37-1F528C0E4D71}\\Category\\Category\\"
                                      L"{6B3A3F61-51D2-4C1E-9A37-1F528C0E4D71}\\{6B3A3F60-51D2-4C1E-9A37-1F528C0E4D71}",
                                      0, KEY_READ, &hKey);
        ok_long(error, ERROR_FILE_NOT_FOUND);
        if (!error)
            RegCloseKey(hKey);
    }
}

static void TestDisplayAttributes(ITfCategoryMgr *pCategoryMgr)
{
    ITfDisplayAttributeMgr *pMgr = NULL;
    IEnumTfDisplayAttributeInfo *pEnum = NULL;
    ITfDisplayAttributeInfo *pInfo = NULL;
    ULONG cFetched;
    BOOL fFound = FALSE;
    HRESULT hr;

    hr = pCategoryMgr->RegisterCategory(CLSID_TestTip, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TestTip);
    ok_hr(hr, S_OK);

    hr = CoCreateInstance(CLSID_TF_DisplayAttributeMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfDisplayAttributeMgr,
                          (void **)&pMgr);
    ok_hr(hr, S_OK);
    if (!pMgr)
        return;

    hr = pMgr->EnumDisplayAttributeInfo(&pEnum);
    ok_hr(hr, S_OK);
    while (pEnum && pEnum->Next(1, &pInfo, &cFetched) == S_OK && cFetched)
    {
        GUID guid;
        if (SUCCEEDED(pInfo->GetGUID(&guid)) && IsEqualGUID(guid, GUID_TestDisplayAttribute))
            fFound = TRUE;
        pInfo->Release();
    }
    ok(fFound, "test attribute not enumerated\n");
    if (pEnum)
        pEnum->Release();

    pInfo = (ITfDisplayAttributeInfo *)(ULONG_PTR)1;
    hr = pMgr->GetDisplayAttributeInfo(GUID_TEST_ITEM, &pInfo, NULL);
    ok_hr(hr, E_FAIL);
    ok(pInfo == NULL, "info %p\n", pInfo);

    pMgr->Release();
    pCategoryMgr->UnregisterCategory(CLSID_TestTip, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TestTip);
}

START_TEST(category)
{
    ITfCategoryMgr *pMgr = NULL;
    HRESULT hr;

    CoInitialize(NULL);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        TestTip_Unregister();
        CoUninitialize();
        return;
    }
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void **)&pMgr);
    ok_hr(hr, S_OK);
    if (pMgr)
    {
        TestCategories(pMgr);
        TestDisplayAttributes(pMgr);
        pMgr->Release();
    }
    TestTip_Unregister();
    CoUninitialize();
}

