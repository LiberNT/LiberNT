/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot manager main routine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>

#define BM_MAX_ENTRIES     GUI_MAX_ENTRIES
#define BM_EXTRA_ITEMS     3
#define BM_NAME_CHARS      64
#define BM_STORE_NAME      L"BCD"
#define BM_DEFAULT_TITLE   L"Boot Manager"
#define BM_BOOT_TIMEOUT    3
#define BM_WINDOWS_MANAGER L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define BM_WINDOWS_STORE   L"\\EFI\\Microsoft\\Boot\\BCD"
#define BM_WINDOWS_TITLE   L"Windows Boot Manager"
#define BM_LIBERNT_MANAGER L"\\EFI\\LiberNT\\Boot\\bootmgfw.efi"
#define BM_LIBERNT_STORE   L"\\EFI\\LiberNT\\Boot\\BCD"
#define BM_LIBERNT_TITLE   L"LiberNT"
#define BM_LIBERNT_ENTRY   L"{354fa6e5-5e45-49af-91bf-1fd2754e470a}"
#define BM_INSTALL_IMAGE_OPTION L"INSTALLIMAGE="
#define BM_UPDATE_STATE    L"\\EFI\\LiberNT\\Boot\\update.state"
#define BM_UPDATE_STARTS   3
#define BM_UPDATE_UNDO_STARTS (BM_UPDATE_STARTS * 2)
#define BM_UPDATE_STATE_CHARS 160
#define BM_MAX_VOLUMES     128
#define BM_USB_SUFFIX      L" (USB)"
#define BM_USB_TITLE       L"USB drive"
#define BM_DISK_SUFFIX     L" (disk %lu)"
#define BM_SUFFIX_CHARS    24

typedef enum _BM_FOUND_SYSTEM
{
    BmFoundLiberNT,
    BmFoundWindows,
    BmFoundRemovable
} BM_FOUND_SYSTEM;

static const PCWSTR BmStorePaths[] =
{
    L"\\EFI\\LiberNT\\Boot\\BCD",
    L"\\Boot\\BCD",
};

static BM_ENTRY BmEntries[BM_MAX_ENTRIES];
static CHAR BmNames[BM_MAX_ENTRIES][BM_NAME_CHARS];
static PCSTR BmMenuNames[BM_MAX_ENTRIES + BM_EXTRA_ITEMS];
static UI_MENU_ICON BmMenuIcons[BM_MAX_ENTRIES + BM_EXTRA_ITEMS];
static UI_IMAGE BmLogos[BM_MAX_ENTRIES];
static const UI_IMAGE* BmMenuImages[BM_MAX_ENTRIES + BM_EXTRA_ITEMS];
static PCWSTR BmTextNames[BM_MAX_ENTRIES];
static WCHAR BmStorePath[BM_PATH_CHARS];
static WCHAR BmTitle[BM_DESCRIPTION_CHARS];
static PVOID BmStore;
static ULONG BmStoreSize;
static FW_VOLUME_SCAN BmScan;
static BM_ENTRY BmTools[BM_MAX_TOOLS];
static CHAR BmToolNames[BM_MAX_TOOLS][BM_NAME_CHARS];
static PCSTR BmToolMenuNames[BM_MAX_TOOLS];
static ULONG BmCount;
static ULONG BmToolCount;
static ULONG BmSelected;
static ULONG BmLiberNTEntry = MAXULONG;
static BOOLEAN BmUsb[BM_MAX_ENTRIES];
static ULONG BmStoreCount;
static PVOID BmKnownVolumes[BM_MAX_VOLUMES];
static ULONG BmKnownCount;
static PVOID BmPassVolumes[BM_MAX_VOLUMES];
static ULONG BmPassCount;
static ULONG BmPassSecond = MAXULONG;
static PULONG BmTrackedItem;

static
PVOID
BmReadStore(
    _Out_writes_z_(Chars) PWSTR Path,
    _In_ ULONG Chars,
    _Out_ PULONG Size)
{
    PVOID Store;
    ULONG i;

    if (FwGetImageDirectory(Path, Chars) &&
        NT_SUCCESS(RtlStringCchCatW(Path, Chars, BM_STORE_NAME)))
    {
        Store = BcdReadStoreFile(NULL, Path, Size);
        if (Store)
            return Store;
    }

    for (i = 0; i < RTL_NUMBER_OF(BmStorePaths); i++)
    {
        Store = BcdReadStoreFile(NULL, BmStorePaths[i], Size);
        if (Store)
        {
            RtlStringCchCopyW(Path, Chars, BmStorePaths[i]);
            return Store;
        }
    }

    return NULL;
}

static
VOID
BmGetStoreDescription(
    _In_ PVOID Device,
    _In_ BOOLEAN Windows,
    _Out_writes_z_(Chars) PWSTR Description,
    _In_ ULONG Chars)
{
    BCD_OBJECT BootMgr, Object, Loader = {HCELL_NIL, 0};
    WCHAR Id[BCD_ID_CHARS], DefaultId[BCD_ID_CHARS];
    PVOID Store;
    ULONG Size, Index, Loaders = 0;

    RtlStringCchCopyW(Description, Chars, Windows ? BM_WINDOWS_TITLE : BM_LIBERNT_TITLE);

    Store = BcdReadStoreFile(Device, Windows ? BM_WINDOWS_STORE : BM_LIBERNT_STORE, &Size);
    if (!Store)
        return;

    if (BcdOpenStore(Store, Size) && BcdOpenObject(BCD_ID_BOOTMGR, &BootMgr))
    {
        if (!BcdGetObjectId(&BootMgr, BcdBootMgrObject_DefaultObject, 0, DefaultId))
            DefaultId[0] = UNICODE_NULL;

        for (Index = 0; BcdGetObjectId(&BootMgr, BcdBootMgrObjectList_DisplayOrder, Index, Id); Index++)
        {
            if (BcdOpenObject(Id, &Object) && Object.Type == BCD_OBJECT_OSLOADER)
            {
                if (Loaders == 0 || (!Windows && _wcsicmp(Id, DefaultId) == 0))
                    Loader = Object;
                Loaders++;
            }
        }

        if (Loaders == 1 || (!Windows && Loaders > 1))
            BcdGetString(&Loader, BcdLibraryString_Description, Description, Chars);
        else if (Windows)
            BcdGetString(&BootMgr, BcdLibraryString_Description, Description, Chars);
    }

    FwFreePages(Store, Size);
    if (BmStore)
        BcdOpenStore(BmStore, BmStoreSize);
}

static
BOOLEAN
BmVolumeIn(
    _In_ PVOID Device,
    _In_reads_(Count) PVOID* Volumes,
    _In_ ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (Volumes[i] == Device)
            return TRUE;
    }
    return FALSE;
}

static
ULONG
BmInsertEntry(
    _In_ BOOLEAN Usb)
{
    ULONG Position = BmCount, i;

    if (BmCount >= RTL_NUMBER_OF(BmEntries))
        return MAXULONG;

    if (!Usb)
    {
        for (Position = BmStoreCount; Position < BmCount && !BmUsb[Position]; Position++)
            ;
    }

    for (i = BmCount; i > Position; i--)
    {
        BmEntries[i] = BmEntries[i - 1];
        BmLogos[i] = BmLogos[i - 1];
        BmUsb[i] = BmUsb[i - 1];
    }

    if (BmSelected < BmCount && BmSelected >= Position)
        BmSelected++;
    if (BmTrackedItem && *BmTrackedItem >= Position)
        (*BmTrackedItem)++;

    RtlZeroMemory(&BmEntries[Position], sizeof(BmEntries[Position]));
    RtlZeroMemory(&BmLogos[Position], sizeof(BmLogos[Position]));
    BmUsb[Position] = Usb;
    BmCount++;
    ASSERT(Position >= BmStoreCount && Position < BmCount);
    return Position;
}

static
VOID
BmRemoveEntry(
    _In_ ULONG Index)
{
    ULONG i;

    ASSERT(Index >= BmStoreCount && Index < BmCount);
    if (FwDebugEnabled)
        DbgPrint("Boot manager: %ls is no longer present\n", BmEntries[Index].Description);

    if (BmLogos[Index].Pixels)
        FwFree(BmLogos[Index].Pixels);

    for (i = Index; i + 1 < BmCount; i++)
    {
        BmEntries[i] = BmEntries[i + 1];
        BmLogos[i] = BmLogos[i + 1];
        BmUsb[i] = BmUsb[i + 1];
    }

    BmCount--;
    RtlZeroMemory(&BmEntries[BmCount], sizeof(BmEntries[BmCount]));
    RtlZeroMemory(&BmLogos[BmCount], sizeof(BmLogos[BmCount]));
    BmUsb[BmCount] = FALSE;

    if (BmSelected > Index || (BmSelected == Index && Index == BmCount && Index > 0))
        BmSelected--;

    if (BmTrackedItem)
    {
        if (*BmTrackedItem > Index)
            (*BmTrackedItem)--;
        else if (*BmTrackedItem == Index && Index == BmCount)
            *BmTrackedItem = Index > 0 ? Index - 1 : Index + 1;
    }
}

static
VOID
BmTrimLabel(
    _Inout_z_ PWSTR Label)
{
    SIZE_T Length = wcslen(Label);

    while (Length > 0 && Label[Length - 1] == L' ')
        Label[--Length] = UNICODE_NULL;
}

static
VOID
BmAppendLocation(
    _In_ PVOID Device,
    _In_ BOOLEAN Usb,
    _Inout_updates_z_(Chars) PWSTR Description,
    _In_ ULONG Chars)
{
    WCHAR Suffix[BM_SUFFIX_CHARS];
    SIZE_T SuffixLength;
    ULONG Disk;

    if (Usb)
        RtlStringCchCopyW(Suffix, RTL_NUMBER_OF(Suffix), BM_USB_SUFFIX);
    else if (!FwGetVolumeDiskNumber(Device, &Disk) ||
             !NT_SUCCESS(RtlStringCchPrintfW(Suffix, RTL_NUMBER_OF(Suffix), BM_DISK_SUFFIX, Disk)))
        return;

    SuffixLength = wcslen(Suffix);
    ASSERT(SuffixLength < Chars);
    if (wcslen(Description) + SuffixLength >= Chars)
        Description[Chars - 1 - SuffixLength] = UNICODE_NULL;
    RtlStringCchCatW(Description, Chars, Suffix);
}

static
BOOLEAN
BmAddFoundEntry(
    _In_ PVOID Device,
    _In_ BOOLEAN Usb,
    _In_ BM_FOUND_SYSTEM System,
    _In_ PCWSTR Path)
{
    PBM_ENTRY Entry;
    ULONG Index;

    Index = BmInsertEntry(Usb);
    if (Index == MAXULONG)
        return FALSE;

    Entry = &BmEntries[Index];
    Entry->FirmwareDevice = Device;
    Entry->FirmwarePath = Path;
    if (System == BmFoundLiberNT)
    {
        Entry->FirmwareOptions = BM_START_DEFAULT_OPTION;
        BmGetStoreDescription(Device, FALSE, Entry->Description, RTL_NUMBER_OF(Entry->Description));
    }
    else if (System == BmFoundWindows)
    {
        BmGetStoreDescription(Device, TRUE, Entry->Description, RTL_NUMBER_OF(Entry->Description));
        if (!BmLoadWindowsLogo(Device, &BmLogos[Index]))
            RtlZeroMemory(&BmLogos[Index], sizeof(BmLogos[Index]));
    }
    else
    {
        if (FwGetVolumeLabel(Device, Entry->Description, RTL_NUMBER_OF(Entry->Description)))
            BmTrimLabel(Entry->Description);
        if (Entry->Description[0] == UNICODE_NULL)
            RtlStringCchCopyW(Entry->Description, RTL_NUMBER_OF(Entry->Description), BM_USB_TITLE);
    }

    BmAppendLocation(Device, Usb, Entry->Description, RTL_NUMBER_OF(Entry->Description));

    if (FwDebugEnabled)
        DbgPrint("Boot manager: found %ls (%ls) as entry %lu\n", Entry->Description, Path, Index + 1);
    return TRUE;
}

static
BOOLEAN
BmExamineVolume(
    _In_ PVOID Device)
{
    PCWSTR Removable;
    BOOLEAN Usb, Boot, Added = FALSE;

    if (BmPassCount < RTL_NUMBER_OF(BmPassVolumes))
        BmPassVolumes[BmPassCount++] = Device;
    if (BmVolumeIn(Device, BmKnownVolumes, BmKnownCount))
        return FALSE;

    Usb = FwIsUsbVolume(Device);
    Boot = FwIsBootVolume(Device);
    if (!Boot && FwFileExists(Device, BM_LIBERNT_MANAGER) &&
        BmAddFoundEntry(Device, Usb, BmFoundLiberNT, BM_LIBERNT_MANAGER))
    {
        Added = TRUE;
    }

    if (FwFileExists(Device, BM_WINDOWS_MANAGER) &&
        BmAddFoundEntry(Device, Usb, BmFoundWindows, BM_WINDOWS_MANAGER))
    {
        Added = TRUE;
    }

    if (!Added && Usb && !Boot)
    {
        Removable = FwGetRemovableMediaPath();
        if (Removable && FwFileExists(Device, Removable))
            Added = BmAddFoundEntry(Device, Usb, BmFoundRemovable, Removable);
    }

    return Added;
}

static
BOOLEAN
BmEndVolumePass(VOID)
{
    BOOLEAN Changed = FALSE;
    ULONG i;

    for (i = BmCount; i-- > BmStoreCount;)
    {
        if (BmEntries[i].FirmwareDevice &&
            !BmVolumeIn(BmEntries[i].FirmwareDevice, BmPassVolumes, BmPassCount))
        {
            BmRemoveEntry(i);
            Changed = TRUE;
        }
    }

    RtlCopyMemory(BmKnownVolumes, BmPassVolumes, BmPassCount * sizeof(BmPassVolumes[0]));
    BmKnownCount = BmPassCount;
    BmPassCount = 0;
    BmPassSecond = ArcGetTime()->Second;
    return Changed;
}

static
BOOLEAN
BmScanStep(
    _Out_ PBOOLEAN Changed)
{
    PVOID Device;
    BOOLEAN More;

    *Changed = FALSE;
    if (BmScan.State == FwVolumeScanDone)
    {
        if (ArcGetTime()->Second == BmPassSecond)
            return FALSE;

        RtlZeroMemory(&BmScan, sizeof(BmScan));
        BmScan.State = FwVolumeScanVolumes;
    }

    More = FwScanFileVolumes(NULL, &BmScan, &Device);
    if (Device && BmExamineVolume(Device))
        *Changed = TRUE;
    if (!More && BmEndVolumePass())
        *Changed = TRUE;
    return More;
}

static
VOID
BmFinishScan(VOID)
{
    BOOLEAN Changed;

    while (BmScan.State != FwVolumeScanDone)
        BmScanStep(&Changed);
}

static
VOID
BmMenuName(
    _In_z_ PCWSTR Description,
    _Out_writes_z_(BM_NAME_CHARS) PCHAR Name)
{
    ULONG i;

    for (i = 0; i < BM_NAME_CHARS - 1 && Description[i] != UNICODE_NULL; i++)
        Name[i] = Description[i] <= 0x7F ? (CHAR)Description[i] : '?';

    Name[i] = ANSI_NULL;
}

static
VOID
BmBuildMenu(VOID)
{
    ULONG i;

    for (i = 0; i < BmCount; i++)
    {
        BmMenuName(BmEntries[i].Description, BmNames[i]);
        BmMenuNames[i] = BmNames[i];
        BmMenuIcons[i] = (!BmEntries[i].FirmwareDevice ||
                          _wcsicmp(BmEntries[i].FirmwarePath, BM_LIBERNT_MANAGER) == 0) ? UiMenuIconLiberNT :
                         BmLogos[i].Pixels ? UiMenuIconImage : UiMenuIconDesktop;
        BmMenuImages[i] = BmLogos[i].Pixels ? &BmLogos[i] : NULL;
    }

    BmMenuNames[BmCount] = NULL;
    BmMenuNames[BmCount + 1] = "Startup options";
    BmMenuNames[BmCount + 2] = "Settings";
    BmMenuIcons[BmCount] = UiMenuIconNone;
    BmMenuIcons[BmCount + 1] = UiMenuIconNone;
    BmMenuIcons[BmCount + 2] = UiMenuIconNone;
}

static
BOOLEAN
BmMenuIdle(
    IN OUT PUI_MENU_INFO MenuInfo,
    OUT PBOOLEAN Changed)
{
    BOOLEAN More, Updated;

    *Changed = FALSE;
    if (MenuInfo->MenuItemList != BmMenuNames)
        return FALSE;

    BmTrackedItem = &MenuInfo->SelectedMenuItem;
    More = BmScanStep(&Updated);
    BmTrackedItem = NULL;
    if (Updated)
    {
        BmBuildMenu();
        MenuInfo->MenuItemCount = BmCount + BM_EXTRA_ITEMS;
        *Changed = TRUE;
    }

    return More;
}

static
BOOLEAN
BmIsUpdateEntry(
    _In_ PBM_ENTRY Entry)
{
    WCHAR Options[BM_LOAD_OPTIONS_CHARS];

    return !Entry->FirmwareDevice &&
           BcdGetString(&Entry->Object, BcdLibraryString_LoadOptionsString, Options, RTL_NUMBER_OF(Options)) &&
           wcsstr(Options, BM_INSTALL_IMAGE_OPTION) != NULL;
}

static
PBM_ENTRY
BmUpdateFallback(
    _In_ PBM_ENTRY Failed)
{
    ULONG i;

    for (i = 0; i < BmCount; i++)
    {
        if (&BmEntries[i] != Failed && !BmEntries[i].FirmwareDevice && !BmIsUpdateEntry(&BmEntries[i]))
            return &BmEntries[i];
    }
    return NULL;
}

static
ULONG
BmReadUpdateStarts(
    _In_ PBM_ENTRY Entry)
{
    CHAR Text[BM_UPDATE_STATE_CHARS], Id[BCD_ID_CHARS];
    PCSTR Starts;
    PUCHAR Data;
    ULONG Size, Length, Count = 0, i;

    for (i = 0; i < BCD_ID_CHARS - 1 && Entry->Id[i] != UNICODE_NULL; i++)
        Id[i] = (CHAR)Entry->Id[i];
    Id[i] = ANSI_NULL;

    Data = FwReadBootFile(BM_UPDATE_STATE, &Size);
    if (!Data)
        return 0;

    Length = min(Size, sizeof(Text) - 1);
    RtlCopyMemory(Text, Data, Length);
    Text[Length] = ANSI_NULL;
    FwFreePages(Data, Size);

    Starts = strstr(Text, "Starts=");
    if (strstr(Text, Id) && Starts)
        Count = strtoul(Starts + 7, NULL, 10);
    return Count;
}

static
BOOLEAN
BmWriteUpdateStarts(
    _In_ PBM_ENTRY Entry,
    _In_ ULONG Count)
{
    CHAR Text[BM_UPDATE_STATE_CHARS];

    return NT_SUCCESS(RtlStringCbPrintfA(Text, sizeof(Text), "Entry=%ls\r\nStarts=%lu\r\n", Entry->Id, Count)) &&
           FwWriteBootFile(BM_UPDATE_STATE, Text, (ULONG)strlen(Text));
}

static
VOID
BmStartSelectedApplication(
    _In_ PBM_ENTRY Entry,
    _In_ PBM_STARTUP Startup)
{
    PBM_STARTUP EntryStartup = Startup;
    PBM_ENTRY Fallback = NULL;
    BM_STARTUP Undo;
    BOOLEAN Cancelled;
    ULONG Starts;

    if (Startup->Profile != 0 && !FwDebugEnabled)
    {
        FwDebugEnable();
        FwDebugAnnounce("LiberNT Boot Manager", TRUE);
    }

    if (BmIsUpdateEntry(Entry))
    {
        Fallback = BmUpdateFallback(Entry);
        Starts = BmReadUpdateStarts(Entry);
        if (Starts >= BM_UPDATE_UNDO_STARTS && Fallback)
        {
            FwPrint(L"\r\nThe update did not finish in %lu starts; starting %s.\r\n", Starts, Fallback->Description);
            Entry = Fallback;
            Fallback = NULL;
        }
        else
        {
            if (Starts >= BM_UPDATE_STARTS)
            {
                /* FIXME: starts are counted until the completion step removes the update, so a working update restarted 3 times before an administrator signs in is rolled back */
                FwPrint(L"\r\nThe update did not finish in %lu starts; rolling it back.\r\n", Starts);
                Undo = *Startup;
                Undo.UndoUpdate = TRUE;
                EntryStartup = &Undo;
            }
            if (!BmWriteUpdateStarts(Entry, Starts + 1))
                FwPrint(L"\r\nThe starts of the update cannot be counted on this boot volume.\r\n");
        }
    }

    if (BmStartApplication(Entry, BmStorePath, EntryStartup, &Cancelled) || Cancelled)
        return;

    if (Fallback)
    {
        FwPrint(L"\r\nThe update could not be started; starting %s.\r\n", Fallback->Description);
        if (BmStartApplication(Fallback, BmStorePath, Startup, &Cancelled) || Cancelled)
            return;
    }

    FwPrint(L"\r\nPress any key to return to the boot menu.\r\n");
    if (FwReadKey() == KEY_EXTENDED)
        FwReadKey();
}

static
VOID
BmStartEntry(
    _In_ PBM_ENTRY Entry,
    _In_ PBM_STARTUP Startup)
{
    FbGuiVtbl.UnInitialize();
    if (!UiKeepFirmwareScreen)
        GuiShowFirmwareLogo();
    BmStartSelectedApplication(Entry, Startup);
    GuiInitialize();
}

static
BOOLEAN
BmChooseStartup(
    _In_ BOOLEAN Graphical,
    _Out_ PBM_STARTUP Startup,
    _Out_ PBM_ENTRY *Entry)
{
    ULONG Tool;

    if (!BmChooseStartupSettings(Graphical, BmToolMenuNames, BmToolCount, Startup, &Tool))
        return FALSE;

    if (Tool < BmToolCount)
    {
        *Entry = &BmTools[Tool];
        return TRUE;
    }

    if (BmLiberNTEntry >= BmCount)
        return FALSE;

    *Entry = &BmEntries[BmLiberNTEntry];
    return TRUE;
}

static
VOID
BmStartupOptions(VOID)
{
    BM_STARTUP Startup;
    PBM_ENTRY Entry;

    if (BmChooseStartup(TRUE, &Startup, &Entry))
        BmStartEntry(Entry, &Startup);
}

static
VOID
BmRestartMenu(VOID)
{
    PCSTR Items[] = {"Restart", "Back"};
    ULONG Selected;

    if (UiDisplayMenu("Restart this device?", NULL, Items, RTL_NUMBER_OF(Items),
                      1, -1, &Selected, TRUE, NULL, NULL) && Selected == 0)
    {
        Reboot();
    }
}

static
VOID
BmSettingsMenu(VOID)
{
    PCSTR Items[3];
    ULONG Count, Selected = 0, FirmwareIndex, RestartIndex, BackIndex;

    for (;;)
    {
        Count = 0;
        FirmwareIndex = MAXULONG;
        if (FwFirmwareSetupSupported())
        {
            FirmwareIndex = Count;
            Items[Count++] = "UEFI firmware settings";
        }

        RestartIndex = Count;
        Items[Count++] = "Restart";
        BackIndex = Count;
        Items[Count++] = "Back";

        if (!UiDisplayMenu("Settings", NULL, Items, Count, Selected, -1,
                           &Selected, TRUE, NULL, NULL) || Selected == BackIndex)
        {
            return;
        }

        if (Selected == FirmwareIndex)
            FwBootToFirmware();
        else if (Selected == RestartIndex)
            BmRestartMenu();
    }
}

static
BOOLEAN
BmMenuKeyFilter(
    IN ULONG KeyPress,
    IN ULONG SelectedMenuItem,
    IN PVOID Context OPTIONAL)
{
    UNREFERENCED_PARAMETER(Context);

    if (SelectedMenuItem < BmCount)
        BmSelected = SelectedMenuItem;

    switch (KeyPress)
    {
        case KEY_F2:
            BmSettingsMenu();
            return TRUE;
    }

    return FALSE;
}

static
VOID
BmRunGuiMenu(
    _In_ LONG Timeout)
{
    BM_STARTUP Startup;
    ULONG Item = BmSelected;

    BmBuildMenu();

    for (;;)
    {
        if (!FbGuiVtbl.DisplayMenu("Choose boot option",
                                   NULL,
                                   BmMenuNames,
                                   BmCount + BM_EXTRA_ITEMS,
                                   Item,
                                   Timeout,
                                   &Item,
                                   FALSE,
                                   BmMenuIcons,
                                   BmMenuKeyFilter,
                                   NULL))
        {
            return;
        }

        Timeout = -1;

        if (Item == BmCount + 1)
        {
            BmStartupOptions();
            Item = BmSelected;
            continue;
        }

        if (Item == BmCount + 2)
        {
            BmSettingsMenu();
            Item = BmSelected;
            continue;
        }

        if (Item >= BmCount)
        {
            Item = BmSelected;
            continue;
        }

        BmSelected = Item;
        RtlZeroMemory(&Startup, sizeof(Startup));
        BmStartEntry(&BmEntries[BmSelected], &Startup);
    }
}

static
VOID
BmRunTextMenu(
    _In_ LONG Timeout)
{
    BM_STARTUP Startup;
    PBM_ENTRY Entry;
    ULONG i;

    for (i = 0; i < BmCount; i++)
        BmTextNames[i] = BmEntries[i].Description;

    if (Timeout >= 0 && !BmUiWaitForKey(Timeout))
    {
        RtlZeroMemory(&Startup, sizeof(Startup));
        BmStartSelectedApplication(&BmEntries[BmSelected], &Startup);
    }

    Timeout = -1;

    for (;;)
    {
        if (!BmUiInitialize())
            return;

        RtlZeroMemory(&Startup, sizeof(Startup));

        switch (BmUiChooseEntry(BmTitle, BmTextNames, BmCount, &BmSelected, Timeout))
        {
            case BmUiActionExit:
                BmUiShutdown();
                return;

            case BmUiActionAdvanced:
                if (!BmChooseStartup(FALSE, &Startup, &Entry))
                {
                    Timeout = -1;
                    BmUiShutdown();
                    continue;
                }
                break;

            default:
                Entry = &BmEntries[BmSelected];
                break;
        }

        Timeout = -1;
        BmUiShutdown();
        GuiShowFirmwareLogo();
        BmStartSelectedApplication(Entry, &Startup);
    }
}

static
VOID
BmRunMenu(
    _In_ LONG Timeout)
{
    if (GuiInitialize())
    {
        UiKeepFirmwareScreen = (Timeout > 0);
        UiMenuIdle = BmMenuIdle;
        UiMenuImages = BmMenuImages;
        BmRunGuiMenu(Timeout);
        UiMenuImages = NULL;
        UiMenuIdle = NULL;
        FbGuiVtbl.UnInitialize();
        return;
    }

    /* TODO: the text menu does not list drives plugged in while it is shown */
    BmFinishScan();
    BmRunTextMenu(Timeout);
}

static
LONG
BmGetTimeout(VOID)
{
    BCD_OBJECT BootMgr;
    ULONGLONG Seconds;

    if (!BmStore)
        return BM_BOOT_TIMEOUT;

    if (!BcdOpenObject(BCD_ID_BOOTMGR, &BootMgr) ||
        !BcdGetInteger(&BootMgr, BcdBootMgrInteger_Timeout, &Seconds))
    {
        return -1;
    }

    return (LONG)min(Seconds, (ULONGLONG)MAXLONG);
}

static
BOOLEAN
BmStartDefaultRequested(VOID)
{
    PCWSTR Options;
    ULONG Chars;
    const ULONG Length = RTL_NUMBER_OF(BM_START_DEFAULT_OPTION) - 1;

    Options = FwGetLoadOptions(&Chars);
    return Options && Chars >= Length &&
           _wcsnicmp(Options, BM_START_DEFAULT_OPTION, Length) == 0 &&
           (Chars == Length || Options[Length] == UNICODE_NULL);
}

static
VOID
BmDebugEntries(
    _In_ ULONG Default,
    _In_ ULONG DefaultTool,
    _In_ LONG Timeout)
{
    ULONG i;

    if (!FwDebugEnabled)
        return;

    DbgPrint("BCD store %ls: %lu entries, %lu tools, timeout %ld s\n", BmStorePath, BmCount, BmToolCount, Timeout);
    for (i = 0; i < BmCount; i++)
        DbgPrint("  Entry %lu: %ls%s\n", i + 1, BmEntries[i].Description, i == Default ? " (default)" : "");
    for (i = 0; i < BmToolCount; i++)
        DbgPrint("  Tool %lu: %ls%s\n", i + 1, BmTools[i].Description, i == DefaultTool ? " (default)" : "");
}

VOID
BlMain(VOID)
{
    BM_STARTUP Startup;
    BCD_OBJECT BootMgr;
    BOOLEAN Debug;
    PVOID Store;
    ULONG Size, Default = 0, DefaultTool = MAXULONG, i;
    LONG Timeout;

    FwDebugAnnounce("LiberNT Boot Manager", TRUE);
    Store = BmReadStore(BmStorePath, RTL_NUMBER_OF(BmStorePath), &Size);

    RtlStringCchCopyW(BmTitle, RTL_NUMBER_OF(BmTitle), BM_DEFAULT_TITLE);

    if (!Store || !BcdOpenStore(Store, Size))
    {
        FwPrint(L"The Boot Configuration Data store is missing or corrupt.\r\n");
    }
    else
    {
        if (!FwDebugEnabled && BcdOpenObject(BCD_ID_BOOTMGR, &BootMgr) &&
            BcdGetBoolean(&BootMgr, BcdLibraryBoolean_DebuggerEnabled, &Debug) && Debug)
        {
            FwDebugEnable();
            FwDebugAnnounce("LiberNT Boot Manager", TRUE);
        }

        BmStore = Store;
        BmStoreSize = Size;
        BmGetDescription(BmTitle, RTL_NUMBER_OF(BmTitle));
        BmCount = BmEnumerateEntries(BmEntries, RTL_NUMBER_OF(BmEntries), &Default);
        BmStoreCount = BmCount;
        if (BmCount == 0)
            FwPrint(L"The Boot Configuration Data store has no operating system entry.\r\n");

        for (i = 0; i < BmCount; i++)
        {
            if (_wcsicmp(BmEntries[i].Id, BM_LIBERNT_ENTRY) == 0)
                BmLiberNTEntry = i;
        }

        BmToolCount = BmEnumerateToolEntries(BmTools, RTL_NUMBER_OF(BmTools), &DefaultTool);
        for (i = 0; i < BmToolCount; i++)
        {
            BmMenuName(BmTools[i].Description, BmToolNames[i]);
            BmToolMenuNames[i] = BmToolNames[i];
        }
    }

    if (BmCount == 0)
        BmFinishScan();

    if (BmCount == 0)
        return;

    BmSelected = Default;
    Timeout = BmGetTimeout();
    BmDebugEntries(Default, DefaultTool, Timeout);
    if (DefaultTool < BmToolCount)
    {
        RtlZeroMemory(&Startup, sizeof(Startup));
        BmStartSelectedApplication(&BmTools[DefaultTool], &Startup);
        Timeout = -1;
    }
    else if (Timeout == 0 || BmStartDefaultRequested())
    {
        if (FwDebugEnabled && Timeout != 0)
            DbgPrint("Boot manager: started by another boot manager, starting entry %lu\n", Default + 1);
        RtlZeroMemory(&Startup, sizeof(Startup));
        BmStartSelectedApplication(&BmEntries[Default], &Startup);
        Timeout = -1;
    }

    BmRunMenu(Timeout);
}
