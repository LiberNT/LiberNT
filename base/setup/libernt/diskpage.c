/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Page that chooses, creates, deletes, formats and resizes the installation partition
 * COPYRIGHT:   Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <ntddstor.h>
#include <fslib/volresize.h>
#include <ndk/obfuncs.h>
#include <initguid.h>
#include <diskguid.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define DISK_MEGABYTE   (1024ULL * 1024ULL)
#define DISK_GIGABYTE   (1024ULL * DISK_MEGABYTE)
#define DISK_MAX_DELETED 32

typedef struct _VOL_PLAN
{
    LIST_ENTRY ListEntry;
    VOL_CREATE_INFO Create;
} VOL_PLAN, *PVOL_PLAN;

typedef struct _NEWPART_CTX
{
    ULONGLONG MaxMb;
    ULONGLONG SizeMb;
} NEWPART_CTX, *PNEWPART_CTX;

typedef struct _RESIZE_CTX
{
    BOOLEAN Extend;
    WCHAR Caption[96];
    ULONGLONG TotalMb;
    ULONGLONG AvailableMb;
    ULONGLONG AmountMb;
} RESIZE_CTX, *PRESIZE_CTX;

static const GUID DiskRecoveryGuid =
{ 0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } };

static LIST_ENTRY VolumePlans = { &VolumePlans, &VolumePlans };
static ULONGLONG ErasedDisks;
static WCHAR DeletedRegions[DISK_MAX_DELETED][96];
static ULONG DeletedCount;

VOID
FormatDiskSize(
    _In_ ULONGLONG Bytes,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    ULONGLONG Tenths;
    PCWSTR Unit;

    if (Bytes >= DISK_GIGABYTE)
    {
        Tenths = (Bytes * 10 + DISK_GIGABYTE / 2) / DISK_GIGABYTE;
        Unit = L"GB";
    }
    else
    {
        Tenths = (Bytes * 10 + DISK_MEGABYTE / 2) / DISK_MEGABYTE;
        Unit = L"MB";
    }

    StringCchPrintfW(Text, cchText, L"%I64u.%I64u %s", Tenths / 10, Tenths % 10, Unit);
}

PVOL_CREATE_INFO
FindVolumeFormatPlan(
    _In_ PVOLENTRY Volume)
{
    PLIST_ENTRY Entry;

    for (Entry = VolumePlans.Flink; Entry != &VolumePlans; Entry = Entry->Flink)
    {
        PVOL_PLAN Plan = CONTAINING_RECORD(Entry, VOL_PLAN, ListEntry);

        if (Plan->Create.Volume == Volume)
            return &Plan->Create;
    }

    return NULL;
}

static BOOLEAN
PlanVolumeFormat(
    _In_ PVOLENTRY Volume,
    _In_ PCWSTR FileSystem,
    _In_opt_ PCWSTR Label)
{
    PVOL_CREATE_INFO Create = FindVolumeFormatPlan(Volume);

    if (!Create)
    {
        PVOL_PLAN Plan = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*Plan));

        if (!Plan)
            return FALSE;
        InsertTailList(&VolumePlans, &Plan->ListEntry);
        Create = &Plan->Create;
    }

    StringCchCopyW(Create->FileSystemName, ARRAYSIZE(Create->FileSystemName), FileSystem);
    Create->MediaFlag = FMIFS_HARDDISK;
    Create->Label = Label;
    Create->QuickFormat = TRUE;
    Create->ClusterSize = 0;
    Create->Volume = Volume;
    return TRUE;
}

static VOID
ForgetVolumePlans(
    _In_opt_ PDISKENTRY DiskEntry,
    _In_opt_ PVOLENTRY Volume,
    _In_ BOOLEAN LogicalOnly)
{
    PLIST_ENTRY Entry = VolumePlans.Flink;

    while (Entry != &VolumePlans)
    {
        PVOL_PLAN Plan = CONTAINING_RECORD(Entry, VOL_PLAN, ListEntry);
        PPARTENTRY PartEntry = Plan->Create.Volume ? Plan->Create.Volume->PartEntry : NULL;
        BOOLEAN Forget;

        Entry = Entry->Flink;

        if (Volume)
            Forget = (Plan->Create.Volume == Volume);
        else if (DiskEntry)
            Forget = PartEntry && PartEntry->DiskEntry == DiskEntry && (!LogicalOnly || PartEntry->LogicalPartition);
        else
            Forget = TRUE;

        if (Forget)
        {
            RemoveEntryList(&Plan->ListEntry);
            HeapFree(ProcessHeap, 0, Plan);
        }
    }
}

static BOOLEAN
IsSystemPartitionEntry(
    _In_ PPARTENTRY PartEntry)
{
    return PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_GPT &&
           IsEqualGUID(&PartEntry->PartitionTypeGuid, &PARTITION_SYSTEM_GUID);
}

static UINT
GetRegionTypeId(
    _In_ PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    if (!PartEntry->IsPartitioned)
        return 0;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        if (IsEqualGUID(&PartEntry->PartitionTypeGuid, &PARTITION_SYSTEM_GUID))
            return IDS_TYPE_SYSTEM;
        if (IsEqualGUID(&PartEntry->PartitionTypeGuid, &PARTITION_MSFT_RESERVED_GUID))
            return IDS_TYPE_MSR;
        if (IsEqualGUID(&PartEntry->PartitionTypeGuid, &PARTITION_BASIC_DATA_GUID))
            return IDS_TYPE_PRIMARY;
        if (IsEqualGUID(&PartEntry->PartitionTypeGuid, &DiskRecoveryGuid))
            return IDS_TYPE_RECOVERY;
        return IDS_TYPE_OEM;
    }

    if (PartEntry == DiskEntry->ExtendedPartition)
        return IDS_TYPE_EXTENDED;
    if (PartEntry->LogicalPartition)
        return IDS_TYPE_LOGICAL;
    if (PartEntry->PartitionType == PARTITION_MSFT_RECOVERY)
        return IDS_TYPE_RECOVERY;
    if (PartEntry->BootIndicator)
        return IDS_TYPE_SYSTEM;
    return IDS_TYPE_PRIMARY;
}

VOID
DescribeDiskRegion(
    _In_ PPARTENTRY PartEntry,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    PVOLENTRY Volume = PartEntry->Volume;

    if (!PartEntry->IsPartitioned)
    {
        LoadPrintfW(Text, cchText, IDS_DISK_UNALLOCATED, PartEntry->DiskEntry->DiskNumber);
    }
    else if (Volume && Volume->Info.VolumeLabel[0] && !FindVolumeFormatPlan(Volume))
    {
        LoadPrintfW(Text, cchText, IDS_DISK_PARTITION_LABEL,
                    PartEntry->DiskEntry->DiskNumber,
                    PartEntry->OnDiskPartitionNumber,
                    Volume->Info.VolumeLabel);
    }
    else
    {
        LoadPrintfW(Text, cchText, IDS_DISK_PARTITION,
                    PartEntry->DiskEntry->DiskNumber,
                    PartEntry->OnDiskPartitionNumber);
    }
}

static BOOLEAN
QueryVolumeFreeBytes(
    _In_ PVOLENTRY Volume,
    _Out_ PULONGLONG FreeBytes)
{
    FILE_FS_SIZE_INFORMATION SizeInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Path;
    WCHAR Root[MAX_PATH];
    HANDLE Handle;
    NTSTATUS Status;

    if (FAILED(StringCchPrintfW(Root, ARRAYSIZE(Root), L"%s\\", Volume->Info.DeviceName)))
        return FALSE;

    RtlInitUnicodeString(&Path, Root);
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, SYNCHRONIZE | FILE_READ_ATTRIBUTES, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtQueryVolumeInformationFile(Handle, &IoStatusBlock, &SizeInfo, sizeof(SizeInfo),
                                          FileFsSizeInformation);
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
        return FALSE;

    *FreeBytes = (ULONGLONG)SizeInfo.AvailableAllocationUnits.QuadPart *
                 SizeInfo.SectorsPerAllocationUnit * SizeInfo.BytesPerSector;
    return TRUE;
}

static VOID
AddRegionRow(
    _In_ HWND hList,
    _In_ PPARTENTRY PartEntry)
{
    ULONGLONG Total = GetPartEntrySizeInBytes(PartEntry);
    ULONGLONG Free = Total;
    PVOLENTRY Volume = PartEntry->Volume;
    PVOL_CREATE_INFO Plan = Volume ? FindVolumeFormatPlan(Volume) : NULL;
    WCHAR Text[160];
    WCHAR Suffix[64];
    LVITEMW Item = {0};
    UINT TypeId;
    INT iItem;
    BOOLEAN HasFree = TRUE;

    if (!PartEntry->IsPartitioned && Total < DISK_MEGABYTE)
        return;

    DescribeDiskRegion(PartEntry, Text, ARRAYSIZE(Text));
    if (PartEntry->IsPartitioned && (PartEntry->New || Plan))
    {
        LoadStringW(SetupData.hInstance,
                    PartEntry->New ? IDS_DISK_SUFFIX_NEW : IDS_DISK_SUFFIX_FORMAT,
                    Suffix, ARRAYSIZE(Suffix));
        StringCchCatW(Text, ARRAYSIZE(Text), Suffix);
    }

    Item.mask = LVIF_TEXT | LVIF_PARAM;
    Item.iItem = ListView_GetItemCount(hList);
    Item.pszText = Text;
    Item.lParam = (LPARAM)PartEntry;
    iItem = ListView_InsertItem(hList, &Item);
    if (iItem < 0)
        return;

    FormatDiskSize(Total, Text, ARRAYSIZE(Text));
    ListView_SetItemText(hList, iItem, 1, Text);

    if (Volume && !PartEntry->New && !Plan && !Volume->New && !IsUnformatted(&Volume->Info))
        HasFree = QueryVolumeFreeBytes(Volume, &Free);
    if (PartEntry->IsPartitioned && !Volume)
        HasFree = FALSE;
    Text[0] = UNICODE_NULL;
    if (HasFree)
        FormatDiskSize(Free, Text, ARRAYSIZE(Text));
    ListView_SetItemText(hList, iItem, 2, Text);

    Text[0] = UNICODE_NULL;
    TypeId = GetRegionTypeId(PartEntry);
    if (TypeId)
        LoadStringW(SetupData.hInstance, TypeId, Text, ARRAYSIZE(Text));
    ListView_SetItemText(hList, iItem, 3, Text);
}

static VOID
DrawDiskList(
    _In_ HWND hList,
    _In_ PPARTLIST List)
{
    PLIST_ENTRY DiskLink, PartLink;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    if (List)
    {
        for (DiskLink = List->DiskListHead.Flink; DiskLink != &List->DiskListHead; DiskLink = DiskLink->Flink)
        {
            PDISKENTRY DiskEntry = CONTAINING_RECORD(DiskLink, DISKENTRY, ListEntry);

            if (DiskEntry->BootMedium)
                continue;

            for (PartLink = DiskEntry->PrimaryPartListHead.Flink;
                 PartLink != &DiskEntry->PrimaryPartListHead;
                 PartLink = PartLink->Flink)
            {
                PPARTENTRY PartEntry = CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry);
                PLIST_ENTRY LogicalLink;

                AddRegionRow(hList, PartEntry);
                if (PartEntry != DiskEntry->ExtendedPartition)
                    continue;

                for (LogicalLink = DiskEntry->LogicalPartListHead.Flink;
                     LogicalLink != &DiskEntry->LogicalPartListHead;
                     LogicalLink = LogicalLink->Flink)
                {
                    AddRegionRow(hList, CONTAINING_RECORD(LogicalLink, PARTENTRY, ListEntry));
                }
            }
        }
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, TRUE);
}

static PPARTENTRY
GetSelectedRegion(
    _In_ HWND hList)
{
    LVITEMW Item = {0};

    Item.iItem = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (Item.iItem < 0)
        return NULL;

    Item.mask = LVIF_PARAM;
    if (!ListView_GetItem(hList, &Item))
        return NULL;

    return (PPARTENTRY)Item.lParam;
}

static VOID
SelectRegion(
    _In_ HWND hList,
    _In_opt_ PPARTENTRY PartEntry)
{
    LVFINDINFOW Find = {0};
    INT iItem = 0;

    if (PartEntry)
    {
        Find.flags = LVFI_PARAM;
        Find.lParam = (LPARAM)PartEntry;
        iItem = ListView_FindItem(hList, -1, &Find);
        if (iItem < 0)
            iItem = 0;
    }

    if (ListView_GetItemCount(hList) == 0)
        return;

    ListView_SetItemState(hList, iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hList, iItem, FALSE);
}

static PPARTENTRY
FindDefaultInstallRegion(
    _In_ PPARTLIST List)
{
    PLIST_ENTRY DiskLink, PartLink;
    PPARTENTRY Best = NULL;
    ULONGLONG BestSize = 0;

    if (!List)
        return NULL;

    for (DiskLink = List->DiskListHead.Flink; DiskLink != &List->DiskListHead; DiskLink = DiskLink->Flink)
    {
        PDISKENTRY DiskEntry = CONTAINING_RECORD(DiskLink, DISKENTRY, ListEntry);

        if (DiskEntry->BootMedium)
            continue;

        for (PartLink = DiskEntry->PrimaryPartListHead.Flink;
             PartLink != &DiskEntry->PrimaryPartListHead;
             PartLink = PartLink->Flink)
        {
            PPARTENTRY PartEntry = CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry);
            ULONGLONG Size = GetPartEntrySizeInBytes(PartEntry);

            if (PartEntry->IsPartitioned || PartitionCreateChecks(PartEntry, 0ULL, 0) != NOT_AN_ERROR)
                continue;

            if (Size > BestSize)
            {
                BestSize = Size;
                Best = PartEntry;
            }
        }
    }

    return Best;
}

static BOOLEAN
HasPendingDiskChanges(
    _In_ PPARTLIST List)
{
    PLIST_ENTRY Entry;

    if (!IsListEmpty(&VolumePlans) || ErasedDisks || DeletedCount)
        return TRUE;

    for (Entry = List->DiskListHead.Flink; Entry != &List->DiskListHead; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, DISKENTRY, ListEntry)->Dirty)
            return TRUE;
    }

    return FALSE;
}

static BOOLEAN
IsResizableVolume(
    _In_ PPARTENTRY PartEntry)
{
    PVOLENTRY Volume = PartEntry->Volume;

    if (!PartEntry->IsPartitioned || PartEntry->New || !Volume || Volume->New)
        return FALSE;
    if (IsSystemPartitionEntry(PartEntry))
        return FALSE;
    return _wcsicmp(Volume->Info.FileSystem, L"NTFS") == 0 || IsUnformatted(&Volume->Info);
}

ULONGLONG
GetFollowingFreeBytes(
    _In_ PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    PLIST_ENTRY Head = PartEntry->LogicalPartition ? &DiskEntry->LogicalPartListHead
                                                   : &DiskEntry->PrimaryPartListHead;
    ULONGLONG Next = PartEntry->StartSector.QuadPart + PartEntry->SectorCount.QuadPart;
    ULONGLONG Bytes = 0;
    PLIST_ENTRY Entry;

    for (Entry = PartEntry->ListEntry.Flink; Entry != Head; Entry = Entry->Flink)
    {
        PPARTENTRY Free = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

        if (Free->IsPartitioned || Free->StartSector.QuadPart != Next)
            break;
        Bytes += Free->SectorCount.QuadPart * DiskEntry->BytesPerSector;
        Next = Free->StartSector.QuadPart + Free->SectorCount.QuadPart;
    }
    return Bytes;
}

static BOOLEAN
DiskHasPartitions(
    _In_ PDISKENTRY DiskEntry)
{
    PLIST_ENTRY Entry;

    for (Entry = DiskEntry->PrimaryPartListHead.Flink; Entry != &DiskEntry->PrimaryPartListHead; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, PARTENTRY, ListEntry)->IsPartitioned)
            return TRUE;
    }
    return FALSE;
}

static UINT
GetInstallBlocker(
    _In_opt_ PPARTENTRY PartEntry)
{
    BOOLEAN UefiMode;
    BOOLEAN EmptyDisk;

    if (!PartEntry)
        return IDS_DISK_SELECT;

    if (PartEntry->SectorCount.QuadPart * PartEntry->DiskEntry->BytesPerSector <
        (ULONGLONG)SetupData.USetupData.RequiredPartitionDiskSpace * DISK_MEGABYTE)
    {
        return IDS_DISK_TOO_SMALL;
    }

    UefiMode = IsUefiFirmware();
    EmptyDisk = !DiskHasPartitions(PartEntry->DiskEntry);
    if ((UefiMode && PartEntry->DiskEntry->DiskStyle != PARTITION_STYLE_GPT) ||
        (!UefiMode && PartEntry->DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
         (PartEntry->DiskEntry->DiskStyle != PARTITION_STYLE_RAW || !EmptyDisk)))
        return IDS_DISK_MODE_MISMATCH;

    if (!PartEntry->IsPartitioned)
        return (PartitionCreateChecks(PartEntry, 0ULL, 0) == NOT_AN_ERROR) ? 0 : IDS_DISK_NO_REGION;

    if (IsContainerPartition(PartEntry->PartitionType) || !PartEntry->Volume)
        return IDS_DISK_NOT_BASIC;

    if (PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_GPT &&
        !IsEqualGUID(&PartEntry->PartitionTypeGuid, &PARTITION_BASIC_DATA_GUID))
    {
        return IDS_DISK_NOT_BASIC;
    }

    return 0;
}

static VOID
UpdateDiskInfo(
    _In_ HWND hwndDlg,
    _In_opt_ PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry;
    WCHAR Name[128];
    WCHAR Size[32];
    WCHAR Text[256];

    if (!PartEntry)
    {
        SetDlgItemTextW(hwndDlg, IDC_DISKINFO, L"");
        return;
    }

    DiskEntry = PartEntry->DiskEntry;
    if (!DiskInfoGetDescription(DiskEntry->DiskNumber, Name, ARRAYSIZE(Name)))
    {
        if (DiskEntry->DriverName.Length)
            StringCchPrintfW(Name, ARRAYSIZE(Name), L"%wZ", &DiskEntry->DriverName);
        else
            Name[0] = UNICODE_NULL;
    }

    FormatDiskSize(DiskEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector, Size, ARRAYSIZE(Size));
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_DISKINFO, DiskEntry->DiskNumber);
    if (Name[0])
    {
        StringCchCatW(Text, ARRAYSIZE(Text), L" ");
        StringCchCatW(Text, ARRAYSIZE(Text), Name);
        StringCchCatW(Text, ARRAYSIZE(Text), L" \x00B7");
    }
    StringCchCatW(Text, ARRAYSIZE(Text), L" ");
    StringCchCatW(Text, ARRAYSIZE(Text), Size);
    StringCchCatW(Text, ARRAYSIZE(Text), L" \x00B7 ");
    StringCchCatW(Text, ARRAYSIZE(Text),
                  DiskEntry->DiskStyle == PARTITION_STYLE_GPT ? L"GPT" :
                  DiskEntry->DiskStyle == PARTITION_STYLE_MBR ? L"MBR" : L"RAW");
    SetDlgItemTextW(hwndDlg, IDC_DISKINFO, Text);
}

static VOID
UpdateDiskButtons(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_DISKLIST);
    PPARTENTRY PartEntry = GetSelectedRegion(hList);
    BOOLEAN Partitioned = PartEntry && PartEntry->IsPartitioned;
    BOOLEAN Pending = HasPendingDiskChanges(pSetupData->PartitionList);
    BOOLEAN Resizable = Partitioned && IsResizableVolume(PartEntry);
    UINT Blocker = GetInstallBlocker(PartEntry);
    WCHAR Note[256];

    EnableDlgItem(hwndDlg, IDC_DISKNEW, PartEntry && !Partitioned && !Blocker);
    EnableDlgItem(hwndDlg, IDC_DISKDELETE, Partitioned);
    EnableDlgItem(hwndDlg, IDC_DISKFORMAT, Partitioned && PartEntry->Volume);
    EnableDlgItem(hwndDlg, IDC_DISKEXTEND, Resizable && !Pending && GetFollowingFreeBytes(PartEntry) >= DISK_MEGABYTE);
    EnableDlgItem(hwndDlg, IDC_DISKSHRINK, Resizable && !Pending && !IsUnformatted(&PartEntry->Volume->Info));
    EnableDlgItem(hwndDlg, IDC_DISKERASE, PartEntry && DiskHasPartitions(PartEntry->DiskEntry));

    Note[0] = UNICODE_NULL;
    if (PartEntry && Blocker)
        LoadStringW(pSetupData->hInstance, Blocker, Note, ARRAYSIZE(Note));
    SetDlgItemTextW(hwndDlg, IDC_DISKNOTE, Note);

    UpdateDiskInfo(hwndDlg, PartEntry);
    SetupFrameSetButtons(hwndDlg, TRUE, PartEntry && !Blocker);
}

static VOID
RedrawDiskList(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData,
    _In_opt_ PPARTENTRY Select)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_DISKLIST);

    DrawDiskList(hList, pSetupData->PartitionList);
    SelectRegion(hList, Select);
    UpdateDiskButtons(hwndDlg, pSetupData);
}

static VOID
ResetPartitionList(
    _Inout_ PSETUPDATA pSetupData)
{
    ForgetVolumePlans(NULL, NULL, FALSE);
    ErasedDisks = 0;
    DeletedCount = 0;
    InstallPartition = NULL;
    SystemPartition = NULL;

    DestroyPartitionList(pSetupData->PartitionList);
    pSetupData->PartitionList = CreatePartitionList();
    DiskInfoRefresh();
}

static VOID
ReloadDiskList(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    HCURSOR OldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));

    ListView_DeleteAllItems(GetDlgItem(hwndDlg, IDC_DISKLIST));
    ResetPartitionList(pSetupData);

    RedrawDiskList(hwndDlg, pSetupData, FindDefaultInstallRegion(pSetupData->PartitionList));
    SetCursor(OldCursor);
}

PPARTENTRY
FindPartitionAt(
    _In_ PPARTLIST List,
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartSector)
{
    PLIST_ENTRY DiskLink, PartLink;

    for (DiskLink = List->DiskListHead.Flink; DiskLink != &List->DiskListHead; DiskLink = DiskLink->Flink)
    {
        PDISKENTRY DiskEntry = CONTAINING_RECORD(DiskLink, DISKENTRY, ListEntry);

        if (DiskEntry->DiskNumber != DiskNumber)
            continue;

        for (PartLink = DiskEntry->PrimaryPartListHead.Flink;
             PartLink != &DiskEntry->PrimaryPartListHead;
             PartLink = PartLink->Flink)
        {
            PPARTENTRY PartEntry = CONTAINING_RECORD(PartLink, PARTENTRY, ListEntry);

            if (PartEntry->IsPartitioned && PartEntry->StartSector.QuadPart == StartSector)
                return PartEntry;
        }
    }

    return NULL;
}

static BOOLEAN
CreateRegionPartition(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData,
    _Inout_ PPARTENTRY* pRegion,
    _In_ ULONGLONG SizeBytes,
    _In_ BOOLEAN Ask)
{
    PPARTLIST List = pSetupData->PartitionList;
    PPARTENTRY Region = *pRegion;
    PPARTENTRY EfiPart = NULL, ReservedPart = NULL;
    WCHAR Text[512];

    if (GptDiskNeedsSystemPartitions(Region))
    {
        if (Ask)
        {
            LoadStringW(pSetupData->hInstance, IDS_DISK_NEW_SYSTEM, Text, ARRAYSIZE(Text));
            if (SetupMessage(GetParent(hwndDlg), TD_INFORMATION_ICON, TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, IDOK, NULL, Text) != IDOK)
                return FALSE;
        }

        if (!CreateGptSystemPartitions(List, &Region, &EfiPart, &ReservedPart) || !Region)
            goto Failed;

        if (EfiPart && EfiPart->Volume)
            PlanVolumeFormat(EfiPart->Volume, L"FAT32", L"System");

        if (SizeBytes > GetPartEntrySizeInBytes(Region))
            SizeBytes = 0;
    }

    if (!CreatePartition(List, Region, SizeBytes, 0))
        goto Failed;

    if (Region->Volume)
        PlanVolumeFormat(Region->Volume, L"NTFS", NULL);

    *pRegion = Region;
    return TRUE;

Failed:
    SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK,
                 MAKEINTRESOURCEW(IDS_ERROR_CREATE_PARTITION), NULL);
    return FALSE;
}

static INT_PTR CALLBACK
NewPartitionDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PNEWPART_CTX Ctx = (PNEWPART_CTX)GetWindowLongPtrW(hwndDlg, DWLP_USER);
    WCHAR Text[128];

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Ctx = (PNEWPART_CTX)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Ctx);
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_NEWPART_RANGE, Ctx->MaxMb);
            SetDlgItemTextW(hwndDlg, IDC_NEWSIZE_RANGE, Text);
            SendDlgItemMessageW(hwndDlg, IDC_NEWSIZE_SPIN, UDM_SETRANGE32, 1,
                                (LPARAM)min(Ctx->MaxMb, (ULONGLONG)MAXLONG));
            StringCchPrintfW(Text, ARRAYSIZE(Text), L"%I64u", Ctx->MaxMb);
            SetDlgItemTextW(hwndDlg, IDC_NEWSIZE, Text);
            SetFocus(GetDlgItem(hwndDlg, IDC_NEWSIZE));
            SendDlgItemMessageW(hwndDlg, IDC_NEWSIZE, EM_SETSEL, 0, -1);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_NEWSIZE:
                    if (HIWORD(wParam) == EN_CHANGE)
                    {
                        ULONGLONG Size = 0;

                        if (GetDlgItemTextW(hwndDlg, IDC_NEWSIZE, Text, ARRAYSIZE(Text)))
                            Size = _wcstoui64(Text, NULL, 10);
                        EnableDlgItem(hwndDlg, IDOK, Size != 0 && Size <= Ctx->MaxMb);
                    }
                    return TRUE;

                case IDOK:
                    Ctx->SizeMb = 0;
                    if (GetDlgItemTextW(hwndDlg, IDC_NEWSIZE, Text, ARRAYSIZE(Text)))
                        Ctx->SizeMb = _wcstoui64(Text, NULL, 10);
                    if (Ctx->SizeMb != 0 && Ctx->SizeMb <= Ctx->MaxMb)
                        EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}

static VOID
DoNewPartition(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    PPARTENTRY Region = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    ULONGLONG MaxBytes;
    NEWPART_CTX Ctx;

    if (!Region || Region->IsPartitioned)
        return;

    MaxBytes = GetPartEntrySizeInBytes(Region);
    Ctx.MaxMb = MaxBytes / DISK_MEGABYTE;
    Ctx.SizeMb = Ctx.MaxMb;
    if (Ctx.MaxMb == 0 ||
        DialogBoxParamW(pSetupData->hInstance, MAKEINTRESOURCEW(IDD_NEWPARTITION),
                        GetParent(hwndDlg), NewPartitionDlgProc, (LPARAM)&Ctx) != IDOK)
    {
        return;
    }

    CreateRegionPartition(hwndDlg, pSetupData, &Region,
                          (Ctx.SizeMb >= Ctx.MaxMb) ? 0 : Ctx.SizeMb * DISK_MEGABYTE, TRUE);

    RedrawDiskList(hwndDlg, pSetupData, Region);
}

static VOID
DoDeletePartition(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    PPARTENTRY PartEntry = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    PPARTENTRY FreeEntry = NULL;
    BOOLEAN Extended;
    WCHAR Name[96];

    if (!PartEntry || !PartEntry->IsPartitioned)
        return;

    Extended = (PartEntry == PartEntry->DiskEntry->ExtendedPartition);
    if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, IDNO,
                     MAKEINTRESOURCEW(IDS_WARN_DELETE_PARTITION_TITLE),
                     MAKEINTRESOURCEW(Extended ? IDS_WARN_DELETE_MBR_EXTENDED_PARTITION
                                               : IDS_WARN_DELETE_PARTITION)) != IDYES)
    {
        return;
    }

    DescribeDiskRegion(PartEntry, Name, ARRAYSIZE(Name));
    if (Extended)
        ForgetVolumePlans(PartEntry->DiskEntry, NULL, TRUE);
    else if (PartEntry->Volume)
        ForgetVolumePlans(NULL, PartEntry->Volume, FALSE);

    if (!PartEntry->New && DeletedCount < DISK_MAX_DELETED)
        StringCchCopyW(DeletedRegions[DeletedCount++], ARRAYSIZE(DeletedRegions[0]), Name);

    DeletePartition(pSetupData->PartitionList, PartEntry, &FreeEntry);
    RedrawDiskList(hwndDlg, pSetupData, FreeEntry);
}

static VOID
DoFormatPartition(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    PPARTENTRY PartEntry = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    WCHAR Text[512];

    if (!PartEntry || !PartEntry->IsPartitioned || !PartEntry->Volume)
        return;

    LoadStringW(pSetupData->hInstance, IDS_DISK_FORMAT_WARN, Text, ARRAYSIZE(Text));
    if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, IDCANCEL, NULL, Text) != IDOK)
        return;

    if (IsSystemPartitionEntry(PartEntry))
        PlanVolumeFormat(PartEntry->Volume, L"FAT32", L"System");
    else
        PlanVolumeFormat(PartEntry->Volume, L"NTFS", NULL);

    RedrawDiskList(hwndDlg, pSetupData, PartEntry);
}

static VOID
DoEraseDisk(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    PPARTENTRY PartEntry = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    PPARTENTRY FreeRegion = NULL;
    PDISKENTRY DiskEntry;
    WCHAR Text[256];

    if (!PartEntry)
        return;

    DiskEntry = PartEntry->DiskEntry;
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_DISK_ERASE_WARN, DiskEntry->DiskNumber);
    if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, IDNO, NULL, Text) != IDYES)
        return;

    ForgetVolumePlans(DiskEntry, NULL, FALSE);
    if (!EraseDisk(pSetupData->PartitionList, DiskEntry, &FreeRegion))
        SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK, MAKEINTRESOURCEW(IDS_ERROR_CREATE_PARTITION), NULL);
    else if (DiskEntry->DiskNumber < 64)
        ErasedDisks |= 1ULL << DiskEntry->DiskNumber;

    RedrawDiskList(hwndDlg, pSetupData, FreeRegion);
}

static VOID
ResizeSetNumber(
    _In_ HWND hwndDlg,
    _In_ INT ControlId,
    _In_ ULONGLONG Value)
{
    WCHAR Buffer[32];

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u", Value);
    SetDlgItemTextW(hwndDlg, ControlId, Buffer);
}

static VOID
ResizeUpdateAfter(
    _In_ HWND hwndDlg,
    _In_ PRESIZE_CTX Ctx)
{
    WCHAR Buffer[32];
    ULONGLONG Amount = 0;

    if (GetDlgItemTextW(hwndDlg, IDC_RESIZE_AMOUNT, Buffer, ARRAYSIZE(Buffer)) != 0)
        Amount = _wcstoui64(Buffer, NULL, 10);
    if (Amount > Ctx->AvailableMb)
        Amount = Ctx->AvailableMb;
    Ctx->AmountMb = Amount;
    ResizeSetNumber(hwndDlg, IDC_RESIZE_AFTER, Ctx->Extend ? Ctx->TotalMb + Amount : Ctx->TotalMb - Amount);
    EnableWindow(GetDlgItem(hwndDlg, IDOK), Amount != 0);
}

static INT_PTR CALLBACK
ResizeDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PRESIZE_CTX Ctx = (PRESIZE_CTX)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            static const UINT ShrinkLabels[] = { IDS_SHRINK_TOTAL, IDS_SHRINK_AVAILABLE, IDS_SHRINK_AMOUNT, IDS_SHRINK_AFTER, IDS_SHRINK_BUTTON };
            static const UINT ExtendLabels[] = { IDS_EXTEND_TOTAL, IDS_EXTEND_AVAILABLE, IDS_EXTEND_AMOUNT, IDS_EXTEND_AFTER, IDS_EXTEND_BUTTON };
            static const INT Controls[] = { IDC_RESIZE_LABEL_TOTAL, IDC_RESIZE_LABEL_AVAILABLE, IDC_RESIZE_LABEL_AMOUNT, IDC_RESIZE_LABEL_AFTER, IDOK };
            WCHAR Text[160];
            HICON Icon = NULL;
            ULONG Index;

            Ctx = (PRESIZE_CTX)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Ctx);
            SetWindowTextW(hwndDlg, Ctx->Caption);
            for (Index = 0; Index < ARRAYSIZE(Controls); Index++)
            {
                LoadStringW(GetModuleHandleW(NULL),
                            Ctx->Extend ? ExtendLabels[Index] : ShrinkLabels[Index],
                            Text,
                            ARRAYSIZE(Text));
                SetDlgItemTextW(hwndDlg, Controls[Index], Text);
            }
            ShowWindow(GetDlgItem(hwndDlg, IDC_RESIZE_NOTE), Ctx->Extend ? SW_HIDE : SW_SHOW);
            ShowWindow(GetDlgItem(hwndDlg, IDC_RESIZE_ICON), Ctx->Extend ? SW_HIDE : SW_SHOW);
            ResizeSetNumber(hwndDlg, IDC_RESIZE_TOTAL, Ctx->TotalMb);
            ResizeSetNumber(hwndDlg, IDC_RESIZE_AVAILABLE, Ctx->AvailableMb);
            SendDlgItemMessageW(hwndDlg, IDC_RESIZE_SPIN, UDM_SETRANGE32, 0,
                                (LPARAM)min(Ctx->AvailableMb, (ULONGLONG)MAXLONG));
            ResizeSetNumber(hwndDlg, IDC_RESIZE_AMOUNT, Ctx->AvailableMb);
            ResizeUpdateAfter(hwndDlg, Ctx);
            if (!Ctx->Extend &&
                SUCCEEDED(LoadIconWithScaleDown(NULL, IDI_INFORMATION,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON),
                                                &Icon)))
            {
                SendDlgItemMessageW(hwndDlg, IDC_RESIZE_ICON, STM_SETICON, (WPARAM)Icon, 0);
            }
            SetFocus(GetDlgItem(hwndDlg, IDC_RESIZE_AMOUNT));
            SendDlgItemMessageW(hwndDlg, IDC_RESIZE_AMOUNT, EM_SETSEL, 0, -1);
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_RESIZE_AMOUNT:
                    if (HIWORD(wParam) == EN_CHANGE && Ctx)
                        ResizeUpdateAfter(hwndDlg, Ctx);
                    return TRUE;

                case IDOK:
                    if (Ctx)
                        ResizeUpdateAfter(hwndDlg, Ctx);
                    if (Ctx && Ctx->AmountMb != 0)
                        EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_DESTROY:
        {
            HICON Icon = (HICON)SendDlgItemMessageW(hwndDlg, IDC_RESIZE_ICON, STM_GETICON, 0, 0);

            if (Icon)
                DestroyIcon(Icon);
            break;
        }
    }
    return FALSE;
}

static VOID
DoResizePartition(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData,
    _In_ BOOLEAN Extend)
{
    PPARTENTRY PartEntry = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    UNICODE_STRING VolumePath;
    VOLRESIZE_INFO Info;
    RESIZE_CTX Ctx;
    WCHAR Format[64];
    WCHAR Name[96];
    WCHAR Text[256];
    HCURSOR OldCursor;
    NTSTATUS Status;

    if (!PartEntry || !IsResizableVolume(PartEntry))
        return;

    if (HasPendingDiskChanges(pSetupData->PartitionList))
    {
        LoadStringW(pSetupData->hInstance, IDS_RESIZE_PENDING, Text, ARRAYSIZE(Text));
        SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON, IDOK, NULL, Text);
        return;
    }

    RtlInitUnicodeString(&VolumePath, PartEntry->Volume->Info.DeviceName);
    ZeroMemory(&Ctx, sizeof(Ctx));
    Ctx.Extend = Extend;
    if (Extend)
    {
        Ctx.TotalMb = GetPartEntrySizeInBytes(PartEntry) / DISK_MEGABYTE;
        Ctx.AvailableMb = GetFollowingFreeBytes(PartEntry) / DISK_MEGABYTE;
    }
    else
    {
        Status = VolResizeQuery(&VolumePath, &Info);
        if (!NT_SUCCESS(Status))
        {
            LoadStringW(pSetupData->hInstance, IDS_RESIZE_FAILED, Text, ARRAYSIZE(Text));
            SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK, NULL, Text);
            return;
        }
        Ctx.TotalMb = Info.PartitionBytes / DISK_MEGABYTE;
        Ctx.AvailableMb = Info.MaxReclaimableBytes / DISK_MEGABYTE;
    }

    DescribeDiskRegion(PartEntry, Name, ARRAYSIZE(Name));
    LoadStringW(pSetupData->hInstance, Extend ? IDS_EXTEND_CAPTION : IDS_SHRINK_CAPTION, Format, ARRAYSIZE(Format));
    StringCchPrintfW(Ctx.Caption, ARRAYSIZE(Ctx.Caption), Format, Name);

    if (DialogBoxParamW(pSetupData->hInstance, MAKEINTRESOURCEW(IDD_RESIZE),
                        GetParent(hwndDlg), ResizeDlgProc, (LPARAM)&Ctx) != IDOK ||
        Ctx.AmountMb == 0)
    {
        return;
    }

    OldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    if (Extend)
    {
        Status = VolResizeExtend(&VolumePath,
                                 Ctx.AmountMb * DISK_MEGABYTE,
                                 !IsUnformatted(&PartEntry->Volume->Info));
    }
    else
    {
        Status = VolResizeShrink(&VolumePath,
                                 min(Ctx.AmountMb * DISK_MEGABYTE, Info.MaxReclaimableBytes),
                                 NULL,
                                 NULL);
    }
    SetCursor(OldCursor);

    if (!NT_SUCCESS(Status))
    {
        LoadStringW(pSetupData->hInstance, IDS_RESIZE_FAILED, Text, ARRAYSIZE(Text));
        SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK, NULL, Text);
    }

    ReloadDiskList(hwndDlg, pSetupData);
}

BOOLEAN
PlanAlongsideInstall(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData,
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartSector,
    _In_ ULONGLONG ShrinkBytes)
{
    PPARTENTRY Windows = FindPartitionAt(pSetupData->PartitionList, DiskNumber, StartSector);
    PPARTENTRY Region = NULL;
    UNICODE_STRING VolumePath;
    HCURSOR OldCursor;
    NTSTATUS Status = STATUS_SUCCESS;
    WCHAR Text[256];

    if (!Windows || !Windows->Volume)
        return FALSE;

    OldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    if (ShrinkBytes)
    {
        RtlInitUnicodeString(&VolumePath, Windows->Volume->Info.DeviceName);
        Status = VolResizeShrink(&VolumePath, ShrinkBytes, NULL, NULL);
    }
    ResetPartitionList(pSetupData);

    Windows = FindPartitionAt(pSetupData->PartitionList, DiskNumber, StartSector);
    if (NT_SUCCESS(Status) && Windows &&
        Windows->ListEntry.Flink != &Windows->DiskEntry->PrimaryPartListHead)
    {
        Region = CONTAINING_RECORD(Windows->ListEntry.Flink, PARTENTRY, ListEntry);
        if (Region->IsPartitioned)
            Region = NULL;
    }
    SetCursor(OldCursor);

    if (!Region)
    {
        LoadStringW(pSetupData->hInstance, IDS_RESIZE_FAILED, Text, ARRAYSIZE(Text));
        SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_OK_BUTTON, IDOK, NULL, Text);
        return FALSE;
    }

    if (!CreateRegionPartition(hwndDlg, pSetupData, &Region, 0ULL, FALSE))
        return FALSE;

    InstallPartition = Region;
    return TRUE;
}

static BOOLEAN
VolumeHasDirectory(
    _In_ PVOLENTRY Volume,
    _In_ PCWSTR Directory)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Path;
    WCHAR Buffer[MAX_PATH];
    HANDLE Handle;

    if (FAILED(StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s%s%s",
                                Volume->Info.DeviceName,
                                (Directory[0] == L'\\') ? L"" : L"\\",
                                Directory)))
    {
        return FALSE;
    }

    RtlInitUnicodeString(&Path, Buffer);
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenFile(&Handle, SYNCHRONIZE | FILE_LIST_DIRECTORY, &ObjectAttributes, &IoStatusBlock,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        return FALSE;
    }

    NtClose(Handle);
    return TRUE;
}

static BOOLEAN
ApplyInstallSelection(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData)
{
    PPARTENTRY PartEntry = GetSelectedRegion(GetDlgItem(hwndDlg, IDC_DISKLIST));
    PVOLENTRY Volume;
    PVOL_CREATE_INFO Plan;
    WCHAR Text[512];
    UINT WarnId = 0;

    if (GetInstallBlocker(PartEntry))
        return FALSE;

    if (!pSetupData->bUnattend &&
        PartEntry->DiskEntry->MediaType == FixedMedia &&
        !PartEntry->DiskEntry->BiosFound &&
        !IsListEmpty(&PartEntry->DiskEntry->PartList->BiosDiskListHead))
    {
        LoadStringW(pSetupData->hInstance, IDS_DISK_NOT_FIRMWARE, Text, ARRAYSIZE(Text));
        if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, IDCANCEL, NULL, Text) != IDOK)
            return FALSE;
    }

    if (!PartEntry->IsPartitioned)
    {
        if (!CreateRegionPartition(hwndDlg, pSetupData, &PartEntry, 0ULL, !pSetupData->bUnattend))
        {
            RedrawDiskList(hwndDlg, pSetupData, PartEntry);
            return FALSE;
        }
        RedrawDiskList(hwndDlg, pSetupData, PartEntry);
    }

    Volume = PartEntry->Volume;
    if (!Volume)
        return FALSE;

    Plan = FindVolumeFormatPlan(Volume);
    if (pSetupData->bUnattend && pSetupData->USetupData.FormatPartition)
    {
        PlanVolumeFormat(Volume, L"NTFS", NULL);
    }
    else if (!Plan || !*Plan->FileSystemName)
    {
        if (Volume->New || IsUnformatted(&Volume->Info))
        {
            WarnId = 0;
        }
        else if (Volume->FormatState != Formatted || IsUnknown(&Volume->Info))
        {
            WarnId = IDS_DISK_FORMAT_WARN;
        }
        else if (_wcsicmp(Volume->Info.FileSystem, L"NTFS") != 0)
        {
            WarnId = IDS_DISK_FORMAT_NTFS;
        }
        else if (VolumeHasDirectory(Volume, pSetupData->USetupData.InstallationDirectory))
        {
            WarnId = IDS_DISK_FORMAT_EXISTING;
        }
        else
        {
            WarnId = (UINT)-1;
        }

        if (WarnId != (UINT)-1)
        {
            if (WarnId && !pSetupData->bUnattend)
            {
                LoadStringW(pSetupData->hInstance, WarnId, Text, ARRAYSIZE(Text));
                if (SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, IDCANCEL, NULL, Text) != IDOK)
                    return FALSE;
            }
            PlanVolumeFormat(Volume, L"NTFS", NULL);
            RedrawDiskList(hwndDlg, pSetupData, PartEntry);
        }
    }

    InstallPartition = PartEntry;
    return TRUE;
}

VOID
DescribeDiskChanges(
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    WCHAR Line[256];
    WCHAR Name[96];
    WCHAR Size[32];
    PLIST_ENTRY Entry;
    ULONG Index;

    Text[0] = UNICODE_NULL;

    for (Index = 0; Index < 64; Index++)
    {
        if (!(ErasedDisks & (1ULL << Index)))
            continue;
        LoadPrintfW(Line, ARRAYSIZE(Line), IDS_READY_ERASE, Index);
        StringCchCatW(Text, cchText, Line);
        StringCchCatW(Text, cchText, L"\r\n");
    }

    for (Index = 0; Index < DeletedCount; Index++)
    {
        LoadPrintfW(Line, ARRAYSIZE(Line), IDS_READY_DELETE, DeletedRegions[Index]);
        StringCchCatW(Text, cchText, Line);
        StringCchCatW(Text, cchText, L"\r\n");
    }

    for (Entry = VolumePlans.Flink; Entry != &VolumePlans; Entry = Entry->Flink)
    {
        PVOL_PLAN Plan = CONTAINING_RECORD(Entry, VOL_PLAN, ListEntry);
        PPARTENTRY PartEntry = Plan->Create.Volume ? Plan->Create.Volume->PartEntry : NULL;

        if (!PartEntry || !*Plan->Create.FileSystemName)
            continue;

        DescribeDiskRegion(PartEntry, Name, ARRAYSIZE(Name));
        FormatDiskSize(GetPartEntrySizeInBytes(PartEntry), Size, ARRAYSIZE(Size));
        LoadPrintfW(Line, ARRAYSIZE(Line), PartEntry->New ? IDS_READY_CREATE : IDS_READY_FORMAT,
                    Name, Size, Plan->Create.FileSystemName);
        StringCchCatW(Text, cchText, Line);
        StringCchCatW(Text, cchText, L"\r\n");
    }
}

INT_PTR
CALLBACK
DiskPageProc(
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
            static const UINT Columns[] = { IDS_DISK_COL_NAME, IDS_DISK_COL_TOTAL, IDS_DISK_COL_FREE, IDS_DISK_COL_TYPE };
            static const INT Widths[] = { 44, 17, 18, 21 };
            static const INT Formats[] = { LVCFMT_LEFT, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_LEFT };
            HWND hList = GetDlgItem(hwndDlg, IDC_DISKLIST);
            LVCOLUMNW Column = {0};
            WCHAR Text[64];
            RECT rc;
            INT Index;

            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);

            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            GetClientRect(hList, &rc);
            rc.right -= GetSystemMetrics(SM_CXVSCROLL);
            for (Index = 0; Index < (INT)ARRAYSIZE(Columns); Index++)
            {
                LoadStringW(pSetupData->hInstance, Columns[Index], Text, ARRAYSIZE(Text));
                Column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
                Column.fmt = Formats[Index];
                Column.cx = rc.right * Widths[Index] / 100;
                Column.pszText = Text;
                Column.iSubItem = Index;
                ListView_InsertColumn(hList, Index, &Column);
            }

            DiskInfoRefresh();
            return FALSE;
        }

        case WM_DESTROY:
            DiskInfoFree();
            break;

        case WM_COMMAND:
            if (HIWORD(wParam) != BN_CLICKED)
                break;

            switch (LOWORD(wParam))
            {
                case IDC_DISKNEW:
                    DoNewPartition(hwndDlg, pSetupData);
                    return TRUE;
                case IDC_DISKDELETE:
                    DoDeletePartition(hwndDlg, pSetupData);
                    return TRUE;
                case IDC_DISKFORMAT:
                    DoFormatPartition(hwndDlg, pSetupData);
                    return TRUE;
                case IDC_DISKEXTEND:
                case IDC_DISKSHRINK:
                    DoResizePartition(hwndDlg, pSetupData, LOWORD(wParam) == IDC_DISKEXTEND);
                    return TRUE;
                case IDC_DISKERASE:
                    DoEraseDisk(hwndDlg, pSetupData);
                    return TRUE;
                case IDC_DISKREFRESH:
                    ReloadDiskList(hwndDlg, pSetupData);
                    return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            LPNMHDR Header = (LPNMHDR)lParam;

            if (Header->idFrom != IDC_DISKLIST)
                break;

            if (Header->code == LVN_ITEMCHANGED &&
                (((LPNMLISTVIEW)lParam)->uChanged & LVIF_STATE))
            {
                UpdateDiskButtons(hwndDlg, pSetupData);
            }
            else if (Header->code == NM_DBLCLK &&
                     !GetInstallBlocker(GetSelectedRegion(Header->hwndFrom)))
            {
                SetupFramePressNext(hwndDlg);
            }
            break;
        }

        case SPN_QUERYSKIP:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, pSetupData->bAlongside);
            return TRUE;

        case SPN_KILLACTIVE:
            ListView_DeleteAllItems(GetDlgItem(hwndDlg, IDC_DISKLIST));
            return TRUE;

        case SPN_SETACTIVE:
        {
            HWND hList = GetDlgItem(hwndDlg, IDC_DISKLIST);

            RedrawDiskList(hwndDlg, pSetupData,
                           InstallPartition ? InstallPartition : FindDefaultInstallRegion(pSetupData->PartitionList));
            SetFocus(hList);

            if (pSetupData->bUnattend && !pSetupData->bUnattendTried)
            {
                PPARTENTRY PartEntry = NULL;

                pSetupData->bUnattendTried = TRUE;
                if (pSetupData->USetupData.DestinationDiskNumber >= 0 &&
                    pSetupData->USetupData.DestinationPartitionNumber >= 0)
                {
                    PartEntry = SelectPartition(pSetupData->PartitionList,
                                                pSetupData->USetupData.DestinationDiskNumber,
                                                pSetupData->USetupData.DestinationPartitionNumber);
                }
                if (PartEntry && IsContainerPartition(PartEntry->PartitionType))
                    PartEntry = NULL;
                if (!PartEntry && pSetupData->USetupData.AutoPartition)
                    PartEntry = FindDefaultInstallRegion(pSetupData->PartitionList);
                if (PartEntry)
                {
                    SelectRegion(hList, PartEntry);
                    UpdateDiskButtons(hwndDlg, pSetupData);
                    SetupFramePressNext(hwndDlg);
                }
            }
            return TRUE;
        }

        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, ApplyInstallSelection(hwndDlg, pSetupData) ? 0 : -1);
            return TRUE;
    }

    return FALSE;
}
