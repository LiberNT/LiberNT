/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell namespace classes served by windows.storage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

HINSTANCE shell32_hInstance;
WCHAR swShell32Name[MAX_PATH];

OBJECT_ENTRY_AUTO(CLSID_ShellFSFolder, CFSFolder)
OBJECT_ENTRY_AUTO(CLSID_MyComputer, CDrivesFolder)
OBJECT_ENTRY_AUTO(CLSID_ShellDesktop, CDesktopFolder)
OBJECT_ENTRY_AUTO(CLSID_ShellItem, CShellItem)
OBJECT_ENTRY_AUTO(CLSID_ShellLink, CShellLink)
OBJECT_ENTRY_AUTO(CLSID_QueryAssociations, CQueryAssociations)

EXTERN_C VOID
WSTORAGE_InitShell(HINSTANCE hInstance)
{
    shell32_hInstance = hInstance;
    UINT cch = GetSystemDirectoryW(swShell32Name, _countof(swShell32Name));
    if (!cch || cch >= _countof(swShell32Name) ||
        !PathAppendW(swShell32Name, L"shell32.dll"))
    {
        StringCchCopyW(swShell32Name, _countof(swShell32Name), L"shell32.dll");
    }
}

EXTERN_C VOID
WSTORAGE_TermShell(VOID)
{
    CComCreatorCentralInstance< ATL::CComObject< CDrivesFolder > >::Term();
    CComCreatorCentralInstance< ATL::CComObject< CDesktopFolder > >::Term();
    shell32_hInstance = NULL;
}
