/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests parsing shell parsing names of registered and unknown CLSIDs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"

static void
TestParse(PCWSTR Name, BOOL Expected)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT hr;

    hr = SHParseDisplayName(Name, NULL, &pidl, 0, NULL);
    if (Expected)
    {
        ok(hr == S_OK, "%s: SHParseDisplayName returned 0x%lx\n", wine_dbgstr_w(Name), hr);
        ok(pidl != NULL, "%s: no PIDL\n", wine_dbgstr_w(Name));
    }
    else
    {
        ok(FAILED(hr), "%s: SHParseDisplayName returned 0x%lx\n", wine_dbgstr_w(Name), hr);
        ok(pidl == NULL, "%s: unexpected PIDL\n", wine_dbgstr_w(Name));
    }
    if (pidl)
        ILFree(pidl);
}

#define TEST_CLSID L"{8A4D5E6B-2C1F-4B7A-9E3D-6F5A4C3B2A19}"
#define TEST_CLASS_KEY L"Software\\Classes\\CLSID\\" TEST_CLSID
#define TEST_NAMESPACE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\" TEST_CLSID

static BOOL
CreateTestKey(PCWSTR Path)
{
    HKEY hKey;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);

    ok(error == ERROR_SUCCESS, "%s: RegCreateKeyExW returned %ld\n", wine_dbgstr_w(Path), error);
    if (error != ERROR_SUCCESS)
        return FALSE;
    RegCloseKey(hKey);
    return TRUE;
}

static void
TestNameSpaceRule(void)
{
    if (!CreateTestKey(TEST_CLASS_KEY L"\\ShellFolder"))
        return;
    TestParse(L"::" TEST_CLSID, FALSE);
    TestParse(L"shell:::" TEST_CLSID, TRUE);

    if (CreateTestKey(TEST_NAMESPACE_KEY))
    {
        TestParse(L"::" TEST_CLSID, TRUE);
        SHDeleteKeyW(HKEY_CURRENT_USER, TEST_NAMESPACE_KEY);
    }
    SHDeleteKeyW(HKEY_CURRENT_USER, TEST_CLASS_KEY);
}

START_TEST(RegItemParse)
{
    HRESULT hr = CoInitialize(NULL);

    TestParse(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", TRUE);
    TestParse(L"::{645FF040-5081-101B-9F08-00AA002F954E}", TRUE);
    TestParse(L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}", TRUE);
    TestParse(L"::{BD84B380-8CA2-1069-AB1D-08000948F534}", FALSE);
    TestParse(L"::" TEST_CLSID, FALSE);
    TestNameSpaceRule();

    if (SUCCEEDED(hr))
        CoUninitialize();
}
