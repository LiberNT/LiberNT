/*
 * ReactOS New devices installation
 * Copyright (C) 2005, 2008 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*
 * PROJECT:         ReactOS Add hardware control panel
 * FILE:            dll/cpl/hdwwiz/hdwwiz.c
 * PURPOSE:         ReactOS Add hardware control panel
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 *                  Dmitry Chapyshev (dmitry@reactos.org)
 */

#include "resource.h"
#include "hdwwiz.h"

/* GLOBALS ******************************************************************/

HINSTANCE hApplet = NULL;
HFONT hTitleFont;
SP_CLASSIMAGELIST_DATA ImageListData;
HANDLE hProcessHeap;
HANDLE hDetectThread;
LONG DetectCancelled;
HDEVINFO *DetectedDevInfo;
LPGUID DetectClassGuids;
PHWTYPE_ENTRY HwTypes;
BOOL SelectAllClasses;
GUID SelectClassGuid;
BOOL SelectListStale = TRUE;
HDEVINFO hSelectDevInfo = INVALID_HANDLE_VALUE;
HDEVINFO hInstallDevInfo = INVALID_HANDLE_VALUE;
SP_DEVINFO_DATA InstallDevInfoData;
SP_DRVINFO_DATA_W InstallDrvInfoData;
DWORD DetectedDevInfoCount;

typedef BOOL (WINAPI *PINSTALL_NEW_DEVICE)(HWND, LPGUID, PDWORD);


/* STATIC FUNCTIONS *********************************************************/

static HFONT
CreateTitleFont(VOID)
{
    NONCLIENTMETRICS ncm;
    LOGFONT LogFont;
    HDC hdc;
    INT FontSize;
    HFONT hFont;

    ncm.cbSize = sizeof(NONCLIENTMETRICS);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);

    LogFont = ncm.lfMessageFont;
    LogFont.lfWeight = FW_BOLD;
    wcscpy(LogFont.lfFaceName, L"MS Shell Dlg");

    hdc = GetDC(NULL);
    FontSize = 12;
    LogFont.lfHeight = 0 - GetDeviceCaps (hdc, LOGPIXELSY) * FontSize / 72;
    hFont = CreateFontIndirect(&LogFont);
    ReleaseDC(NULL, hdc);

    return hFont;
}

static INT_PTR CALLBACK
StartPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /* Set title font */
            SendDlgItemMessage(hwndDlg, IDC_FINISHTITLE, WM_SETFONT, (WPARAM)hTitleFont, (LPARAM)TRUE);
        }
        break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

static INT_PTR CALLBACK
FinishPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /* Set title font */
            SendDlgItemMessage(hwndDlg, IDC_FINISHTITLE, WM_SETFONT, (WPARAM)hTitleFont, (LPARAM)TRUE);
        }
        break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    /* Only "Finish" button */
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_FINISH);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

static INT_PTR CALLBACK
SelectWayPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT | PSWIZB_BACK);
                    if (SendDlgItemMessage(hwndDlg, IDC_AUTOINSTALL, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        SendDlgItemMessage(hwndDlg, IDC_MANUALLYINST, BM_SETCHECK, 0, 0);
                    else
                    {
                        SendDlgItemMessage(hwndDlg, IDC_AUTOINSTALL, BM_SETCHECK, 1, 1);
                        SendDlgItemMessage(hwndDlg, IDC_MANUALLYINST, BM_SETCHECK, 0, 0);
                    }
                }
                break;

                case PSN_WIZNEXT:
                {
                    if (SendDlgItemMessage(hwndDlg, IDC_AUTOINSTALL, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_PROGRESSPAGE);
                    else
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_HWTYPESPAGE);

                    return TRUE;
                }
            }
        }
        break;
    }

    return FALSE;
}

static int __cdecl
CompareHwTypes(const void *a, const void *b)
{
    return _wcsicmp(((const HWTYPE_ENTRY *)a)->Description, ((const HWTYPE_ENTRY *)b)->Description);
}

static VOID
InitHardWareTypesPage(HWND hwndDlg)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_HWTYPESLIST);
    WCHAR szBuffer[MAX_STR_SIZE];
    LPGUID ClassGuids = NULL;
    DWORD ClassCount = 0;
    LV_COLUMN Column;
    LV_ITEM Item;
    RECT Rect;
    DWORD i;

    if (!hList) return;

    ZeroMemory(&Column, sizeof(LV_COLUMN));
    GetClientRect(hList, &Rect);
    Column.mask = LVCF_FMT | LVCF_WIDTH;
    Column.fmt = LVCFMT_LEFT;
    Column.cx = Rect.right - GetSystemMetrics(SM_CXVSCROLL);
    (VOID) ListView_InsertColumn(hList, 0, &Column);
    (VOID) ListView_SetImageList(hList, ImageListData.ImageList, LVSIL_SMALL);
    (VOID) ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

    ZeroMemory(&Item, sizeof(LV_ITEM));
    Item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
    LoadString(hApplet, IDS_SHOWALLDEVICES, szBuffer, ARRAYSIZE(szBuffer));
    Item.pszText = szBuffer;
    Item.iImage = -1;
    Item.lParam = -1;
    (VOID) ListView_InsertItem(hList, &Item);

    SetupDiBuildClassInfoList(DIBCI_NOINSTALLCLASS, NULL, 0, &ClassCount);
    if (ClassCount != 0)
    {
        ClassGuids = HeapAlloc(hProcessHeap, 0, ClassCount * sizeof(GUID));
        HwTypes = HeapAlloc(hProcessHeap, 0, ClassCount * sizeof(HWTYPE_ENTRY));
    }

    if (ClassGuids != NULL && HwTypes != NULL &&
        SetupDiBuildClassInfoList(DIBCI_NOINSTALLCLASS, ClassGuids, ClassCount, &ClassCount))
    {
        for (i = 0; i < ClassCount; i++)
        {
            HwTypes[i].ClassGuid = ClassGuids[i];
            if (!SetupDiGetClassDescription(&ClassGuids[i], HwTypes[i].Description, ARRAYSIZE(HwTypes[i].Description), NULL) &&
                !SetupDiClassNameFromGuid(&ClassGuids[i], HwTypes[i].Description, ARRAYSIZE(HwTypes[i].Description), NULL))
            {
                HwTypes[i].Description[0] = UNICODE_NULL;
            }
        }

        qsort(HwTypes, ClassCount, sizeof(HWTYPE_ENTRY), CompareHwTypes);

        for (i = 0; i < ClassCount; i++)
        {
            Item.iItem = ListView_GetItemCount(hList);
            Item.pszText = HwTypes[i].Description;
            Item.lParam = i;
            if (!SetupDiGetClassImageIndex(&ImageListData, &HwTypes[i].ClassGuid, &Item.iImage))
                Item.iImage = -1;
            (VOID) ListView_InsertItem(hList, &Item);
        }
    }

    HeapFree(hProcessHeap, 0, ClassGuids);

    ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static INT_PTR CALLBACK
HdTypesPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            InitHardWareTypesPage(hwndDlg);
        }
        break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
                }
                break;

                case PSN_WIZBACK:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_SELECTWAYPAGE);
                    return TRUE;
                }

                case PSN_WIZNEXT:
                {
                    HWND hList = GetDlgItem(hwndDlg, IDC_HWTYPESLIST);
                    LV_ITEM Item;

                    ZeroMemory(&Item, sizeof(LV_ITEM));
                    Item.mask = LVIF_PARAM;
                    Item.iItem = ListView_GetNextItem(hList, -1, LVNI_SELECTED);

                    ZeroMemory(&SelectClassGuid, sizeof(GUID));
                    SelectAllClasses = TRUE;
                    if (Item.iItem != -1 && ListView_GetItem(hList, &Item) && Item.lParam != -1)
                    {
                        SelectClassGuid = HwTypes[Item.lParam].ClassGuid;
                        SelectAllClasses = FALSE;
                    }
                    SelectListStale = TRUE;

                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_SELECTDRIVERPAGE);
                    return TRUE;
                }
            }
        }
        break;
    }

    return FALSE;
}

static VOID
FreeInstallDevice(VOID)
{
    if (hInstallDevInfo != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(hInstallDevInfo);
        hInstallDevInfo = INVALID_HANDLE_VALUE;
    }
}

static VOID
InitSelectDriverList(HWND hList, UINT uHeaderId)
{
    WCHAR szHeader[MAX_STR_SIZE];
    LV_COLUMN Column;
    RECT Rect;

    LoadString(hApplet, uHeaderId, szHeader, ARRAYSIZE(szHeader));
    GetClientRect(hList, &Rect);

    ZeroMemory(&Column, sizeof(LV_COLUMN));
    Column.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
    Column.fmt = LVCFMT_LEFT;
    Column.pszText = szHeader;
    Column.cx = Rect.right - GetSystemMetrics(SM_CXVSCROLL);
    (VOID) ListView_InsertColumn(hList, 0, &Column);
    (VOID) ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);
}

static VOID
FillModelList(HWND hwndDlg)
{
    HWND hMfgList = GetDlgItem(hwndDlg, IDC_MFGLIST);
    HWND hModelList = GetDlgItem(hwndDlg, IDC_MODELLIST);
    SP_DRVINFO_DATA_W DrvInfoData;
    WCHAR szMfg[LINE_LEN];
    LV_ITEM Item;
    INT iMfg;
    DWORD i;

    (VOID) ListView_DeleteAllItems(hModelList);

    iMfg = ListView_GetNextItem(hMfgList, -1, LVNI_SELECTED);
    if (iMfg == -1 || hSelectDevInfo == INVALID_HANDLE_VALUE)
        return;

    szMfg[0] = UNICODE_NULL;
    ListView_GetItemText(hMfgList, iMfg, 0, szMfg, ARRAYSIZE(szMfg));

    ZeroMemory(&Item, sizeof(LV_ITEM));
    Item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
    if (!SetupDiGetClassImageIndex(&ImageListData, &SelectClassGuid, &Item.iImage))
        Item.iImage = -1;

    DrvInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
    for (i = 0; SetupDiEnumDriverInfoW(hSelectDevInfo, NULL, SPDIT_CLASSDRIVER, i, &DrvInfoData); i++)
    {
        if (_wcsicmp(DrvInfoData.MfgName, szMfg) != 0)
            continue;

        Item.iItem = ListView_GetItemCount(hModelList);
        Item.pszText = DrvInfoData.Description;
        Item.lParam = i;
        (VOID) ListView_InsertItem(hModelList, &Item);
    }

    ListView_SetItemState(hModelList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static VOID
FillManufacturerList(HWND hwndDlg)
{
    HWND hMfgList = GetDlgItem(hwndDlg, IDC_MFGLIST);
    SP_DRVINFO_DATA_W DrvInfoData;
    LVFINDINFO FindInfo;
    LV_ITEM Item;
    DWORD i;

    (VOID) ListView_DeleteAllItems(hMfgList);
    (VOID) ListView_DeleteAllItems(GetDlgItem(hwndDlg, IDC_MODELLIST));

    if (hSelectDevInfo == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(&Item, sizeof(LV_ITEM));
    Item.mask = LVIF_TEXT;
    ZeroMemory(&FindInfo, sizeof(LVFINDINFO));
    FindInfo.flags = LVFI_STRING;

    DrvInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
    for (i = 0; SetupDiEnumDriverInfoW(hSelectDevInfo, NULL, SPDIT_CLASSDRIVER, i, &DrvInfoData); i++)
    {
        FindInfo.psz = DrvInfoData.MfgName;
        if (ListView_FindItem(hMfgList, -1, &FindInfo) != -1)
            continue;

        Item.iItem = ListView_GetItemCount(hMfgList);
        Item.pszText = DrvInfoData.MfgName;
        (VOID) ListView_InsertItem(hMfgList, &Item);
    }

    ListView_SetItemState(hMfgList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static VOID
BuildSelectDriverList(HWND hwndDlg, LPCWSTR InfPath OPTIONAL)
{
    SP_DEVINSTALL_PARAMS_W InstallParams;
    HCURSOR hOldCursor;

    hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (hSelectDevInfo != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(hSelectDevInfo);

    hSelectDevInfo = SetupDiCreateDeviceInfoList(SelectAllClasses ? NULL : &SelectClassGuid, GetParent(hwndDlg));
    if (hSelectDevInfo != INVALID_HANDLE_VALUE)
    {
        if (InfPath != NULL)
        {
            InstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS_W);
            if (SetupDiGetDeviceInstallParamsW(hSelectDevInfo, NULL, &InstallParams))
            {
                InstallParams.Flags |= DI_ENUMSINGLEINF;
                lstrcpynW(InstallParams.DriverPath, InfPath, ARRAYSIZE(InstallParams.DriverPath));
                SetupDiSetDeviceInstallParamsW(hSelectDevInfo, NULL, &InstallParams);
            }
        }

        SetupDiBuildDriverInfoList(hSelectDevInfo, NULL, SPDIT_CLASSDRIVER);
    }

    FillManufacturerList(hwndDlg);

    SetCursor(hOldCursor);
}

static VOID
HaveDisk(HWND hwndDlg)
{
    WCHAR szFilter[MAX_STR_SIZE + 8];
    WCHAR szFile[MAX_PATH];
    OPENFILENAMEW ofn;
    SIZE_T Length;

    ZeroMemory(szFilter, sizeof(szFilter));
    LoadString(hApplet, IDS_INFFILTER, szFilter, MAX_STR_SIZE);
    Length = wcslen(szFilter) + 1;
    wcscpy(&szFilter[Length], L"*.inf");

    szFile[0] = UNICODE_NULL;

    ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = GetParent(hwndDlg);
    ofn.lpstrFilter = szFilter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn))
        BuildSelectDriverList(hwndDlg, szFile);
}

static VOID
UpdateSelectDriverButtons(HWND hwndDlg)
{
    if (ListView_GetNextItem(GetDlgItem(hwndDlg, IDC_MODELLIST), -1, LVNI_SELECTED) != -1)
        PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
    else
        PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK);
}

static BOOL
PrepareSelectedDriver(HWND hwndDlg)
{
    HWND hModelList = GetDlgItem(hwndDlg, IDC_MODELLIST);
    PSP_DRVINFO_DETAIL_DATA_W DrvInfoDetail = NULL;
    SP_DEVINSTALL_PARAMS_W InstallParams;
    SP_DRVINFO_DATA_W DrvInfoData;
    WCHAR ClassName[MAX_CLASS_NAME_LEN];
    WCHAR HardwareId[MAX_DEVICE_ID_LEN + 2];
    DWORD RequiredSize = 0;
    GUID ClassGuid;
    LV_ITEM Item;
    BOOL Result = FALSE;
    DWORD i;

    FreeInstallDevice();

    ZeroMemory(&Item, sizeof(LV_ITEM));
    Item.mask = LVIF_PARAM;
    Item.iItem = ListView_GetNextItem(hModelList, -1, LVNI_SELECTED);
    if (Item.iItem == -1 || !ListView_GetItem(hModelList, &Item))
        return FALSE;

    DrvInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
    if (!SetupDiEnumDriverInfoW(hSelectDevInfo, NULL, SPDIT_CLASSDRIVER, (DWORD)Item.lParam, &DrvInfoData))
        return FALSE;

    SetupDiGetDriverInfoDetailW(hSelectDevInfo, NULL, &DrvInfoData, NULL, 0, &RequiredSize);
    if (RequiredSize < sizeof(SP_DRVINFO_DETAIL_DATA_W))
        RequiredSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

    DrvInfoDetail = HeapAlloc(hProcessHeap, HEAP_ZERO_MEMORY, RequiredSize);
    if (DrvInfoDetail == NULL)
        return FALSE;

    DrvInfoDetail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
    if (!SetupDiGetDriverInfoDetailW(hSelectDevInfo, NULL, &DrvInfoData, DrvInfoDetail, RequiredSize, NULL))
        goto done;

    if (!SetupDiGetINFClassW(DrvInfoDetail->InfFileName, &ClassGuid, ClassName, ARRAYSIZE(ClassName), NULL))
        goto done;

    hInstallDevInfo = SetupDiCreateDeviceInfoList(&ClassGuid, GetParent(hwndDlg));
    if (hInstallDevInfo == INVALID_HANDLE_VALUE)
        goto done;

    InstallDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (!SetupDiCreateDeviceInfoW(hInstallDevInfo, ClassName, &ClassGuid, NULL, GetParent(hwndDlg), DICD_GENERATE_ID, &InstallDevInfoData))
        goto done;

    if (DrvInfoDetail->HardwareID[0] != UNICODE_NULL)
    {
        ZeroMemory(HardwareId, sizeof(HardwareId));
        lstrcpynW(HardwareId, DrvInfoDetail->HardwareID, MAX_DEVICE_ID_LEN);
        SetupDiSetDeviceRegistryPropertyW(hInstallDevInfo,
                                          &InstallDevInfoData,
                                          SPDRP_HARDWAREID,
                                          (PBYTE)HardwareId,
                                          (DWORD)(wcslen(HardwareId) + 2) * sizeof(WCHAR));
    }

    InstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS_W);
    if (!SetupDiGetDeviceInstallParamsW(hInstallDevInfo, &InstallDevInfoData, &InstallParams))
        goto done;

    InstallParams.Flags |= DI_ENUMSINGLEINF;
    InstallParams.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    lstrcpynW(InstallParams.DriverPath, DrvInfoDetail->InfFileName, ARRAYSIZE(InstallParams.DriverPath));
    if (!SetupDiSetDeviceInstallParamsW(hInstallDevInfo, &InstallDevInfoData, &InstallParams))
        goto done;

    if (!SetupDiBuildDriverInfoList(hInstallDevInfo, &InstallDevInfoData, SPDIT_CLASSDRIVER))
        goto done;

    InstallDrvInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
    for (i = 0; SetupDiEnumDriverInfoW(hInstallDevInfo, &InstallDevInfoData, SPDIT_CLASSDRIVER, i, &InstallDrvInfoData); i++)
    {
        if (_wcsicmp(InstallDrvInfoData.Description, DrvInfoData.Description) == 0 &&
            _wcsicmp(InstallDrvInfoData.MfgName, DrvInfoData.MfgName) == 0 &&
            _wcsicmp(InstallDrvInfoData.ProviderName, DrvInfoData.ProviderName) == 0)
        {
            Result = SetupDiSetSelectedDriverW(hInstallDevInfo, &InstallDevInfoData, &InstallDrvInfoData);
            break;
        }
    }

done:
    HeapFree(hProcessHeap, 0, DrvInfoDetail);
    if (!Result)
        FreeInstallDevice();
    return Result;
}

static VOID
SetPageIcon(HWND hwndDlg, INT nIDDlgItem, CONST GUID *ClassGuid)
{
    HICON hIcon = NULL;

    if (ClassGuid != NULL && !SetupDiLoadClassIcon(ClassGuid, &hIcon, NULL))
        hIcon = NULL;

    hIcon = (HICON)SendDlgItemMessage(hwndDlg, nIDDlgItem, STM_SETICON, (WPARAM)hIcon, 0);
    if (hIcon)
        DestroyIcon(hIcon);
}

static VOID
ShowInstallError(HWND hwndSheet, DWORD Error)
{
    WCHAR szTitle[MAX_STR_SIZE];
    WCHAR szCode[16];
    LPWSTR lpMessage = NULL;

    GetWindowText(hwndSheet, szTitle, ARRAYSIZE(szTitle));
    if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL,
                        Error,
                        0,
                        (LPWSTR)&lpMessage,
                        0,
                        NULL))
    {
        lpMessage = NULL;
        wsprintfW(szCode, L"0x%08lx", Error);
    }

    MessageBoxW(hwndSheet, lpMessage ? lpMessage : szCode, szTitle, MB_OK | MB_ICONERROR);
    LocalFree(lpMessage);
}

static INT_PTR CALLBACK
SelectDriverPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            InitSelectDriverList(GetDlgItem(hwndDlg, IDC_MFGLIST), IDS_MANUFACTURER);
            InitSelectDriverList(GetDlgItem(hwndDlg, IDC_MODELLIST), IDS_MODEL);
            (VOID) ListView_SetImageList(GetDlgItem(hwndDlg, IDC_MODELLIST), ImageListData.ImageList, LVSIL_SMALL);
        }
        break;

        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDC_HAVEDISK && HIWORD(wParam) == BN_CLICKED)
            {
                HaveDisk(hwndDlg);
                UpdateSelectDriverButtons(hwndDlg);
            }
        }
        break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case LVN_ITEMCHANGED:
                {
                    LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;

                    if ((pnmlv->uChanged & LVIF_STATE) &&
                        ((pnmlv->uNewState ^ pnmlv->uOldState) & LVIS_SELECTED))
                    {
                        if (lpnm->idFrom == IDC_MFGLIST && (pnmlv->uNewState & LVIS_SELECTED))
                            FillModelList(hwndDlg);
                        UpdateSelectDriverButtons(hwndDlg);
                    }
                }
                break;

                case PSN_SETACTIVE:
                {
                    if (SelectListStale)
                    {
                        SetPageIcon(hwndDlg, IDC_SELDRVICON, &SelectClassGuid);
                        BuildSelectDriverList(hwndDlg, NULL);
                        SelectListStale = FALSE;
                    }
                    UpdateSelectDriverButtons(hwndDlg);
                }
                break;

                case PSN_WIZBACK:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_HWTYPESPAGE);
                    return TRUE;
                }

                case PSN_WIZNEXT:
                {
                    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                    BOOL Prepared = PrepareSelectedDriver(hwndDlg);
                    DWORD Error = GetLastError();

                    SetCursor(hOldCursor);
                    if (!Prepared)
                    {
                        ShowInstallError(GetParent(hwndDlg), Error);
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }

                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_READYPAGE);
                    return TRUE;
                }
            }
        }
        break;

        case WM_DESTROY:
        {
            SetPageIcon(hwndDlg, IDC_SELDRVICON, NULL);
        }
        break;
    }

    return FALSE;
}

static INT_PTR CALLBACK
ReadyPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    SetDlgItemText(hwndDlg, IDC_READYDEVICE, InstallDrvInfoData.Description);
                    SetPageIcon(hwndDlg, IDC_READYICON, &InstallDevInfoData.ClassGuid);
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
                }
                break;

                case PSN_WIZBACK:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_SELECTDRIVERPAGE);
                    return TRUE;
                }

                case PSN_WIZNEXT:
                {
                    HWND hwndSheet = GetParent(hwndDlg);
                    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                    BOOL Installed;
                    DWORD Error;

                    Installed = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hInstallDevInfo, &InstallDevInfoData) &&
                                DiInstallDevice(hwndSheet, hInstallDevInfo, &InstallDevInfoData, &InstallDrvInfoData, 0, NULL);
                    Error = GetLastError();
                    SetCursor(hOldCursor);

                    if (!Installed)
                    {
                        ShowInstallError(hwndSheet, Error);
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }

                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_FINISHPAGE);
                    return TRUE;
                }
            }
        }
        break;

        case WM_DESTROY:
        {
            SetPageIcon(hwndDlg, IDC_READYICON, NULL);
        }
        break;
    }

    return FALSE;
}

static VOID
FreeDetectedDevices(VOID)
{
    DWORD i;

    for (i = 0; i < DetectedDevInfoCount; i++)
        SetupDiDestroyDeviceInfoList(DetectedDevInfo[i]);

    HeapFree(hProcessHeap, 0, DetectedDevInfo);
    DetectedDevInfo = NULL;
    DetectedDevInfoCount = 0;

    HeapFree(hProcessHeap, 0, DetectClassGuids);
    DetectClassGuids = NULL;
}

static BOOL CALLBACK
DetectProgressNotify(PVOID ProgressNotifyParam, DWORD DetectComplete)
{
    PostMessage((HWND)ProgressNotifyParam, WM_DETECT_PROGRESS, DetectComplete, 0);
    return DetectCancelled != 0;
}

static DWORD WINAPI
DetectHardwareThread(LPVOID lpParameter)
{
    HWND hwndDlg = (HWND)lpParameter;
    SP_DETECTDEVICE_PARAMS DetectParams;
    SP_DEVINFO_DATA DevInfoData;
    HDEVINFO hDevInfo;
    DWORD ClassCount = 0;
    DWORD Index;
    DWORD i;

    SetupDiBuildClassInfoList(DIBCI_NOINSTALLCLASS, NULL, 0, &ClassCount);
    if (ClassCount != 0)
    {
        DetectClassGuids = HeapAlloc(hProcessHeap, 0, ClassCount * sizeof(GUID));
        DetectedDevInfo = HeapAlloc(hProcessHeap, 0, ClassCount * sizeof(HDEVINFO));
    }

    if (DetectClassGuids != NULL && DetectedDevInfo != NULL &&
        SetupDiBuildClassInfoList(DIBCI_NOINSTALLCLASS, DetectClassGuids, ClassCount, &ClassCount))
    {
        PostMessage(hwndDlg, WM_DETECT_START, ClassCount, 0);

        for (i = 0; i < ClassCount && !DetectCancelled; i++)
        {
            PostMessage(hwndDlg, WM_DETECT_CLASS, i, 0);

            hDevInfo = SetupDiCreateDeviceInfoList(&DetectClassGuids[i], NULL);
            if (hDevInfo == INVALID_HANDLE_VALUE)
                continue;

            DetectParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            DetectParams.ClassInstallHeader.InstallFunction = DIF_DETECT;
            DetectParams.DetectProgressNotify = DetectProgressNotify;
            DetectParams.ProgressNotifyParam = hwndDlg;
            if (SetupDiSetClassInstallParams(hDevInfo, NULL, &DetectParams.ClassInstallHeader, sizeof(DetectParams)))
                SetupDiCallClassInstaller(DIF_DETECT, hDevInfo, NULL);

            Index = 0;
            DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
            while (SetupDiEnumDeviceInfo(hDevInfo, Index, &DevInfoData))
            {
                if (SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hDevInfo, &DevInfoData))
                {
                    Index++;
                }
                else if (!SetupDiDeleteDeviceInfo(hDevInfo, &DevInfoData))
                {
                    Index = 0;
                    break;
                }
            }

            if (Index != 0)
                DetectedDevInfo[DetectedDevInfoCount++] = hDevInfo;
            else
                SetupDiDestroyDeviceInfoList(hDevInfo);
        }
    }

    PostMessage(hwndDlg, WM_DETECT_DONE, 0, 0);
    return 0;
}

static INT_PTR CALLBACK
ProgressPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_DETECT_START:
            SendDlgItemMessage(hwndDlg, IDC_TPROGRESSBAR, PBM_SETRANGE32, 0, wParam);
            break;

        case WM_DETECT_CLASS:
        {
            WCHAR szClassDesc[MAX_STR_SIZE];

            if (!SetupDiGetClassDescription(&DetectClassGuids[wParam], szClassDesc, ARRAYSIZE(szClassDesc), NULL))
                szClassDesc[0] = UNICODE_NULL;

            SendDlgItemMessage(hwndDlg, IDC_TPROGRESSBAR, PBM_SETPOS, wParam, 0);
            SendDlgItemMessage(hwndDlg, IDC_PROGRESSBAR, PBM_SETPOS, 0, 0);
            SetDlgItemText(hwndDlg, IDC_PROGRESSTEXT, szClassDesc);
            break;
        }

        case WM_DETECT_PROGRESS:
            SendDlgItemMessage(hwndDlg, IDC_PROGRESSBAR, PBM_SETPOS, wParam, 0);
            break;

        case WM_DETECT_DONE:
        {
            HWND hwndSheet = GetParent(hwndDlg);
            WCHAR szTitle[MAX_STR_SIZE];

            if (hDetectThread)
            {
                WaitForSingleObject(hDetectThread, INFINITE);
                CloseHandle(hDetectThread);
                hDetectThread = NULL;
            }

            LoadString(hApplet,
                       DetectedDevInfoCount != 0 ? IDS_FOUNDTITLE : IDS_NOTFOUNDTITLE,
                       szTitle,
                       ARRAYSIZE(szTitle));
            SendMessage(hwndSheet,
                        PSM_SETHEADERTITLE,
                        SendMessage(hwndSheet, PSM_IDTOINDEX, 0, IDD_DETECTEDHWPAGE),
                        (LPARAM)szTitle);
            PropSheet_SetCurSelByID(hwndSheet, IDD_DETECTEDHWPAGE);
            break;
        }

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    PropSheet_SetWizButtons(GetParent(hwndDlg), 0);
                    SendDlgItemMessage(hwndDlg, IDC_TPROGRESSBAR, PBM_SETPOS, 0, 0);
                    SendDlgItemMessage(hwndDlg, IDC_PROGRESSBAR, PBM_SETPOS, 0, 0);
                    SetDlgItemText(hwndDlg, IDC_PROGRESSTEXT, L"");

                    FreeDetectedDevices();
                    DetectCancelled = 0;
                    hDetectThread = CreateThread(NULL, 0, DetectHardwareThread, hwndDlg, 0, NULL);
                    if (!hDetectThread)
                        PostMessage(hwndDlg, WM_DETECT_DONE, 0, 0);
                }
                break;

                case PSN_KILLACTIVE:
                {
                    if (hDetectThread)
                    {
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE);
                        return TRUE;
                    }
                }
                break;

                case PSN_QUERYCANCEL:
                {
                    InterlockedExchange(&DetectCancelled, 1);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

static VOID
InstallDetectedDevices(HWND hwndParent)
{
    SP_DEVINFO_DATA DevInfoData;
    SP_DRVINFO_DATA_W DrvInfoData;
    HCURSOR hOldCursor;
    DWORD Index;
    DWORD i;

    hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    for (i = 0; i < DetectedDevInfoCount; i++)
    {
        DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        for (Index = 0; SetupDiEnumDeviceInfo(DetectedDevInfo[i], Index, &DevInfoData); Index++)
        {
            DrvInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
            if (SetupDiGetSelectedDriverW(DetectedDevInfo[i], &DevInfoData, &DrvInfoData))
                DiInstallDevice(hwndParent, DetectedDevInfo[i], &DevInfoData, &DrvInfoData, 0, NULL);
            else
                DiInstallDevice(hwndParent, DetectedDevInfo[i], &DevInfoData, NULL, 0, NULL);
        }
    }

    SetCursor(hOldCursor);
}

static VOID
FillDetectedHardwareList(HWND hwndDlg)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_DETECTEDHWLIST);
    WCHAR szBuffer[MAX_STR_SIZE];
    SP_DEVINFO_DATA DevInfoData;
    LV_ITEM Item;
    DWORD Index;
    DWORD i;

    (VOID) ListView_DeleteAllItems(hList);

    ZeroMemory(&Item, sizeof(LV_ITEM));
    Item.mask = LVIF_TEXT | LVIF_IMAGE;
    Item.pszText = szBuffer;

    for (i = 0; i < DetectedDevInfoCount; i++)
    {
        DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        for (Index = 0; SetupDiEnumDeviceInfo(DetectedDevInfo[i], Index, &DevInfoData); Index++)
        {
            if (!SetupDiGetDeviceRegistryProperty(DetectedDevInfo[i],
                                                  &DevInfoData,
                                                  SPDRP_FRIENDLYNAME,
                                                  NULL,
                                                  (PBYTE)szBuffer,
                                                  sizeof(szBuffer),
                                                  NULL) &&
                !SetupDiGetDeviceRegistryProperty(DetectedDevInfo[i],
                                                  &DevInfoData,
                                                  SPDRP_DEVICEDESC,
                                                  NULL,
                                                  (PBYTE)szBuffer,
                                                  sizeof(szBuffer),
                                                  NULL))
            {
                szBuffer[0] = UNICODE_NULL;
            }

            if (!SetupDiGetClassImageIndex(&ImageListData, &DevInfoData.ClassGuid, &Item.iImage))
                Item.iImage = -1;

            Item.iItem = ListView_GetItemCount(hList);
            (VOID) ListView_InsertItem(hList, &Item);
        }
    }
}

static INT_PTR CALLBACK
DetectedHwPageDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            HWND hList = GetDlgItem(hwndDlg, IDC_DETECTEDHWLIST);
            LV_COLUMN Column;
            RECT Rect;

            GetClientRect(hList, &Rect);
            ZeroMemory(&Column, sizeof(LV_COLUMN));
            Column.mask = LVCF_FMT | LVCF_WIDTH;
            Column.fmt = LVCFMT_LEFT;
            Column.cx = Rect.right - GetSystemMetrics(SM_CXVSCROLL);
            (VOID) ListView_InsertColumn(hList, 0, &Column);
            (VOID) ListView_SetImageList(hList, ImageListData.ImageList, LVSIL_SMALL);
        }
        break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                {
                    WCHAR szText[MAX_STR_SIZE];
                    INT nShow = (DetectedDevInfoCount != 0) ? SW_SHOW : SW_HIDE;

                    if (DetectedDevInfoCount != 0)
                        szText[0] = UNICODE_NULL;
                    else
                        LoadString(hApplet, IDS_NOTFOUNDTEXT, szText, ARRAYSIZE(szText));
                    SetDlgItemText(hwndDlg, IDC_STATUSTEXT, szText);

                    ShowWindow(GetDlgItem(hwndDlg, IDC_DETECTEDHWTEXT), nShow);
                    ShowWindow(GetDlgItem(hwndDlg, IDC_DETECTEDHWLIST), nShow);
                    FillDetectedHardwareList(hwndDlg);

                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
                }
                break;

                case PSN_WIZBACK:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_SELECTWAYPAGE);
                    return TRUE;
                }

                case PSN_WIZNEXT:
                {
                    if (DetectedDevInfoCount == 0)
                    {
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_HWTYPESPAGE);
                        return TRUE;
                    }

                    InstallDetectedDevices(GetParent(hwndDlg));
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_FINISHPAGE);
                    return TRUE;
                }
            }
        }
        break;
    }

    return FALSE;
}

static int CALLBACK
PropSheetProc(HWND hwndDlg, UINT uMsg, LPARAM lParam)
{
    // NOTE: This callback is needed to set large icon correctly.
    HICON hIcon;
    switch (uMsg)
    {
        case PSCB_INITIALIZED:
        {
            hIcon = LoadIconW(hApplet, MAKEINTRESOURCEW(IDI_CPLICON));
            SendMessageW(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            break;
        }
    }
    return 0;
}

static VOID
HardwareWizardInit(HWND hwnd)
{
    HPROPSHEETPAGE ahpsp[10];
    PROPSHEETPAGE psp = {0};
    PROPSHEETHEADER psh;
    UINT nPages = 0;

    /* Create the Start page, until setup is working */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_HIDEHEADER;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = StartPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_STARTPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create select search way page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_SELECTWAYTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = SelectWayPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_SELECTWAYPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create hardware types page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_HDTYPESTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = HdTypesPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_HWTYPESPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create progress page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_SEARCHTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = ProgressPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_PROGRESSPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_NOTFOUNDTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = DetectedHwPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_DETECTEDHWPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_SELECTDRIVERTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = SelectDriverPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_SELECTDRIVERPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_READYTITLE);
    psp.pszHeaderSubTitle = NULL;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = ReadyPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_READYPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create finish page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_HIDEHEADER;
    psp.hInstance = hApplet;
    psp.lParam = 0;
    psp.pfnDlgProc = FinishPageDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_FINISHPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create the property sheet */
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_WIZARD97 | PSH_WATERMARK | PSH_HEADER | PSH_USEICONID | PSH_USECALLBACK;
    psh.hInstance = hApplet;
    psh.pszIcon = MAKEINTRESOURCEW(IDI_CPLICON);
    psh.hwndParent = hwnd;
    psh.nPages = nPages;
    psh.nStartPage = 0;
    psh.phpage = ahpsp;
    psh.pszbmWatermark = MAKEINTRESOURCE(IDB_WATERMARK);
    psh.pszbmHeader = MAKEINTRESOURCE(IDB_HEADER);
    psh.pfnCallback = PropSheetProc;

    /* Create title font */
    hTitleFont = CreateTitleFont();

    ImageListData.cbSize = sizeof(ImageListData);
    SetupDiGetClassImageList(&ImageListData);

    /* Display the wizard */
    PropertySheet(&psh);

    if (hDetectThread)
    {
        InterlockedExchange(&DetectCancelled, 1);
        WaitForSingleObject(hDetectThread, INFINITE);
        CloseHandle(hDetectThread);
        hDetectThread = NULL;
    }
    FreeDetectedDevices();
    FreeInstallDevice();
    if (hSelectDevInfo != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(hSelectDevInfo);
        hSelectDevInfo = INVALID_HANDLE_VALUE;
    }
    HeapFree(hProcessHeap, 0, HwTypes);
    HwTypes = NULL;
    SetupDiDestroyClassImageList(&ImageListData);

    DeleteObject(hTitleFont);
}

/* FUNCTIONS ****************************************************************/

BOOL WINAPI
InstallNewDevice(HWND hwndParent, LPGUID ClassGuid, PDWORD pReboot)
{
    return FALSE;
}

VOID WINAPI
AddHardwareWizard(HWND hwnd, LPWSTR lpName)
{
    if (lpName != NULL)
    {
        DPRINT1("No support of remote installation yet!\n");
        return;
    }

    HardwareWizardInit(hwnd);
}

/* Control Panel Callback */
LONG CALLBACK
CPlApplet(HWND hwndCpl, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
    switch (uMsg)
    {
        case CPL_INIT:
            return TRUE;

        case CPL_GETCOUNT:
            return 1;

        case CPL_INQUIRE:
            {
                CPLINFO *CPlInfo = (CPLINFO*)lParam2;
                CPlInfo->lData = 0;
                CPlInfo->idIcon = IDI_CPLICON;
                CPlInfo->idName = IDS_CPLNAME;
                CPlInfo->idInfo = IDS_CPLDESCRIPTION;
            }
            break;

        case CPL_DBLCLK:
            AddHardwareWizard(hwndCpl, NULL);
            break;
    }

    return FALSE;
}

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(lpvReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            hApplet = hinstDLL;
            hProcessHeap = GetProcessHeap();
            DisableThreadLibraryCalls(hinstDLL);
            break;
    }

    return TRUE;
}
