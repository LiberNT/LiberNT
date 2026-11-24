/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that image file types resolve to an existing icon, type name and open command
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shlwapi.h>
#include <strsafe.h>

static BOOL ModuleExists(LPCWSTR pszModule)
{
    WCHAR szPath[MAX_PATH];
    if (!ExpandEnvironmentStringsW(pszModule, szPath, _countof(szPath)))
        return FALSE;
    PathUnquoteSpacesW(szPath);
    if (PathIsRelativeW(szPath) && !SearchPathW(NULL, szPath, NULL, _countof(szPath), szPath, NULL))
        return FALSE;
    return GetFileAttributesW(szPath) != INVALID_FILE_ATTRIBUTES;
}

static void CheckIcon(LPCWSTR pszExt)
{
    WCHAR szIcon[MAX_PATH + 16], szPath[MAX_PATH];
    DWORD cch = _countof(szIcon);
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_DEFAULTICON, pszExt, NULL, szIcon, &cch);
    if (FAILED(hr))
    {
        skip("%S has no default icon (0x%lx)\n", pszExt, hr);
        return;
    }
    if (!wcscmp(szIcon, L"%1"))
        return;

    INT iIndex = PathParseIconLocationW(szIcon);
    ok(ExpandEnvironmentStringsW(szIcon, szPath, _countof(szPath)) != 0, "%S icon %S does not expand\n", pszExt, szIcon);
    ok(ModuleExists(szPath), "%S icon module %S is missing\n", pszExt, szPath);
    HICON hIcon = NULL;
    ok(ExtractIconExW(szPath, iIndex, &hIcon, NULL, 1) == 1 && hIcon, "%S icon %S,%d cannot be extracted\n", pszExt,
       szPath, iIndex);
    if (hIcon)
        DestroyIcon(hIcon);
}

static void CheckTypeName(LPCWSTR pszExt)
{
    WCHAR szName[256];
    DWORD cch = _countof(szName);
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_FRIENDLYDOCNAME, pszExt, NULL, szName, &cch);
    ok(SUCCEEDED(hr) && szName[0] && szName[0] != L'@', "%S type name is '%S' (0x%lx)\n", pszExt,
       SUCCEEDED(hr) ? szName : L"", hr);
}

static void CheckOpen(LPCWSTR pszExt)
{
    WCHAR szCommand[1024], szExe[MAX_PATH];
    DWORD cch = _countof(szCommand);
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_COMMAND, pszExt, L"open", szCommand, &cch);
    if (FAILED(hr))
    {
        skip("%S has no open command (0x%lx)\n", pszExt, hr);
        return;
    }
    cch = _countof(szExe);
    hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, pszExt, L"open", szExe, &cch);
    ok(SUCCEEDED(hr) && ModuleExists(szExe), "%S opens with a missing program '%S' (0x%lx)\n", pszExt,
       SUCCEEDED(hr) ? szExe : L"", hr);

    LPCWSTR pszArgs = PathGetArgsW(szCommand);
    if (SUCCEEDED(hr) && !_wcsicmp(PathFindFileNameW(szExe), L"rundll32.exe") && pszArgs && *pszArgs)
    {
        WCHAR szDll[MAX_PATH];
        StringCchCopyW(szDll, _countof(szDll), pszArgs);
        PWSTR pszComma = wcschr(szDll, L',');
        if (pszComma)
            *pszComma = UNICODE_NULL;
        PathRemoveBlanksW(szDll);
        ok(ModuleExists(szDll), "%S opens through a missing library '%S'\n", pszExt, szDll);
    }
}

START_TEST(ImageAssociations)
{
    static const LPCWSTR Extensions[] = { L".bmp", L".png", L".jpg", L".jpeg", L".gif", L".tif", L".tiff", L".ico" };

    for (UINT i = 0; i < _countof(Extensions); ++i)
    {
        CheckIcon(Extensions[i]);
        CheckTypeName(Extensions[i]);
        CheckOpen(Extensions[i]);
    }
}
