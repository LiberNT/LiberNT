/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Main-window action and menu-state helpers.
 */

#include "precomp.h"
#include <shellapi.h>

static
DM_MENU_FOCUS
DmMainWndMenuFocusFromViewTarget(
    _In_ DM_VIEW_TARGET Target)
{
    switch (Target)
    {
        case DmViewTargetTop:
            return DmMenuFocusTop;

        case DmViewTargetBottom:
            return DmMenuFocusBottom;

        case DmViewTargetDiskList:
            return DmMenuFocusDiskList;

        default:
            return DmMenuFocusNone;
    }
}

static
VOID
DmMainWndUpdateDynamicActionMenuText(
    _In_ PMAIN_WND_INFO Info)
{
    MENUITEMINFOW ItemInfo;
    DM_ACTION_CONTEXT ActionContext;
    WCHAR Verb[64];
    WCHAR MenuText[64];

    if (Info == NULL || Info->hMenu == NULL)
        return;

    DmMainWndBuildActionContext(Info, &ActionContext);

    DmActionBuildVerbText(&ActionContext, IDM_ACTION_CREATE_PARTITION, Verb, ARRAYSIZE(Verb));
    if (_wcsicmp(Verb, L"Create Logical Drive") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"New &Simple Volume...");
    else if (_wcsicmp(Verb, L"Create Volume") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"New &Simple Volume...");
    else
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"New &Simple Volume...");

    ZeroMemory(&ItemInfo, sizeof(ItemInfo));
    ItemInfo.cbSize = sizeof(ItemInfo);
    ItemInfo.fMask = MIIM_STRING;
    ItemInfo.dwTypeData = MenuText;
    SetMenuItemInfoW(Info->hMenu, IDM_ACTION_CREATE_PARTITION, FALSE, &ItemInfo);

    DmActionBuildVerbText(&ActionContext, IDM_ACTION_DELETE_VOLUME, Verb, ARRAYSIZE(Verb));
    if (_wcsicmp(Verb, L"Delete Partition") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"Delete &Partition...");
    else
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"De&lete Volume...");

    ZeroMemory(&ItemInfo, sizeof(ItemInfo));
    ItemInfo.cbSize = sizeof(ItemInfo);
    ItemInfo.fMask = MIIM_STRING;
    ItemInfo.dwTypeData = MenuText;
    SetMenuItemInfoW(Info->hMenu, IDM_ACTION_DELETE_VOLUME, FALSE, &ItemInfo);
}

VOID
DmMainWndCacheSelectedVolumeName(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListCacheSelection(&Info->VolumeListContext,
                               Info->hVolumeView);
}

VOID
DmMainWndRestoreSelectedVolume(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListRestoreSelection(&Info->VolumeListContext,
                                 Info->hVolumeView,
                                 &Info->Snapshot);
}

VOID
DmMainWndBuildActionContext(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_ACTION_CONTEXT Context)
{
    ZeroMemory(Context, sizeof(*Context));
    Context->hWnd = Info->hMainWnd;
    Context->hStatusBar = Info->hStatusBar;
    Context->Snapshot = &Info->Snapshot;

    if (Info->HasCommandContextOverride)
    {
        Context->Disk = (PDM_DISK)Info->CommandDiskOverride;
        Context->Region = (PDM_REGION)Info->CommandRegionOverride;
        Context->Volume = (PDM_VOLUME)Info->CommandVolumeOverride;
    }
    else if (Info->FocusTarget == DmViewTargetBottom &&
             Info->SelectedDisk != NULL)
    {
        Context->Disk = (PDM_DISK)Info->SelectedDisk;
        Context->Region = (PDM_REGION)Info->SelectedRegion;
        Context->Volume = (Info->SelectedRegion != NULL)
                              ? Info->SelectedRegion->Volume
                              : NULL;
    }
    else
    {
        Context->Volume = DmMainWndGetSelectedVolume(Info);

        if (Context->Volume != NULL)
        {
            Context->Disk = Context->Volume->Disk;
            Context->Region = Context->Volume->Region;
        }
    }
}

VOID
DmMainWndUpdateMenuState(
    _In_ PMAIN_WND_INFO Info)
{
    DM_ACTION_CONTEXT ActionContext;

    if (Info == NULL)
        return;

    DmMainWndBuildActionContext(Info, &ActionContext);
    DmMenuStateSetActionContext(&Info->MenuState, &ActionContext);
    DmMenuStateSetFocus(&Info->MenuState,
                        DmMainWndMenuFocusFromViewTarget(Info->FocusTarget),
                        DmLayoutViewCommandFromTarget(Info->FocusTarget));

    if (Info->hMenu != NULL)
    {
        DmMainWndUpdateDynamicActionMenuText(Info);
        DmMenuStateApplyMenu(&Info->MenuState, Info->hMenu);
        DrawMenuBar(Info->hMainWnd);
    }

    if (Info->hToolbar != NULL)
    {
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_NAV_BACK, MAKELONG(FALSE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_NAV_FORWARD, MAKELONG(FALSE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_ACTION_RESCAN, MAKELONG(TRUE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_ACTION_REFRESH, MAKELONG(TRUE, 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_CREATE_PARTITION,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_CREATE_PARTITION), 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_DELETE_VOLUME,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_DELETE_VOLUME), 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_PROPERTIES,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_PROPERTIES), 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_HELP_ABOUT, MAKELONG(TRUE, 0));
        InvalidateRect(Info->hToolbar, NULL, TRUE);
    }
}

static VOID
DmMainWndPopulateDiskList(
    _In_ PMAIN_WND_INFO Info)
{
    const DM_DISK *Disk;
    LVITEMW Item;
    WCHAR Text[64];
    ULONGLONG Unallocated;
    ULONG DiskIndex;
    ULONG RegionIndex;

    if (Info->hDiskListView == NULL)
        return;

    ListView_DeleteAllItems(Info->hDiskListView);
    for (DiskIndex = 0; DiskIndex < Info->Snapshot.DiskCount; DiskIndex++)
    {
        Disk = &Info->Snapshot.Disks[DiskIndex];

        StringCchPrintfW(Text, ARRAYSIZE(Text), L"Disk %lu", Disk->DiskNumber);
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT;
        Item.iItem = (INT)DiskIndex;
        Item.pszText = Text;
        ListView_InsertItem(Info->hDiskListView, &Item);

        StringCchCopyW(Text,
                       ARRAYSIZE(Text),
                       Disk->IsDynamic ? L"Dynamic" : (Disk->IsRemovable ? L"Removable" : L"Basic"));
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 1, Text);

        DmDiskViewFormatSize(DmDiskViewGetShownDiskSize(Disk), Text, ARRAYSIZE(Text));
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 2, Text);

        Unallocated = 0;
        for (RegionIndex = 0; RegionIndex < Disk->RegionCount; RegionIndex++)
        {
            if (Disk->Regions[RegionIndex].Type == DmRegionFree && !Disk->Regions[RegionIndex].IsLogical)
                Unallocated += Disk->Regions[RegionIndex].Length;
        }
        if (Unallocated < 1024ULL * 1024ULL)
            StringCchCopyW(Text, ARRAYSIZE(Text), L"0 MB");
        else
            DmDiskViewFormatSize(Unallocated, Text, ARRAYSIZE(Text));
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 3, Text);

        StringCchCopyW(Text, ARRAYSIZE(Text), Disk->IsOffline ? L"Offline" : L"Online");
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 4, Text);

        DmGetBusTypeLabel(Disk->BusType, Text, ARRAYSIZE(Text));
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 5, Text);

        DmGetPartitionStyleLabel(Disk->PartitionStyle, Text, ARRAYSIZE(Text));
        ListView_SetItemText(Info->hDiskListView, (INT)DiskIndex, 6, Text);
    }
}

VOID
DmMainWndPopulateVolumeList(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListPopulate(Info->hVolumeView, &Info->Snapshot);
    DmSortApplyToListView(Info->hVolumeView, &Info->SortContext);
    DmMainWndPopulateDiskList(Info);
    DmMainWndRestoreSelectedVolume(Info);
    DmMainWndUpdateMenuState(Info);
}

static VOID
DmDrivePathsFill(
    _In_ HWND hwndDlg,
    _In_ PMAIN_WND_INFO Info)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_DP_LIST);
    const DM_VOLUME *Volume;
    LVITEMW Item;
    WCHAR Name[MAX_PATH];
    ULONG VolumeIndex;
    ULONG PathIndex;
    INT Row = 0;

    ListView_DeleteAllItems(hList);
    for (VolumeIndex = 0; VolumeIndex < Info->Snapshot.VolumeCount; VolumeIndex++)
    {
        Volume = &Info->Snapshot.Volumes[VolumeIndex];
        DmVolumeListBuildName(Volume, Name, ARRAYSIZE(Name));
        for (PathIndex = 0;
             PathIndex < Volume->FolderMountPointCount && PathIndex < DM_MAX_MOUNT_PATHS;
             PathIndex++)
        {
            ZeroMemory(&Item, sizeof(Item));
            Item.mask = LVIF_TEXT;
            Item.iItem = Row;
            Item.pszText = (LPWSTR)Volume->FolderMountPoints[PathIndex];
            ListView_InsertItem(hList, &Item);
            ListView_SetItemText(hList, Row, 1, Name);
            Row++;
        }
    }

    EnableWindow(GetDlgItem(hwndDlg, IDC_DP_REMOVE), Row != 0);
}

static INT_PTR CALLBACK
DmDrivePathsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PMAIN_WND_INFO Info = (PMAIN_WND_INFO)GetWindowLongPtrW(hwndDlg, DWLP_USER);
    HWND hList;
    LVCOLUMNW Column;
    WCHAR Text[MAX_PATH];
    INT Index;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Info = (PMAIN_WND_INFO)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, lParam);
            hList = GetDlgItem(hwndDlg, IDC_DP_LIST);
            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);
            ZeroMemory(&Column, sizeof(Column));
            Column.mask = LVCF_TEXT | LVCF_WIDTH;
            Column.pszText = Text;
            Column.cx = 220;
            LoadStringW(hInstance, IDS_DP_COL_FOLDER, Text, ARRAYSIZE(Text));
            ListView_InsertColumn(hList, 0, &Column);
            Column.cx = 140;
            LoadStringW(hInstance, IDS_DP_COL_VOLUME, Text, ARRAYSIZE(Text));
            ListView_InsertColumn(hList, 1, &Column);
            DmDrivePathsFill(hwndDlg, Info);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_DP_REMOVE:
                    hList = GetDlgItem(hwndDlg, IDC_DP_LIST);
                    Index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                    if (Index < 0)
                        return TRUE;
                    ListView_GetItemText(hList, Index, 0, Text, ARRAYSIZE(Text));
                    if (Text[0] != UNICODE_NULL && Text[wcslen(Text) - 1] != L'\\')
                        StringCchCatW(Text, ARRAYSIZE(Text), L"\\");
                    if (!DeleteVolumeMountPointW(Text))
                    {
                        MessageBoxW(hwndDlg,
                                    L"The drive path could not be removed.",
                                    L"Disk Management",
                                    MB_OK | MB_ICONERROR);
                        return TRUE;
                    }
                    DmSnapshotRefresh(&Info->Snapshot);
                    DmMainWndPopulateVolumeList(Info);
                    InvalidateRect(Info->hDiskView, NULL, TRUE);
                    DmDrivePathsFill(hwndDlg, Info);
                    return TRUE;

                case IDOK:
                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}

VOID
DmMainWndShowDrivePaths(
    _In_ PMAIN_WND_INFO Info)
{
    if (Info == NULL)
        return;

    DialogBoxParamW(hInstance,
                    MAKEINTRESOURCEW(IDD_DRIVE_PATHS),
                    Info->hMainWnd,
                    DmDrivePathsDlgProc,
                    (LPARAM)Info);
}

VOID
DmMainWndOpenVolume(
    _In_ PMAIN_WND_INFO Info,
    _In_ BOOL Explore)
{
    DM_ACTION_CONTEXT ActionContext;
    WCHAR Path[MAX_PATH];

    if (Info == NULL)
        return;

    DmMainWndBuildActionContext(Info, &ActionContext);
    if (ActionContext.Volume == NULL && ActionContext.Region != NULL)
        ActionContext.Volume = ActionContext.Region->Volume;

    if (ActionContext.Volume == NULL)
        return;

    if (ActionContext.Volume->HasDriveLetter)
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"%C:\\", towupper(ActionContext.Volume->DriveLetter));
    else if (ActionContext.Volume->FolderMountPoint[0] != UNICODE_NULL)
        StringCchCopyW(Path, ARRAYSIZE(Path), ActionContext.Volume->FolderMountPoint);
    else
        return;

    ShellExecuteW(Info->hMainWnd, Explore ? L"explore" : L"open", Path, NULL, NULL, SW_SHOWNORMAL);
}

BOOL
DmMainWndExecuteCommand(
    _In_ PMAIN_WND_INFO Info,
    _In_ UINT CommandId)
{
    HCURSOR hWait;
    HCURSOR hOldCursor;
    DM_ACTION_CONTEXT ActionContext;
    BOOL RefreshesSnapshot;
    BOOL Success;

    if (Info == NULL)
        return FALSE;

    DmMainWndBuildActionContext(Info, &ActionContext);
    RefreshesSnapshot = (CommandId == IDM_ACTION_REFRESH ||
                         CommandId == IDM_ACTION_RESCAN ||
                         DmActionMutatesSnapshot(CommandId));

    if (RefreshesSnapshot)
    {
        DmMainWndCacheSelectedVolumeName(Info);
        Info->SelectedDisk = NULL;
        Info->SelectedRegion = NULL;

        /* The local action context has already captured the clicked target.
           Do not retain snapshot pointers across a refresh. */
        Info->HasCommandContextOverride = FALSE;
        Info->CommandDiskOverride = NULL;
        Info->CommandRegionOverride = NULL;
        Info->CommandVolumeOverride = NULL;
    }


    hWait = LoadCursorW(NULL, IDC_WAIT);
    hOldCursor = SetCursor(hWait);
    Info->bInCommand = TRUE;
    Success = DmActionExecute(&ActionContext, CommandId);
    Info->bInCommand = FALSE;
    SetCursor(hOldCursor);

    /* Deferred drive letter / format settings from a create action are
       applied by the pending timer once the new volume arrives; mutating
       actions also get a delayed refresh so asynchronous PnP arrivals and
       removals are picked up without a manual refresh. */
    if (DmActionHasPendingCreate())
    {
        SetTimer(Info->hMainWnd, IDT_PENDING_CREATE, DISKMGMT_PENDING_CREATE_POLL_MS, NULL);
    }
    if (Success && DmActionMutatesSnapshot(CommandId))
    {
        /* Synchronize the view with the committed layout immediately. The
           device-change timer remains armed to pick up delayed PnP changes. */
        DmSnapshotRefresh(&Info->Snapshot);
        Info->DeviceChangeShot = 0;
        SetTimer(Info->hMainWnd,
                 IDT_DEVICE_CHANGE,
                 DISKMGMT_DEVICE_CHANGE_POLL_MS,
                 NULL);
    }

    if (!Success)
    {
        StatusBarLoadString(Info->hStatusBar, 0, hInstance, IDS_STATUS_READY);
        return FALSE;
    }

    if (RefreshesSnapshot)
    {
        DmMainWndPopulateVolumeList(Info);
        InvalidateRect(Info->hDiskView, NULL, TRUE);
        DmMainWndUpdateDiskViewScrollInfo(Info);
    }

    DmMainWndUpdateMenuState(Info);
    StatusBarLoadString(Info->hStatusBar, 0, hInstance, IDS_STATUS_READY);
    return TRUE;
}
