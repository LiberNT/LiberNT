/*
 * PROJECT:     ReactOS Headers
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     shdocvw.dll undocumented APIs
 * COPYRIGHT:   Copyright 2024-2025 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#pragma once

#include <exdisp.h> // For IShellWindows

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI
IEILIsEqual(
    _In_ LPCITEMIDLIST pidl1,
    _In_ LPCITEMIDLIST pidl2,
    _In_ BOOL bUnknown);

HRESULT WINAPI
AddUrlToFavorites(
    _In_ HWND hwnd,
    _In_opt_ LPCWSTR pszUrlW,
    _In_ LPCWSTR pszTitleW,
    _In_ BOOL fDisplayUI);

HRESULT WINAPI
WinList_NotifyNewLocation(
    _In_ IShellWindows *pShellWindows,
    _In_ LONG lCookie,
    _In_ LPCITEMIDLIST pidl);

HRESULT WINAPI
WinList_FindFolderWindow(
    _In_ LPCITEMIDLIST pidl,
    _In_ DWORD dwUnused,
    _Out_opt_ PLONG phwnd,
    _Out_opt_ IWebBrowserApp **ppWebBrowserApp);

#ifdef __cplusplus
} /* extern "C" */
#endif
