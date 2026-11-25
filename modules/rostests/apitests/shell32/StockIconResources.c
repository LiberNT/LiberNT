/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A module serves the icon images of its SystemResources .mun
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>

static BOOL
GetIconImage(HMODULE Module, UINT GroupId, int Size, PBYTE *Image, DWORD *ImageSize)
{
    HRSRC Resource;
    PBYTE Directory;
    int IconId;

    *Image = NULL;
    *ImageSize = 0;
    Resource = FindResourceW(Module, MAKEINTRESOURCEW(GroupId), (LPCWSTR)RT_GROUP_ICON);
    Directory = Resource ? LockResource(LoadResource(Module, Resource)) : NULL;
    if (!Directory)
        return FALSE;
    IconId = LookupIconIdFromDirectoryEx(Directory, TRUE, Size, Size, LR_DEFAULTCOLOR);
    Resource = IconId ? FindResourceW(Module, MAKEINTRESOURCEW(IconId), (LPCWSTR)RT_ICON) : NULL;
    *Image = Resource ? LockResource(LoadResource(Module, Resource)) : NULL;
    *ImageSize = Resource ? SizeofResource(Module, Resource) : 0;
    return *Image != NULL;
}

static void
CheckStockIcon(SHSTOCKICONID Id)
{
    static const int Sizes[] = {16, 32, 48};
    SHSTOCKICONINFO Info = {sizeof(Info)};
    PBYTE Loaded, Stored;
    DWORD LoadedSize, StoredSize;
    WCHAR Mun[MAX_PATH];
    HMODULE Module, Resources;
    HRESULT hr;
    UINT i;

    hr = SHGetStockIconInfo(Id, SHGSI_ICONLOCATION, &Info);
    ok(hr == S_OK, "stock icon %d: SHGetStockIconInfo returned %#lx\n", Id, hr);
    if (FAILED(hr))
        return;
    ok(Info.iIcon < 0, "stock icon %d: index %d is not a resource id\n", Id, Info.iIcon);
    if (Info.iIcon >= 0)
        return;
    GetWindowsDirectoryW(Mun, ARRAYSIZE(Mun));
    PathAppendW(Mun, L"SystemResources");
    PathAppendW(Mun, PathFindFileNameW(Info.szPath));
    StringCchCatW(Mun, ARRAYSIZE(Mun), L".mun");
    Module = LoadLibraryW(Info.szPath);
    ok(Module != NULL, "stock icon %d: cannot load %ls: %lu\n", Id, Info.szPath, GetLastError());
    Resources = LoadLibraryExW(Mun, NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    ok(Resources != NULL, "stock icon %d: cannot load %ls: %lu\n", Id, Mun, GetLastError());
    for (i = 0; Module && Resources && i < ARRAYSIZE(Sizes); i++)
    {
        ok(GetIconImage(Resources, -Info.iIcon, Sizes[i], &Stored, &StoredSize),
           "stock icon %d: no %d px image of group %d in %ls\n", Id, Sizes[i], -Info.iIcon, Mun);
        ok(GetIconImage(Module, -Info.iIcon, Sizes[i], &Loaded, &LoadedSize),
           "stock icon %d: no %d px image of group %d in %ls\n", Id, Sizes[i], -Info.iIcon, Info.szPath);
        if (Stored && Loaded)
        {
            ok(LoadedSize == StoredSize && !memcmp(Loaded, Stored, StoredSize),
               "stock icon %d: %d px image of %ls (%lu bytes) is not the one of %ls (%lu bytes)\n",
               Id, Sizes[i], Info.szPath, LoadedSize, Mun, StoredSize);
        }
    }
    if (Resources)
        FreeLibrary(Resources);
    if (Module)
        FreeLibrary(Module);
}

START_TEST(StockIconResources)
{
    CheckStockIcon(SIID_FOLDER);
    CheckStockIcon(SIID_FOLDEROPEN);
    CheckStockIcon(SIID_DOCNOASSOC);
}
