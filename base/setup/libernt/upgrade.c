/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Page that upgrades a LiberNT installation found on the computer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <winver.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define UPGRADE_MAX_TARGETS 8
#define UPGRADE_KERNEL      L"\\Windows\\System32\\ntoskrnl.exe"
#define UPGRADE_INVENTORY   L"\\Windows\\" UPGRADE_INVENTORY_PATH

typedef struct _UPGRADE_TARGET
{
    PPARTENTRY Partition;
    BOOLEAN Hibernated;
    WCHAR Region[96];
    WCHAR Version[128];
} UPGRADE_TARGET, *PUPGRADE_TARGET;

static UPGRADE_TARGET Targets[UPGRADE_MAX_TARGETS];
static ULONG TargetCount;
static BOOLEAN Searched;
static PUPGRADE_TARGET Selected;
static WCHAR SourceVersion[128];

BOOL
ReadInventoryVersion(
    _In_ PCWSTR Path,
    _Out_writes_(cchVersion) PWSTR Version,
    _In_ SIZE_T cchVersion)
{
    CHAR Line[256];
    PCHAR End;
    DWORD Read = 0;
    HANDLE File;
    BOOL Success;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Success = ReadFile(File, Line, sizeof(Line) - 1, &Read, NULL);
    CloseHandle(File);
    if (!Success || Read < 3 || Line[0] != ';' || Line[1] != ' ')
        return FALSE;

    Line[Read] = ANSI_NULL;
    End = strpbrk(Line, "\r\n");
    if (End)
        *End = ANSI_NULL;
    return MultiByteToWideChar(CP_UTF8, 0, Line + 2, -1, Version, (int)cchVersion) > 1;
}

BOOL
GetKernelMachine(
    _In_ PCWSTR Path,
    _Out_ PWORD Machine)
{
    IMAGE_DOS_HEADER DosHeader;
    IMAGE_FILE_HEADER FileHeader;
    DWORD Signature, Read;
    HANDLE File;
    BOOL Success;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Success = ReadFile(File, &DosHeader, sizeof(DosHeader), &Read, NULL) && Read == sizeof(DosHeader) &&
              DosHeader.e_magic == IMAGE_DOS_SIGNATURE &&
              SetFilePointer(File, DosHeader.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
              ReadFile(File, &Signature, sizeof(Signature), &Read, NULL) && Read == sizeof(Signature) &&
              Signature == IMAGE_NT_SIGNATURE &&
              ReadFile(File, &FileHeader, sizeof(FileHeader), &Read, NULL) && Read == sizeof(FileHeader);
    CloseHandle(File);

    if (Success)
        *Machine = FileHeader.Machine;
    return Success;
}

BOOL
IsLiberNtKernel(
    _In_ PCWSTR Path)
{
    WCHAR Key[64];
    PWORD Translation;
    PWSTR Product;
    PVOID Data;
    DWORD Size, Ignored;
    UINT Length;
    BOOL Found = FALSE;

    Size = GetFileVersionInfoSizeW(Path, &Ignored);
    if (!Size)
        return FALSE;

    Data = HeapAlloc(ProcessHeap, 0, Size);
    if (!Data)
        return FALSE;

    if (GetFileVersionInfoW(Path, 0, Size, Data) &&
        VerQueryValueW(Data, L"\\VarFileInfo\\Translation", (PVOID*)&Translation, &Length) &&
        Length >= 2 * sizeof(WORD) &&
        SUCCEEDED(StringCchPrintfW(Key, ARRAYSIZE(Key), L"\\StringFileInfo\\%04x%04x\\ProductName",
                                   Translation[0], Translation[1])) &&
        VerQueryValueW(Data, Key, (PVOID*)&Product, &Length) && Length)
    {
        Found = (wcsstr(Product, L"LiberNT") != NULL);
    }

    HeapFree(ProcessHeap, 0, Data);
    return Found;
}

static VOID
AddUpgradeTarget(
    _In_ PSETUPDATA pSetupData,
    _In_ PPARTENTRY PartEntry,
    _In_ WORD SourceMachine)
{
    PVOLENTRY Volume = PartEntry->Volume;
    PUPGRADE_TARGET Target;
    WCHAR Path[MAX_PATH];
    WORD Machine;

    if (TargetCount >= ARRAYSIZE(Targets) ||
        !PartEntry->IsPartitioned || PartEntry->New || !Volume || Volume->New ||
        _wcsicmp(Volume->Info.DeviceName, pSetupData->USetupData.SourceRootPath.Buffer) == 0 ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s" UPGRADE_KERNEL,
                                Volume->Info.DeviceName)) ||
        !GetKernelMachine(Path, &Machine) ||
        Machine != SourceMachine ||
        !IsLiberNtKernel(Path))
    {
        return;
    }

    Target = &Targets[TargetCount++];
    Target->Partition = PartEntry;
    Target->Hibernated = IsWindowsHibernated(Volume->Info.DeviceName);
    DescribeDiskRegion(PartEntry, Target->Region, ARRAYSIZE(Target->Region));
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s" UPGRADE_INVENTORY,
                                Volume->Info.DeviceName)) ||
        !ReadInventoryVersion(Path, Target->Version, ARRAYSIZE(Target->Version)))
    {
        LoadStringW(pSetupData->hInstance, IDS_UPGRADE_EARLIER, Target->Version, ARRAYSIZE(Target->Version));
    }
    DPRINT1("LiberNT found on %S: %S\n", Volume->Info.DeviceName, Target->Version);
}

ULONG
FindUpgradeTargets(
    _In_ PSETUPDATA pSetupData)
{
    PLIST_ENTRY DiskLink, PartLink;
    PDISKENTRY DiskEntry;
    WCHAR Path[MAX_PATH];
    WORD SourceMachine;

    if (Searched)
        return TargetCount;
    Searched = TRUE;

    if (!pSetupData->PartitionList ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%wZ" UPGRADE_KERNEL,
                                &pSetupData->USetupData.SourceRootPath)) ||
        !GetKernelMachine(Path, &SourceMachine))
    {
        return 0;
    }

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%wZ" UPGRADE_INVENTORY,
                                &pSetupData->USetupData.SourceRootPath)) ||
        !ReadInventoryVersion(Path, SourceVersion, ARRAYSIZE(SourceVersion)))
    {
        DPRINT1("The installation source has no file inventory\n");
        return 0;
    }

    for (DiskLink = pSetupData->PartitionList->DiskListHead.Flink;
         DiskLink != &pSetupData->PartitionList->DiskListHead;
         DiskLink = DiskLink->Flink)
    {
        DiskEntry = CONTAINING_RECORD(DiskLink, DISKENTRY, ListEntry);
        for (PartLink = DiskEntry->PrimaryPartListHead.Flink;
             PartLink != &DiskEntry->PrimaryPartListHead;
             PartLink = PartLink->Flink)
        {
            AddUpgradeTarget(pSetupData, CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry), SourceMachine);
        }
        for (PartLink = DiskEntry->LogicalPartListHead.Flink;
             PartLink != &DiskEntry->LogicalPartListHead;
             PartLink = PartLink->Flink)
        {
            AddUpgradeTarget(pSetupData, CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry), SourceMachine);
        }
    }

    return TargetCount;
}

static BOOL
SelectUpgradeTarget(
    _Inout_ PSETUPDATA pSetupData,
    _In_ ULONG Index)
{
    if (Index >= TargetCount || Targets[Index].Hibernated)
        return FALSE;

    Selected = &Targets[Index];
    InstallPartition = Selected->Partition;
    pSetupData->bUpgrade = TRUE;
    pSetupData->bAlongside = FALSE;
    pSetupData->bWindowsFirst = FALSE;
    return TRUE;
}

BOOL
SelectUnattendedUpgrade(
    _Inout_ PSETUPDATA pSetupData)
{
    ULONG Index;

    for (Index = 0; Index < FindUpgradeTargets(pSetupData); Index++)
    {
        if (SelectUpgradeTarget(pSetupData, Index))
            return TRUE;
    }
    return FALSE;
}

VOID
DescribeUpgrade(
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    WCHAR Line[512];

    Text[0] = UNICODE_NULL;
    if (!Selected)
        return;

    LoadPrintfW(Line, ARRAYSIZE(Line), IDS_READY_UPGRADE, Selected->Region, Selected->Version, SourceVersion);
    StringCchCatW(Text, cchText, Line);
    StringCchCatW(Text, cchText, L"\r\n\r\n");
    LoadPrintfW(Line, ARRAYSIZE(Line), IDS_READY_UPGRADE_KEEP);
    StringCchCatW(Text, cchText, Line);
}

static VOID
InitUpgradePage(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_UPGRADE_LIST);
    WCHAR Text[512];
    ULONG Index;

    if (ComboBox_GetCount(hList) == 0)
    {
        for (Index = 0; Index < TargetCount; Index++)
        {
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPGRADE_ENTRY, Targets[Index].Region, Targets[Index].Version);
            ComboBox_AddString(hList, Text);
        }
        ComboBox_SetCurSel(hList, 0);
        CheckRadioButton(hwndDlg, IDC_UPGRADE_KEEP, IDC_UPGRADE_NEW, IDC_UPGRADE_KEEP);
    }

    SetResText(GetDlgItem(hwndDlg, IDC_UPGRADE_NEW),
               pSetupData->bUpgradeOnly ? IDS_UPGRADE_NEW_COPY : IDS_UPGRADE_NEW_INSTALL);
}

static VOID
UpdateUpgradeNote(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    INT Index = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_UPGRADE_LIST));
    BOOL Keep = IsDlgButtonChecked(hwndDlg, IDC_UPGRADE_KEEP) == BST_CHECKED;
    WCHAR Text[512];

    EnableDlgItem(hwndDlg, IDC_UPGRADE_LIST, Keep);
    if (Keep && Index >= 0 && (ULONG)Index < TargetCount && Targets[Index].Hibernated)
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPGRADE_HIBERNATED, Targets[Index].Region);
    else
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPGRADE_NOTE, SourceVersion);
    SetDlgItemTextW(hwndDlg, IDC_UPGRADE_NOTE, Text);
    SetupFrameSetButtons(hwndDlg, FALSE,
                         !Keep || (Index >= 0 && (ULONG)Index < TargetCount && !Targets[Index].Hibernated));
}

static BOOL
ApplyUpgradeChoice(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    INT Index;

    if (IsDlgButtonChecked(hwndDlg, IDC_UPGRADE_KEEP) == BST_CHECKED)
    {
        Index = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_UPGRADE_LIST));
        return Index >= 0 && SelectUpgradeTarget(pSetupData, (ULONG)Index);
    }

    if (pSetupData->bUpgrade)
        InstallPartition = NULL;
    pSetupData->bUpgrade = FALSE;
    Selected = NULL;
    if (pSetupData->bUpgradeOnly)
    {
        EndDialog(GetParent(hwndDlg), IDCANCEL);
        return FALSE;
    }
    return TRUE;
}

INT_PTR
CALLBACK
UpgradePageProc(
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
            return FALSE;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED &&
                (LOWORD(wParam) == IDC_UPGRADE_KEEP || LOWORD(wParam) == IDC_UPGRADE_NEW))
            {
                CheckRadioButton(hwndDlg, IDC_UPGRADE_KEEP, IDC_UPGRADE_NEW, LOWORD(wParam));
                UpdateUpgradeNote(hwndDlg, pSetupData);
                return TRUE;
            }
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_UPGRADE_LIST)
            {
                UpdateUpgradeNote(hwndDlg, pSetupData);
                return TRUE;
            }
            break;

        case SPN_QUERYSKIP:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, FindUpgradeTargets(pSetupData) == 0);
            return TRUE;

        case SPN_SETACTIVE:
            InitUpgradePage(hwndDlg, pSetupData);
            UpdateUpgradeNote(hwndDlg, pSetupData);
            SetFocus(GetDlgItem(hwndDlg, IsDlgButtonChecked(hwndDlg, IDC_UPGRADE_KEEP) == BST_CHECKED ?
                                         IDC_UPGRADE_KEEP : IDC_UPGRADE_NEW));
            return TRUE;

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, ApplyUpgradeChoice(hwndDlg, pSetupData) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}

/* EOF */
