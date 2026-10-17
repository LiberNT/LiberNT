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
static PCSTR BmStartupNames[BM_MAX_ENTRIES];
static ULONG BmStartupEntries[BM_MAX_ENTRIES];
static PCWSTR BmTextNames[BM_MAX_ENTRIES];
static WCHAR BmStorePath[BM_PATH_CHARS];
static WCHAR BmTitle[BM_DESCRIPTION_CHARS];
static PVOID BmStore;
static ULONG BmStoreSize;
static FW_VOLUME_SCAN BmScan;
static ULONG BmCount;
static ULONG BmSelected;

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
        Store = FwReadBootFile(Path, Size);
        if (Store)
            return Store;
    }

    for (i = 0; i < RTL_NUMBER_OF(BmStorePaths); i++)
    {
        Store = FwReadBootFile(BmStorePaths[i], Size);
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
BmGetWindowsDescription(
    _In_ PVOID Device,
    _Out_writes_z_(Chars) PWSTR Description,
    _In_ ULONG Chars)
{
    BCD_OBJECT BootMgr, Object, Loader = {HCELL_NIL, 0};
    WCHAR Id[BCD_ID_CHARS];
    PVOID Store;
    ULONG Size, Index, Loaders = 0;

    RtlStringCchCopyW(Description, Chars, BM_WINDOWS_TITLE);

    Store = FwReadDeviceFile(Device, BM_WINDOWS_STORE, &Size);
    if (!Store)
        return;

    if (BcdOpenStore(Store, Size) && BcdOpenObject(BCD_ID_BOOTMGR, &BootMgr))
    {
        for (Index = 0; BcdGetObjectId(&BootMgr, BcdBootMgrObjectList_DisplayOrder, Index, Id); Index++)
        {
            if (BcdOpenObject(Id, &Object) && Object.Type == BCD_OBJECT_OSLOADER)
            {
                Loader = Object;
                Loaders++;
            }
        }

        if (Loaders == 1)
            BcdGetString(&Loader, BcdLibraryString_Description, Description, Chars);
        else
            BcdGetString(&BootMgr, BcdLibraryString_Description, Description, Chars);
    }

    FwFreePages(Store, Size);
}

static
BOOLEAN
BmAddWindows(
    _In_ PVOID Device)
{
    PBM_ENTRY Entry;

    if (BmCount >= RTL_NUMBER_OF(BmEntries))
        return FALSE;

    Entry = &BmEntries[BmCount];
    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->FirmwareDevice = Device;
    Entry->FirmwarePath = BM_WINDOWS_MANAGER;
    BmGetWindowsDescription(Device, Entry->Description, RTL_NUMBER_OF(Entry->Description));
    if (!BmLoadWindowsLogo(Device, &BmLogos[BmCount]))
        RtlZeroMemory(&BmLogos[BmCount], sizeof(BmLogos[BmCount]));

    if (BmStore)
        BcdOpenStore(BmStore, BmStoreSize);

    BmCount++;
    return TRUE;
}

static
BOOLEAN
BmScanStep(
    _Out_ PBOOLEAN Added)
{
    PVOID Device;
    BOOLEAN More;

    More = FwScanFileVolumes(BM_WINDOWS_MANAGER, &BmScan, &Device);
    *Added = Device && BmAddWindows(Device);
    return More;
}

static
VOID
BmFinishScan(VOID)
{
    BOOLEAN Added;

    while (BmScanStep(&Added))
        ;
}

static
VOID
BmBuildMenu(VOID)
{
    ULONG i, j;

    for (i = 0; i < BmCount; i++)
    {
        for (j = 0; j < BM_NAME_CHARS - 1 && BmEntries[i].Description[j] != UNICODE_NULL; j++)
            BmNames[i][j] = BmEntries[i].Description[j] <= 0x7F ? (CHAR)BmEntries[i].Description[j] : '?';

        BmNames[i][j] = ANSI_NULL;
        BmMenuNames[i] = BmNames[i];
        BmMenuIcons[i] = !BmEntries[i].FirmwareDevice ? UiMenuIconLiberNT :
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
    ULONG Entries;
    BOOLEAN More, Added;

    *Changed = FALSE;
    if (MenuInfo->MenuItemList != BmMenuNames)
        return FALSE;

    Entries = MenuInfo->MenuItemCount - BM_EXTRA_ITEMS;
    More = BmScanStep(&Added);
    if (Added)
    {
        BmBuildMenu();
        if (MenuInfo->SelectedMenuItem >= Entries)
            MenuInfo->SelectedMenuItem += BmCount - Entries;
        MenuInfo->MenuItemCount = BmCount + BM_EXTRA_ITEMS;
        *Changed = TRUE;
    }

    return More;
}

static
VOID
BmStartEntry(
    _In_ ULONG Index,
    _In_ BOOLEAN AskStartup)
{
    BM_STARTUP Startup;

    RtlZeroMemory(&Startup, sizeof(Startup));
    if (AskStartup && !BmEntries[Index].FirmwareDevice && !BmChooseStartupSettings(TRUE, &Startup))
        return;

    FbGuiVtbl.UnInitialize();
    if (!UiKeepFirmwareScreen)
        GuiShowFirmwareLogo();
    BmStartApplication(&BmEntries[Index], BmStorePath, &Startup);
    GuiInitialize();
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

        case KEY_F8:
            if (!BmEntries[BmSelected].FirmwareDevice)
                BmStartEntry(BmSelected, TRUE);
            return TRUE;
    }

    return FALSE;
}

static
VOID
BmStartupOptions(VOID)
{
    ULONG Count = 0, Selected = 0, i;

    for (i = 0; i < BmCount; i++)
    {
        if (BmEntries[i].FirmwareDevice)
            continue;

        if (i == BmSelected)
            Selected = Count;

        BmStartupEntries[Count] = i;
        BmStartupNames[Count++] = BmMenuNames[i];
    }

    if (Count == 0)
        return;

    if (Count == 1 ||
        UiDisplayMenu("Startup options / Choose a system",
                      NULL,
                      BmStartupNames,
                      Count,
                      Selected,
                      -1,
                      &Selected,
                      TRUE,
                      NULL,
                      NULL))
    {
        BmSelected = BmStartupEntries[Selected];
        BmStartEntry(BmSelected, TRUE);
    }
}

static
VOID
BmRunGuiMenu(
    _In_ LONG Timeout)
{
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
        BmStartEntry(BmSelected, FALSE);
    }
}

static
VOID
BmRunTextMenu(
    _In_ LONG Timeout)
{
    BM_STARTUP Startup;
    ULONG i;

    for (i = 0; i < BmCount; i++)
        BmTextNames[i] = BmEntries[i].Description;

    if (Timeout >= 0 && !BmUiWaitForKey(Timeout))
    {
        RtlZeroMemory(&Startup, sizeof(Startup));
        BmStartApplication(&BmEntries[BmSelected], BmStorePath, &Startup);
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
                if (!BmChooseStartupSettings(FALSE, &Startup))
                {
                    Timeout = -1;
                    BmUiShutdown();
                    continue;
                }
                break;

            default:
                break;
        }

        Timeout = -1;
        BmUiShutdown();
        GuiShowFirmwareLogo();
        BmStartApplication(&BmEntries[BmSelected], BmStorePath, &Startup);
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

    BmFinishScan();
    BmRunTextMenu(Timeout);
}

VOID
BlMain(VOID)
{
    PVOID Store;
    ULONG Size, Default = 0;

    Store = BmReadStore(BmStorePath, RTL_NUMBER_OF(BmStorePath), &Size);

    RtlStringCchCopyW(BmTitle, RTL_NUMBER_OF(BmTitle), BM_DEFAULT_TITLE);

    if (!Store || !BcdOpenStore(Store, Size))
    {
        FwPrint(L"The Boot Configuration Data store is missing or corrupt.\r\n");
    }
    else
    {
        BmStore = Store;
        BmStoreSize = Size;
        BmGetDescription(BmTitle, RTL_NUMBER_OF(BmTitle));
        BmCount = BmEnumerateEntries(BmEntries, RTL_NUMBER_OF(BmEntries), &Default);
        if (BmCount == 0)
            FwPrint(L"The Boot Configuration Data store has no operating system entry.\r\n");
    }

    if (BmCount == 0)
        BmFinishScan();

    if (BmCount == 0)
        return;

    BmSelected = Default;
    BmRunMenu(BM_BOOT_TIMEOUT);
}
