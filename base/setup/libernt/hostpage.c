/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pages of Setup started on Windows: install LiberNT alongside it or upgrade LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <winioctl.h>
#include <objbase.h>
#include <ndk/setypes.h>

#include "resource.h"
#include "host.h"
#include "update.h"

#define NDEBUG
#include <debug.h>

#define HOST_MAX_LIBERNT        8
#define HOST_MEGABYTE           (1024ULL * 1024ULL)
#define HOST_MIN_LIBERNT_MB     (16ULL * 1024ULL)
#define HOST_LINE_MB            1024
#define HOST_PAGE_MB            (10 * 1024)
#define HOST_WINDOWS11_BUILD    22000
#define HOST_WINDOWS10_BUILD    10240
#define HOST_STAGE_MARGIN       (64ULL * 1024ULL * 1024ULL)
#define HOST_ESP_MARGIN         (256ULL * 1024ULL)
#define HOST_RESTART_STEPS      300
#define HOST_RESTART_TIMER      1
#define HOST_PARTITION_LABEL    L"LiberNT"
#define HOST_STORAGE_VOLUME_ERRORS 42008
#define PM_HOST_STEP            (WM_APP + 0x300)
#define PM_HOST_PROGRESS        (WM_APP + 0x301)
#define PM_HOST_DONE            (WM_APP + 0x302)
#define PM_HOST_SIZES           (WM_APP + 0x303)

typedef enum _HOST_PAGE
{
    HOST_PAGE_START,
    HOST_PAGE_TYPE,
    HOST_PAGE_READY,
    HOST_PAGE_PROCESS,
    HOST_PAGE_DONE,
    HOST_PAGE_COUNT
} HOST_PAGE;

typedef struct _HOST_SETUP
{
    WCHAR SourceRoot[MAX_PATH];
    WCHAR SourceVersion[HOST_VERSION_CHARS];
    ULONG SourceBuild;
    WORD Machine;
    ULONGLONG ImageBytes;
    HOST_PARTITION Windows;
    HOST_PARTITION System;
    HOST_PARTITION Prepared;
    HOST_PARTITION Store;
    BOOL HavePrepared;
    volatile LONG SizesReady;
    HWND hwndType;
    UINT Problem;
    DWORD SizeError;
    ULONGLONG WindowsMb;
    ULONGLONG FreeAfterBytes;
    ULONGLONG MinWindowsMb;
    ULONGLONG MaxWindowsMb;
    ULONGLONG EspBytes;
    ULONG LiberNTCount;
    HOST_PARTITION LiberNT[HOST_MAX_LIBERNT];
    WCHAR LiberNTVersions[HOST_MAX_LIBERNT][HOST_VERSION_CHARS];
    ULONG Selected;
    BOOL Upgrade;
    ULONGLONG WindowsBytes;
    BOOL WindowsFirst;
    HWND hwndProcess;
    UINT Step;
    DWORD Error;
    BOOL Auto;
    BOOL AutoUpgrade;
    BOOL NoReboot;
    ULONGLONG AutoSizeMb;
    WCHAR Unattend[MAX_PATH];
} HOST_SETUP;

static HOST_SETUP Host;

static INT_PTR CALLBACK HostStartPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK HostTypePageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK HostReadyPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK HostProcessPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK HostDonePageProc(HWND, UINT, WPARAM, LPARAM);

static const SETUP_PAGE_INFO HostPages[HOST_PAGE_COUNT] =
{
    { IDD_UPGRADEPAGE,     HostStartPageProc,   IDS_HOST_STARTTITLE,   IDS_HOST_STARTSUBTITLE,   SETUP_PAGE_OPTIONAL },
    { IDD_INSTALLTYPEPAGE, HostTypePageProc,    IDS_TYPETITLE,         IDS_TYPESUBTITLE,
      SETUP_PAGE_BACK | SETUP_PAGE_OPTIONAL },
    { IDD_READYPAGE,       HostReadyPageProc,   IDS_READYTITLE,        IDS_HOST_READYSUBTITLE,   SETUP_PAGE_BACK },
    { IDD_PROCESSPAGE,     HostProcessPageProc, IDS_HOST_PROCESSTITLE, IDS_HOST_PROCESSSUBTITLE, SETUP_PAGE_NO_CANCEL },
    { IDD_FINISHPAGE,      HostDonePageProc,    IDS_HOST_DONETITLE,    0,                        SETUP_PAGE_CLOSE },
};

static VOID
HostMessage(
    _In_opt_ HWND hwndOwner,
    _In_ PCWSTR Icon,
    _In_ PCWSTR Text)
{
    WCHAR Title[128];

    HostLog(L"%s", Text);
    if (Host.Auto)
    {
        if (Host.Error == ERROR_SUCCESS)
            Host.Error = ERROR_NOT_SUPPORTED;
        return;
    }
    LoadStringW(GetModuleHandleW(NULL), IDS_HOST_CANNOTTITLE, Title, ARRAYSIZE(Title));
    SetupMessage(hwndOwner, Icon, TDCBF_CLOSE_BUTTON, IDCLOSE, Title, Text);
}

static VOID
HostProblem(
    _In_opt_ HWND hwndOwner,
    _In_ UINT MessageId,
    ...)
{
    WCHAR Format[512], Text[1024];
    va_list Args;

    Text[0] = UNICODE_NULL;
    if (LoadStringW(GetModuleHandleW(NULL), MessageId, Format, ARRAYSIZE(Format)))
    {
        va_start(Args, MessageId);
        StringCchVPrintfW(Text, ARRAYSIZE(Text), Format, Args);
        va_end(Args);
    }
    HostMessage(hwndOwner, TD_ERROR_ICON, Text);
}

static INT
HostAsk(
    _In_opt_ HWND hwndOwner,
    _In_ PCWSTR Icon,
    _In_ PCWSTR Title,
    _In_ PCWSTR Text,
    _In_reads_(Count) const TASKDIALOG_BUTTON* Buttons,
    _In_ UINT Count)
{
    TASKDIALOGCONFIG Config;
    INT Button = IDCANCEL;

    ZeroMemory(&Config, sizeof(Config));
    Config.cbSize = sizeof(Config);
    Config.hwndParent = hwndOwner;
    Config.hInstance = GetModuleHandleW(NULL);
    Config.dwFlags = TDF_USE_COMMAND_LINKS | (hwndOwner ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
    Config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    Config.pszWindowTitle = MAKEINTRESOURCEW(IDS_CAPTION);
    Config.pszMainIcon = Icon;
    Config.pszMainInstruction = Title;
    Config.pszContent = Text;
    Config.pButtons = Buttons;
    Config.cButtons = Count;
    Config.nDefaultButton = Count ? Buttons[0].nButtonID : IDCANCEL;

    if (FAILED(TaskDialogIndirect(&Config, &Button, NULL, NULL)))
        return IDCANCEL;
    return Button;
}

static BOOL
IsElevated(VOID)
{
    TOKEN_ELEVATION Elevation;
    HANDLE Token;
    DWORD Size;
    BOOL Elevated = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return FALSE;
    if (GetTokenInformation(Token, TokenElevation, &Elevation, sizeof(Elevation), &Size))
        Elevated = Elevation.TokenIsElevated != 0;
    CloseHandle(Token);
    return Elevated;
}

static VOID
DescribePartition(
    _In_ PHOST_PARTITION Partition,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    LoadPrintfW(Text, cchText, IDS_DISK_PARTITION, Partition->DiskNumber, Partition->PartitionNumber);
}

static VOID
LoadWindowsName(
    _Inout_ PSETUPDATA pSetupData)
{
    RTL_OSVERSIONINFOW Version;
    UINT NameId = IDS_TYPE_WINDOWS;

    Version.dwOSVersionInfoSize = sizeof(Version);
    if (NT_SUCCESS(RtlGetVersion(&Version)))
    {
        NameId = (Version.dwBuildNumber >= HOST_WINDOWS11_BUILD) ? IDS_TYPE_WINDOWS11 :
                 (Version.dwBuildNumber >= HOST_WINDOWS10_BUILD) ? IDS_TYPE_WINDOWS10 : IDS_TYPE_WINDOWS;
    }
    LoadStringW(pSetupData->hInstance, NameId, pSetupData->WindowsName, ARRAYSIZE(pSetupData->WindowsName));
}

static ULONGLONG
GetSourceFileSize(
    _In_ PCWSTR Relative)
{
    WIN32_FILE_ATTRIBUTE_DATA Data;
    WCHAR Path[MAX_PATH];

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s%s", Host.SourceRoot, Relative)) ||
        !GetFileAttributesExW(Path, GetFileExInfoStandard, &Data))
    {
        return 0;
    }
    return ((ULONGLONG)Data.nFileSizeHigh << 32) | Data.nFileSizeLow;
}

static BOOL
CheckSource(
    _Inout_ PSETUPDATA pSetupData)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(GetModuleHandleW(NULL));
    USHORT ProcessMachine, NativeMachine;
    WCHAR Path[MAX_PATH];
    WORD KernelMachine;
    PWSTR Slash;

    if (!Headers)
        return FALSE;
    Host.Machine = Headers->FileHeader.Machine;
    if (IsWow64Process2(GetCurrentProcess(), &ProcessMachine, &NativeMachine) && NativeMachine != Host.Machine)
    {
        HostProblem(NULL, IDS_HOST_WRONGARCH);
        return FALSE;
    }

    if (!GetModuleFileNameW(NULL, Host.SourceRoot, ARRAYSIZE(Host.SourceRoot)) ||
        !(Slash = wcsrchr(Host.SourceRoot, L'\\')))
    {
        return FALSE;
    }
    Slash[1] = UNICODE_NULL;

    if (wcslen(Host.SourceRoot) != 3 || GetDriveTypeW(Host.SourceRoot) != DRIVE_CDROM ||
        HostReadImageSize(Host.SourceRoot, &Host.ImageBytes) != ERROR_SUCCESS ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sWindows\\System32\\ntoskrnl.exe", Host.SourceRoot)) ||
        !GetKernelMachine(Path, &KernelMachine) || !IsLiberNtKernel(Path))
    {
        HostLog(L"Setup was not started from a LiberNT image (%s)", Host.SourceRoot);
        HostProblem(NULL, IDS_HOST_NOTMEDIA);
        return FALSE;
    }
    if (KernelMachine != Host.Machine || !HostGetRemovableBootName(Host.Machine))
    {
        HostProblem(NULL, IDS_HOST_WRONGARCH);
        return FALSE;
    }

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sWindows\\%s", Host.SourceRoot, UPGRADE_INVENTORY_PATH)) ||
        !ReadInventoryVersion(Path, Host.SourceVersion, ARRAYSIZE(Host.SourceVersion)))
    {
        LoadStringW(pSetupData->hInstance, IDS_TYPE_LIBERNT, Host.SourceVersion, ARRAYSIZE(Host.SourceVersion));
    }
    Host.SourceBuild = ReadInventoryBuild(Path);

    Host.EspBytes = GetSourceFileSize(L"Windows\\system32\\winload.efi") + HOST_ESP_MARGIN;
    Path[0] = UNICODE_NULL;
    if (SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"efi\\boot\\%s", HostGetRemovableBootName(Host.Machine))))
        Host.EspBytes += GetSourceFileSize(Path);
    HostLog(L"Image %s: %I64u bytes, %s, build %lu", Host.SourceRoot, Host.ImageBytes, Host.SourceVersion,
            Host.SourceBuild);
    return TRUE;
}

static BOOL
CheckSecureBoot(
    _Inout_ PSETUPDATA pSetupData)
{
    TASKDIALOG_BUTTON Button;
    WCHAR Title[128], Text[512], Label[96];

    if (!HostIsSecureBootOn())
        return TRUE;
    if (Host.Auto)
    {
        HostProblem(NULL, IDS_HOST_SECUREBOOT);
        return FALSE;
    }

    LoadStringW(pSetupData->hInstance, IDS_HOST_SECUREBOOT_TITLE, Title, ARRAYSIZE(Title));
    LoadStringW(pSetupData->hInstance, IDS_HOST_SECUREBOOT, Text, ARRAYSIZE(Text));
    LoadStringW(pSetupData->hInstance, IDS_HOST_FIRMWAREBTN, Label, ARRAYSIZE(Label));
    Button.nButtonID = IDYES;
    Button.pszButtonText = Label;
    if (HostAsk(NULL, TD_WARNING_ICON, Title, Text, &Button, HostIsFirmwareSetupSupported() ? 1 : 0) == IDYES)
    {
        if (HostSetFirmwareSetupRequest())
            pSetupData->bMustReboot = TRUE;
        else
            HostLog(L"The firmware settings cannot be requested, error %lu", GetLastError());
    }
    return FALSE;
}

static DWORD
UndoPreparation(VOID)
{
    ULONGLONG MinBytes, MaxBytes;
    DWORD Error, Result;

    Result = HostRemoveSetupBoot(&Host.System);
    Error = HostDeletePartition(&Host.Prepared);
    if (Error == ERROR_SUCCESS)
    {
        Error = HostGetSupportedSize(&Host.Windows, &MinBytes, &MaxBytes);
        if (Error == ERROR_SUCCESS && MaxBytes > Host.Windows.Length)
            Error = HostResizePartition(&Host.Windows, MaxBytes);
    }
    HostLog(L"Preparation undone: boot %lu, partition %lu", Result, Error);
    return Error != ERROR_SUCCESS ? Error : Result;
}

static BOOL
CheckPreparation(
    _Inout_ PSETUPDATA pSetupData)
{
    TASKDIALOG_BUTTON Buttons[2];
    WCHAR Title[128], Text[512], Continue[96], Undo[96], Size[32];
    DWORD Error;

    if (HostFindPreparedPartition(&Host.Prepared) != ERROR_SUCCESS)
        return TRUE;
    HostLog(L"A partition was prepared earlier on %s", Host.Prepared.DeviceName);
    if (Host.Auto)
    {
        Host.HavePrepared = !Host.AutoUpgrade;
        return TRUE;
    }

    FormatDiskSize(Host.Prepared.Length, Size, ARRAYSIZE(Size));
    LoadStringW(pSetupData->hInstance, IDS_HOST_PENDING_TITLE, Title, ARRAYSIZE(Title));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_PENDING, Size, pSetupData->WindowsName);
    LoadStringW(pSetupData->hInstance, IDS_HOST_CONTINUEBTN, Continue, ARRAYSIZE(Continue));
    LoadStringW(pSetupData->hInstance, IDS_HOST_UNDOBTN, Undo, ARRAYSIZE(Undo));
    Buttons[0].nButtonID = IDYES;
    Buttons[0].pszButtonText = Continue;
    Buttons[1].nButtonID = IDNO;
    Buttons[1].pszButtonText = Undo;

    switch (HostAsk(NULL, TD_INFORMATION_ICON, Title, Text, Buttons, ARRAYSIZE(Buttons)))
    {
        case IDYES:
            Host.HavePrepared = TRUE;
            Host.Upgrade = FALSE;
            return TRUE;

        case IDNO:
            Error = UndoPreparation();
            if (Error == ERROR_SUCCESS)
            {
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_UNDONE, pSetupData->WindowsName);
                SetupMessage(NULL, TD_INFORMATION_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
            }
            else
            {
                HostProblem(NULL, IDS_HOST_UNDOFAILED, Error, Error, pSetupData->WindowsName);
            }
            return FALSE;
    }
    return FALSE;
}

static BOOL
CheckComputer(
    _Inout_ PSETUPDATA pSetupData)
{
    FIRMWARE_TYPE Firmware = FirmwareTypeUnknown;
    DWORD Error;

    if (!IsElevated())
    {
        HostProblem(NULL, IDS_HOST_NOTADMIN);
        return FALSE;
    }
    if (!CheckSource(pSetupData))
        return FALSE;

    LoadWindowsName(pSetupData);
    if (!GetFirmwareType(&Firmware) || Firmware != FirmwareTypeUefi)
    {
        /* TODO: install from Windows started by a legacy BIOS (no firmware boot options to start LiberNT Setup) */
        HostProblem(NULL, IDS_HOST_NOUEFI);
        return FALSE;
    }

    Error = HostGetWindowsPartition(&Host.Windows);
    if (Error != ERROR_SUCCESS || Host.Windows.Style != PARTITION_STYLE_GPT)
    {
        /* TODO: Windows on an MBR disk started by UEFI */
        HostLog(L"The Windows partition is not usable: %lu, style %d", Error, Host.Windows.Style);
        HostProblem(NULL, IDS_HOST_NOGPT);
        return FALSE;
    }

    Error = HostGetSystemPartition(&Host.System);
    if (Error != ERROR_SUCCESS)
    {
        HostProblem(NULL, IDS_HOST_NOESP, Error);
        return FALSE;
    }

    if (!CheckSecureBoot(pSetupData))
        return FALSE;

    Host.LiberNTCount = HostFindLiberNT(Host.Machine, Host.LiberNT, Host.LiberNTVersions, HOST_MAX_LIBERNT);
    Host.Upgrade = Host.LiberNTCount != 0;
    Host.WindowsFirst = FALSE;
    return CheckPreparation(pSetupData);
}

static VOID
UpdateStartNote(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    BOOL Upgrade = IsDlgButtonChecked(hwndDlg, IDC_UPGRADE_KEEP) == BST_CHECKED;
    WCHAR Text[1024];

    EnableDlgItem(hwndDlg, IDC_UPGRADE_LIST, Upgrade);
    if (Upgrade)
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_UPGRADENOTE, Host.SourceVersion);
    else
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_INSTALLNOTE, pSetupData->WindowsName, Host.SourceVersion);
    SetDlgItemTextW(hwndDlg, IDC_UPGRADE_NOTE, Text);
}

static BOOL
CheckUpgradeTarget(
    _In_opt_ HWND hwndOwner,
    _In_ ULONG Index)
{
    PHOST_PARTITION Target = &Host.LiberNT[Index];
    WCHAR Region[96], Stage[32], Size[32], Path[MAX_PATH];
    ULARGE_INTEGER FreeBytes;

    DescribePartition(Target, Region, ARRAYSIZE(Region));
    if (IsWindowsHibernated(Target->DeviceName))
    {
        HostProblem(hwndOwner, IDS_UPGRADE_HIBERNATED, Region);
        return FALSE;
    }
    if (HostReadUpgradeStage(Target, Stage, ARRAYSIZE(Stage)) == ERROR_SUCCESS &&
        (_wcsicmp(Stage, L"write") == 0 || _wcsicmp(Stage, L"rollback") == 0))
    {
        HostProblem(hwndOwner, IDS_HOST_INUSE, Region);
        return FALSE;
    }
    if (HostFindLiberNTStore(Target, &Host.System, &Host.Store) != ERROR_SUCCESS)
    {
        HostProblem(hwndOwner, IDS_HOST_NOSTORE, Region);
        return FALSE;
    }
    if (HostPath(Target->DeviceName, L"\\", Path) && GetDiskFreeSpaceExW(Path, &FreeBytes, NULL, NULL) &&
        FreeBytes.QuadPart < Host.ImageBytes + HOST_STAGE_MARGIN)
    {
        FormatDiskSize(Host.ImageBytes + HOST_STAGE_MARGIN, Size, ARRAYSIZE(Size));
        HostProblem(hwndOwner, IDS_HOST_LOWSPACE, Region, Size);
        return FALSE;
    }
    return TRUE;
}

static INT_PTR CALLBACK
HostStartPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    WCHAR Region[96], Text[256];
    ULONG Index;
    LRESULT Selected;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            for (Index = 0; Index < Host.LiberNTCount; Index++)
            {
                DescribePartition(&Host.LiberNT[Index], Region, ARRAYSIZE(Region));
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPGRADE_ENTRY, Region, Host.LiberNTVersions[Index]);
                SendDlgItemMessageW(hwndDlg, IDC_UPGRADE_LIST, CB_ADDSTRING, 0, (LPARAM)Text);
            }
            SendDlgItemMessageW(hwndDlg, IDC_UPGRADE_LIST, CB_SETCURSEL, 0, 0);
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_INSTALLNEW, pSetupData->WindowsName);
            SetDlgItemTextW(hwndDlg, IDC_UPGRADE_NEW, Text);
            CheckRadioButton(hwndDlg, IDC_UPGRADE_KEEP, IDC_UPGRADE_NEW, IDC_UPGRADE_KEEP);
            return FALSE;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED &&
                (LOWORD(wParam) == IDC_UPGRADE_KEEP || LOWORD(wParam) == IDC_UPGRADE_NEW))
            {
                CheckRadioButton(hwndDlg, IDC_UPGRADE_KEEP, IDC_UPGRADE_NEW, LOWORD(wParam));
                UpdateStartNote(hwndDlg, pSetupData);
                return TRUE;
            }
            break;

        case SPN_QUERYSKIP:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, Host.LiberNTCount == 0 || Host.HavePrepared);
            return TRUE;

        case SPN_SETACTIVE:
            UpdateStartNote(hwndDlg, pSetupData);
            SetupFrameSetButtons(hwndDlg, FALSE, TRUE);
            return TRUE;

        case SPN_WIZNEXT:
            Host.Upgrade = IsDlgButtonChecked(hwndDlg, IDC_UPGRADE_KEEP) == BST_CHECKED;
            Selected = SendDlgItemMessageW(hwndDlg, IDC_UPGRADE_LIST, CB_GETCURSEL, 0, 0);
            Host.Selected = (Selected >= 0 && (ULONG)Selected < Host.LiberNTCount) ? (ULONG)Selected : 0;
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT,
                              (!Host.Upgrade || CheckUpgradeTarget(GetParent(hwndDlg), Host.Selected)) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}

static BOOL
HasEspSpace(VOID)
{
    ULARGE_INTEGER FreeBytes;
    WCHAR Path[MAX_PATH];

    return !HostPath(Host.System.DeviceName, L"\\", Path) ||
           !GetDiskFreeSpaceExW(Path, &FreeBytes, NULL, NULL) ||
           FreeBytes.QuadPart >= Host.EspBytes;
}

static VOID
QuerySizes(VOID)
{
    ULONGLONG MinBytes, MaxBytes;

    Host.Problem = 0;
    Host.WindowsMb = Host.Windows.Length / HOST_MEGABYTE;
    Host.FreeAfterBytes = HostGetFreeBytesAfter(&Host.Windows);
    Host.SizeError = HostGetSupportedSize(&Host.Windows, &MinBytes, &MaxBytes);
    if (Host.SizeError == HOST_STORAGE_VOLUME_ERRORS)
    {
        Host.Problem = IDS_HOST_VOLUMEERRORS;
    }
    else if (Host.SizeError != ERROR_SUCCESS)
    {
        Host.Problem = IDS_HOST_NORESIZE;
    }
    else
    {
        Host.MinWindowsMb = min(Host.WindowsMb, (MinBytes + HOST_MEGABYTE - 1) / HOST_MEGABYTE);
        Host.MaxWindowsMb = 0;
        if (Host.WindowsMb + Host.FreeAfterBytes / HOST_MEGABYTE >= Host.MinWindowsMb + HOST_MIN_LIBERNT_MB)
        {
            Host.MaxWindowsMb = min(Host.WindowsMb,
                                    Host.WindowsMb + Host.FreeAfterBytes / HOST_MEGABYTE - HOST_MIN_LIBERNT_MB);
        }
        if (Host.MaxWindowsMb < Host.MinWindowsMb || Host.MaxWindowsMb == 0)
            Host.Problem = IDS_HOST_NOSPACE;
    }
    if (!Host.Problem && !HasEspSpace())
        Host.Problem = IDS_HOST_ESPFULL;
    HostLog(L"Windows %I64u MB, %I64u MB free after it, can shrink to %I64u MB: problem %u", Host.WindowsMb,
            Host.FreeAfterBytes / HOST_MEGABYTE, Host.MinWindowsMb, Host.Problem);
}

static DWORD
WINAPI
SizeThread(
    _In_ LPVOID Parameter)
{
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HWND hwndType;

    UNREFERENCED_PARAMETER(Parameter);
    QuerySizes();
    if (SUCCEEDED(hrCom))
        CoUninitialize();
    InterlockedExchange(&Host.SizesReady, TRUE);
    hwndType = InterlockedCompareExchangePointer((PVOID*)&Host.hwndType, NULL, NULL);
    if (hwndType)
        PostMessageW(hwndType, PM_HOST_SIZES, 0, 0);
    return 0;
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
    WCHAR Name[32], Size[32], Text[96];

    FormatDiskSize(WindowsMb * HOST_MEGABYTE, Size, ARRAYSIZE(Size));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_SPLITSIZE, pSetupData->WindowsName, Size);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_WINSIZE, Text);

    LoadStringW(pSetupData->hInstance, IDS_TYPE_LIBERNT, Name, ARRAYSIZE(Name));
    FormatDiskSize((Host.WindowsMb - WindowsMb) * HOST_MEGABYTE + Host.FreeAfterBytes, Size, ARRAYSIZE(Size));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_SPLITSIZE, Name, Size);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_LNTSIZE, Text);
}

static VOID
FormatSizeProblem(
    _In_ PSETUPDATA pSetupData,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    WCHAR Drive[4] = L"", Directory[MAX_PATH], Size[32];

    switch (Host.Problem)
    {
        case 0:
            LoadPrintfW(Text, cchText, IDS_HOST_SHRINKNOTE, pSetupData->WindowsName);
            break;
        case IDS_HOST_NOSPACE:
            LoadPrintfW(Text, cchText, IDS_HOST_NOSPACE, pSetupData->WindowsName, HOST_MIN_LIBERNT_MB / 1024,
                        pSetupData->WindowsName);
            break;
        case IDS_HOST_VOLUMEERRORS:
            if (GetSystemWindowsDirectoryW(Directory, ARRAYSIZE(Directory)))
                StringCchPrintfW(Drive, ARRAYSIZE(Drive), L"%.2s", Directory);
            LoadPrintfW(Text, cchText, IDS_HOST_VOLUMEERRORS, pSetupData->WindowsName, Drive);
            break;
        case IDS_HOST_NORESIZE:
            LoadPrintfW(Text, cchText, IDS_HOST_NORESIZE, pSetupData->WindowsName, Host.SizeError);
            break;
        case IDS_HOST_ESPFULL:
            FormatDiskSize(Host.EspBytes, Size, ARRAYSIZE(Size));
            LoadPrintfW(Text, cchText, IDS_HOST_ESPFULL, Size);
            break;
        default:
            Text[0] = UNICODE_NULL;
            break;
    }
}

static VOID
UpdateTypeControls(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    BOOL Usable = !Host.Problem;
    BOOL WindowsFirst = IsDlgButtonChecked(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS) == BST_CHECKED;
    WCHAR Text[512];

    EnableDlgItem(hwndDlg, IDC_TYPE_ALONGSIDE, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_SPLIT, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_WINSIZE, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_LNTSIZE, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_LABEL, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_LIBERNT, Usable);
    EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS, Usable);
    ShowDlgItem(hwndDlg, IDC_TYPE_WARNICON, (Usable && WindowsFirst) ? SW_SHOW : SW_HIDE);
    ShowDlgItem(hwndDlg, IDC_TYPE_WARNING, (Usable && WindowsFirst) ? SW_SHOW : SW_HIDE);

    FormatSizeProblem(pSetupData, Text, ARRAYSIZE(Text));
    SetDlgItemTextW(hwndDlg, IDC_TYPE_NOTE, Text);
    SetupFrameSetButtons(hwndDlg, Host.LiberNTCount != 0, Usable);
}

static VOID
InitTypePage(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    WCHAR Region[96], Drive[4] = L"", Text[256];
    ULONGLONG Default;

    if (GetSystemWindowsDirectoryW(Text, ARRAYSIZE(Text)))
        StringCchPrintfW(Drive, ARRAYSIZE(Drive), L"%.2s", Text);
    LoadPrintfW(Region, ARRAYSIZE(Region), IDS_DISK_PARTITION_LABEL, Host.Windows.DiskNumber,
                Host.Windows.PartitionNumber, Drive);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_FOUND, pSetupData->WindowsName, Region);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_FOUND, Text);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_ALONGSIDE, pSetupData->WindowsName);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_ALONGSIDE, Text);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS, pSetupData->WindowsName);
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_TYPE_WARNING, pSetupData->WindowsName);
    SetDlgItemTextW(hwndDlg, IDC_TYPE_WARNING, Text);
    ShowDlgItem(hwndDlg, IDC_TYPE_MANUAL, SW_HIDE);
    CheckDlgButton(hwndDlg, IDC_TYPE_ALONGSIDE, BST_CHECKED);
    CheckRadioButton(hwndDlg, IDC_TYPE_DEFAULT_LIBERNT, IDC_TYPE_DEFAULT_WINDOWS,
                     Host.WindowsFirst ? IDC_TYPE_DEFAULT_WINDOWS : IDC_TYPE_DEFAULT_LIBERNT);

    if (!InterlockedCompareExchange(&Host.SizesReady, 0, 0))
    {
        SetDlgItemTextW(hwndDlg, IDC_TYPE_WINSIZE, L"");
        SetDlgItemTextW(hwndDlg, IDC_TYPE_LNTSIZE, L"");
        EnableDlgItem(hwndDlg, IDC_TYPE_SPLIT, FALSE);
        EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_LIBERNT, FALSE);
        EnableDlgItem(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS, FALSE);
        ShowDlgItem(hwndDlg, IDC_TYPE_WARNICON, SW_HIDE);
        ShowDlgItem(hwndDlg, IDC_TYPE_WARNING, SW_HIDE);
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_CHECKINGSIZE, pSetupData->WindowsName);
        SetDlgItemTextW(hwndDlg, IDC_TYPE_NOTE, Text);
        SetupFrameSetButtons(hwndDlg, Host.LiberNTCount != 0, FALSE);
        return;
    }

    if (!Host.Problem)
    {
        Default = Host.WindowsBytes ? Host.WindowsBytes / HOST_MEGABYTE
                                    : Host.MaxWindowsMb - (Host.MaxWindowsMb - Host.MinWindowsMb) / 2;
        Default = max(Host.MinWindowsMb, min(Host.MaxWindowsMb, Default));
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETRANGEMIN, FALSE, (LPARAM)Host.MinWindowsMb);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETRANGEMAX, FALSE, (LPARAM)Host.MaxWindowsMb);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETLINESIZE, 0, HOST_LINE_MB);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETPAGESIZE, 0, HOST_PAGE_MB);
        SendDlgItemMessageW(hwndDlg, IDC_TYPE_SPLIT, TBM_SETPOS, TRUE, (LPARAM)Default);
        UpdateSplitLabels(hwndDlg, pSetupData);
    }
    else
    {
        SetDlgItemTextW(hwndDlg, IDC_TYPE_WINSIZE, L"");
        SetDlgItemTextW(hwndDlg, IDC_TYPE_LNTSIZE, L"");
    }
    UpdateTypeControls(hwndDlg, pSetupData);
}

static INT_PTR CALLBACK
HostTypePageProc(
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
            InterlockedExchangePointer((PVOID*)&Host.hwndType, hwndDlg);
            if (SUCCEEDED(LoadIconWithScaleDown(NULL, IDI_WARNING, GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), &Icon)))
            {
                SendDlgItemMessageW(hwndDlg, IDC_TYPE_WARNICON, STM_SETICON, (WPARAM)Icon, 0);
            }
            return FALSE;
        }

        case WM_DESTROY:
        {
            HICON Icon = (HICON)SendDlgItemMessageW(hwndDlg, IDC_TYPE_WARNICON, STM_GETICON, 0, 0);

            InterlockedExchangePointer((PVOID*)&Host.hwndType, NULL);
            if (Icon)
                DestroyIcon(Icon);
            break;
        }

        case PM_HOST_SIZES:
            if (IsWindowVisible(hwndDlg))
                InitTypePage(hwndDlg, pSetupData);
            return TRUE;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED &&
                (LOWORD(wParam) == IDC_TYPE_DEFAULT_LIBERNT || LOWORD(wParam) == IDC_TYPE_DEFAULT_WINDOWS))
            {
                UpdateTypeControls(hwndDlg, pSetupData);
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
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, Host.Upgrade || Host.HavePrepared);
            return TRUE;

        case SPN_SETACTIVE:
            InitTypePage(hwndDlg, pSetupData);
            SetFocus(GetDlgItem(hwndDlg, IDC_TYPE_SPLIT));
            return TRUE;

        case SPN_WIZNEXT:
            if (Host.Problem || !InterlockedCompareExchange(&Host.SizesReady, 0, 0))
            {
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
                return TRUE;
            }
            Host.WindowsBytes = GetSplitWindowsMb(hwndDlg) * HOST_MEGABYTE;
            Host.WindowsFirst = IsDlgButtonChecked(hwndDlg, IDC_TYPE_DEFAULT_WINDOWS) == BST_CHECKED;
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
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
    WCHAR Format[512], Line[1024];
    va_list Args;

    if (!LoadStringW(GetModuleHandleW(NULL), uID, Format, ARRAYSIZE(Format)))
        return;
    va_start(Args, uID);
    StringCchVPrintfW(Line, ARRAYSIZE(Line), Format, Args);
    va_end(Args);
    StringCchCatW(Text, cchText, Line);
    StringCchCatW(Text, cchText, L"\r\n");
}

static VOID
FillHostSummary(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    WCHAR Text[2048], Region[96], Before[32], After[32], Size[32], Image[32], LiberNT[32], Drive[4], Directory[MAX_PATH];
    ULONGLONG LiberNTBytes;

    Text[0] = UNICODE_NULL;
    FormatDiskSize(Host.ImageBytes, Image, ARRAYSIZE(Image));
    if (Host.Upgrade)
    {
        DescribePartition(&Host.LiberNT[Host.Selected], Region, ARRAYSIZE(Region));
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_UPGRADE, Region, Host.LiberNTVersions[Host.Selected],
                   Host.SourceVersion);
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_UPGRADE_KEEP);
        StringCchCatW(Text, ARRAYSIZE(Text), L"\r\n");
        AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_UPGRADE, Image);
    }
    else if (Host.HavePrepared)
    {
        FormatDiskSize(Host.Prepared.Length, Size, ARRAYSIZE(Size));
        AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_PREPARED, Size);
        StringCchCatW(Text, ARRAYSIZE(Text), L"\r\n");
        AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_RESTART, Image);
    }
    else
    {
        Drive[0] = UNICODE_NULL;
        if (GetSystemWindowsDirectoryW(Directory, ARRAYSIZE(Directory)))
            StringCchPrintfW(Drive, ARRAYSIZE(Drive), L"%.2s", Directory);
        if (Host.WindowsBytes < Host.Windows.Length)
        {
            FormatDiskSize(Host.Windows.Length, Before, ARRAYSIZE(Before));
            FormatDiskSize(Host.WindowsBytes, After, ARRAYSIZE(After));
            AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_SHRINK, pSetupData->WindowsName, Drive, Before, After);
        }
        LiberNTBytes = Host.Windows.Length - Host.WindowsBytes + Host.FreeAfterBytes;
        FormatDiskSize(LiberNTBytes, Size, ARRAYSIZE(Size));
        AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_CREATE, Size);
        LoadStringW(pSetupData->hInstance, IDS_TYPE_LIBERNT, LiberNT, ARRAYSIZE(LiberNT));
        AppendLine(Text, ARRAYSIZE(Text), IDS_READY_DEFAULTOS, Host.WindowsFirst ? pSetupData->WindowsName : LiberNT);
        StringCchCatW(Text, ARRAYSIZE(Text), L"\r\n");
        AppendLine(Text, ARRAYSIZE(Text), IDS_HOST_READY_RESTART, Image);
    }
    SetDlgItemTextW(hwndDlg, IDC_READY_SUMMARY, Text);
}

static INT_PTR CALLBACK
HostReadyPageProc(
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
                SetupFrameSetButtons(hwndDlg, TRUE, IsDlgButtonChecked(hwndDlg, IDC_CONFIRM_INSTALL) == BST_CHECKED);
                return TRUE;
            }
            break;

        case SPN_SETACTIVE:
            FillHostSummary(hwndDlg, pSetupData);
            CheckDlgButton(hwndDlg, IDC_CONFIRM_INSTALL, BST_UNCHECKED);
            if (Host.Upgrade)
                SetupFrameSetTitle(hwndDlg, IDS_UPGRADE_READYTITLE, IDS_UPGRADE_READYSUBTITLE);
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, Host.Upgrade ? IDS_UPGRADEBTN : IDS_INSTALLBTN);
            SetupFrameSetButtons(hwndDlg, TRUE, FALSE);
            SetFocus(GetDlgItem(hwndDlg, IDC_CONFIRM_INSTALL));
            return TRUE;

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT,
                              IsDlgButtonChecked(hwndDlg, IDC_CONFIRM_INSTALL) == BST_CHECKED ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}

static VOID
SetStep(
    _In_ UINT Step)
{
    WCHAR Text[128];

    Host.Step = Step;
    LoadPrintfW(Text, ARRAYSIZE(Text), Step, L"Windows");
    HostLog(L"%s", Text);
    if (Host.hwndProcess)
        PostMessageW(Host.hwndProcess, PM_HOST_STEP, Step, 0);
}

static BOOL
CALLBACK
ReportProgress(
    _In_opt_ PVOID Context,
    _In_ ULONGLONG Done,
    _In_ ULONGLONG Total)
{
    UNREFERENCED_PARAMETER(Context);
    if (!Host.hwndProcess)
        return TRUE;
    PostMessageW(Host.hwndProcess, PM_HOST_PROGRESS, Total ? (WPARAM)(Done * 100 / Total) : 0,
                 (LPARAM)(Done / HOST_MEGABYTE));
    return TRUE;
}

static DWORD
CopyAndCheckImage(
    _In_ PHOST_PARTITION Target,
    _Out_writes_(HOST_SHA256_CHARS) PWSTR Hash)
{
    WCHAR Image[MAX_PATH], Check[HOST_SHA256_CHARS];
    DWORD Error;

    if (!HostPath(Target->DeviceName, HOST_IMAGE_PATH, Image))
        return ERROR_BUFFER_OVERFLOW;
    SetStep(IDS_HOST_STEP_COPY);
    Error = HostCopyImage(Host.SourceRoot, Host.ImageBytes, Image, ReportProgress, NULL, Hash);
    if (Error != ERROR_SUCCESS)
        return Error;
    SetStep(IDS_HOST_STEP_CHECK);
    Error = HostHashFile(Image, ReportProgress, NULL, Check);
    if (Error == ERROR_SUCCESS && _wcsicmp(Check, Hash) != 0)
    {
        HostLog(L"The copied image has SHA-256 %s instead of %s", Check, Hash);
        Error = ERROR_CRC;
    }
    return Error;
}

static DWORD
WriteSetupFile(
    _In_ PSETUPDATA pSetupData,
    _In_ PHOST_PARTITION Target)
{
    WCHAR Path[MAX_PATH];
    DWORD Error = HostCreateStageDirectory(Target);

    if (Error != ERROR_SUCCESS)
        return Error;
    if (!HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\" HOST_SETUP_FILE, Path))
        return ERROR_BUFFER_OVERFLOW;
    if (!WritePrivateProfileStringW(HOST_SETUP_SECTION, L"WindowsFirst", Host.WindowsFirst ? L"1" : L"0", Path) ||
        !WritePrivateProfileStringW(HOST_SETUP_SECTION, L"WindowsName", pSetupData->WindowsName, Path) ||
        !WritePrivateProfileStringW(HOST_SETUP_SECTION, L"Image", HOST_IMAGE_PATH, Path) ||
        !WritePrivateProfileStringW(HOST_SETUP_SECTION, L"Version", Host.SourceVersion, Path))
    {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

static DWORD
PrepareInstall(
    _In_ PSETUPDATA pSetupData)
{
    HOST_PARTITION Target;
    WCHAR Hash[HOST_SHA256_CHARS], Path[MAX_PATH];
    ULONGLONG OriginalEnd, Start, Bytes;
    DWORD Error;

    if (Host.HavePrepared)
    {
        Target = Host.Prepared;
        Error = HostCreateStageDirectory(&Target);
    }
    else
    {
        OriginalEnd = Host.Windows.Offset + Host.Windows.Length + Host.FreeAfterBytes;
        if (Host.WindowsBytes < Host.Windows.Length)
        {
            SetStep(IDS_HOST_STEP_SHRINK);
            Error = HostResizePartition(&Host.Windows, Host.WindowsBytes);
            if (Error != ERROR_SUCCESS)
                return Error;
        }

        Start = ALIGN_UP_BY(Host.Windows.Offset + Host.Windows.Length, HOST_MEGABYTE);
        if (OriginalEnd <= Start)
            return ERROR_DISK_FULL;
        Bytes = ALIGN_DOWN_BY(OriginalEnd - Start, HOST_MEGABYTE);

        SetStep(IDS_HOST_STEP_CREATE);
        Error = HostCreatePartition(&Host.Windows, Bytes, &Target);
        if (Error != ERROR_SUCCESS)
            return Error;
        SetStep(IDS_HOST_STEP_FORMAT);
        Error = HostFormatPartition(&Target, HOST_PARTITION_LABEL);
        if (Error == ERROR_SUCCESS)
            Error = WriteSetupFile(pSetupData, &Target);
    }
    if (Error == ERROR_SUCCESS && Host.Unattend[0])
    {
        if (!HostPath(Target.DeviceName, L"\\unattend.inf", Path))
            Error = ERROR_BUFFER_OVERFLOW;
        else
            Error = HostCopyFileVerified(Host.Unattend, Path);
    }
    if (Error == ERROR_SUCCESS)
        Error = CopyAndCheckImage(&Target, Hash);
    if (Error != ERROR_SUCCESS)
        return Error;

    SetStep(IDS_HOST_STEP_BOOT);
    Error = HostInstallSetupBoot(Host.SourceRoot, Host.Machine, &Host.System, &Target);
    if (Error == ERROR_SUCCESS)
        Error = HostSetBootNext(&Host.System, HOST_SETUP_MANAGER, HOST_SETUP_DESCRIPTION);
    return Error;
}

static DWORD
PrepareUpgrade(VOID)
{
    PHOST_PARTITION Target = &Host.LiberNT[Host.Selected];
    WCHAR Hash[HOST_SHA256_CHARS], Source[MAX_PATH], Loader[MAX_PATH], Number[16];
    WCHAR EntryId[UPDATE_ID_CHARS], PreviousDefault[UPDATE_ID_CHARS];
    DWORD Error;

    SetStep(IDS_HOST_STEP_STAGE);
    Error = HostClearUpgradeStage(&Host.Store, Target);
    if (Error == ERROR_SUCCESS)
        Error = HostCreateStageDirectory(Target);
    if (Error == ERROR_SUCCESS)
        Error = CopyAndCheckImage(Target, Hash);

    if (Error == ERROR_SUCCESS)
    {
        SetStep(IDS_HOST_STEP_STAGE);
        if (FAILED(StringCchPrintfW(Source, ARRAYSIZE(Source), L"%sWindows\\system32\\winload.efi", Host.SourceRoot)) ||
            !HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\" UPDATE_STAGE_LOADER, Loader))
        {
            Error = ERROR_BUFFER_OVERFLOW;
        }
        else
        {
            Error = HostCopyFileVerified(Source, Loader);
        }
    }

    StringCchPrintfW(Number, ARRAYSIZE(Number), L"%lu", Host.SourceBuild);
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"TagId", L"0");
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"TagName", Host.SourceVersion);
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"Build", Number);
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"ImageSha256", Hash);
    if (Error == ERROR_SUCCESS)
        Error = HostAddUpgradeEntry(&Host.Store, Target, Hash, EntryId, PreviousDefault);
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"Entry", EntryId);
    if (Error == ERROR_SUCCESS)
        Error = HostWriteUpgradeRequest(Target, L"PreviousDefault", PreviousDefault);
    if (Error == ERROR_SUCCESS)
        Error = HostSetCompletionCommand(Target);
    if (Error == ERROR_SUCCESS)
        Error = HostSetBootNext(&Host.Store, HOST_LIBERNT_MANAGER, HOST_LIBERNT_DESCRIPTION);

    if (Error != ERROR_SUCCESS)
        HostLog(L"Upgrade staging failed with %lu; staged files removed: %lu", Error,
                HostClearUpgradeStage(&Host.Store, Target));
    return Error;
}

static DWORD
WINAPI
HostWorker(
    _In_ LPVOID Parameter)
{
    PSETUPDATA pSetupData = (PSETUPDATA)Parameter;
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    DWORD Error;

    Error = Host.Upgrade ? PrepareUpgrade() : PrepareInstall(pSetupData);
    if (SUCCEEDED(hrCom))
        CoUninitialize();
    HostLog(L"Setup on Windows finished step %u with %lu", Host.Step, Error);
    if (Host.hwndProcess)
        PostMessageW(Host.hwndProcess, PM_HOST_DONE, Error, 0);
    return Error;
}

static INT_PTR CALLBACK
HostProcessPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    WCHAR Text[128];
    HANDLE Thread;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            SetDlgItemTextW(hwndDlg, IDC_ACTIVITY, L"");
            SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
            SendDlgItemMessageW(hwndDlg, IDC_PROCESSPROGRESS, PBM_SETRANGE32, 0, 100);
            return FALSE;

        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, FALSE);
            EnableSetupCancel(GetParent(hwndDlg), FALSE);
            Host.hwndProcess = hwndDlg;
            Host.Error = ERROR_SUCCESS;
            Thread = CreateThread(NULL, 0, HostWorker, pSetupData, 0, NULL);
            if (Thread)
                CloseHandle(Thread);
            else
                PostMessageW(hwndDlg, PM_HOST_DONE, GetLastError(), 0);
            return TRUE;

        case PM_HOST_STEP:
            LoadPrintfW(Text, ARRAYSIZE(Text), (UINT)wParam, pSetupData->WindowsName);
            SetDlgItemTextW(hwndDlg, IDC_ACTIVITY, Text);
            SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
            SendDlgItemMessageW(hwndDlg, IDC_PROCESSPROGRESS, PBM_SETPOS, 0, 0);
            return TRUE;

        case PM_HOST_PROGRESS:
            SendDlgItemMessageW(hwndDlg, IDC_PROCESSPROGRESS, PBM_SETPOS, wParam, 0);
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_PROGRESS, (ULONGLONG)lParam, Host.ImageBytes / HOST_MEGABYTE);
            SetDlgItemTextW(hwndDlg, IDC_ITEM, Text);
            return TRUE;

        case PM_HOST_DONE:
            Host.Error = (DWORD)wParam;
            SetupFrameGoTo(hwndDlg, HOST_PAGE_DONE);
            return TRUE;
    }

    return FALSE;
}

static INT_PTR CALLBACK
HostDonePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    static HBITMAP hLogo;
    static SIZE LogoSize;
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    WCHAR Step[128], Text[512];

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

        case WM_TIMER:
        {
            HWND hProgress = GetDlgItem(hwndDlg, IDC_RESTART_PROGRESS);
            INT Position = (INT)SendMessageW(hProgress, PBM_GETPOS, 0, 0);

            if (Position >= HOST_RESTART_STEPS)
            {
                KillTimer(hwndDlg, HOST_RESTART_TIMER);
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
            BOOL Success = Host.Error == ERROR_SUCCESS;

            if (Success)
            {
                SetupFrameSetTitle(hwndDlg, Host.Upgrade ? IDS_HOST_UPGRADEDONETITLE : IDS_HOST_DONETITLE, 0);
                SetResText(GetDlgItem(hwndDlg, IDC_FINISHTEXT), Host.Upgrade ? IDS_HOST_UPGRADEDONE : IDS_HOST_DONE);
            }
            else
            {
                SetupFrameSetTitle(hwndDlg, IDS_HOST_FAILEDTITLE, 0);
                LoadPrintfW(Step, ARRAYSIZE(Step), Host.Step ? Host.Step : IDS_HOST_STEP_STAGE, pSetupData->WindowsName);
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_HOST_FAILED, Step, Host.Error, Host.Error);
                SetDlgItemTextW(hwndDlg, IDC_FINISHTEXT, Text);
            }
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, IDS_RESTARTBTN);
            SetupFrameSetButtonText(hwndDlg, IDCANCEL, Success ? IDS_POSTPONEBTN : IDS_CLOSEBTN);
            SetupFrameShowButtons(hwndDlg, FALSE, Success, TRUE);
            SetupFrameSetButtons(hwndDlg, FALSE, Success);
            EnableSetupCancel(GetParent(hwndDlg), TRUE);
            ShowDlgItem(hwndDlg, IDC_RESTART_PROGRESS, Success ? SW_SHOW : SW_HIDE);
            if (Success)
            {
                SendDlgItemMessageW(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, HOST_RESTART_STEPS));
                SendDlgItemMessageW(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETPOS, 0, 0);
                SetTimer(hwndDlg, HOST_RESTART_TIMER, 50, NULL);
            }
            SetFocus(GetDlgItem(GetParent(hwndDlg), Success ? IDC_FRAME_NEXT : IDCANCEL));
            return TRUE;
        }

        case SPN_WIZNEXT:
            KillTimer(hwndDlg, HOST_RESTART_TIMER);
            pSetupData->bMustReboot = Host.Error == ERROR_SUCCESS;
            EndDialog(GetParent(hwndDlg), IDOK);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;
    }

    return FALSE;
}

static PCWSTR
NextArgument(
    _In_ PCWSTR Text,
    _Out_writes_(cchValue) PWSTR Value,
    _In_ SIZE_T cchValue)
{
    SIZE_T Length = 0;
    BOOL Quoted;

    while (*Text == L' ' || *Text == L'\t')
        Text++;
    Quoted = (*Text == L'"');
    if (Quoted)
        Text++;
    while (*Text && (Quoted ? *Text != L'"' : (*Text != L' ' && *Text != L'\t')))
    {
        if (Length + 1 < cchValue)
            Value[Length++] = *Text;
        Text++;
    }
    if (Quoted && *Text == L'"')
        Text++;
    Value[Length] = UNICODE_NULL;
    return Text;
}

static BOOL
ParseCommandLine(
    _In_ PCWSTR CommandLine)
{
    WCHAR Option[MAX_PATH], Value[MAX_PATH];
    PCWSTR Text = CommandLine;

    for (;;)
    {
        Text = NextArgument(Text, Option, ARRAYSIZE(Option));
        if (!Option[0])
            return TRUE;
        if (_wcsicmp(Option, L"/noreboot") == 0)
        {
            Host.NoReboot = TRUE;
            continue;
        }

        Text = NextArgument(Text, Value, ARRAYSIZE(Value));
        if (_wcsicmp(Option, L"/auto") == 0 && (_wcsicmp(Value, L"install") == 0 || _wcsicmp(Value, L"upgrade") == 0))
        {
            Host.Auto = TRUE;
            Host.AutoUpgrade = _wcsicmp(Value, L"upgrade") == 0;
        }
        else if (_wcsicmp(Option, L"/size") == 0 && Value[0])
        {
            Host.AutoSizeMb = _wcstoui64(Value, NULL, 10);
        }
        else if (_wcsicmp(Option, L"/defaultos") == 0 &&
                 (_wcsicmp(Value, L"windows") == 0 || _wcsicmp(Value, L"libernt") == 0))
        {
            Host.WindowsFirst = _wcsicmp(Value, L"windows") == 0;
        }
        else if (_wcsicmp(Option, L"/unattend") == 0 && Value[0])
        {
            if (!GetFullPathNameW(Value, ARRAYSIZE(Host.Unattend), Host.Unattend, NULL))
                return FALSE;
        }
        else
        {
            HostLog(L"Unknown option %s %s", Option, Value);
            return FALSE;
        }
    }
}

static DWORD
RunAutomatic(
    _Inout_ PSETUPDATA pSetupData)
{
    WCHAR Text[1024];
    ULONGLONG LiberNTMb;

    Host.Upgrade = Host.AutoUpgrade;
    if (Host.Upgrade)
    {
        if (Host.LiberNTCount == 0)
        {
            HostLog(L"No LiberNT installation to upgrade");
            return ERROR_NOT_FOUND;
        }
        Host.Selected = 0;
        if (!CheckUpgradeTarget(NULL, Host.Selected))
            return Host.Error ? Host.Error : ERROR_NOT_SUPPORTED;
        return PrepareUpgrade();
    }

    if (!Host.HavePrepared)
    {
        QuerySizes();
        if (Host.Problem)
        {
            FormatSizeProblem(pSetupData, Text, ARRAYSIZE(Text));
            HostMessage(NULL, TD_ERROR_ICON, Text);
            return Host.Error;
        }
        LiberNTMb = Host.AutoSizeMb ? Host.AutoSizeMb
                                    : Host.WindowsMb + Host.FreeAfterBytes / HOST_MEGABYTE -
                                      (Host.MaxWindowsMb - (Host.MaxWindowsMb - Host.MinWindowsMb) / 2);
        if (Host.WindowsMb + Host.FreeAfterBytes / HOST_MEGABYTE < LiberNTMb)
            return ERROR_DISK_FULL;
        Host.WindowsBytes = max(Host.MinWindowsMb,
                                min(Host.MaxWindowsMb,
                                    Host.WindowsMb + Host.FreeAfterBytes / HOST_MEGABYTE - LiberNTMb)) * HOST_MEGABYTE;
    }
    return PrepareInstall(pSetupData);
}

INT
RunHostSetup(
    _Inout_ PSETUPDATA pSetupData,
    _In_ PCWSTR CommandLine)
{
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HANDLE Thread;

    ZeroMemory(&Host, sizeof(Host));
    pSetupData->bWindowsHost = TRUE;
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,
                         EOAC_NONE, NULL);
    HostLog(L"Setup started on Windows: %s", CommandLine);
    if (!ParseCommandLine(CommandLine))
    {
        Host.Error = ERROR_INVALID_PARAMETER;
    }
    else if (!CheckComputer(pSetupData))
    {
        if (Host.Error == ERROR_SUCCESS)
            Host.Error = ERROR_CANCELLED;
    }
    else if (Host.Auto)
    {
        Host.Error = RunAutomatic(pSetupData);
        pSetupData->bMustReboot = Host.Error == ERROR_SUCCESS && !Host.NoReboot;
    }
    else
    {
        Thread = Host.HavePrepared ? NULL : CreateThread(NULL, 0, SizeThread, NULL, 0, NULL);
        if (!Thread && !Host.HavePrepared)
            SizeThread(NULL);
        RunSetupFrame(pSetupData, HostPages, ARRAYSIZE(HostPages));
        if (Thread)
        {
            WaitForSingleObject(Thread, INFINITE);
            CloseHandle(Thread);
        }
    }
    HostLog(L"Setup on Windows ends with %lu, restart %d", Host.Error, pSetupData->bMustReboot);
    if (SUCCEEDED(hrCom))
        CoUninitialize();
    return (INT)Host.Error;
}
