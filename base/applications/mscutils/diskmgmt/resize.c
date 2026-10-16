/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shrink Volume dialog and Extend Volume Wizard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <prsht.h>
#include <fslib/volresize.h>

#define DM_MEGABYTE (1024ULL * 1024ULL)

typedef struct _DM_SHRINK_DIALOG
{
    WCHAR Caption[96];
    ULONGLONG TotalMb;
    ULONGLONG AvailableMb;
    ULONGLONG AmountMb;
} DM_SHRINK_DIALOG, *PDM_SHRINK_DIALOG;

typedef struct _DM_EXTEND_WIZARD
{
    ULONG DiskNumber;
    ULONGLONG CurrentMb;
    ULONGLONG FreeMb;
    ULONGLONG AmountMb;
    BOOL DiskSelected;
    HFONT TitleFont;
    BOOL Finished;
} DM_EXTEND_WIZARD, *PDM_EXTEND_WIZARD;

typedef struct _DM_RESIZE_WORK
{
    UNICODE_STRING VolumePath;
    ULONGLONG Bytes;
    BOOLEAN Extend;
    BOOLEAN ExtendFileSystem;
    NTSTATUS Status;
} DM_RESIZE_WORK, *PDM_RESIZE_WORK;

static VOID
DmResizeLoadString(
    _In_ UINT StringId,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (LoadStringW(GetModuleHandleW(NULL), StringId, Buffer, (int)cchBuffer) == 0 && cchBuffer > 0)
        Buffer[0] = UNICODE_NULL;
}

static BOOL
DmResizeVolumePath(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Volume->DeviceName[0] != UNICODE_NULL)
        return SUCCEEDED(StringCchCopyW(Buffer, cchBuffer, Volume->DeviceName));

    if (Volume->HasStorageDeviceNumber)
    {
        return SUCCEEDED(StringCchPrintfW(Buffer,
                                          cchBuffer,
                                          L"\\Device\\Harddisk%lu\\Partition%lu",
                                          Volume->StorageDiskNumber,
                                          Volume->StoragePartitionNumber));
    }

    return FALSE;
}

static BOOL
DmResizeIsNtfs(
    _In_ const DM_VOLUME *Volume)
{
    return _wcsicmp(Volume->FileSystem, L"NTFS") == 0;
}

static BOOL
DmResizeIsRaw(
    _In_ const DM_VOLUME *Volume)
{
    return Volume->FileSystem[0] == UNICODE_NULL ||
           _wcsicmp(Volume->FileSystem, L"RAW") == 0;
}

static DWORD WINAPI
DmResizeWorker(
    _In_ LPVOID Parameter)
{
    PDM_RESIZE_WORK Work = (PDM_RESIZE_WORK)Parameter;

    if (Work->Extend)
        Work->Status = VolResizeExtend(&Work->VolumePath, Work->Bytes, Work->ExtendFileSystem);
    else
        Work->Status = VolResizeShrink(&Work->VolumePath, Work->Bytes, NULL, NULL);
    return 0;
}

static NTSTATUS
DmResizeRun(
    _In_ HWND hWnd,
    _Inout_ PDM_RESIZE_WORK Work)
{
    HCURSOR OldCursor;
    HANDLE Thread;
    MSG Message;

    Thread = CreateThread(NULL, 0, DmResizeWorker, Work, 0, NULL);
    if (Thread == NULL)
    {
        DmResizeWorker(Work);
        return Work->Status;
    }

    OldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    EnableWindow(hWnd, FALSE);
    while (MsgWaitForMultipleObjects(1, &Thread, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        SetCursor(LoadCursorW(NULL, IDC_WAIT));
    }
    EnableWindow(hWnd, TRUE);
    SetCursor(OldCursor);
    CloseHandle(Thread);
    return Work->Status;
}

static VOID
DmResizeShowError(
    _In_ HWND hWnd,
    _In_ UINT TitleId,
    _In_ NTSTATUS Status)
{
    WCHAR Title[64];
    WCHAR Text[512];
    DWORD Error = RtlNtStatusToDosError(Status);

    DmResizeLoadString(TitleId, Title, ARRAYSIZE(Title));
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       Error,
                       0,
                       Text,
                       ARRAYSIZE(Text),
                       NULL) == 0)
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"0x%08lx", Status);
    }
    MessageBoxW(hWnd, Text, Title, MB_OK | MB_ICONERROR);
}

static VOID
DmShrinkSetNumber(
    _In_ HWND hwndDlg,
    _In_ INT ControlId,
    _In_ ULONGLONG Value)
{
    WCHAR Buffer[32];

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u", Value);
    SetDlgItemTextW(hwndDlg, ControlId, Buffer);
}

static VOID
DmShrinkUpdateAfter(
    _In_ HWND hwndDlg,
    _In_ PDM_SHRINK_DIALOG Dialog)
{
    WCHAR Buffer[32];
    ULONGLONG Amount = 0;

    if (GetDlgItemTextW(hwndDlg, IDC_SHRINK_AMOUNT, Buffer, ARRAYSIZE(Buffer)) != 0)
        Amount = _wcstoui64(Buffer, NULL, 10);
    if (Amount > Dialog->AvailableMb)
        Amount = Dialog->AvailableMb;

    Dialog->AmountMb = Amount;
    DmShrinkSetNumber(hwndDlg, IDC_SHRINK_AFTER, Dialog->TotalMb - Amount);
    EnableWindow(GetDlgItem(hwndDlg, IDOK), Amount != 0);
}

static INT_PTR CALLBACK
DmShrinkDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_SHRINK_DIALOG Dialog = (PDM_SHRINK_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            HICON Icon = NULL;

            Dialog = (PDM_SHRINK_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Dialog);
            SetWindowTextW(hwndDlg, Dialog->Caption);
            DmShrinkSetNumber(hwndDlg, IDC_SHRINK_TOTAL, Dialog->TotalMb);
            DmShrinkSetNumber(hwndDlg, IDC_SHRINK_AVAILABLE, Dialog->AvailableMb);
            SendDlgItemMessageW(hwndDlg, IDC_SHRINK_SPIN, UDM_SETRANGE32, 0, (LPARAM)min(Dialog->AvailableMb, (ULONGLONG)MAXLONG));
            DmShrinkSetNumber(hwndDlg, IDC_SHRINK_AMOUNT, Dialog->AvailableMb);
            DmShrinkUpdateAfter(hwndDlg, Dialog);
            if (SUCCEEDED(LoadIconWithScaleDown(NULL, IDI_INFORMATION,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON),
                                                &Icon)))
            {
                SendDlgItemMessageW(hwndDlg, IDC_SHRINK_ICON, STM_SETICON, (WPARAM)Icon, 0);
            }
            SetFocus(GetDlgItem(hwndDlg, IDC_SHRINK_AMOUNT));
            SendDlgItemMessageW(hwndDlg, IDC_SHRINK_AMOUNT, EM_SETSEL, 0, -1);
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_SHRINK_AMOUNT:
                    if (HIWORD(wParam) == EN_CHANGE && Dialog != NULL)
                        DmShrinkUpdateAfter(hwndDlg, Dialog);
                    return TRUE;

                case IDOK:
                    if (Dialog != NULL)
                    {
                        DmShrinkUpdateAfter(hwndDlg, Dialog);
                        if (Dialog->AmountMb == 0)
                            return TRUE;
                    }
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_DESTROY:
        {
            HICON Icon = (HICON)SendDlgItemMessageW(hwndDlg, IDC_SHRINK_ICON, STM_GETICON, 0, 0);

            if (Icon != NULL)
                DestroyIcon(Icon);
            break;
        }
    }

    return FALSE;
}

BOOL
DmResizeCanShrink(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    const DM_VOLUME *Volume = Context->Volume;

    return Volume != NULL &&
           !Volume->IsSystem &&
           !Volume->IsDynamic &&
           Volume->ExtentCount <= 1 &&
           (DmResizeIsNtfs(Volume) || DmResizeIsRaw(Volume));
}

BOOL
DmResizeCanExtend(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_opt_ const DM_REGION *FreeRegion)
{
    const DM_VOLUME *Volume = Context->Volume;

    return Volume != NULL &&
           FreeRegion != NULL &&
           FreeRegion->Length >= DM_MEGABYTE &&
           !Volume->IsSystem &&
           !Volume->IsDynamic &&
           Volume->ExtentCount <= 1 &&
           (DmResizeIsNtfs(Volume) || DmResizeIsRaw(Volume));
}

BOOL
DmResizeShrinkVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    DM_SHRINK_DIALOG Dialog;
    DM_RESIZE_WORK Work;
    VOLRESIZE_INFO Info;
    WCHAR VolumePath[MAX_PATH];
    WCHAR Format[64];
    WCHAR Name[64];
    HWND hQuery;
    NTSTATUS Status;

    if (Context == NULL || Context->Volume == NULL || !DmResizeCanShrink(Context))
        return FALSE;
    if (!DmResizeVolumePath(Context->Volume, VolumePath, ARRAYSIZE(VolumePath)))
        return FALSE;

    ZeroMemory(&Work, sizeof(Work));
    RtlInitUnicodeString(&Work.VolumePath, VolumePath);

    hQuery = CreateDialogW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SHRINK_QUERY), Context->hWnd, NULL);
    if (hQuery != NULL)
    {
        ShowWindow(hQuery, SW_SHOW);
        UpdateWindow(hQuery);
    }
    Status = VolResizeQuery(&Work.VolumePath, &Info);
    if (hQuery != NULL)
        DestroyWindow(hQuery);
    if (!NT_SUCCESS(Status))
    {
        DmResizeShowError(Context->hWnd, IDS_SHRINK_TITLE, Status);
        return FALSE;
    }

    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.TotalMb = Info.PartitionBytes / DM_MEGABYTE;
    Dialog.AvailableMb = Info.MaxReclaimableBytes / DM_MEGABYTE;
    DmResizeLoadString(IDS_SHRINK_CAPTION, Format, ARRAYSIZE(Format));
    if (Context->Volume->HasDriveLetter)
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"%c:", towupper(Context->Volume->DriveLetter));
    else
        DmVolumeListBuildName(Context->Volume, Name, ARRAYSIZE(Name));
    StringCchPrintfW(Dialog.Caption, ARRAYSIZE(Dialog.Caption), Format, Name);

    if (DialogBoxParamW(GetModuleHandleW(NULL),
                        MAKEINTRESOURCEW(IDD_SHRINK_VOLUME),
                        Context->hWnd,
                        DmShrinkDlgProc,
                        (LPARAM)&Dialog) != IDOK ||
        Dialog.AmountMb == 0)
    {
        return FALSE;
    }

    Work.Bytes = Dialog.AmountMb * DM_MEGABYTE;
    if (Work.Bytes > Info.MaxReclaimableBytes)
        Work.Bytes = Info.MaxReclaimableBytes;
    Work.Extend = FALSE;
    Status = DmResizeRun(Context->hWnd, &Work);
    if (!NT_SUCCESS(Status))
    {
        DmResizeShowError(Context->hWnd, IDS_SHRINK_TITLE, Status);
        return FALSE;
    }
    return TRUE;
}

static VOID
DmExtendFillLists(
    _In_ HWND hwndDlg,
    _In_ PDM_EXTEND_WIZARD Wizard)
{
    WCHAR Format[64];
    WCHAR Text[96];

    DmResizeLoadString(IDS_EXTEND_DISK_ENTRY, Format, ARRAYSIZE(Format));
    StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Wizard->DiskNumber, Wizard->FreeMb);
    SendDlgItemMessageW(hwndDlg, IDC_EXTEND_AVAILABLE, LB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(hwndDlg, IDC_EXTEND_SELECTED, LB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(hwndDlg,
                        Wizard->DiskSelected ? IDC_EXTEND_SELECTED : IDC_EXTEND_AVAILABLE,
                        LB_ADDSTRING,
                        0,
                        (LPARAM)Text);
    EnableWindow(GetDlgItem(hwndDlg, IDC_EXTEND_ADD), !Wizard->DiskSelected);
    EnableWindow(GetDlgItem(hwndDlg, IDC_EXTEND_REMOVE), Wizard->DiskSelected);
    EnableWindow(GetDlgItem(hwndDlg, IDC_EXTEND_REMOVE_ALL), Wizard->DiskSelected);
    EnableWindow(GetDlgItem(hwndDlg, IDC_EXTEND_AMOUNT), Wizard->DiskSelected);
    EnableWindow(GetDlgItem(hwndDlg, IDC_EXTEND_SPIN), Wizard->DiskSelected);
    SendDlgItemMessageW(hwndDlg, IDC_EXTEND_SPIN, UDM_SETRANGE32, 0,
                        (LPARAM)(Wizard->DiskSelected ? min(Wizard->FreeMb, (ULONGLONG)MAXLONG) : 0));
    DmShrinkSetNumber(hwndDlg, IDC_EXTEND_AMOUNT, Wizard->DiskSelected ? Wizard->FreeMb : 0);
    DmShrinkSetNumber(hwndDlg, IDC_EXTEND_MAX, Wizard->DiskSelected ? Wizard->FreeMb : 0);
}

static VOID
DmExtendUpdateTotal(
    _In_ HWND hwndDlg,
    _In_ PDM_EXTEND_WIZARD Wizard)
{
    WCHAR Buffer[32];
    ULONGLONG Amount = 0;

    if (Wizard->DiskSelected &&
        GetDlgItemTextW(hwndDlg, IDC_EXTEND_AMOUNT, Buffer, ARRAYSIZE(Buffer)) != 0)
    {
        Amount = _wcstoui64(Buffer, NULL, 10);
    }
    if (Amount > Wizard->FreeMb)
        Amount = Wizard->FreeMb;
    Wizard->AmountMb = Amount;
    DmShrinkSetNumber(hwndDlg, IDC_EXTEND_TOTAL, Wizard->CurrentMb + Amount);
    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | (Amount != 0 ? PSWIZB_NEXT : 0));
}

static PDM_EXTEND_WIZARD
DmExtendGetWizard(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        PDM_EXTEND_WIZARD Wizard = (PDM_EXTEND_WIZARD)((LPPROPSHEETPAGEW)lParam)->lParam;

        SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Wizard);
        return Wizard;
    }
    return (PDM_EXTEND_WIZARD)GetWindowLongPtrW(hwndDlg, DWLP_USER);
}

static INT_PTR CALLBACK
DmExtendWelcomeProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_EXTEND_WIZARD Wizard = DmExtendGetWizard(hwndDlg, uMsg, lParam);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            if (Wizard != NULL && Wizard->TitleFont != NULL)
                SendDlgItemMessageW(hwndDlg, IDC_EXTEND_TITLE, WM_SETFONT, (WPARAM)Wizard->TitleFont, TRUE);
            return TRUE;

        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->code == PSN_SETACTIVE)
            {
                PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT);
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static INT_PTR CALLBACK
DmExtendSelectProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_EXTEND_WIZARD Wizard = DmExtendGetWizard(hwndDlg, uMsg, lParam);

    if (Wizard == NULL)
        return FALSE;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            DmExtendFillLists(hwndDlg, Wizard);
            DmExtendUpdateTotal(hwndDlg, Wizard);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_EXTEND_ADD:
                    Wizard->DiskSelected = TRUE;
                    DmExtendFillLists(hwndDlg, Wizard);
                    DmExtendUpdateTotal(hwndDlg, Wizard);
                    return TRUE;

                case IDC_EXTEND_REMOVE:
                case IDC_EXTEND_REMOVE_ALL:
                    Wizard->DiskSelected = FALSE;
                    DmExtendFillLists(hwndDlg, Wizard);
                    DmExtendUpdateTotal(hwndDlg, Wizard);
                    return TRUE;

                case IDC_EXTEND_AMOUNT:
                    if (HIWORD(wParam) == EN_CHANGE)
                        DmExtendUpdateTotal(hwndDlg, Wizard);
                    return TRUE;
            }
            break;

        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->code == PSN_SETACTIVE)
            {
                DmExtendUpdateTotal(hwndDlg, Wizard);
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static INT_PTR CALLBACK
DmExtendFinishProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_EXTEND_WIZARD Wizard = DmExtendGetWizard(hwndDlg, uMsg, lParam);

    if (Wizard == NULL)
        return FALSE;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            if (Wizard->TitleFont != NULL)
                SendDlgItemMessageW(hwndDlg, IDC_EXTEND_TITLE, WM_SETFONT, (WPARAM)Wizard->TitleFont, TRUE);
            return TRUE;

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {
                case PSN_SETACTIVE:
                {
                    WCHAR Format[64];
                    WCHAR Text[96];

                    DmResizeLoadString(IDS_EXTEND_DISK_SELECTED, Format, ARRAYSIZE(Format));
                    StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Wizard->DiskNumber, Wizard->AmountMb);
                    SendDlgItemMessageW(hwndDlg, IDC_EXTEND_SETTINGS, LB_RESETCONTENT, 0, 0);
                    SendDlgItemMessageW(hwndDlg, IDC_EXTEND_SETTINGS, LB_ADDSTRING, 0, (LPARAM)Text);
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_FINISH);
                    SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
                    return TRUE;
                }

                case PSN_WIZFINISH:
                    Wizard->Finished = TRUE;
                    SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

BOOL
DmResizeExtendVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ const DM_REGION *FreeRegion)
{
    PROPSHEETPAGEW Page;
    HPROPSHEETPAGE Pages[3];
    PROPSHEETHEADERW Header;
    DM_EXTEND_WIZARD Wizard;
    DM_RESIZE_WORK Work;
    WCHAR VolumePath[MAX_PATH];
    WCHAR Title[64];
    WCHAR SubTitle[128];
    NONCLIENTMETRICSW Metrics;
    LOGFONTW TitleFont;
    NTSTATUS Status;

    if (Context == NULL || Context->Volume == NULL || Context->Disk == NULL ||
        !DmResizeCanExtend(Context, FreeRegion))
    {
        return FALSE;
    }
    if (!DmResizeVolumePath(Context->Volume, VolumePath, ARRAYSIZE(VolumePath)))
        return FALSE;

    ZeroMemory(&Wizard, sizeof(Wizard));
    Wizard.DiskNumber = Context->Disk->DiskNumber;
    Wizard.CurrentMb = Context->Region->Length / DM_MEGABYTE;
    Wizard.FreeMb = FreeRegion->Length / DM_MEGABYTE;
    Wizard.AmountMb = Wizard.FreeMb;
    Wizard.DiskSelected = TRUE;

    ZeroMemory(&Metrics, sizeof(Metrics));
    Metrics.cbSize = sizeof(Metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
    {
        HDC hdc = GetDC(NULL);

        TitleFont = Metrics.lfMessageFont;
        TitleFont.lfWeight = FW_BOLD;
        TitleFont.lfHeight = -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);
        Wizard.TitleFont = CreateFontIndirectW(&TitleFont);
    }

    ZeroMemory(&Page, sizeof(Page));
    Page.dwSize = sizeof(Page);
    Page.hInstance = GetModuleHandleW(NULL);
    Page.lParam = (LPARAM)&Wizard;

    Page.dwFlags = PSP_HIDEHEADER;
    Page.pszTemplate = MAKEINTRESOURCEW(IDD_EXTEND_WELCOME);
    Page.pfnDlgProc = DmExtendWelcomeProc;
    Pages[0] = CreatePropertySheetPageW(&Page);

    DmResizeLoadString(IDS_EXTEND_SELECT_TITLE, Title, ARRAYSIZE(Title));
    DmResizeLoadString(IDS_EXTEND_SELECT_SUBTITLE, SubTitle, ARRAYSIZE(SubTitle));
    Page.dwFlags = PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    Page.pszHeaderTitle = Title;
    Page.pszHeaderSubTitle = SubTitle;
    Page.pszTemplate = MAKEINTRESOURCEW(IDD_EXTEND_SELECT);
    Page.pfnDlgProc = DmExtendSelectProc;
    Pages[1] = CreatePropertySheetPageW(&Page);

    Page.dwFlags = PSP_HIDEHEADER;
    Page.pszHeaderTitle = NULL;
    Page.pszHeaderSubTitle = NULL;
    Page.pszTemplate = MAKEINTRESOURCEW(IDD_EXTEND_FINISH);
    Page.pfnDlgProc = DmExtendFinishProc;
    Pages[2] = CreatePropertySheetPageW(&Page);

    ZeroMemory(&Header, sizeof(Header));
    Header.dwSize = sizeof(Header);
    Header.dwFlags = PSH_WIZARD97 | PSH_WATERMARK;
    Header.hwndParent = Context->hWnd;
    Header.hInstance = GetModuleHandleW(NULL);
    Header.pszCaption = MAKEINTRESOURCEW(IDS_EXTEND_CAPTION);
    Header.nPages = ARRAYSIZE(Pages);
    Header.phpage = Pages;
    Header.pszbmWatermark = MAKEINTRESOURCEW(IDB_WATERMARK);

    PropertySheetW(&Header);
    if (Wizard.TitleFont != NULL)
        DeleteObject(Wizard.TitleFont);
    if (!Wizard.Finished || Wizard.AmountMb == 0)
        return FALSE;

    ZeroMemory(&Work, sizeof(Work));
    RtlInitUnicodeString(&Work.VolumePath, VolumePath);
    Work.Bytes = Wizard.AmountMb * DM_MEGABYTE;
    Work.Extend = TRUE;
    Work.ExtendFileSystem = DmResizeIsNtfs(Context->Volume);
    Status = DmResizeRun(Context->hWnd, &Work);
    if (!NT_SUCCESS(Status))
    {
        DmResizeShowError(Context->hWnd, IDS_EXTEND_CAPTION, Status);
        return FALSE;
    }
    return TRUE;
}
