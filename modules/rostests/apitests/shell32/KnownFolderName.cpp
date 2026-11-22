/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the localized display names of the user's known folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <knownfolders.h>
#include <shobjidl.h>
#include <strsafe.h>

static void test_known_folder(REFKNOWNFOLDERID id, PCSTR what, BOOL sameItemName)
{
    WCHAR module[MAX_PATH], indirect[MAX_PATH + 16], expected[MAX_PATH];
    PWSTR path = NULL, display = NULL;
    IShellItem *item = NULL;
    int resource = 0;
    HRESULT hr;

    hr = SHGetKnownFolderPath(id, KF_FLAG_CREATE | KF_FLAG_INIT, NULL, &path);
    ok(hr == S_OK, "%s: SHGetKnownFolderPath hr 0x%lx\n", what, hr);
    if (FAILED(hr))
        return;

    ok(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES &&
       (GetFileAttributesW(path) & FILE_ATTRIBUTE_READONLY), "%s: folder attrs %lx\n", what,
       GetFileAttributesW(path));

    module[0] = 0;
    hr = SHGetLocalizedName(path, module, ARRAYSIZE(module), &resource);
    ok(hr == S_OK, "%s: SHGetLocalizedName hr 0x%lx\n", what, hr);
    if (FAILED(hr))
    {
        CoTaskMemFree(path);
        return;
    }
    ok(module[0] && resource > 0, "%s: module '%ls' id %d\n", what, module, resource);

    expected[0] = 0;
    StringCchPrintfW(indirect, ARRAYSIZE(indirect), L"@%ls,-%d", module, resource);
    hr = SHLoadIndirectString(indirect, expected, ARRAYSIZE(expected), NULL);
    ok(hr == S_OK && expected[0], "%s: SHLoadIndirectString('%ls') hr 0x%lx\n", what, indirect, hr);

    hr = SHCreateItemFromParsingName(path, NULL, IID_IShellItem, (void **)&item);
    ok(hr == S_OK, "%s: SHCreateItemFromParsingName hr 0x%lx\n", what, hr);
    if (SUCCEEDED(hr))
    {
        hr = item->GetDisplayName(SIGDN_NORMALDISPLAY, &display);
        ok(hr == S_OK, "%s: path item GetDisplayName hr 0x%lx\n", what, hr);
        if (SUCCEEDED(hr))
        {
            ok(!lstrcmpW(display, expected), "%s: path item name '%ls', expected '%ls'\n", what, display, expected);
            CoTaskMemFree(display);
        }
        item->Release();
    }

    hr = SHGetKnownFolderItem(id, KF_FLAG_DEFAULT, NULL, IID_IShellItem, (void **)&item);
    ok(hr == S_OK, "%s: SHGetKnownFolderItem hr 0x%lx\n", what, hr);
    if (SUCCEEDED(hr) && !sameItemName)
        item->Release();
    else if (SUCCEEDED(hr))
    {
        hr = item->GetDisplayName(SIGDN_NORMALDISPLAY, &display);
        ok(hr == S_OK, "%s: known folder item GetDisplayName hr 0x%lx\n", what, hr);
        if (SUCCEEDED(hr))
        {
            ok(!lstrcmpW(display, expected), "%s: known folder item name '%ls', expected '%ls'\n", what, display,
               expected);
            CoTaskMemFree(display);
        }
        item->Release();
    }

    CoTaskMemFree(path);
}

START_TEST(KnownFolderName)
{
    HRESULT hr = CoInitialize(NULL);

    ok(SUCCEEDED(hr), "CoInitialize hr 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    test_known_folder(FOLDERID_Desktop, "Desktop", FALSE);
    test_known_folder(FOLDERID_Documents, "Documents", TRUE);
    test_known_folder(FOLDERID_Downloads, "Downloads", TRUE);
    test_known_folder(FOLDERID_Favorites, "Favorites", TRUE);
    test_known_folder(FOLDERID_Music, "Music", TRUE);
    test_known_folder(FOLDERID_Pictures, "Pictures", TRUE);
    test_known_folder(FOLDERID_Videos, "Videos", TRUE);

    CoUninitialize();
}
