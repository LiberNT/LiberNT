/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Setup pages for the summary and the finish
 * COPYRIGHT:   Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define RESTART_STEPS   300
#define RESTART_TIMER   1

static INT_PTR CALLBACK ReadyPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK DonePageProc(HWND, UINT, WPARAM, LPARAM);

static const SETUP_PAGE_INFO SetupPages[SETUP_PAGE_COUNT] =
{
    { IDD_LANGUAGEPAGE, LanguagePageProc, IDS_LANGUAGETITLE, IDS_LANGUAGESUBTITLE,    SETUP_PAGE_SKIP_UNATTEND },
    { IDD_DISKPAGE,     DiskPageProc,     IDS_DRIVETITLE,    IDS_DISKSUBTITLE,        SETUP_PAGE_BACK },
    { IDD_ACCOUNTPAGE,  AccountPageProc,  IDS_ACCOUNTTITLE,  IDS_ACCOUNTZONESUBTITLE, SETUP_PAGE_BACK | SETUP_PAGE_SKIP_UNATTEND },
    { IDD_READYPAGE,    ReadyPageProc,    IDS_READYTITLE,    IDS_READYSUBTITLE,       SETUP_PAGE_BACK | SETUP_PAGE_SKIP_UNATTEND },
    { IDD_PROCESSPAGE,  InstallPageProc,  IDS_PROCESSTITLE,  IDS_PROCESSSUBTITLE,     0 },
    { IDD_FINISHPAGE,   DonePageProc,     IDS_DONETITLE,     0,                       0 },
};

INT_PTR
RunSetupWizard(
    _In_ PSETUPDATA pSetupData)
{
    return RunSetupFrame(pSetupData, SetupPages, ARRAYSIZE(SetupPages));
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

    if (!LoadStringW(GetModuleHandleW(NULL), uID, Format, ARRAYSIZE(Format)))
        return;

    va_start(Args, uID);
    StringCchVPrintfW(Line, ARRAYSIZE(Line), Format, Args);
    va_end(Args);

    StringCchCatW(Text, cchText, Line);
    StringCchCatW(Text, cchText, L"\r\n");
}

static PCWSTR
FindChoiceName(
    _In_reads_(Count) const SETUP_CHOICE* Choices,
    _In_ ULONG Count,
    _In_ PCWSTR Id)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (_wcsicmp(Choices[Index].Id, Id) == 0)
            return Choices[Index].Name;
    }
    return Id;
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
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_LANGUAGE,
               FindChoiceName(pSetupData->Locales, pSetupData->LocaleCount, pSetupData->DefaultLanguage));
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_KEYBOARD,
               FindChoiceName(pSetupData->Layouts, pSetupData->LayoutCount, pSetupData->DefaultKBLayout));
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_COMPUTER, pSetupData->ComputerName);
    AppendLine(Text, ARRAYSIZE(Text), IDS_READY_ACCOUNT, pSetupData->UserName);
    if (pSetupData->TimeZoneName[0])
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_TIMEZONE, pSetupData->TimeZoneName);

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
            if (LOWORD(wParam) != WA_INACTIVE)
                SetTimer(hwndDlg, RESTART_TIMER, 50, NULL);
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
            BOOL Success = pSetupData->bInstalled;
            UINT TextId;

            pSetupData->bMustReboot = pSetupData->bUnattend || !GetShellWindow();
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
