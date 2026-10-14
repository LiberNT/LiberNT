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

static const PCWSTR BmStorePaths[] =
{
    L"\\EFI\\LiberNT\\Boot\\BCD",
    L"\\EFI\\Microsoft\\Boot\\BCD",
    L"\\Boot\\BCD",
};

static BM_ENTRY BmEntries[BM_MAX_ENTRIES];
static CHAR BmNames[BM_MAX_ENTRIES][BM_NAME_CHARS];
static PCSTR BmMenuNames[BM_MAX_ENTRIES + BM_EXTRA_ITEMS];
static UI_MENU_ICON BmMenuIcons[BM_MAX_ENTRIES + BM_EXTRA_ITEMS];
static PCWSTR BmTextNames[BM_MAX_ENTRIES];
static WCHAR BmStorePath[BM_PATH_CHARS];
static WCHAR BmTitle[BM_DESCRIPTION_CHARS];
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
BmStartEntry(
    _In_ ULONG Index,
    _In_ BOOLEAN AskStartup)
{
    BM_STARTUP Startup;

    RtlZeroMemory(&Startup, sizeof(Startup));
    if (AskStartup && !BmChooseStartupSettings(TRUE, &Startup))
        return;

    FbGuiVtbl.UnInitialize();
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
            BmStartEntry(BmSelected, TRUE);
            return TRUE;
    }

    return FALSE;
}

static
VOID
BmRunGuiMenu(
    _In_ LONG Timeout)
{
    ULONG Item = BmSelected;
    ULONG i, j;

    for (i = 0; i < BmCount; i++)
    {
        for (j = 0; j < BM_NAME_CHARS - 1 && BmEntries[i].Description[j] != UNICODE_NULL; j++)
            BmNames[i][j] = BmEntries[i].Description[j] <= 0x7F ? (CHAR)BmEntries[i].Description[j] : '?';

        BmNames[i][j] = ANSI_NULL;
        BmMenuNames[i] = BmNames[i];
        BmMenuIcons[i] = UiMenuIconLiberNT;
    }

    BmMenuNames[BmCount] = NULL;
    BmMenuNames[BmCount + 1] = "Startup options";
    BmMenuNames[BmCount + 2] = "Settings";
    BmMenuIcons[BmCount] = UiMenuIconNone;
    BmMenuIcons[BmCount + 1] = UiMenuIconNone;
    BmMenuIcons[BmCount + 2] = UiMenuIconNone;

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
            if (BmCount == 1 ||
                UiDisplayMenu("Startup options / Choose a system",
                              NULL,
                              BmMenuNames,
                              BmCount,
                              BmSelected,
                              -1,
                              &BmSelected,
                              TRUE,
                              NULL,
                              NULL))
            {
                BmStartEntry(BmSelected, TRUE);
            }

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
        BmRunGuiMenu(Timeout);
        FbGuiVtbl.UnInitialize();
        return;
    }

    BmRunTextMenu(Timeout);
}

VOID
BlMain(VOID)
{
    BM_STARTUP Startup;
    ULONGLONG Seconds;
    PVOID Store;
    ULONG Size, Default;
    LONG Timeout;

    Store = BmReadStore(BmStorePath, RTL_NUMBER_OF(BmStorePath), &Size);
    if (!Store || !BcdOpenStore(Store, Size))
    {
        FwPrint(L"The Boot Configuration Data store is missing or corrupt.\r\n");
        return;
    }

    BmCount = BmEnumerateEntries(BmEntries, RTL_NUMBER_OF(BmEntries), &Default);
    if (BmCount == 0)
    {
        FwPrint(L"The Boot Configuration Data store has no operating system entry.\r\n");
        return;
    }

    if (!BmGetDescription(BmTitle, RTL_NUMBER_OF(BmTitle)))
        RtlStringCchCopyW(BmTitle, RTL_NUMBER_OF(BmTitle), BM_DEFAULT_TITLE);

    BmSelected = Default;
    Timeout = BmGetTimeout(&Seconds) ? (LONG)min(Seconds, (ULONGLONG)MAXLONG) : -1;

    if (BmCount > 1 || BmGetDisplayBootMenu())
    {
        BmRunMenu(Timeout);
        return;
    }

    RtlZeroMemory(&Startup, sizeof(Startup));
    if (!BmStartApplication(&BmEntries[Default], BmStorePath, &Startup))
        BmRunMenu(-1);
}
