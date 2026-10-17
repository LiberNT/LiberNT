/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Main window: disk sidebar, details and operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "diskmgmt11.h"

#include <dbt.h>

#define DU_MAIN_CLASS   L"LiberNTDiskManagement"
#define IDT_REFRESH     1
#define DU_INFO_MAX     6

#define ITEM_CATEGORY   1
#define ITEM_DISK       2
#define ITEM_PART       3
#define ITEM_LPARAM(Type, Disk, Part) ((LPARAM)(((Type) << 24) | (((Disk) & 0xFF) << 16) | ((Part) & 0xFFFF)))
#define ITEM_TYPE(l)    ((ULONG)((l) >> 24) & 0xFF)
#define ITEM_DISKIDX(l) ((ULONG)((l) >> 16) & 0xFF)
#define ITEM_PARTIDX(l) ((ULONG)(l) & 0xFFFF)

HINSTANCE g_hInstance;
DU_MODEL g_Model;

static const struct
{
    UINT Command;
    UINT Text;
} Commands[] =
{
    { IDC_NEWVOLUME, IDS_TB_NEW },
    { IDC_DELETE, IDS_TB_DELETE },
    { IDC_RESIZE, IDS_TB_RESIZE },
    { IDC_ERASE, IDS_TB_ERASE },
    { IDC_CHECK, IDS_TB_CHECK },
    { IDC_MOUNT, IDS_TB_MOUNT },
    { IDC_INFO, IDS_TB_INFO },
    { IDC_REFRESH, IDS_TB_REFRESH },
};

typedef struct _DU_MAIN
{
    HWND hwnd;
    HWND Tree;
    HWND Title;
    HWND Sub1;
    HWND Sub2;
    HWND Parts;
    HWND Usage;
    HWND UsageText;
    HWND InfoLabel[DU_INFO_MAX];
    HWND InfoValue[DU_INFO_MAX];
    INT InfoCount;
    INT InfoLabelWidth;
    INT InfoValueWidth[DU_INFO_MAX];
    INT ButtonWidth[ARRAYSIZE(Commands)];
    HIMAGELIST SmallImages;
    HFONT Font;
    HFONT TitleFont;
    INT Dpi;
    INT LineHeight;
    INT TitleHeight;
    INT SidebarWidth;
    INT Corner;
    BOOL ShowParts;
    BOOL ShowUsage;
    DU_SELECTION Sel;
    BOOL Rebuilding;
    BOOL Filling;
} DU_MAIN;

static DU_MAIN Main;

static INT
S(INT Value)
{
    return MulDiv(Value, Main.Dpi, 96);
}

INT
DuMessage(
    _In_ HWND hwnd,
    _In_ PCWSTR Icon,
    _In_ TASKDIALOG_COMMON_BUTTON_FLAGS Buttons,
    _In_ PCWSTR MainText,
    _In_opt_ PCWSTR Content)
{
    TASKDIALOGCONFIG Config;
    INT Button = IDCANCEL;

    ZeroMemory(&Config, sizeof(Config));
    Config.cbSize = sizeof(Config);
    Config.hwndParent = hwnd;
    Config.hInstance = g_hInstance;
    Config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    Config.dwCommonButtons = Buttons;
    Config.pszWindowTitle = MAKEINTRESOURCEW(IDS_APPTITLE);
    Config.pszMainIcon = Icon;
    Config.pszMainInstruction = MainText;
    Config.pszContent = Content;
    Config.nDefaultButton = (Buttons & TDCBF_CANCEL_BUTTON) ? IDCANCEL : 0;
    if (FAILED(TaskDialogIndirect(&Config, &Button, NULL, NULL)))
        return IDCANCEL;
    return Button;
}

VOID
DuError(
    _In_ HWND hwnd,
    _In_ UINT MainId,
    _In_ DWORD Error)
{
    WCHAR MainText[256];
    PWSTR Message = NULL;

    DuLoadString(MainId, MainText, ARRAYSIZE(MainText));
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, Error, 0, (PWSTR)&Message, 0, NULL);
    DuMessage(hwnd, TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, MainText, Message);
    if (Message)
        LocalFree(Message);
}

static HFONT
CreateUiFont(
    _In_ INT PointSize,
    _In_ LONG Weight)
{
    NONCLIENTMETRICSW Metrics;
    LOGFONTW Font;

    Metrics.cbSize = sizeof(Metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
        return NULL;
    Font = Metrics.lfMessageFont;
    if (PointSize)
        Font.lfHeight = -MulDiv(PointSize, Main.Dpi, 72);
    Font.lfWeight = Weight;
    return CreateFontIndirectW(&Font);
}

static INT
SysIconIndex(
    _In_ const DU_DISK *Disk,
    _In_opt_ const DU_PART *Part)
{
    SHSTOCKICONINFO Info;
    SHSTOCKICONID Id = Disk->Category == DuCategoryExternal ? SIID_DRIVEREMOVE : SIID_DRIVEFIXED;

    if (Part)
    {
        PDU_VOLUME Volume = DuPartVolume(&g_Model, Part);
        SHFILEINFOW File;

        if (Volume && Volume->HasFileSystem && Volume->MountPath[0] &&
            SHGetFileInfoW(Volume->MountPath, 0, &File, sizeof(File), SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
        {
            return File.iIcon;
        }
        if (Part->Kind == DuPartFree || !Volume || !Volume->HasFileSystem)
            Id = SIID_DRIVEUNKNOWN;
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    if (FAILED(SHGetStockIconInfo(Id, SHGSI_SYSICONINDEX, &Info)))
        return -1;
    return Info.iSysImageIndex;
}

static HTREEITEM
TreeAdd(
    _In_ HTREEITEM Parent,
    _In_ PCWSTR Text,
    _In_ INT Image,
    _In_ UINT State,
    _In_ LPARAM lParam)
{
    TVINSERTSTRUCTW Insert;

    ZeroMemory(&Insert, sizeof(Insert));
    Insert.hParent = Parent;
    Insert.hInsertAfter = TVI_LAST;
    Insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    Insert.item.pszText = (PWSTR)Text;
    Insert.item.lParam = lParam;
    Insert.item.state = TVIS_EXPANDED | State;
    Insert.item.stateMask = TVIS_EXPANDED | TVIS_BOLD;
    Insert.item.iImage = Image;
    Insert.item.iSelectedImage = Image;
    return (HTREEITEM)SendMessageW(Main.Tree, TVM_INSERTITEMW, 0, (LPARAM)&Insert);
}

static VOID
RebuildTree(
    _In_ ULONG KeepDisk,
    _In_ ULONGLONG KeepOffset,
    _In_ BOOL KeepPart)
{
    HTREEITEM Select = NULL, First = NULL;
    ULONG Category, d, p;

    Main.Rebuilding = TRUE;
    SendMessageW(Main.Tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(Main.Tree);

    for (Category = 0; Category < DuCategoryCount; Category++)
    {
        HTREEITEM CategoryItem = NULL;
        WCHAR Text[MAX_PATH];

        for (d = 0; d < g_Model.DiskCount; d++)
        {
            PDU_DISK Disk = &g_Model.Disks[d];
            HTREEITEM DiskItem;

            if (Disk->Category != Category)
                continue;
            if (!CategoryItem)
            {
                DuLoadString(IDS_CAT_INTERNAL + Category, Text, ARRAYSIZE(Text));
                CategoryItem = TreeAdd(TVI_ROOT, Text, I_IMAGENONE, TVIS_BOLD, ITEM_LPARAM(ITEM_CATEGORY, 0, Category));
            }
            DuDiskTitle(Disk, Text, ARRAYSIZE(Text));
            DiskItem = TreeAdd(CategoryItem, Text, SysIconIndex(Disk, NULL), 0, ITEM_LPARAM(ITEM_DISK, d, 0));
            if (!First)
                First = DiskItem;
            if (!KeepPart && Disk->Number == KeepDisk)
                Select = DiskItem;

            for (p = 0; p < Disk->PartCount; p++)
            {
                PDU_PART Part = &Disk->Parts[p];
                HTREEITEM PartItem;

                if (Part->Kind == DuPartMsr)
                    continue;
                DuPartTitle(&g_Model, Part, Text, ARRAYSIZE(Text));
                PartItem = TreeAdd(DiskItem, Text, SysIconIndex(Disk, Part), 0, ITEM_LPARAM(ITEM_PART, d, p));
                if (KeepPart && Disk->Number == KeepDisk && Part->Offset == KeepOffset)
                    Select = PartItem;
            }
        }
    }

    SendMessageW(Main.Tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(Main.Tree, NULL, TRUE);
    Main.Rebuilding = FALSE;
    Main.Sel.Type = DuSelNone;
    if (!Select)
        Select = First;
    if (Select)
    {
        TreeView_SelectItem(Main.Tree, Select);
        TreeView_EnsureVisible(Main.Tree, Select);
    }
}

static VOID
SetInfo(
    _In_reads_(Count) const UINT *Names,
    _In_reads_(Count) PCWSTR *Values,
    _In_ INT Count)
{
    WCHAR Name[128];
    HDC hdc = GetDC(Main.hwnd);
    HGDIOBJ Old = SelectObject(hdc, Main.Font);
    SIZE Size;
    INT i;

    Main.InfoCount = 0;
    Main.InfoLabelWidth = 0;
    for (i = 0; i < Count && Main.InfoCount < DU_INFO_MAX; i++)
    {
        if (!Values[i][0])
            continue;
        DuLoadString(Names[i], Name, ARRAYSIZE(Name));
        SetWindowTextW(Main.InfoLabel[Main.InfoCount], Name);
        SetWindowTextW(Main.InfoValue[Main.InfoCount], Values[i]);
        if (GetTextExtentPoint32W(hdc, Name, lstrlenW(Name), &Size))
            Main.InfoLabelWidth = max(Main.InfoLabelWidth, Size.cx);
        Main.InfoValueWidth[Main.InfoCount] =
            GetTextExtentPoint32W(hdc, Values[i], lstrlenW(Values[i]), &Size) ? Size.cx : 0;
        Main.InfoCount++;
    }
    SelectObject(hdc, Old);
    ReleaseDC(Main.hwnd, hdc);
}

static VOID
FillParts(
    _In_ PDU_DISK Disk)
{
    ULONG p;
    INT Row = 0;

    Main.Filling = TRUE;
    SendMessageW(Main.Parts, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(Main.Parts);
    for (p = 0; p < Disk->PartCount; p++)
    {
        PDU_PART Part = &Disk->Parts[p];
        PDU_VOLUME Volume = DuPartVolume(&g_Model, Part);
        WCHAR Text[MAX_PATH];
        LVITEMW Item;

        DuPartTitle(&g_Model, Part, Text, ARRAYSIZE(Text));
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        Item.iItem = Row;
        Item.pszText = Text;
        Item.lParam = (LPARAM)p;
        Item.iImage = SysIconIndex(Disk, Part);
        SendMessageW(Main.Parts, LVM_INSERTITEMW, 0, (LPARAM)&Item);

        DuPartKindName(Disk, Part, Text, ARRAYSIZE(Text));
        ListView_SetItemText(Main.Parts, Row, 1, Text);
        StringCchCopyW(Text, ARRAYSIZE(Text), (Volume && Volume->HasFileSystem) ? Volume->FileSystem : L"");
        ListView_SetItemText(Main.Parts, Row, 2, Text);
        DuFormatSize(Part->Length, Text, ARRAYSIZE(Text));
        ListView_SetItemText(Main.Parts, Row, 3, Text);
        if (Volume && Volume->HasFileSystem)
            DuFormatSize(Volume->Free, Text, ARRAYSIZE(Text));
        else
            Text[0] = UNICODE_NULL;
        ListView_SetItemText(Main.Parts, Row, 4, Text);
        Row++;
    }
    SendMessageW(Main.Parts, WM_SETREDRAW, TRUE, 0);
    Main.Filling = FALSE;
}

static VOID
ShowDiskDetails(
    _In_ PDU_DISK Disk)
{
    static const UINT Names[] = { IDS_INFO_UNALLOCATED, IDS_INFO_SECTOR, IDS_INFO_DEVICE, IDS_INFO_DISKID, IDS_INFO_SERIAL };
    WCHAR Text[256], Scheme[64], Bus[64], DiskName[64], Count[64];
    WCHAR Capacity[32], Unalloc[32], Sector[32], Device[64], Id[64];
    PCWSTR Values[ARRAYSIZE(Names)];
    ULONGLONG Free = 0;
    ULONG p, Used = 0;

    DuDiskTitle(Disk, Text, ARRAYSIZE(Text));
    SetWindowTextW(Main.Title, Text);
    DuLoadString(Disk->Style == PARTITION_STYLE_GPT ? IDS_SCHEME_GPT :
                 Disk->Style == PARTITION_STYLE_MBR ? IDS_SCHEME_MBR : IDS_SCHEME_RAW,
                 Scheme, ARRAYSIZE(Scheme));
    DuBusName(Disk->Bus, Bus, ARRAYSIZE(Bus));
    StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s \x2022 %s", Scheme, Bus);
    SetWindowTextW(Main.Sub1, Text);

    for (p = 0; p < Disk->PartCount; p++)
    {
        if (Disk->Parts[p].Kind == DuPartFree)
            Free += Disk->Parts[p].Length;
        else
            Used++;
    }
    if (Disk->Style == PARTITION_STYLE_RAW)
        Free = Disk->Size;
    DuFormatSize(Disk->Size, Capacity, ARRAYSIZE(Capacity));
    DuPrintf(DiskName, ARRAYSIZE(DiskName), IDS_DISK_N, Disk->Number);
    DuPrintf(Count, ARRAYSIZE(Count), IDS_CAPTION_PARTITIONS, Used);
    StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s \x2022 %s \x2022 %s", DiskName, Capacity, Count);
    SetWindowTextW(Main.Sub2, Text);

    DuFormatSize(Free, Unalloc, ARRAYSIZE(Unalloc));
    StringCchPrintfW(Sector, ARRAYSIZE(Sector), L"%lu", Disk->BytesPerSector);
    StringCchPrintfW(Device, ARRAYSIZE(Device), L"\\\\.\\PhysicalDrive%lu", Disk->Number);
    Id[0] = UNICODE_NULL;
    if (Disk->Style == PARTITION_STYLE_GPT)
        StringFromGUID2(&Disk->DiskId, Id, ARRAYSIZE(Id));
    else if (Disk->Style == PARTITION_STYLE_MBR)
        StringCchPrintfW(Id, ARRAYSIZE(Id), L"%08lX", Disk->Signature);

    Values[0] = Unalloc; Values[1] = Sector; Values[2] = Device; Values[3] = Id; Values[4] = Disk->Serial;
    SetInfo(Names, Values, ARRAYSIZE(Names));
    FillParts(Disk);
    Main.ShowParts = TRUE;
    Main.ShowUsage = FALSE;
}

static VOID
ShowPartDetails(
    _In_ PDU_DISK Disk,
    _In_ ULONG Index)
{
    static const UINT Names[] = { IDS_INFO_MOUNT, IDS_INFO_DEVICE, IDS_INFO_OFFSET, IDS_INFO_VOLSERIAL };
    PDU_PART Part = &Disk->Parts[Index];
    PDU_VOLUME Volume = DuPartVolume(&g_Model, Part);
    BOOL HasFs = Volume && Volume->HasFileSystem;
    WCHAR Text[256], Kind[64], Role[128], Location[64];
    WCHAR Mount[MAX_PATH], Capacity[32], Offset[32], Serial[32];
    PCWSTR Values[ARRAYSIZE(Names)];

    DuPartTitle(&g_Model, Part, Text, ARRAYSIZE(Text));
    SetWindowTextW(Main.Title, Text);
    DuPartKindName(Disk, Part, Kind, ARRAYSIZE(Kind));
    if (HasFs)
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s \x2022 %s", Volume->FileSystem, Kind);
    else
        StringCchCopyW(Text, ARRAYSIZE(Text), Kind);
    SetWindowTextW(Main.Sub1, Text);

    DuFormatSize(Part->Length, Capacity, ARRAYSIZE(Capacity));
    if (Part->Number)
        DuPrintf(Location, ARRAYSIZE(Location), IDS_LOCATION, Disk->Number, Part->Number);
    else
        DuPrintf(Location, ARRAYSIZE(Location), IDS_DISK_N, Disk->Number);
    Role[0] = UNICODE_NULL;
    if (Volume && Volume->Boot)
        DuLoadString(IDS_SYSTEM_VOLUME, Role, ARRAYSIZE(Role));
    else if (Part->Kind == DuPartEsp)
        DuLoadString(IDS_BOOT_PARTITION, Role, ARRAYSIZE(Role));
    if (Role[0])
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s \x2022 %s \x2022 %s", Capacity, Location, Role);
    else
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s \x2022 %s", Capacity, Location);
    SetWindowTextW(Main.Sub2, Text);

    Mount[0] = UNICODE_NULL;
    if (Volume && Volume->MountPath[0] && !(Volume->MountPath[1] == L':' && Volume->MountPath[3] == UNICODE_NULL))
        StringCchCopyW(Mount, ARRAYSIZE(Mount), Volume->MountPath);
    Serial[0] = UNICODE_NULL;
    if (HasFs)
    {
        ULONGLONG UsedBytes = Volume->Capacity - min(Volume->Capacity, Volume->Free);
        WCHAR Used[32], Avail[32];

        StringCchPrintfW(Serial, ARRAYSIZE(Serial), L"%04X-%04X", HIWORD(Volume->Serial), LOWORD(Volume->Serial));
        SendMessageW(Main.Usage, PBM_SETRANGE32, 0, 1000);
        SendMessageW(Main.Usage, PBM_SETPOS, Volume->Capacity ? (WPARAM)(UsedBytes * 1000 / Volume->Capacity) : 0, 0);
        SendMessageW(Main.Usage, PBM_SETSTATE,
                     (Volume->Capacity && Volume->Free * 10 < Volume->Capacity) ? PBST_ERROR : PBST_NORMAL, 0);
        DuFormatSize(UsedBytes, Used, ARRAYSIZE(Used));
        DuFormatSize(Volume->Free, Avail, ARRAYSIZE(Avail));
        DuPrintf(Text, ARRAYSIZE(Text), IDS_USAGE, Used, Avail);
        SetWindowTextW(Main.UsageText, Text);
    }
    DuFormatSize(Part->Offset, Offset, ARRAYSIZE(Offset));

    Values[0] = Mount; Values[1] = Volume ? Volume->DeviceName : L""; Values[2] = Offset; Values[3] = Serial;
    SetInfo(Names, Values, ARRAYSIZE(Names));
    Main.ShowParts = FALSE;
    Main.ShowUsage = HasFs;
}

static VOID
UpdateButtons(VOID)
{
    WCHAR Text[64];

    EnableWindow(GetDlgItem(Main.hwnd, IDC_NEWVOLUME), DuCanNewVolume(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_DELETE), DuCanDelete(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_CHECK), DuCanCheck(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_RESIZE), DuCanResize(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_ERASE), DuCanErase(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_MOUNT), DuCanMount(&Main.Sel));
    EnableWindow(GetDlgItem(Main.hwnd, IDC_INFO), DuCanInfo(&Main.Sel));

    DuLoadString(DuIsMounted(&Main.Sel) ? IDS_TB_UNMOUNT : IDS_TB_MOUNT, Text, ARRAYSIZE(Text));
    SetWindowTextW(GetDlgItem(Main.hwnd, IDC_MOUNT), Text);
}

static INT
ListRowHeight(VOID)
{
    RECT rcItem;

    if (ListView_GetItemCount(Main.Parts) > 0)
    {
        rcItem.left = LVIR_BOUNDS;
        if (SendMessageW(Main.Parts, LVM_GETITEMRECT, 0, (LPARAM)&rcItem))
            return rcItem.bottom - rcItem.top;
    }
    return S(20);
}

static INT
ListHeaderHeight(VOID)
{
    HWND Header = ListView_GetHeader(Main.Parts);
    RECT rc;

    if (Header && GetWindowRect(Header, &rc) && rc.bottom > rc.top)
        return rc.bottom - rc.top;
    return S(22);
}

static VOID
Layout(VOID)
{
    RECT rc;
    INT Left, Right, Width, x, y, i, Rows, Height, Label, Row, PerColumn, Second;
    UINT Flags = SWP_NOZORDER | SWP_NOACTIVATE;
    UINT TextFlags = Flags | SWP_NOCOPYBITS;
    HDWP Defer;

    if (!Main.Tree)
        return;
    GetClientRect(Main.hwnd, &rc);
    Left = Main.SidebarWidth + S(14);
    Right = rc.right - S(16);
    Width = max(Right - Left, S(200));
    Row = Main.LineHeight + S(2);
    PerColumn = (Main.InfoCount + 1) / 2;

    Defer = BeginDeferWindowPos(40);
    Defer = DeferWindowPos(Defer, Main.Tree, NULL, S(8), S(8), Main.SidebarWidth - S(12), rc.bottom - S(16), Flags);

    y = Main.Corner + S(8);
    Defer = DeferWindowPos(Defer, Main.Title, NULL, Left, y, Width, Main.TitleHeight, TextFlags);
    y += Main.TitleHeight + S(2);
    Defer = DeferWindowPos(Defer, Main.Sub1, NULL, Left, y, Width, Main.LineHeight, TextFlags);
    y += Main.LineHeight;
    Defer = DeferWindowPos(Defer, Main.Sub2, NULL, Left, y, Width, Main.LineHeight, TextFlags);
    y += Main.LineHeight + S(10);

    x = Left;
    for (i = 0; i < (INT)ARRAYSIZE(Commands); i++)
    {
        INT w = Main.ButtonWidth[i];

        if (Commands[i].Command == IDC_REFRESH)
            x = Right - w;
        Defer = DeferWindowPos(Defer, GetDlgItem(Main.hwnd, Commands[i].Command), NULL, x, y, w, S(23), Flags);
        x += w + S(6);
    }
    y += S(23) + S(10);

    if (Main.ShowParts)
    {
        Rows = max(ListView_GetItemCount(Main.Parts), 1);
        Height = ListHeaderHeight() + Rows * ListRowHeight() + S(6);
        Height = min(Height, max(S(80), rc.bottom - y - PerColumn * Row - S(24)));
        Defer = DeferWindowPos(Defer, Main.Parts, NULL, Left, y, Width, Height, Flags);
        y += Height + S(10);
    }

    if (Main.ShowUsage)
    {
        Defer = DeferWindowPos(Defer, Main.Usage, NULL, Left, y, Width, S(12), Flags);
        Defer = DeferWindowPos(Defer, Main.UsageText, NULL, Left, y + S(16), Width, Main.LineHeight, TextFlags);
        y += S(16) + Main.LineHeight + S(8);
    }

    Label = Main.InfoLabelWidth + S(12);
    Second = 0;
    for (i = 0; i < PerColumn; i++)
        Second = max(Second, Main.InfoValueWidth[i]);
    Second = min(Left + Label + Second + S(32), Left + Width / 2);
    for (i = 0; i < Main.InfoCount; i++)
    {
        INT Top = y + (i % PerColumn) * Row;
        INT Column = (i < PerColumn) ? Left : Second;
        INT Limit = (i < PerColumn) ? Second - S(16) : Right;
        INT Value = max(0, min(Main.InfoValueWidth[i] + S(2), Limit - Column - Label));

        Defer = DeferWindowPos(Defer, Main.InfoLabel[i], NULL, Column, Top, Main.InfoLabelWidth + S(2), Main.LineHeight, TextFlags);
        Defer = DeferWindowPos(Defer, Main.InfoValue[i], NULL, Column + Label, Top, Value, Main.LineHeight, TextFlags);
    }
    EndDeferWindowPos(Defer);

    ListView_SetColumnWidth(Main.Parts, 0, max(S(120), Width - S(170) - 3 * S(90) - S(24)));
}

static VOID
UpdateDetails(VOID)
{
    BOOL Show = Main.Sel.Type != DuSelNone;
    INT i;

    if (Main.Sel.Type == DuSelDisk)
        ShowDiskDetails(&g_Model.Disks[Main.Sel.Disk]);
    else if (Main.Sel.Type == DuSelPart)
        ShowPartDetails(&g_Model.Disks[Main.Sel.Disk], Main.Sel.Part);
    ShowWindow(Main.Title, Show ? SW_SHOW : SW_HIDE);
    ShowWindow(Main.Sub1, Show ? SW_SHOW : SW_HIDE);
    ShowWindow(Main.Sub2, Show ? SW_SHOW : SW_HIDE);
    ShowWindow(Main.Parts, Show && Main.ShowParts ? SW_SHOW : SW_HIDE);
    for (i = 0; i < DU_INFO_MAX; i++)
    {
        ShowWindow(Main.InfoLabel[i], Show && i < Main.InfoCount ? SW_SHOW : SW_HIDE);
        ShowWindow(Main.InfoValue[i], Show && i < Main.InfoCount ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(Main.Usage, Show && Main.ShowUsage ? SW_SHOW : SW_HIDE);
    ShowWindow(Main.UsageText, Show && Main.ShowUsage ? SW_SHOW : SW_HIDE);
    UpdateButtons();
    Layout();
}

static VOID
Refresh(VOID)
{
    ULONG Disk = 0;
    ULONGLONG Offset = 0;
    BOOL KeepPart = FALSE;

    if (Main.Sel.Type != DuSelNone)
    {
        Disk = g_Model.Disks[Main.Sel.Disk].Number;
        if (Main.Sel.Type == DuSelPart)
        {
            Offset = g_Model.Disks[Main.Sel.Disk].Parts[Main.Sel.Part].Offset;
            KeepPart = TRUE;
        }
    }
    DuModelRefresh(&g_Model);
    RebuildTree(Disk, Offset, KeepPart);
    if (!TreeView_GetSelection(Main.Tree))
        UpdateDetails();
}

static VOID
OnTreeSelect(
    _In_ HTREEITEM Item)
{
    TVITEMW tvi;

    if (Main.Rebuilding || !Item)
        return;
    ZeroMemory(&tvi, sizeof(tvi));
    tvi.mask = TVIF_PARAM;
    tvi.hItem = Item;
    if (!TreeView_GetItem(Main.Tree, &tvi))
        return;

    if (ITEM_TYPE(tvi.lParam) == ITEM_CATEGORY)
    {
        HTREEITEM Child = TreeView_GetChild(Main.Tree, Item);

        if (Child)
            TreeView_SelectItem(Main.Tree, Child);
        return;
    }
    Main.Sel.Disk = ITEM_DISKIDX(tvi.lParam);
    Main.Sel.Part = ITEM_PARTIDX(tvi.lParam);
    Main.Sel.Type = ITEM_TYPE(tvi.lParam) == ITEM_DISK ? DuSelDisk : DuSelPart;
    UpdateDetails();
}

static VOID
SelectPartInTree(
    _In_ ULONG Disk,
    _In_ ULONG Part)
{
    HTREEITEM Category, DiskItem, Item;
    TVITEMW tvi;

    for (Category = TreeView_GetRoot(Main.Tree); Category; Category = TreeView_GetNextSibling(Main.Tree, Category))
    {
        for (DiskItem = TreeView_GetChild(Main.Tree, Category); DiskItem; DiskItem = TreeView_GetNextSibling(Main.Tree, DiskItem))
        {
            for (Item = TreeView_GetChild(Main.Tree, DiskItem); Item; Item = TreeView_GetNextSibling(Main.Tree, Item))
            {
                ZeroMemory(&tvi, sizeof(tvi));
                tvi.mask = TVIF_PARAM;
                tvi.hItem = Item;
                if (TreeView_GetItem(Main.Tree, &tvi) &&
                    ITEM_DISKIDX(tvi.lParam) == Disk && ITEM_PARTIDX(tvi.lParam) == Part)
                {
                    TreeView_SelectItem(Main.Tree, Item);
                    return;
                }
            }
        }
    }
}

static VOID
OnPartSelected(
    _In_ INT Row)
{
    LVITEMW Item;

    if (Main.Filling || Main.Sel.Type == DuSelNone || Row < 0)
        return;
    ZeroMemory(&Item, sizeof(Item));
    Item.mask = LVIF_PARAM;
    Item.iItem = Row;
    if (ListView_GetItem(Main.Parts, &Item))
        SelectPartInTree(Main.Sel.Disk, (ULONG)Item.lParam);
}

static VOID
AddColumn(
    _In_ INT Index,
    _In_ UINT Text,
    _In_ INT Width,
    _In_ INT Format)
{
    WCHAR Name[64];
    LVCOLUMNW Column;

    ZeroMemory(&Column, sizeof(Column));
    Column.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    Column.fmt = Format;
    Column.cx = Width;
    DuLoadString(Text, Name, ARRAYSIZE(Name));
    Column.pszText = Name;
    SendMessageW(Main.Parts, LVM_INSERTCOLUMNW, Index, (LPARAM)&Column);
}

static HWND
CreateLabel(
    _In_ DWORD Style,
    _In_opt_ HFONT Font)
{
    HWND Label = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | SS_NOPREFIX | SS_ENDELLIPSIS | Style,
                                 0, 0, 0, 0, Main.hwnd, NULL, g_hInstance, NULL);

    SendMessageW(Label, WM_SETFONT, (WPARAM)(Font ? Font : Main.Font), FALSE);
    return Label;
}

static INT
TextWidth(
    _In_ HDC hdc,
    _In_ UINT Id)
{
    WCHAR Text[64];
    RECT rc = { 0, 0, 0, 0 };

    DuLoadString(Id, Text, ARRAYSIZE(Text));
    DrawTextW(hdc, Text, -1, &rc, DT_CALCRECT | DT_SINGLELINE);
    return rc.right;
}

static VOID
CreateButtons(VOID)
{
    HDC hdc = GetDC(Main.hwnd);
    HGDIOBJ Old = SelectObject(hdc, Main.Font);
    INT i;

    for (i = 0; i < (INT)ARRAYSIZE(Commands); i++)
    {
        WCHAR Text[64];
        HWND Button;
        INT Width;

        DuLoadString(Commands[i].Text, Text, ARRAYSIZE(Text));
        Button = CreateWindowExW(0, WC_BUTTONW, Text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 0, 0, Main.hwnd, (HMENU)(ULONG_PTR)Commands[i].Command, g_hInstance, NULL);
        SendMessageW(Button, WM_SETFONT, (WPARAM)Main.Font, FALSE);
        Width = TextWidth(hdc, Commands[i].Text);
        if (Commands[i].Command == IDC_MOUNT)
            Width = max(Width, TextWidth(hdc, IDS_TB_UNMOUNT));
        Main.ButtonWidth[i] = max(S(54), Width + S(24));
    }
    SelectObject(hdc, Old);
    ReleaseDC(Main.hwnd, hdc);
}

static VOID
PaintPanelEdge(
    _In_ HDC hdc,
    _In_ INT x,
    _In_ INT y,
    _In_ INT cx,
    _In_ INT cy,
    _In_ INT Radius,
    _In_ COLORREF Color)
{
    BITMAPINFO bmi;
    PULONG Bits = NULL;
    HBITMAP hbm;
    HDC hdcMem;
    LONG Limit = (LONG)Radius * 8 * (LONG)Radius * 8;
    INT i, j, sx, sy;

    if (cx <= 0 || cy <= 0)
        return;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    if (!hbm)
        return;
    for (j = 0; j < cy; j++)
    {
        for (i = 0; i < cx; i++)
        {
            ULONG Alpha = 255;

            if (i < Radius && j < Radius)
            {
                ULONG Inside = 0;

                for (sy = 0; sy < 4; sy++)
                {
                    for (sx = 0; sx < 4; sx++)
                    {
                        LONG dx = Radius * 8 - (i * 8 + sx * 2 + 1);
                        LONG dy = Radius * 8 - (j * 8 + sy * 2 + 1);

                        if (dx * dx + dy * dy <= Limit)
                            Inside++;
                    }
                }
                Alpha = Inside * 255 / 16;
            }
            Bits[(SIZE_T)j * cx + i] = (Alpha << 24) |
                                       ((GetRValue(Color) * Alpha / 255) << 16) |
                                       ((GetGValue(Color) * Alpha / 255) << 8) |
                                       (GetBValue(Color) * Alpha / 255);
        }
    }
    hdcMem = CreateCompatibleDC(hdc);
    if (hdcMem)
    {
        HGDIOBJ hOld = SelectObject(hdcMem, hbm);

        BitBlt(hdc, x, y, cx, cy, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
    }
    DeleteObject(hbm);
}

static VOID
PaintMain(
    _In_ HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc, Area;
    HDC hdc = BeginPaint(hwnd, &ps);
    COLORREF Panel = GetSysColor(COLOR_3DFACE);

    GetClientRect(hwnd, &rc);
    Area = rc;
    Area.right = Main.SidebarWidth;
    FillRect(hdc, &Area, GetStockObject(BLACK_BRUSH));
    Area = rc;
    Area.left = Main.SidebarWidth + Main.Corner;
    Area.top = Main.Corner;
    FillRect(hdc, &Area, GetSysColorBrush(COLOR_3DFACE));
    PaintPanelEdge(hdc, Main.SidebarWidth, 0, rc.right - Main.SidebarWidth, Main.Corner, Main.Corner, Panel);
    PaintPanelEdge(hdc, Main.SidebarWidth, Main.Corner, Main.Corner, rc.bottom - Main.Corner, 0, Panel);
    EndPaint(hwnd, &ps);
}

static VOID
RunCommand(
    _In_ UINT Command)
{
    DU_SELECTION Sel = Main.Sel;
    BOOL Changed = FALSE;

    switch (Command)
    {
        case IDC_NEWVOLUME:
            Changed = DuNewVolume(Main.hwnd, &Sel);
            break;
        case IDC_DELETE:
            Changed = DuDelete(Main.hwnd, &Sel);
            break;
        case IDC_CHECK:
            Changed = DuCheck(Main.hwnd, &Sel);
            break;
        case IDC_RESIZE:
            Changed = DuResize(Main.hwnd, &Sel);
            break;
        case IDC_ERASE:
            Changed = DuErase(Main.hwnd, &Sel);
            break;
        case IDC_MOUNT:
            Changed = DuMount(Main.hwnd, &Sel);
            break;
        case IDC_INFO:
            DuInfo(Main.hwnd, &Sel);
            break;
        case IDC_REFRESH:
            Changed = TRUE;
            break;
    }
    if (Changed)
        Refresh();
}

static VOID
OnCreate(
    _In_ HWND hwnd)
{
    MARGINS Margins = { 0, 0, 0, 0 };
    SHFILEINFOW File;
    TEXTMETRICW Metrics;
    HDC hdc = GetDC(hwnd);
    HGDIOBJ Old;
    INT i;

    Main.hwnd = hwnd;
    Main.Dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    Main.SidebarWidth = S(220);
    Main.Corner = S(8);
    Main.Font = CreateUiFont(0, FW_NORMAL);
    Main.TitleFont = CreateUiFont(12, FW_BOLD);
    Old = SelectObject(hdc, Main.Font);
    Main.LineHeight = GetTextMetricsW(hdc, &Metrics) ? Metrics.tmHeight + Metrics.tmExternalLeading : S(16);
    SelectObject(hdc, Main.TitleFont);
    Main.TitleHeight = GetTextMetricsW(hdc, &Metrics) ? Metrics.tmHeight + Metrics.tmExternalLeading : S(22);
    SelectObject(hdc, Old);
    ReleaseDC(hwnd, hdc);

    Margins.cxLeftWidth = Main.SidebarWidth + Main.Corner;
    Margins.cyTopHeight = Main.Corner;
    DwmExtendFrameIntoClientArea(hwnd, &Margins);

    Main.SmallImages = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &File, sizeof(File), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

    Main.Tree = CreateWindowExW(0, WC_TREEVIEWW, NULL,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT |
                                TVS_DISABLEDRAGDROP | TVS_NOHSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU)IDC_TREE, g_hInstance, NULL);
    SetWindowTheme(Main.Tree, L"Explorer", NULL);
    SendMessageW(Main.Tree, WM_SETFONT, (WPARAM)Main.Font, FALSE);
    TreeView_SetBkColor(Main.Tree, RGB(0, 0, 0));
    TreeView_SetTextColor(Main.Tree, RGB(0xFF, 0xFF, 0xFF));
    TreeView_SetImageList(Main.Tree, Main.SmallImages, TVSIL_NORMAL);
    TreeView_SetItemHeight(Main.Tree, S(24));
    TreeView_SetIndent(Main.Tree, S(16));

    Main.Title = CreateLabel(SS_LEFT, Main.TitleFont);
    Main.Sub1 = CreateLabel(SS_LEFT, NULL);
    Main.Sub2 = CreateLabel(SS_LEFT, NULL);
    CreateButtons();

    Main.Parts = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                                 WS_CHILD | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | LVS_SHAREIMAGELISTS,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_PARTS, g_hInstance, NULL);
    SendMessageW(Main.Parts, WM_SETFONT, (WPARAM)Main.Font, FALSE);
    ListView_SetExtendedListViewStyle(Main.Parts, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetImageList(Main.Parts, Main.SmallImages, LVSIL_SMALL);
    AddColumn(0, IDS_COL_NAME, S(200), LVCFMT_LEFT);
    AddColumn(1, IDS_COL_TYPE, S(170), LVCFMT_LEFT);
    AddColumn(2, IDS_COL_FS, S(90), LVCFMT_LEFT);
    AddColumn(3, IDS_COL_CAPACITY, S(90), LVCFMT_RIGHT);
    AddColumn(4, IDS_COL_FREE, S(90), LVCFMT_RIGHT);

    Main.Usage = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD,
                                 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
    Main.UsageText = CreateLabel(SS_LEFT, NULL);
    for (i = 0; i < DU_INFO_MAX; i++)
    {
        Main.InfoLabel[i] = CreateLabel(SS_LEFT, NULL);
        Main.InfoValue[i] = CreateLabel(SS_LEFT, NULL);
    }

    DuModelRefresh(&g_Model);
    RebuildTree(0, 0, FALSE);
    if (!TreeView_GetSelection(Main.Tree))
        UpdateDetails();
}

static LRESULT CALLBACK
MainWndProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;

        case WM_SIZE:
            Layout();
            return 0;

        case WM_GETMINMAXINFO:
            ((PMINMAXINFO)lParam)->ptMinTrackSize.x = S(800);
            ((PMINMAXINFO)lParam)->ptMinTrackSize.y = S(520);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintMain(hwnd);
            return 0;

        case WM_NOTIFY:
        {
            LPNMHDR Header = (LPNMHDR)lParam;

            if (Header->hwndFrom == Main.Tree)
            {
                if (Header->code == TVN_SELCHANGEDW)
                    OnTreeSelect(((LPNMTREEVIEWW)lParam)->itemNew.hItem);
                else if (Header->code == TVN_ITEMEXPANDINGW &&
                         ITEM_TYPE(((LPNMTREEVIEWW)lParam)->itemNew.lParam) == ITEM_CATEGORY)
                    return TRUE;
            }
            else if (Header->hwndFrom == Main.Parts && Header->code == LVN_ITEMCHANGED)
            {
                LPNMLISTVIEW Change = (LPNMLISTVIEW)lParam;

                if ((Change->uNewState & LVIS_SELECTED) && !(Change->uOldState & LVIS_SELECTED))
                    OnPartSelected(Change->iItem);
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_NEWVOLUME:
                case IDC_DELETE:
                case IDC_CHECK:
                case IDC_RESIZE:
                case IDC_ERASE:
                case IDC_MOUNT:
                case IDC_INFO:
                case IDC_REFRESH:
                    RunCommand(LOWORD(wParam));
                    return 0;
            }
            break;

        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)
                SetTimer(hwnd, IDT_REFRESH, 700, NULL);
            break;

        case WM_TIMER:
            if (wParam == IDT_REFRESH)
            {
                KillTimer(hwnd, IDT_REFRESH);
                Refresh();
            }
            return 0;

        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            InvalidateRect(hwnd, NULL, TRUE);
            break;

        case WM_DESTROY:
            DeleteObject(Main.Font);
            DeleteObject(Main.TitleFont);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

INT WINAPI
wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ INT nCmdShow)
{
    INITCOMMONCONTROLSEX Icc = { sizeof(Icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                              ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS };
    WNDCLASSEXW wc;
    WCHAR Title[128];
    RECT rcWork;
    HWND hwnd;
    HDC hdc;
    MSG Msg;
    INT Width, Height;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_hInstance = hInstance;
    InitCommonControlsEx(&Icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    wc.hIconSm = LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = DU_MAIN_CLASS;
    if (!RegisterClassExW(&wc))
        return 1;

    DuLoadString(IDS_APPTITLE, Title, ARRAYSIZE(Title));
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    hdc = GetDC(NULL);
    Width = min(MulDiv(1100, GetDeviceCaps(hdc, LOGPIXELSX), 96), rcWork.right - rcWork.left);
    Height = min(MulDiv(720, GetDeviceCaps(hdc, LOGPIXELSY), 96), rcWork.bottom - rcWork.top);
    ReleaseDC(NULL, hdc);
    hwnd = CreateWindowExW(0, DU_MAIN_CLASS, Title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           rcWork.left + (rcWork.right - rcWork.left - Width) / 2,
                           rcWork.top + (rcWork.bottom - rcWork.top - Height) / 2,
                           Width, Height, NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessageW(&Msg, NULL, 0, 0))
    {
        if (IsDialogMessageW(hwnd, &Msg))
            continue;
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
    CoUninitialize();
    return (INT)Msg.wParam;
}
