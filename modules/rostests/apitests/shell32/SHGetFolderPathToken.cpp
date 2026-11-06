/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHGetFolderPath with the default user token on machine-wide folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"

START_TEST(SHGetFolderPathToken)
{
    static const int Folders[] =
    {
        CSIDL_COMMON_ADMINTOOLS,
        CSIDL_COMMON_APPDATA,
        CSIDL_COMMON_PROGRAMS,
        CSIDL_COMMON_STARTMENU,
    };
    static const DWORD Types[] = { SHGFP_TYPE_CURRENT, SHGFP_TYPE_DEFAULT };

    for (SIZE_T Folder = 0; Folder < _countof(Folders); ++Folder)
    {
        for (SIZE_T Type = 0; Type < _countof(Types); ++Type)
        {
            WCHAR CurrentUser[MAX_PATH] = L"", DefaultUser[MAX_PATH] = L"";
            HRESULT hr;

            hr = SHGetFolderPathW(NULL, Folders[Folder] | CSIDL_FLAG_DONT_VERIFY, NULL, Types[Type], CurrentUser);
            ok(hr == S_OK, "folder %d type %lu: current user hr 0x%08lx\n", Folders[Folder], Types[Type], hr);

            hr = SHGetFolderPathW(NULL, Folders[Folder] | CSIDL_FLAG_DONT_VERIFY, (HANDLE)-1, Types[Type], DefaultUser);
            ok(hr == S_OK, "folder %d type %lu: default user hr 0x%08lx\n", Folders[Folder], Types[Type], hr);
            ok(!lstrcmpiW(CurrentUser, DefaultUser), "folder %d type %lu: '%ls' and '%ls'\n",
               Folders[Folder], Types[Type], CurrentUser, DefaultUser);
        }
    }
}
