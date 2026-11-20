/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the User's Files folder of the desktop namespace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>

START_TEST(UsersFilesFolder)
{
    WCHAR szProfile[MAX_PATH];
    CComHeapPtr<ITEMIDLIST> pidlUsersFiles, pidlProfile, pidlDocuments;
    CComHeapPtr<WCHAR> pszUsersFiles, pszProfile;
    CComPtr<IShellFolder> psfDesktop, psfUsersFiles;
    CComPtr<IEnumIDList> penum;
    SFGAOF Attributes;
    HRESULT hr;

    hr = CoInitialize(NULL);
    ok_hr(hr, S_OK);

    Attributes = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_NONENUMERATED;
    hr = SHParseDisplayName(L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}", NULL, &pidlUsersFiles, Attributes,
                            &Attributes);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok(Attributes & SFGAO_FOLDER, "User's Files is not a folder (0x%lx)\n", Attributes);
        ok(Attributes & SFGAO_FILESYSTEM, "User's Files is not in the file system (0x%lx)\n", Attributes);
        ok(!(Attributes & SFGAO_NONENUMERATED), "User's Files is not enumerated (0x%lx)\n", Attributes);

        ok(SHGetSpecialFolderPathW(NULL, szProfile, CSIDL_PROFILE, FALSE), "No profile folder\n");
        hr = SHParseDisplayName(szProfile, NULL, &pidlProfile, 0, NULL);
        ok_hr(hr, S_OK);
        hr = SHGetNameFromIDList(pidlUsersFiles, SIGDN_NORMALDISPLAY, &pszUsersFiles);
        ok_hr(hr, S_OK);
        if (pidlProfile)
        {
            hr = SHGetNameFromIDList(pidlProfile, SIGDN_NORMALDISPLAY, &pszProfile);
            ok_hr(hr, S_OK);
        }
        if (pszUsersFiles && pszProfile)
            ok(!wcscmp(pszUsersFiles, pszProfile), "User's Files is named '%S', the profile folder '%S'\n",
               (PWSTR)pszUsersFiles, (PWSTR)pszProfile);

        hr = SHGetDesktopFolder(&psfDesktop);
        ok_hr(hr, S_OK);
        if (psfDesktop)
        {
            hr = psfDesktop->BindToObject(pidlUsersFiles, NULL, IID_PPV_ARG(IShellFolder, &psfUsersFiles));
            ok_hr(hr, S_OK);
        }
        if (psfUsersFiles)
        {
            hr = psfUsersFiles->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &penum);
            ok_hr(hr, S_OK);
        }
    }

    Attributes = SFGAO_NONENUMERATED;
    hr = SHParseDisplayName(L"::{450D8FBA-AD25-11D0-98A8-0800361B1103}", NULL, &pidlDocuments, Attributes,
                            &Attributes);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        ok(Attributes & SFGAO_NONENUMERATED, "Documents is enumerated on the desktop (0x%lx)\n", Attributes);

    CoUninitialize();
}
