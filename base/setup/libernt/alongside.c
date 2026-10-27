/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Page that installs LiberNT alongside an existing Windows installation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <winver.h>
#include <fslib/volresize.h>
#include <ndk/obfuncs.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define TYPE_MEGABYTE           (1024ULL * 1024ULL)
#define TYPE_MIN_LIBERNT_MB     (16ULL * 1024ULL)
#define TYPE_LINE_MB            1024
#define TYPE_PAGE_MB            (10 * 1024)
#define TYPE_WINDOWS11_BUILD    22000
#define TYPE_WINDOWS10_BUILD    10240
#define TYPE_WINDOWS_COLOR      RGB(0, 120, 215)
#define TYPE_LIBERNT_COLOR      RGB(16, 137, 62)

typedef struct _TYPE_STATE
{
    BOOLEAN Found;
    BOOLEAN Planned;
    UINT Problem;
    ULONG DiskNumber;
    ULONGLONG StartSector;
    ULONGLONG WindowsMb;
    ULONGLONG FreeAfterMb;
    ULONGLONG MinWindowsMb;
    ULONGLONG MaxWindowsMb;
    WCHAR Region[96];
} TYPE_STATE;

static TYPE_STATE TypeState;
static BOOLEAN FromImage;

static BOOLEAN
IsRunningFromImage(VOID)
{
    UNICODE_STRING SystemRoot = RTL_CONSTANT_STRING(L"\\SystemRoot");
    UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    PFILE_FS_ATTRIBUTE_INFORMATION Info = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &SystemRoot, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtQueryVolumeInformationFile(Handle, &IoStatusBlock, Info, sizeof(Buffer), FileFsAttributeInformation);
    NtClose(Handle);
    return NT_SUCCESS(Status) &&
           Info->FileSystemNameLength == 4 * sizeof(WCHAR) &&
           _wcsnicmp(Info->FileSystemName, L"NTFS", 4) == 0;
}

static BOOLEAN
OpenVolumeFile(
    _In_ PCWSTR DeviceName,
    _In_ PCWSTR File,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Path;
    WCHAR Buffer[MAX_PATH];

    if (FAILED(StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s%s", DeviceName, File)))
        return FALSE;

    RtlInitUnicodeString(&Path, Buffer);
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NT_SUCCESS(NtOpenFile(Handle, Access | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
}

static BOOLEAN
IsWindowsVolume(
    _In_ PCWSTR DeviceName)
{
    HANDLE Handle;

    if (!OpenVolumeFile(DeviceName, L"\\Windows\\System32\\ntoskrnl.exe", FILE_READ_ATTRIBUTES, &Handle))
        return FALSE;
    NtClose(Handle);
    return TRUE;
}

static BOOLEAN
IsWindowsHibernated(
    _In_ PCWSTR DeviceName)
{
    IO_STATUS_BLOCK IoStatusBlock;
    CHAR Signature[4];
    HANDLE Handle;
    NTSTATUS Status;

    if (!OpenVolumeFile(DeviceName, L"\\hiberfil.sys", FILE_READ_DATA, &Handle))
        return FALSE;

    Status = NtReadFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Signature, sizeof(Signature), NULL, NULL);
    NtClose(Handle);
    return NT_SUCCESS(Status) &&
           IoStatusBlock.Information == sizeof(Signature) &&
           _strnicmp(Signature, "hibr", sizeof(Signature)) == 0;
}

static BOOLEAN
GetWindowsBuild(
    _In_ PCWSTR DeviceName,
    _Out_ PULONG Build)
{
    VS_FIXEDFILEINFO *Info;
    WCHAR Path[MAX_PATH];
    WCHAR Key[64];
    PWORD Translation;
    PWSTR Company;
    PVOID Data;
    DWORD Size, Ignored;
    UINT Length;
    BOOLEAN Windows = TRUE;

    *Build = 0;
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s\\Windows\\System32\\ntoskrnl.exe",
                                DeviceName)))
    {
        return TRUE;
    }

    Size = GetFileVersionInfoSizeW(Path, &Ignored);
    if (!Size)
        return TRUE;

    Data = HeapAlloc(ProcessHeap, 0, Size);
    if (!Data)
        return TRUE;

    if (GetFileVersionInfoW(Path, 0, Size, Data))
    {
        if (VerQueryValueW(Data, L"\\VarFileInfo\\Translation", (PVOID*)&Translation, &Length) &&
            Length >= 2 * sizeof(WORD) &&
            SUCCEEDED(StringCchPrintfW(Key, ARRAYSIZE(Key), L"\\StringFileInfo\\%04x%04x\\CompanyName",
                                       Translation[0], Translation[1])) &&
            VerQueryValueW(Data, Key, (PVOID*)&Company, &Length) && Length &&
            !wcsstr(Company, L"Microsoft"))
        {
            Windows = FALSE;
        }

        if (VerQueryValueW(Data, L"\\", (PVOID*)&Info, &Length) && Length >= sizeof(*Info))
            *Build = HIWORD(Info->dwFileVersionLS);
    }

    HeapFree(ProcessHeap, 0, Data);
    return Windows;
}

static VOID
FindWindowsInstallation(
    _Inout_ PSETUPDATA pSetupData)
{
    PLIST_ENTRY DiskLink, PartLink;
    PPARTENTRY Windows = NULL;
    VOLRESIZE_INFO Info;
    UNICODE_STRING VolumePath;
    ULONG Build = 0;
    UINT NameId;
    HCURSOR OldCursor;

    ZeroMemory(&TypeState, sizeof(TypeState));
    if (!pSetupData->PartitionList)
        return;

    OldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    for (DiskLink = pSetupData->PartitionList->DiskListHead.Flink;
         DiskLink != &pSetupData->PartitionList->DiskListHead && !Windows;
         DiskLink = DiskLink->Flink)
    {
        PDISKENTRY DiskEntry = CONTAINING_RECORD(DiskLink, DISKENTRY, ListEntry);

        if (DiskEntry->BootMedium || DiskEntry->DiskStyle != PARTITION_STYLE_GPT || !IsUefiFirmware())
            continue;

        for (PartLink = DiskEntry->PrimaryPartListHead.Flink;
             PartLink != &DiskEntry->PrimaryPartListHead && !Windows;
             PartLink = PartLink->Flink)
        {
            PPARTENTRY PartEntry = CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry);
            PVOLENTRY Volume = PartEntry->Volume;

            if (PartEntry->IsPartitioned && !PartEntry->New && Volume && !Volume->New &&
                _wcsicmp(Volume->Info.FileSystem, L"NTFS") == 0 &&
                IsWindowsVolume(Volume->Info.DeviceName) &&
                GetWindowsBuild(Volume->Info.DeviceName, &Build))
            {
                Windows = PartEntry;
            }
        }
    }

    if (!Windows)
    {
        SetCursor(OldCursor);
        return;
    }

    TypeState.Found = TRUE;
    TypeState.DiskNumber = Windows->DiskEntry->DiskNumber;
    TypeState.StartSector = Windows->StartSector.QuadPart;
    DescribeDiskRegion(Windows, TypeState.Region, ARRAYSIZE(TypeState.Region));

    NameId = (Build >= TYPE_WINDOWS11_BUILD) ? IDS_TYPE_WINDOWS11 :
             (Build >= TYPE_WINDOWS10_BUILD) ? IDS_TYPE_WINDOWS10 : IDS_TYPE_WINDOWS;
    LoadStringW(pSetupData->hInstance, NameId, pSetupData->WindowsName, ARRAYSIZE(pSetupData->WindowsName));

    RtlInitUnicodeString(&VolumePath, Windows->Volume->Info.DeviceName);
    if (IsWindowsHibernated(Windows->Volume->Info.DeviceName))
    {
        TypeState.Problem = IDS_TYPE_HIBERNATED;
    }
    else if (!NT_SUCCESS(VolResizeQuery(&VolumePath, &Info)))
    {
        TypeState.Problem = IDS_TYPE_NORESIZE;
    }
    else
    {
        TypeState.WindowsMb = Info.PartitionBytes / TYPE_MEGABYTE;
        TypeState.FreeAfterMb = GetFollowingFreeBytes(Windows) / TYPE_MEGABYTE;
        TypeState.MinWindowsMb = TypeState.WindowsMb -
                                 min(TypeState.WindowsMb, Info.MaxReclaimableBytes / TYPE_MEGABYTE);
        if (TypeState.WindowsMb + TypeState.FreeAfterMb >= TypeState.MinWindowsMb + TYPE_MIN_LIBERNT_MB)
        {
            TypeState.MaxWindowsMb = min(TypeState.WindowsMb,
                                         TypeState.WindowsMb + TypeState.FreeAfterMb - TYPE_MIN_LIBERNT_MB);
        }
        if (TypeState.MaxWindowsMb < TypeState.MinWindowsMb || TypeState.MaxWindowsMb == 0)
            TypeState.Problem = IDS_TYPE_NOSPACE;
    }
    SetCursor(OldCursor);
}

static ULONGLONG
GetSplitWindowsMb(
    _In_ HWND hwndDlg)
{
    return (ULONGLONG)SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_GETPOS, 0, 0);
}

static VOID
UpdateSplitLabels(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    ULONGLONG WindowsMb = GetSplitWindowsMb(hwndDlg);
    WCHAR Name[32];
    WCHAR Size[32];
    WCHAR Text[96];

    FormatDiskSize(WindowsMb * TYPE_MEGABYTE, Size, ARRAYSIZE(Size));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_SPLITSIZE, pSetupData->WindowsName, Size);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_WINSIZE, Text);

    LoadStringW(pSetupData->hInstance, IDS_TYPE_LIBERNT, Name, ARRAYSIZE(Name));
    FormatDiskSize((TypeState.WindowsMb + TypeState.FreeAfterMb - WindowsMb) * TYPE_MEGABYTE, Size, ARRAYSIZE(Size));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_SPLITSIZE, Name, Size);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_LNTSIZE, Text);
}

static VOID
PaintSplitChannel(
    _In_ HWND hTrack,
    _In_ HDC hdc,
    _In_ const RECT *Channel)
{
    BOOL Enabled = IsWindowEnabled(hTrack);
    RECT Thumb, Left, Right;
    HBRUSH Brush;

    SendMessageW(hTrack, TBM_GETTHUMBRECT, 0, (LPARAM)&Thumb);
    Left = *Channel;
    InflateRect(&Left, 0, 3);
    Right = Left;
    Left.right = Right.left = (Thumb.left + Thumb.right) / 2;

    Brush = CreateSolidBrush(Enabled ? TYPE_WINDOWS_COLOR : GetSysColor(COLOR_BTNSHADOW));
    FillRect(hdc, &Left, Brush);
    DeleteObject(Brush);

    Brush = CreateSolidBrush(Enabled ? TYPE_LIBERNT_COLOR : GetSysColor(COLOR_3DLIGHT));
    FillRect(hdc, &Right, Brush);
    DeleteObject(Brush);
}

static VOID
UpdateTypeControls(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    BOOL Alongside = IsDlgButtonChecked(hwndDlg, IDC_TYPE_ALONGSIDE) == BST_CHECKED;
    BOOL CanResize = TypeState.Found && !TypeState.Problem && !TypeState.Planned;
    BOOL Usable = CanResize || TypeState.Planned;
    BOOL WindowsFirst = IsDlgButtonChecked(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS) == BST_CHECKED;
    WCHAR Text[512];
    WCHAR Size[32];
    WCHAR Install[32];

    EnableDlgItem(hwndDlg, IDC_TYPE_ALONGSIDE, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_SPLIT, Alongside && CanResize);
    EnableDlgItem(hwndDlg, IDC_TYPE_WINSIZE, Alongside && CanResize);
    EnableDlgItem(hwndDlg, IDC_TYPE_LNTSIZE, Alongside && CanResize);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_LABEL, Alongside && Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_LIBERNT, Alongside && Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS, Alongside && Usable);
    ShowDlgItem(hwndDlg, IDC_TYPE_WARNICON, (Alongside && Usable && WindowsFirst) ? SW_SHOW : SW_HIDE);
    ShowDlgItem(hwndDlg, IDC_TYPE_WARNING, (Alongside && Usable && WindowsFirst) ? SW_SHOW : SW_HIDE);

    Text[0] = UNICODE_NULL;
    if (TypeState.Problem == IDS_TYPE_HIBERNATED)
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_HIBERNATED,
                    pSetupData->WindowsName, pSetupData->WindowsName);
    }
    else if (TypeState.Problem == IDS_TYPE_NOSPACE)
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_NOSPACE,
                    pSetupData->WindowsName, TYPE_MIN_LIBERNT_MB / 1024);
    }
    else if (TypeState.Problem)
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), TypeState.Problem, pSetupData->WindowsName);
    }
    else if (TypeState.Planned && InstallPartition)
    {
        PPARTENTRY Windows = FindPartitionAt(pSetupData->PartitionList, TypeState.DiskNumber, TypeState.StartSector);

        FormatDiskSize(Windows ? GetPartEntrySizeInBytes(Windows) : 0, Size, ARRAYSIZE(Size));
        FormatDiskSize(GetPartEntrySizeInBytes(InstallPartition), Install, ARRAYSIZE(Install));
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_PLANNED, pSetupData->WindowsName, Size, Install);
    }
    else if (Alongside)
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_SHRINKNOTE, pSetupData->WindowsName);
    }
    SetDlgItemTextW(hwndDlg, IDC_TYPE_NOTE, Text);

    SetupFrameSetButtons(hwndDlg, TRUE, !Alongside || Usable);
}

static VOID
InitTypePage(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    BOOL Alongside = TypeState.Planned || (!TypeState.Problem && (pSetupData->bAlongside ||
                                          IsDlgButtonChecked(hwndDlg, IDC_TYPE_MANUAL) != BST_CHECKED));
    ULONGLONG Default;
    WCHAR Text[256];

    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_FOUND, pSetupData->WindowsName, TypeState.Region);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_FOUND, Text);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_ALONGSIDE, pSetupData->WindowsName);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_ALONGSIDE, Text);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS, pSetupData->WindowsName);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_WARNING, pSetupData->WindowsName);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_WARNING, Text);

    CheckRadioButton(hwndDlg, IDC_TYPE_ALONGSIDE, IDC_TYPE_MANUAL,
                     Alongside ? IDC_TYPE_ALONGSIDE : IDC_TYPE_MANUAL);
    CheckRadioButton(hwndDlg, IDC_TYPE_DEFAULT_LIBERNT, IDC_TYPE_DEFAULT_WINDOWS,
                     pSetupData->bWindowsFirst ? IDC_TYPE_DEFAULT_WINDOWS : IDC_TYPE_DEFAULT_LIBERNT);

    if (!TypeState.Planned && !TypeState.Problem)
    {
        Default = TypeState.MaxWindowsMb - (TypeState.MaxWindowsMb - TypeState.MinWindowsMb) / 2;
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETRANGEMIN, FALSE, (LPARAM)TypeState.MinWindowsMb);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETRANGEMAX, FALSE, (LPARAM)TypeState.MaxWindowsMb);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETLINESIZE, 0, TYPE_LINE_MB);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETPAGESIZE, 0, TYPE_PAGE_MB);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETPOS, TRUE, (LPARAM)Default);
        UpdateSplitLabels(hwndDlg, pSetupData);
    }
    else if (!TypeState.Planned)
    {
        SetDlgItemTextW(hwndDlg, IDC_TYPE_WINSIZE, L"");
        SetDlgItemTextW(hwndDlg, IDC_TYPE_LNTSIZE, L"");
    }

    UpdateTypeControls(hwndDlg, pSetupData);
}

static BOOLEAN
ApplyInstallType(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    ULONGLONG WindowsMb, LiberNtMb;
    WCHAR Title[128];
    WCHAR Text[512];
    WCHAR Before[32], After[32], Install[32];

    if (IsDlgButtonChecked(hwndDlg, IDC_TYPE_ALONGSIDE) != BST_CHECKED)
    {
        pSetupData->bAlongside = FALSE;
        pSetupData->bWindowsFirst = FALSE;
        TypeState.Planned = FALSE;
        return TRUE;
    }

    pSetupData->bWindowsFirst = IsDlgButtonChecked(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS) == BST_CHECKED;
    if (TypeState.Planned)
    {
        pSetupData->bAlongside = TRUE;
        return TRUE;
    }

    if (!TypeState.Found || TypeState.Problem)
        return FALSE;

    WindowsMb = GetSplitWindowsMb(hwndDlg);
    LiberNtMb = TypeState.WindowsMb + TypeState.FreeAfterMb - WindowsMb;
    FormatDiskSize(TypeState.WindowsMb * TYPE_MEGABYTE, Before, ARRAYSIZE(Before));
    FormatDiskSize(WindowsMb * TYPE_MEGABYTE, After, ARRAYSIZE(After));
    FormatDiskSize(LiberNtMb * TYPE_MEGABYTE, Install, ARRAYSIZE(Install));
    LoadPrintfW(Title, ARRAYSIZE(Title), IDS_TYPE_CONFIRM_TITLE, pSetupData->WindowsName);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_CONFIRM, pSetupData->WindowsName, Before, After, Install);
    if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, IDNO,
                     Title, Text) != IDYES)
    {
        return FALSE;
    }

    if (!PlanAlongsideInstall(hwndDlg, pSetupData, TypeState.DiskNumber, TypeState.StartSector,
                              (TypeState.WindowsMb - WindowsMb) * TYPE_MEGABYTE))
    {
        FindWindowsInstallation(pSetupData);
        InitTypePage(hwndDlg, pSetupData);
        return FALSE;
    }

    TypeState.Planned = TRUE;
    pSetupData->bAlongside = TRUE;
    return TRUE;
}

INT_PTR
CALLBACK
InstallTypePageProc(
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
            HICON Icon = NULL;

            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            FromImage = IsRunningFromImage();
            if (SUCCEEDED(LoadIconWithScaleDown(NULL, IDI_WARNING,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON),
                                                &Icon)))
            {
                SendDlgItemMessageW(hwndDlg, IDC_TYPE_WARNICON, STM_SETICON, (WPARAM)Icon, 0);
            }
            return FALSE;
        }

        case WM_DESTROY:
        {
            HICON Icon = (HICON)SendDlgItemMessageW(hwndDlg, IDC_TYPE_WARNICON, STM_GETICON, 0, 0);

            if (Icon)
                DestroyIcon(Icon);
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED &&
                (LOWORD(wParam) == IDC_TYPE_ALONGSIDE || LOWORD(wParam) == IDC_TYPE_MANUAL ||
                 LOWORD(wParam) == IDC_TYPE_DEFAULT_LIBERNT || LOWORD(wParam) == IDC_TYPE_DEFAULT_WINDOWS))
            {
                if (LOWORD(wParam) == IDC_TYPE_ALONGSIDE || LOWORD(wParam) == IDC_TYPE_MANUAL)
                    CheckRadioButton(hwndDlg, IDC_TYPE_ALONGSIDE, IDC_TYPE_MANUAL, LOWORD(wParam));
                UpdateTypeControls(hwndDlg, pSetupData);
                InvalidateRect(GetDlgItem(hwndDlg, IDC_TYPE_SPLIT), NULL, TRUE);
                return TRUE;
            }
            break;

        case WM_HSCROLL:
            if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_TYPE_SPLIT))
            {
                UpdateSplitLabels(hwndDlg, pSetupData);
                return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            LPNMCUSTOMDRAW Draw = (LPNMCUSTOMDRAW)lParam;
            LRESULT Result = CDRF_DODEFAULT;

            if (Draw->hdr.idFrom != IDC_TYPE_SPLIT || Draw->hdr.code != NM_CUSTOMDRAW)
                break;

            if (Draw->dwDrawStage == CDDS_PREPAINT)
            {
                Result = CDRF_NOTIFYITEMDRAW;
            }
            else if (Draw->dwDrawStage == CDDS_ITEMPREPAINT && Draw->dwItemSpec == TBCD_CHANNEL)
            {
                PaintSplitChannel(Draw->hdr.hwndFrom, Draw->hdc, &Draw->rc);
                Result = CDRF_SKIPDEFAULT;
            }
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, Result);
            return TRUE;
        }

        case SPN_QUERYSKIP:
            if (FromImage && !TypeState.Planned)
                FindWindowsInstallation(pSetupData);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, !TypeState.Found);
            return TRUE;

        case SPN_SETACTIVE:
            InitTypePage(hwndDlg, pSetupData);
            SetFocus(GetDlgItem(hwndDlg, IsDlgButtonChecked(hwndDlg, IDC_TYPE_ALONGSIDE) == BST_CHECKED ?
                                         IDC_TYPE_ALONGSIDE : IDC_TYPE_MANUAL));
            return TRUE;

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, ApplyInstallType(hwndDlg, pSetupData) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}
