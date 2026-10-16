/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Window shared by Setup and the first-boot setup
 * COPYRIGHT:   Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"

#include "resource.h"

#define NDEBUG
#include <debug.h>

typedef struct _SETUP_FRAME
{
    PSETUPDATA SetupData;
    HWND hwnd;
    const SETUP_PAGE_INFO* PageInfo;
    INT PageCount;
    HWND Pages[SETUP_MAX_PAGES];
    INT Current;
    HBITMAP hWatermark;
    SIZE Watermark;
} SETUP_FRAME;

static SETUP_FRAME Frame;

VOID
LoadPrintfW(
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText,
    _In_ UINT uID,
    ...)
{
    WCHAR Format[512];
    va_list Args;

    Text[0] = UNICODE_NULL;
    if (!LoadStringW(GetModuleHandleW(NULL), uID, Format, ARRAYSIZE(Format)))
        return;

    va_start(Args, uID);
    StringCchVPrintfW(Text, cchText, Format, Args);
    va_end(Args);
}

INT
SetupMessage(
    _In_opt_ HWND hwndOwner,
    _In_opt_ PCWSTR Icon,
    _In_ TASKDIALOG_COMMON_BUTTON_FLAGS Buttons,
    _In_ INT DefaultButton,
    _In_opt_ PCWSTR MainInstruction,
    _In_opt_ PCWSTR Content)
{
    TASKDIALOGCONFIG Config;
    INT Button = IDCANCEL;

    ZeroMemory(&Config, sizeof(Config));
    Config.cbSize = sizeof(Config);
    Config.hwndParent = hwndOwner;
    Config.hInstance = GetModuleHandleW(NULL);
    Config.dwFlags = hwndOwner ? TDF_POSITION_RELATIVE_TO_WINDOW : 0;
    Config.dwCommonButtons = Buttons;
    Config.pszWindowTitle = MAKEINTRESOURCEW(IDS_CAPTION);
    Config.pszMainIcon = Icon;
    Config.pszMainInstruction = MainInstruction;
    Config.pszContent = Content;
    Config.nDefaultButton = DefaultButton;

    if (FAILED(TaskDialogIndirect(&Config, &Button, NULL, NULL)))
        return IDCANCEL;
    return Button;
}

static HFONT
CreateFrameFont(
    _In_ INT PointSize)
{
    NONCLIENTMETRICSW Metrics;
    LOGFONTW Font;
    HDC hdc;

    Metrics.cbSize = sizeof(Metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
        return NULL;

    Font = Metrics.lfMessageFont;
    Font.lfWeight = FW_BOLD;
    if (PointSize)
    {
        hdc = GetDC(NULL);
        Font.lfHeight = -MulDiv(PointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);
    }
    return CreateFontIndirectW(&Font);
}

VOID
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
    BOOL Back = !!(Frame.PageInfo[Page].Flags & SETUP_PAGE_BACK);

    if (Frame.Current >= 0)
    {
        SendMessageW(Frame.Pages[Frame.Current], SPN_KILLACTIVE, 0, 0);
        ShowWindow(Frame.Pages[Frame.Current], SW_HIDE);
    }

    Frame.Current = Page;
    SetupFrameSetTitle(hPage, Frame.PageInfo[Page].Title, Frame.PageInfo[Page].Subtitle);
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
    _In_ INT Page)
{
    UNREFERENCED_PARAMETER(hPage);
    FrameShowPage(Page);
}

static INT
FrameNextPage(
    _In_ INT Page)
{
    while (Page < Frame.PageCount && Frame.SetupData->bUnattend &&
           (Frame.PageInfo[Page].Flags & SETUP_PAGE_SKIP_UNATTEND))
    {
        Page++;
    }
    return Page;
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

    Page = FrameNextPage(Current + 1);
    if (Page < Frame.PageCount)
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

    if (SetupMessage(Frame.hwnd, TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, IDNO,
                     MAKEINTRESOURCEW(IDS_ABORTSETUP2), MAKEINTRESOURCEW(IDS_ABORTSETUP)) == IDYES)
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
            if (!pSetupData->hTitleFont)
                pSetupData->hTitleFont = CreateFrameFont(12);
            if (!pSetupData->hBoldFont)
                pSetupData->hBoldFont = CreateFrameFont(0);
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
            for (Page = 0; Page < Frame.PageCount; Page++)
            {
                Frame.Pages[Page] = CreateDialogParamW(pSetupData->hInstance,
                                                       MAKEINTRESOURCEW(Frame.PageInfo[Page].Template),
                                                       hwnd,
                                                       Frame.PageInfo[Page].Proc,
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
            FrameShowPage(FrameNextPage(0));
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
            if (Frame.SetupData->hTitleFont)
                DeleteObject(Frame.SetupData->hTitleFont);
            if (Frame.SetupData->hBoldFont)
                DeleteObject(Frame.SetupData->hBoldFont);
            Frame.SetupData->hTitleFont = NULL;
            Frame.SetupData->hBoldFont = NULL;
            if (Frame.hWatermark)
                DeleteObject(Frame.hWatermark);
            Frame.hWatermark = NULL;
            break;
    }

    return FALSE;
}

INT_PTR
RunSetupFrame(
    _In_ PSETUPDATA pSetupData,
    _In_reads_(PageCount) const SETUP_PAGE_INFO* PageInfo,
    _In_ INT PageCount)
{
    Frame.PageInfo = PageInfo;
    Frame.PageCount = min(PageCount, SETUP_MAX_PAGES);
    return DialogBoxParamW(pSetupData->hInstance, MAKEINTRESOURCEW(IDD_SETUPFRAME),
                           NULL, SetupFrameProc, (LPARAM)pSetupData);
}
