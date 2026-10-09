/*
 * Copyright (C) 2004 Eric Kohl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once


typedef struct _ITEMSDATA
{
    HWND hwndDlg;
} ITEMSDATA, *PITEMSDATA;

typedef struct _REGISTRATIONNOTIFY
{
    ULONG Progress;
    UINT ActivityID;
    LPCWSTR CurrentItem;
    LPCWSTR ErrorMessage;
    UINT MessageID;
    DWORD LastError;
} REGISTRATIONNOTIFY, *PREGISTRATIONNOTIFY;


#define PM_REGISTRATION_NOTIFY (WM_APP + 1)
/* Private Message used to communicate progress from the background
   registration thread to the main thread.
   wParam = 0 Registration in progress
          = 1 Registration completed
   lParam = Pointer to a REGISTRATIONNOTIFY structure */

#define PM_ITEM_START (WM_APP + 2)
/* Start of a new Item
   wParam = item number
   lParam = number of steps */

#define PM_ITEM_END   (WM_APP + 3)
/* End of a new Item
   wParam = item number
   lParam = Error Code */

#define PM_STEP_START (WM_APP + 4)
#define PM_STEP_END   (WM_APP + 5)
#define PM_ITEMS_DONE (WM_APP + 6)


extern HINSTANCE hDllInstance;
extern HINF hSysSetupInf;

/* addons.c */
HRESULT
InstallOptionalComponents(
    _In_ PITEMSDATA pItemsData);

HRESULT
RunCommandAndWait(
    _In_ PWCHAR Command);

/* install */

VOID
CreateTempDir(
    IN LPCWSTR VarName);

BOOL
InstallSysSetupInfDevices(VOID);

BOOL
InstallSysSetupInfComponents(VOID);

BOOL
InitializeProgramFilesDir(VOID);

VOID
InitializeDefaultUserLocale(VOID);

DWORD
SaveDefaultUserHive(VOID);

BOOL
RegisterTypeLibraries(
    _In_ PITEMSDATA pItemsData,
    _In_ PREGISTRATIONNOTIFY pNotify,
    _In_ HINF hinf,
    _In_ LPCWSTR szSection);

VOID
InstallStartMenuItems(
    _In_ PITEMSDATA pItemsData);

/* netinstall.c */

BOOL
InstallNetworkComponent(
    _In_ PWSTR pszComponentId,
    _In_ BOOL bOffline);

/* security.c */

LONG
CountSecuritySteps(VOID);

DWORD
InstallSecurity(
    _In_ PITEMSDATA pItemsData,
    _In_ PREGISTRATIONNOTIFY pNotify);

VOID
InstallLiveCDPrivileges(VOID);

DWORD
InstallTargetSystem(VOID);

NTSTATUS
CreateTargetAccount(
    _In_ PCWSTR UserName,
    _In_ PCWSTR Password);

/* wizard.c */

BOOL
DoWriteInstallationType(INSTALLATION_TYPE nOption);

BOOL
WriteOwnerSettings(PCWSTR OwnerName,
                   PCWSTR OwnerOrganization);

VOID
EnableVisualTheme(
    _In_opt_ HWND hwndParent,
    _In_opt_ PCWSTR ThemeFile);

/* EOF */
