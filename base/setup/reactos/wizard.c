/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Setup window with the account, summary and finish pages
 * COPYRIGHT:   Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "reactos.h"
#include <tzlib.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define RESTART_STEPS   300
#define RESTART_TIMER   1

typedef struct _SETUP_PAGE_INFO
{
    UINT Template;
    DLGPROC Proc;
    UINT Title;
    UINT Subtitle;
} SETUP_PAGE_INFO;

typedef struct _SETUP_FRAME
{
    PSETUPDATA SetupData;
    HWND hwnd;
    HWND Pages[SETUP_PAGE_COUNT];
    INT Current;
    HBITMAP hWatermark;
    SIZE Watermark;
} SETUP_FRAME;

typedef struct _TIMEZONE_ITEM
{
    ULONG Index;
    BOOL HasDaylight;
    WCHAR Description[128];
} TIMEZONE_ITEM, *PTIMEZONE_ITEM;

typedef struct _TIMEZONE_LIST
{
    PTIMEZONE_ITEM Items;
    ULONG Count;
} TIMEZONE_LIST, *PTIMEZONE_LIST;

static INT_PTR CALLBACK AccountPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK ReadyPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK DonePageProc(HWND, UINT, WPARAM, LPARAM);

static const SETUP_PAGE_INFO PageInfo[SETUP_PAGE_COUNT] =
{
    { IDD_DISKPAGE,    DiskPageProc,    IDS_DRIVETITLE,   IDS_DISKSUBTITLE },
    { IDD_ACCOUNTPAGE, AccountPageProc, IDS_ACCOUNTTITLE, IDS_ACCOUNTZONESUBTITLE },
    { IDD_READYPAGE,   ReadyPageProc,   IDS_READYTITLE,   IDS_READYSUBTITLE },
    { IDD_PROCESSPAGE, InstallPageProc, IDS_PROCESSTITLE, IDS_PROCESSSUBTITLE },
    { IDD_FINISHPAGE,  DonePageProc,    IDS_DONETITLE,    0 },
};

static SETUP_FRAME Frame;
static TIMEZONE_LIST TimeZones;
static WCHAR TimeZoneName[128];

static VOID
SetResText(
    _In_ HWND hWnd,
    _In_ UINT uID)
{
    WCHAR Text[512];

    Text[0] = UNICODE_NULL;
    if (uID)
        LoadStringW(Frame.SetupData->hInstance, uID, Text, ARRAYSIZE(Text));
    SetWindowTextW(hWnd, Text);
}

VOID
EnableSetupCancel(
    _In_ HWND hFrame,
    _In_ BOOL Enable)
{
    EnableDlgItem(hFrame, IDCANCEL, Enable);
    EnableMenuItem(GetSystemMenu(hFrame, FALSE),
                   SC_CLOSE,
                   MF_BYCOMMAND | (Enable ? MF_ENABLED : MF_GRAYED));
}

VOID
SetupFrameSetButtons(
    _In_ HWND hPage,
    _In_ BOOL Back,
    _In_ BOOL Next)
{
    HWND hFrame = GetParent(hPage);

    EnableDlgItem(hFrame, IDC_FRAME_BACK, Back);
    EnableDlgItem(hFrame, IDC_FRAME_NEXT, Next);
}

VOID
SetupFrameShowButtons(
    _In_ HWND hPage,
    _In_ BOOL Back,
    _In_ BOOL Next,
    _In_ BOOL Cancel)
{
    HWND hFrame = GetParent(hPage);

    ShowDlgItem(hFrame, IDC_FRAME_BACK, Back ? SW_SHOW : SW_HIDE);
    ShowDlgItem(hFrame, IDC_FRAME_NEXT, Next ? SW_SHOW : SW_HIDE);
    ShowDlgItem(hFrame, IDCANCEL, Cancel ? SW_SHOW : SW_HIDE);
}

VOID
SetupFrameSetButtonText(
    _In_ HWND hPage,
    _In_ INT ButtonId,
    _In_ UINT uID)
{
    SetResText(GetDlgItem(GetParent(hPage), ButtonId), uID);
}

VOID
SetupFrameSetTitle(
    _In_ HWND hPage,
    _In_ UINT Title,
    _In_ UINT Subtitle)
{
    HWND hFrame = GetParent(hPage);

    SetResText(GetDlgItem(hFrame, IDC_FRAME_TITLE), Title);
    SetResText(GetDlgItem(hFrame, IDC_FRAME_SUBTITLE), Subtitle);
}

VOID
SetupFramePressNext(
    _In_ HWND hPage)
{
    HWND hFrame = GetParent(hPage);

    PostMessageW(hFrame, WM_COMMAND, MAKEWPARAM(IDC_FRAME_NEXT, BN_CLICKED),
                 (LPARAM)GetDlgItem(hFrame, IDC_FRAME_NEXT));
}

static VOID
FrameShowPage(
    _In_ INT Page)
{
    HWND hPage = Frame.Pages[Page];
    BOOL Back = (Page == SETUP_PAGE_ACCOUNT || Page == SETUP_PAGE_READY);

    if (Frame.Current >= 0)
    {
        SendMessageW(Frame.Pages[Frame.Current], SPN_KILLACTIVE, 0, 0);
        ShowWindow(Frame.Pages[Frame.Current], SW_HIDE);
    }

    Frame.Current = Page;
    SetupFrameSetTitle(hPage, PageInfo[Page].Title, PageInfo[Page].Subtitle);
    SetupFrameSetButtonText(hPage, IDC_FRAME_BACK, IDS_BACKBTN);
    SetupFrameSetButtonText(hPage, IDC_FRAME_NEXT, IDS_NEXTBTN);
    SetupFrameSetButtonText(hPage, IDCANCEL, IDS_CANCELBTN);
    SetupFrameShowButtons(hPage, Back, TRUE, TRUE);
    SetupFrameSetButtons(hPage, Back, TRUE);
    EnableSetupCancel(Frame.hwnd, TRUE);

    ShowWindow(hPage, SW_SHOW);
    SendMessageW(hPage, SPN_SETACTIVE, 0, 0);
}

VOID
SetupFrameGoTo(
    _In_ HWND hPage,
    _In_ SETUP_PAGE Page)
{
    UNREFERENCED_PARAMETER(hPage);
    FrameShowPage(Page);
}

static VOID
FrameNext(VOID)
{
    HWND hNext = GetDlgItem(Frame.hwnd, IDC_FRAME_NEXT);
    INT Current = Frame.Current;
    INT Page;

    if (!IsWindowVisible(hNext) || !IsWindowEnabled(hNext))
        return;

    if (SendMessageW(Frame.Pages[Current], SPN_WIZNEXT, 0, 0) == -1 || Frame.Current != Current)
        return;

    Page = Current + 1;
    if (Current == SETUP_PAGE_DISK && Frame.SetupData->bUnattend)
        Page = SETUP_PAGE_INSTALL;
    if (Page < SETUP_PAGE_COUNT)
        FrameShowPage(Page);
}

static VOID
FrameBack(VOID)
{
    HWND hBack = GetDlgItem(Frame.hwnd, IDC_FRAME_BACK);
    INT Current = Frame.Current;

    if (!IsWindowVisible(hBack) || !IsWindowEnabled(hBack))
        return;

    if (SendMessageW(Frame.Pages[Current], SPN_WIZBACK, 0, 0) == -1 || Frame.Current != Current)
        return;

    if (Current > 0)
        FrameShowPage(Current - 1);
}

static VOID
FrameCancel(VOID)
{
    if (!IsWindowEnabled(GetDlgItem(Frame.hwnd, IDCANCEL)))
        return;

    if (SendMessageW(Frame.Pages[Frame.Current], SPN_QUERYCANCEL, 0, 0))
        return;

    if (DisplayMessage(Frame.hwnd, MB_YESNO | MB_ICONQUESTION,
                       MAKEINTRESOURCEW(IDS_ABORTSETUP2),
                       MAKEINTRESOURCEW(IDS_ABORTSETUP)) == IDYES)
    {
        EndDialog(Frame.hwnd, IDCANCEL);
    }
}

static VOID
FramePaintColumn(
    _In_ HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hdc = BeginPaint(hwnd, &ps);

    GetClientRect(hwnd, &rc);
    rc.right = Frame.Watermark.cx;
    FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));

    if (Frame.hWatermark)
    {
        HDC hdcMem = CreateCompatibleDC(hdc);

        if (hdcMem)
        {
            HGDIOBJ hOld = SelectObject(hdcMem, Frame.hWatermark);

            BitBlt(hdc, 0, 0, Frame.Watermark.cx, min(Frame.Watermark.cy, rc.bottom), hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOld);
            DeleteDC(hdcMem);
        }
    }

    EndPaint(hwnd, &ps);
}

static VOID
FrameCenter(
    _In_ HWND hwnd)
{
    RECT rcWork, rcWindow;

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    GetWindowRect(hwnd, &rcWindow);
    SetWindowPos(hwnd, HWND_TOP,
                 rcWork.left + ((rcWork.right - rcWork.left) - (rcWindow.right - rcWindow.left)) / 2,
                 rcWork.top + ((rcWork.bottom - rcWork.top) - (rcWindow.bottom - rcWindow.top)) / 2,
                 0, 0, SWP_NOSIZE);
}

static INT_PTR CALLBACK
SetupFrameProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            PSETUPDATA pSetupData = (PSETUPDATA)lParam;
            MARGINS Margins = {0, 0, 0, 0};
            HICON hIcon;
            BITMAP bm;
            RECT rc;
            INT Page;

            Frame.SetupData = pSetupData;
            Frame.hwnd = hwnd;
            Frame.Current = -1;

            hIcon = LoadIconW(pSetupData->hInstance, MAKEINTRESOURCEW(IDI_MAIN));
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SetDlgItemFont(hwnd, IDC_FRAME_TITLE, pSetupData->hTitleFont, FALSE);

            Frame.hWatermark = LoadImageW(pSetupData->hInstance, MAKEINTRESOURCEW(IDB_WATERMARK),
                                          IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (Frame.hWatermark && GetObjectW(Frame.hWatermark, sizeof(bm), &bm))
            {
                Frame.Watermark.cx = bm.bmWidth;
                Frame.Watermark.cy = bm.bmHeight;
            }
            Margins.cxLeftWidth = Frame.Watermark.cx;
            DwmExtendFrameIntoClientArea(hwnd, &Margins);

            GetWindowRect(GetDlgItem(hwnd, IDC_FRAME_PAGE), &rc);
            MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rc, 2);
            for (Page = 0; Page < SETUP_PAGE_COUNT; Page++)
            {
                Frame.Pages[Page] = CreateDialogParamW(pSetupData->hInstance,
                                                       MAKEINTRESOURCEW(PageInfo[Page].Template),
                                                       hwnd,
                                                       PageInfo[Page].Proc,
                                                       (LPARAM)pSetupData);
                if (!Frame.Pages[Page])
                {
                    EndDialog(hwnd, IDABORT);
                    return FALSE;
                }
                SetWindowPos(Frame.Pages[Page], HWND_TOP, rc.left, rc.top,
                             rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
            }

            FrameCenter(hwnd);
            FrameShowPage(SETUP_PAGE_DISK);
            return FALSE;
        }

        case WM_PAINT:
            FramePaintColumn(hwnd);
            return TRUE;

        case WM_ACTIVATE:
            if (Frame.Current >= 0)
                SendMessageW(Frame.Pages[Frame.Current], WM_ACTIVATE, wParam, lParam);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                case IDC_FRAME_NEXT:
                    FrameNext();
                    return TRUE;
                case IDC_FRAME_BACK:
                    FrameBack();
                    return TRUE;
                case IDCANCEL:
                    FrameCancel();
                    return TRUE;
            }
            break;

        case WM_DESTROY:
            if (Frame.hWatermark)
                DeleteObject(Frame.hWatermark);
            Frame.hWatermark = NULL;
            break;
    }

    return FALSE;
}

INT_PTR
RunSetupWizard(
    _In_ PSETUPDATA pSetupData)
{
    return DialogBoxParamW(pSetupData->hInstance, MAKEINTRESOURCEW(IDD_SETUPFRAME),
                           NULL, SetupFrameProc, (LPARAM)pSetupData);
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
        TimeZoneName[0] = UNICODE_NULL;
        if (iItem >= 0 && (ULONG)iItem < TimeZones.Count)
        {
            pSetupData->TimeZoneIndex = TimeZones.Items[iItem].Index;
            StringCchCopyW(TimeZoneName, ARRAYSIZE(TimeZoneName), TimeZones.Items[iItem].Description);
        }
        pSetupData->AutoDaylight = (IsDlgButtonChecked(hwndDlg, IDC_AUTODAYLIGHT) == BST_CHECKED);
    }

    SecureZeroMemory(Password1, sizeof(Password1));
    SecureZeroMemory(Password2, sizeof(Password2));

    if (ErrorId != 0)
    {
        DisplayError(GetParent(hwndDlg), 0, ErrorId);
        SetFocus(GetDlgItem(hwndDlg, FocusId));
        return FALSE;
    }

    return TRUE;
}

static INT_PTR CALLBACK
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
            {
                SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_ADDSTRING, 0, (LPARAM)TimeZones.Items[Index].Description);
                if (TimeZones.Items[Index].Index == pSetupData->TimeZoneIndex)
                    iItem = (INT)Index;
            }
            SendDlgItemMessageW(hwndDlg, IDC_TIMEZONELIST, CB_SETCURSEL, iItem, 0);
            UpdateAutoDaylight(hwndDlg, pSetupData->AutoDaylight);
            if ((ULONG)iItem < TimeZones.Count)
                StringCchCopyW(TimeZoneName, ARRAYSIZE(TimeZoneName), TimeZones.Items[iItem].Description);
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

static VOID
AppendLine(
    _Inout_updates_(cchText) PWSTR Text,
    _In_ SIZE_T cchText,
    _In_ UINT uID,
    ...)
{
    WCHAR Format[256];
    WCHAR Line[512];
    va_list Args;

    if (!LoadStringW(Frame.SetupData->hInstance, uID, Format, ARRAYSIZE(Format)))
        return;

    va_start(Args, uID);
    StringCchVPrintfW(Line, ARRAYSIZE(Line), Format, Args);
    va_end(Args);

    StringCchCatW(Text, cchText, Line);
    StringCchCatW(Text, cchText, L"\r\n");
}

static VOID
FillSummary(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    WCHAR Text[4096];
    WCHAR Changes[2048];
    WCHAR Target[96];
    WCHAR Size[32];

    Text[0] = UNICODE_NULL;
    if (InstallPartition)
    {
        DescribeDiskRegion(InstallPartition, Target, ARRAYSIZE(Target));
        FormatDiskSize(GetPartEntrySizeInBytes(InstallPartition), Size, ARRAYSIZE(Size));
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_TARGET, Target, Size);
    }

    DescribeDiskChanges(Changes, ARRAYSIZE(Changes));
    if (Changes[0])
        StringCchCatW(Text, ARRAYSIZE(Text), Changes);
    else
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_NOCHANGES);

    StringCchCatW(Text, ARRAYSIZE(Text), L"\r\n");
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_COMPUTER, pSetupData->ComputerName);
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_ACCOUNT, pSetupData->UserName);
    if (TimeZoneName[0])
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_TIMEZONE, TimeZoneName);

    SetDlgItemTextW(hwndDlg, IDC_READY_SUMMARY, Text);
}

static INT_PTR CALLBACK
ReadyPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            SetDlgItemFont(hwndDlg, IDC_READY_WARNING, pSetupData->hBoldFont, FALSE);
            return FALSE;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_CONFIRM_INSTALL)
            {
                SetupFrameSetButtons(hwndDlg, TRUE,
                                     IsDlgButtonChecked(hwndDlg, IDC_CONFIRM_INSTALL) == BST_CHECKED);
            }
            break;

        case SPN_SETACTIVE:
            FillSummary(hwndDlg, pSetupData);
            CheckDlgButton(hwndDlg, IDC_CONFIRM_INSTALL, BST_UNCHECKED);
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, IDS_INSTALLBTN);
            SetupFrameSetButtons(hwndDlg, TRUE, FALSE);
            SetFocus(GetDlgItem(hwndDlg, IDC_CONFIRM_INSTALL));
            return TRUE;

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT,
                              (IsDlgButtonChecked(hwndDlg, IDC_CONFIRM_INSTALL) == BST_CHECKED) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}

static HBITMAP
CreateAlphaLogo(
    _In_ HINSTANCE hInstance,
    _Out_ PSIZE Size)
{
    BITMAP LogoBitmap, MaskBitmap;
    BITMAPINFO bmi;
    HBITMAP hLogo, hMask, hAlpha = NULL;
    HDC hdcLogo = NULL, hdcMask = NULL;
    PDWORD Bits;
    INT x, y;

    Size->cx = Size->cy = 0;
    hLogo = LoadImageW(hInstance, MAKEINTRESOURCEW(IDB_LOGO), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    hMask = LoadImageW(hInstance, MAKEINTRESOURCEW(IDB_LOGOMASK), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    if (!hLogo || !hMask ||
        !GetObjectW(hLogo, sizeof(LogoBitmap), &LogoBitmap) ||
        !GetObjectW(hMask, sizeof(MaskBitmap), &MaskBitmap) ||
        LogoBitmap.bmWidth != MaskBitmap.bmWidth ||
        LogoBitmap.bmHeight != MaskBitmap.bmHeight)
    {
        goto Quit;
    }

    hdcLogo = CreateCompatibleDC(NULL);
    hdcMask = CreateCompatibleDC(NULL);
    if (!hdcLogo || !hdcMask)
        goto Quit;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = LogoBitmap.bmWidth;
    bmi.bmiHeader.biHeight = -LogoBitmap.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hAlpha = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (PVOID*)&Bits, NULL, 0);
    if (!hAlpha)
        goto Quit;

    SelectObject(hdcLogo, hLogo);
    SelectObject(hdcMask, hMask);
    for (y = 0; y < LogoBitmap.bmHeight; y++)
    {
        for (x = 0; x < LogoBitmap.bmWidth; x++)
        {
            DWORD Alpha = GetPixel(hdcMask, x, y) & 0xFF;
            COLORREF Color = GetPixel(hdcLogo, x, y);

            *Bits++ = (GetBValue(Color) * Alpha / 255) |
                      ((GetGValue(Color) * Alpha / 255) << 8) |
                      ((GetRValue(Color) * Alpha / 255) << 16) |
                      (Alpha << 24);
        }
    }

    Size->cx = LogoBitmap.bmWidth;
    Size->cy = LogoBitmap.bmHeight;

Quit:
    if (hdcMask)
        DeleteDC(hdcMask);
    if (hdcLogo)
        DeleteDC(hdcLogo);
    if (hMask)
        DeleteObject(hMask);
    if (hLogo)
        DeleteObject(hLogo);
    return hAlpha;
}

static VOID
DrawLogo(
    _In_ LPDRAWITEMSTRUCT DrawItem,
    _In_ HBITMAP hLogo,
    _In_ PSIZE Size)
{
    BLENDFUNCTION Blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC hdcMem;
    INT Width = DrawItem->rcItem.right - DrawItem->rcItem.left;
    INT Height = DrawItem->rcItem.bottom - DrawItem->rcItem.top;
    INT cx = Size->cx, cy = Size->cy;

    if (!hLogo || !cx || !cy)
        return;

    if (cy > Height)
    {
        cx = MulDiv(cx, Height, cy);
        cy = Height;
    }

    hdcMem = CreateCompatibleDC(DrawItem->hDC);
    if (!hdcMem)
        return;

    SelectObject(hdcMem, hLogo);
    GdiAlphaBlend(DrawItem->hDC,
                  DrawItem->rcItem.left + (Width - cx) / 2,
                  DrawItem->rcItem.top + (Height - cy) / 2,
                  cx, cy,
                  hdcMem, 0, 0, Size->cx, Size->cy, Blend);
    DeleteDC(hdcMem);
}

static INT_PTR CALLBACK
DonePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    static HBITMAP hLogo;
    static SIZE LogoSize;
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            hLogo = CreateAlphaLogo(pSetupData->hInstance, &LogoSize);
            return FALSE;

        case WM_DESTROY:
            if (hLogo)
                DeleteObject(hLogo);
            hLogo = NULL;
            break;

        case WM_DRAWITEM:
            if (wParam == IDC_FINISHLOGO)
            {
                DrawLogo((LPDRAWITEMSTRUCT)lParam, hLogo, &LogoSize);
                return TRUE;
            }
            break;

        case WM_ACTIVATE:
            if (!pSetupData || !pSetupData->bMustReboot)
                break;
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                if (AreThereInteractiveWindows(GetParent(hwndDlg)))
                    KillTimer(hwndDlg, RESTART_TIMER);
            }
            else
            {
                SetTimer(hwndDlg, RESTART_TIMER, 50, NULL);
            }
            break;

        case WM_TIMER:
        {
            HWND hProgress = GetDlgItem(hwndDlg, IDC_RESTART_PROGRESS);
            INT Position = (INT)SendMessageW(hProgress, PBM_GETPOS, 0, 0);

            if (Position >= RESTART_STEPS)
            {
                KillTimer(hwndDlg, RESTART_TIMER);
                pSetupData->bMustReboot = TRUE;
                EndDialog(GetParent(hwndDlg), IDOK);
            }
            else
            {
                SendMessageW(hProgress, PBM_SETPOS, Position + 1, 0);
            }
            return TRUE;
        }

        case SPN_SETACTIVE:
        {
            BOOL Success = !pSetupData->bStopInstall;
            UINT TextId;

            pSetupData->bMustReboot = pSetupData->bUnattend || !IsShellActive();
            if (pSetupData->bMustReboot)
                TextId = Success ? IDS_DONE_REBOOT : IDS_ABORT_REBOOT;
            else
                TextId = Success ? IDS_FINISH_NO_REBOOT : IDS_ABORT_NO_REBOOT;

            SetupFrameSetTitle(hwndDlg, Success ? IDS_DONETITLE : IDS_ABORTTITLE, 0);
            SetResText(GetDlgItem(hwndDlg, IDC_FINISHTEXT), TextId);
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, IDS_RESTARTBTN);
            SetupFrameSetButtonText(hwndDlg, IDCANCEL, Success ? IDS_POSTPONEBTN : IDS_CLOSEBTN);
            SetupFrameShowButtons(hwndDlg, FALSE, TRUE, !pSetupData->bMustReboot);
            SetupFrameSetButtons(hwndDlg, FALSE, TRUE);
            EnableSetupCancel(GetParent(hwndDlg), !pSetupData->bMustReboot);
            ShowDlgItem(hwndDlg, IDC_RESTART_PROGRESS, pSetupData->bMustReboot ? SW_SHOW : SW_HIDE);

            if (pSetupData->bMustReboot)
            {
                SendDlgItemMessageW(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, RESTART_STEPS));
                SendDlgItemMessageW(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETPOS, 0, 0);
                SetTimer(hwndDlg, RESTART_TIMER, 50, NULL);
            }

            ShowWindow(GetParent(hwndDlg), SW_SHOW);
            SwitchToThisWindow(GetParent(hwndDlg), TRUE);
            SetFocus(GetDlgItem(GetParent(hwndDlg), IDC_FRAME_NEXT));
            return TRUE;
        }

        case SPN_WIZNEXT:
            KillTimer(hwndDlg, RESTART_TIMER);
            pSetupData->bMustReboot = TRUE;
            EndDialog(GetParent(hwndDlg), IDOK);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;

        case SPN_QUERYCANCEL:
            KillTimer(hwndDlg, RESTART_TIMER);
            EndDialog(GetParent(hwndDlg), IDCANCEL);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
    }

    return FALSE;
}
