/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     State and services the graphical boot menu depends on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>

#define GUI_MESSAGE_CHARS     512
#define GUI_PROGRESS_FULL     100
#define GUI_PROGRESS_EXPECTED 25

ULONG UiScreenWidth;
ULONG UiScreenHeight;
BOOLEAN UiKeepFirmwareScreen;
UI_PROGRESS_BAR UiProgressBar;
BOOLEAN UiShowTime;
BOOLEAN UiMenuBox;
BOOLEAN UiCenterMenu;
BOOLEAN UiUseSpecialEffects;
UCHAR UiBackdropFgColor;
UCHAR UiBackdropBgColor;
UCHAR UiBackdropFillStyle;
UCHAR UiStatusBarFgColor;
UCHAR UiStatusBarBgColor;
UCHAR UiTitleBoxFgColor;
UCHAR UiTitleBoxBgColor;
UCHAR UiMessageBoxFgColor;
UCHAR UiMessageBoxBgColor;
UCHAR UiMenuFgColor;
UCHAR UiMenuBgColor;
UCHAR UiTextColor;
UCHAR UiSelectedTextColor;
UCHAR UiSelectedTextBgColor;
UCHAR UiEditBoxTextColor;
UCHAR UiEditBoxBgColor;

TIMEINFO*
ArcGetTime(VOID)
{
    static TIMEINFO TimeInfo;
    TIME_FIELDS Time;

    if (FwGetTime(&Time))
    {
        TimeInfo.Year = Time.Year;
        TimeInfo.Month = Time.Month;
        TimeInfo.Day = Time.Day;
        TimeInfo.Hour = Time.Hour;
        TimeInfo.Minute = Time.Minute;
        TimeInfo.Second = Time.Second;
    }

    return &TimeInfo;
}

VOID
UiInitProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText)
{
    UiProgressBar.Left = Left;
    UiProgressBar.Top = Top;
    UiProgressBar.Right = Right;
    UiProgressBar.Bottom = Bottom;
    UiProgressBar.State.Floor = 0;
    UiProgressBar.State.Bias = GUI_PROGRESS_FULL;
    UiProgressBar.Indicator.Count = 0;
    UiProgressBar.Indicator.Expected = GUI_PROGRESS_EXPECTED;
    UiProgressBar.Indicator.Percentage = 0;
    UiProgressBar.Show = TRUE;
    FbGuiVtbl.SetProgressBarText(ProgressText);
    FbGuiVtbl.TickProgressBar(0);
}

BOOLEAN
GuiInitialize(VOID)
{
    ULONG Width, Height, Depth, CellWidth, CellHeight;

    VidFbGetDisplaySize(&Width, &Height, &Depth);
    FbConsGetCellSize(&CellWidth, &CellHeight);
    if (Width == 0 || Height == 0 || CellWidth == 0 || CellHeight == 0)
        return FALSE;

    UiScreenWidth = Width / CellWidth;
    UiScreenHeight = Height / CellHeight;
    return FbGuiVtbl.Initialize();
}

BOOLEAN
UiDisplayMenu(
    IN PCSTR MenuHeader,
    IN PCSTR MenuFooter OPTIONAL,
    IN PCSTR MenuItemList[],
    IN ULONG MenuItemCount,
    IN ULONG DefaultMenuItem,
    IN LONG MenuTimeOut,
    OUT PULONG SelectedMenuItem,
    IN BOOLEAN CanEscape,
    IN UiMenuKeyPressFilterCallback KeyPressFilter OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    return FbGuiVtbl.DisplayMenu(MenuHeader,
                                 MenuFooter,
                                 MenuItemList,
                                 MenuItemCount,
                                 DefaultMenuItem,
                                 MenuTimeOut,
                                 SelectedMenuItem,
                                 CanEscape,
                                 NULL,
                                 KeyPressFilter,
                                 Context);
}

VOID
UiMessageBox(
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[GUI_MESSAGE_CHARS];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    FbGuiVtbl.MessageBox(Text);
}
