/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     First-boot setup declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _OOBE_APP
{
    WCHAR Name[128];
    WCHAR Url[512];
    WCHAR File[MAX_PATH];
    WCHAR Arguments[256];
    WCHAR Signer[128];
    WCHAR Sha256[65];
    BOOL Msi;
    BOOL Selected;
    DWORD Result;
} OOBE_APP, *POOBE_APP;

typedef struct _OOBE_PROGRESS
{
    HWND hwndActivity;
    HWND hwndItem;
    HWND hwndProgress;
} OOBE_PROGRESS, *POOBE_PROGRESS;

ULONG
LoadOobeApps(
    _Out_ POOBE_APP* Apps);

INT_PTR
CALLBACK
AppsPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

DWORD
InstallOobeApp(
    _Inout_ POOBE_APP App,
    _In_ POOBE_PROGRESS Progress);

extern POOBE_APP OobeApps;
extern ULONG OobeAppCount;
