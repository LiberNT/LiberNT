/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Control Panel item of the desktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>

#define CONTROLPANEL_ITEM L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}"
#define EXPLORER_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"

static void TestDefaultVerb(PCIDLIST_ABSOLUTE pidl)
{
    CComPtr<IShellFolder> psfParent;
    CComPtr<IContextMenu> pcm;
    PCUITEMID_CHILD pidlChild;
    WCHAR szVerb[64] = L"";
    HMENU hMenu;
    UINT id;
    HRESULT hr;

    hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psfParent), &pidlChild);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = psfParent->GetUIObjectOf(NULL, 1, &pidlChild, IID_IContextMenu, NULL, (void **)&pcm);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    hMenu = CreatePopupMenu();
    hr = pcm->QueryContextMenu(hMenu, 0, 1, 0x7fff, CMF_DEFAULTONLY);
    ok(SUCCEEDED(hr), "QueryContextMenu failed: 0x%lx\n", hr);
    id = GetMenuDefaultItem(hMenu, FALSE, 0);
    ok(id != (UINT)-1, "No default verb\n");
    if (id != (UINT)-1)
    {
        hr = pcm->GetCommandString(id - 1, GCS_VERBW, NULL, (LPSTR)szVerb, _countof(szVerb));
        ok_hr(hr, S_OK);
        ok(!_wcsicmp(szVerb, L"open"), "Default verb is '%S'\n", szVerb);
    }
    DestroyMenu(hMenu);
}

START_TEST(ControlPanelItem)
{
    CComHeapPtr<ITEMIDLIST> pidlItem;
    CComHeapPtr<WCHAR> pszItem;
    CComPtr<IExecuteCommand> pec;
    WCHAR szIndirect[MAX_PATH], szLocalized[MAX_PATH];
    DWORD dwHidden, cbData;
    SFGAOF Attributes;
    HKEY hKey;
    LONG error;
    HRESULT hr;

    hr = CoInitialize(NULL);
    ok_hr(hr, S_OK);

    hr = CoCreateInstance(CLSID_OpenControlPanel, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IExecuteCommand, &pec));
    ok_hr(hr, S_OK);

    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, EXPLORER_KEY L"Desktop\\NameSpace\\" CONTROLPANEL_ITEM, 0, KEY_READ,
                          &hKey);
    ok_long(error, ERROR_SUCCESS);
    if (error == ERROR_SUCCESS)
        RegCloseKey(hKey);

    dwHidden = 0;
    cbData = sizeof(dwHidden);
    error = RegGetValueW(HKEY_LOCAL_MACHINE, EXPLORER_KEY L"HideDesktopIcons\\NewStartPanel", CONTROLPANEL_ITEM,
                         RRF_RT_REG_DWORD, NULL, &dwHidden, &cbData);
    ok_long(error, ERROR_SUCCESS);
    ok_long(dwHidden, 1);

    Attributes = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_CANDELETE;
    hr = SHParseDisplayName(L"::" CONTROLPANEL_ITEM, NULL, &pidlItem, Attributes, &Attributes);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok(!(Attributes & SFGAO_FOLDER), "The Control Panel item is a folder (0x%lx)\n", Attributes);
        ok(!(Attributes & SFGAO_FILESYSTEM), "The Control Panel item is in the file system (0x%lx)\n", Attributes);

        hr = SHGetNameFromIDList(pidlItem, SIGDN_NORMALDISPLAY, &pszItem);
        ok_hr(hr, S_OK);
        cbData = sizeof(szIndirect);
        error = RegGetValueW(HKEY_CLASSES_ROOT, L"CLSID\\" CONTROLPANEL_ITEM, L"LocalizedString", RRF_RT_REG_SZ, NULL,
                             szIndirect, &cbData);
        ok_long(error, ERROR_SUCCESS);
        szLocalized[0] = UNICODE_NULL;
        if (error == ERROR_SUCCESS)
        {
            hr = SHLoadIndirectString(szIndirect, szLocalized, _countof(szLocalized), NULL);
            ok_hr(hr, S_OK);
        }
        if (pszItem && szLocalized[0])
            ok(!wcscmp(pszItem, szLocalized), "The item is named '%S', its LocalizedString is '%S'\n",
               (PWSTR)pszItem, szLocalized);

        TestDefaultVerb(pidlItem);
    }

    CoUninitialize();
}
