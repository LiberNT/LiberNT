/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Account and time zone page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <tzlib.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

typedef struct _TIMEZONE_ITEM
{
    ULONG Index;
    LONG Bias;
    BOOL HasDaylight;
    WCHAR Description[128];
} TIMEZONE_ITEM, *PTIMEZONE_ITEM;

typedef struct _TIMEZONE_LIST
{
    PTIMEZONE_ITEM Items;
    ULONG Count;
} TIMEZONE_LIST, *PTIMEZONE_LIST;

static TIMEZONE_LIST TimeZones;

VOID
GenerateComputerName(
    _Out_writes_(MAX_COMPUTERNAME_LENGTH + 1) PWSTR Buffer)
{
    static const WCHAR Chars[] = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    UINT i;

    srand(GetTickCount());
    StringCchCopyW(Buffer, MAX_COMPUTERNAME_LENGTH + 1, L"LIBERNT-");
    for (i = (UINT)wcslen(Buffer); i < MAX_COMPUTERNAME_LENGTH; i++)
        Buffer[i] = Chars[rand() % (ARRAYSIZE(Chars) - 1)];
    Buffer[MAX_COMPUTERNAME_LENGTH] = UNICODE_NULL;
}

BOOL
IsValidSetupComputerName(
    _In_ PCWSTR Name)
{
    SIZE_T Length = wcslen(Name);
    BOOL OnlyDigits = TRUE;
    PCWSTR p;

    if (Length == 0 || Length > MAX_COMPUTERNAME_LENGTH)
        return FALSE;

    for (p = Name; *p; p++)
    {
        if (!((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z') ||
              (*p >= L'0' && *p <= L'9') || *p == L'-'))
        {
            return FALSE;
        }
        if (*p < L'0' || *p > L'9')
            OnlyDigits = FALSE;
    }

    return !OnlyDigits;
}

BOOL
IsValidSetupUserName(
    _In_ PCWSTR Name)
{
    SIZE_T Length = wcslen(Name);
    BOOL OnlyDotsAndSpaces = TRUE;
    PCWSTR p;

    if (Length == 0 || Length > 20)
        return FALSE;

    for (p = Name; *p; p++)
    {
        if (*p < L' ' || wcschr(L"\"/\\[]:;|=,+*?<>@", *p))
            return FALSE;
        if (*p != L'.' && *p != L' ')
            OnlyDotsAndSpaces = FALSE;
    }

    return !OnlyDotsAndSpaces;
}

static BOOL
IsExistingAccountName(
    _In_ PCWSTR Name)
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    DWORD cbSid = sizeof(Sid);
    WCHAR Domain[MAX_PATH];
    DWORD cchDomain = ARRAYSIZE(Domain);
    SID_NAME_USE Use;

    return LookupAccountNameW(NULL, Name, Sid, &cbSid, Domain, &cchDomain, &Use);
}

static LONG
AddTimeZoneItem(
    IN HKEY hZoneKey,
    IN PVOID Context)
{
    PTIMEZONE_LIST List = (PTIMEZONE_LIST)Context;
    PTIMEZONE_ITEM Items;
    TIMEZONE_ITEM Item;
    REG_TZI_FORMAT TimeZoneInfo;
    ULONG DescriptionSize = sizeof(Item.Description);
    LONG Error;

    ZeroMemory(&Item, sizeof(Item));
    Error = QueryTimeZoneData(hZoneKey,
                              &Item.Index,
                              &TimeZoneInfo,
                              Item.Description,
                              &DescriptionSize,
                              NULL,
                              NULL,
                              NULL,
                              NULL);
    if (Error != ERROR_SUCCESS)
        return ERROR_SUCCESS;

    Item.Bias = TimeZoneInfo.Bias;
    Item.HasDaylight = (TimeZoneInfo.StandardDate.wMonth != 0 &&
                        TimeZoneInfo.DaylightDate.wMonth != 0);

    if (List->Items)
        Items = HeapReAlloc(ProcessHeap, 0, List->Items, (List->Count + 1) * sizeof(Item));
    else
        Items = HeapAlloc(ProcessHeap, 0, sizeof(Item));
    if (!Items)
        return ERROR_NOT_ENOUGH_MEMORY;

    Items[List->Count++] = Item;
    List->Items = Items;
    return ERROR_SUCCESS;
}

static int __cdecl
CompareTimeZoneItems(
    const void* Item1,
    const void* Item2)
{
    ULONG Index1 = ((const TIMEZONE_ITEM*)Item1)->Index;
    ULONG Index2 = ((const TIMEZONE_ITEM*)Item2)->Index;

    return (Index1 < Index2) ? -1 : (Index1 > Index2);
}

static INT
FindDefaultTimeZone(
    _In_ ULONG TimeZoneIndex)
{
    TIME_ZONE_INFORMATION Current;
    ULONG Index;
    INT Match = -1;

    for (Index = 0; Index < TimeZones.Count; Index++)
    {
        if (TimeZones.Items[Index].Index == TimeZoneIndex)
            return (INT)Index;
    }

    if (GetTimeZoneInformation(&Current) == TIME_ZONE_ID_INVALID)
        return 0;

    for (Index = 0; Index < TimeZones.Count; Index++)
    {
        if (TimeZones.Items[Index].Bias != Current.Bias)
            continue;
        if (!TimeZones.Items[Index].HasDaylight)
            return (INT)Index;
        if (Match < 0)
            Match = (INT)Index;
    }

    return (Match < 0) ? 0 : Match;
}

static VOID
UpdateAutoDaylight(
    _In_ HWND hwndDlg,
    _In_ BOOL Check)
{
    INT iItem = (INT)SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_GETCURSEL, 0, 0);
    BOOL HasDaylight = (iItem >= 0 && (ULONG)iItem < TimeZones.Count && TimeZones.Items[iItem].HasDaylight);

    EnableDlgItem(hwndDlg, IDC_AUTODAYLIGHT, HasDaylight);
    CheckDlgButton(hwndDlg, IDC_AUTODAYLIGHT, (HasDaylight && Check) ? BST_CHECKED : BST_UNCHECKED);
}

static BOOL
ApplyAccountSettings(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 2];
    WCHAR UserName[UNLEN + 1];
    WCHAR Password1[128];
    WCHAR Password2[128];
    UINT ErrorId = 0, FocusId = 0;
    INT iItem;

    GetDlgItemTextW(hwndDlg, IDC_COMPUTERNAME, ComputerName, ARRAYSIZE(ComputerName));
    GetDlgItemTextW(hwndDlg, IDC_USERNAME, UserName, ARRAYSIZE(UserName));
    GetDlgItemTextW(hwndDlg, IDC_PASSWORD1, Password1, ARRAYSIZE(Password1));
    GetDlgItemTextW(hwndDlg, IDC_PASSWORD2, Password2, ARRAYSIZE(Password2));

    if (!IsValidSetupComputerName(ComputerName))
    {
        ErrorId = IDS_ERROR_COMPUTERNAME;
        FocusId = IDC_COMPUTERNAME;
    }
    else if (!IsValidSetupUserName(UserName))
    {
        ErrorId = IDS_ERROR_USERNAME;
        FocusId = IDC_USERNAME;
    }
    else if (_wcsicmp(UserName, ComputerName) == 0 || IsExistingAccountName(UserName))
    {
        ErrorId = IDS_ERROR_USEREXISTS;
        FocusId = IDC_USERNAME;
    }
    else if (wcscmp(Password1, Password2) != 0)
    {
        ErrorId = IDS_ERROR_PASSWORDMATCH;
        FocusId = IDC_PASSWORD1;
    }

    if (ErrorId == 0)
    {
        CharUpperW(ComputerName);
        StringCchCopyW(pSetupData->ComputerName, ARRAYSIZE(pSetupData->ComputerName), ComputerName);
        StringCchCopyW(pSetupData->UserName, ARRAYSIZE(pSetupData->UserName), UserName);
        StringCchCopyW(pSetupData->Password, ARRAYSIZE(pSetupData->Password), Password1);

        iItem = (INT)SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_GETCURSEL, 0, 0);
        pSetupData->TimeZoneName[0] = UNICODE_NULL;
        if (iItem >= 0 && (ULONG)iItem < TimeZones.Count)
        {
            pSetupData->TimeZoneIndex = TimeZones.Items[iItem].Index;
            StringCchCopyW(pSetupData->TimeZoneName, ARRAYSIZE(pSetupData->TimeZoneName), TimeZones.Items[iItem].Description);
        }
        pSetupData->AutoDaylight = (IsDlgButtonChecked(hwndDlg, IDC_AUTODAYLIGHT) == BST_CHECKED);
    }

    SecureZeroMemory(Password1, sizeof(Password1));
    SecureZeroMemory(Password2, sizeof(Password2));

    if (ErrorId != 0)
    {
        SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK, NULL, MAKEINTRESOURCEW(ErrorId));
        SetFocus(GetDlgItem(hwndDlg, FocusId));
        return FALSE;
    }

    return TRUE;
}

INT_PTR
CALLBACK
AccountPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            ULONG Index;
            INT iItem = 0;

            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);

            SendDlgItemMessageW(hwndDlg, IDC_COMPUTERNAME, EM_LIMITTEXT, MAX_COMPUTERNAME_LENGTH, 0);
            SendDlgItemMessageW(hwndDlg, IDC_USERNAME, EM_LIMITTEXT, 20, 0);
            SendDlgItemMessageW(hwndDlg, IDC_PASSWORD1, EM_LIMITTEXT, 127, 0);
            SendDlgItemMessageW(hwndDlg, IDC_PASSWORD2, EM_LIMITTEXT, 127, 0);
            SetDlgItemTextW(hwndDlg, IDC_COMPUTERNAME, pSetupData->ComputerName);
            SetDlgItemTextW(hwndDlg, IDC_USERNAME, pSetupData->UserName);

            EnumerateTimeZoneList(AddTimeZoneItem, &TimeZones);
            if (TimeZones.Count)
                qsort(TimeZones.Items, TimeZones.Count, sizeof(*TimeZones.Items), CompareTimeZoneItems);
            for (Index = 0; Index < TimeZones.Count; Index++)
                SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_ADDSTRING, 0, (LPARAM)TimeZones.Items[Index].Description);
            iItem = FindDefaultTimeZone(pSetupData->TimeZoneIndex);
            SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_SETCURSEL, iItem, 0);
            UpdateAutoDaylight(hwndDlg, pSetupData->AutoDaylight);
            if ((ULONG)iItem < TimeZones.Count)
                StringCchCopyW(pSetupData->TimeZoneName, ARRAYSIZE(pSetupData->TimeZoneName), TimeZones.Items[iItem].Description);
            return FALSE;
        }

        case WM_DESTROY:
            if (TimeZones.Items)
                HeapFree(ProcessHeap, 0, TimeZones.Items);
            TimeZones.Items = NULL;
            TimeZones.Count = 0;
            break;

        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_TIMEZONELIST)
                UpdateAutoDaylight(hwndDlg, TRUE);
            break;

        case SPN_SETACTIVE:
            SetFocus(GetDlgItem(hwndDlg, IDC_USERNAME));
            return TRUE;

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, ApplyAccountSettings(hwndDlg, pSetupData) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}
