/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Graphical boot menu interface and the firmware services it uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <bmui.h>

#define KEY_VOLUME_UP   0x100
#define KEY_VOLUME_DOWN 0x101

#define GUI_MAX_ENTRIES 16

#define ATTR(cFore, cBack) ((cBack << 4) | cFore)

#define COLOR_BLACK     0
#define COLOR_GRAY      7
#define COLOR_DARKGRAY  8
#define COLOR_WHITE     15

#define FrLdrTempAlloc(Size, Tag) FwAllocate(Size)
#define FrLdrTempFree(Memory, Tag) FwFree(Memory)
#define DebugGetErrorLines(Lines, MaxLines) ((void)(Lines), (void)(MaxLines), 0UL)

#define MachConsKbHit() FwKeyAvailable()
#define MachConsGetCh() FwReadKey()
#define MachVideoSync() FwVideoSync()
#define MachHwIdle() FwIdle()
#define MachGetPointerState(Width, Height, State) FwGetPointerState((Width), (Height), (State))
#define MachSetInputTimer(Milliseconds) FwSetInputTimer(Milliseconds)
#define MachInputTimerExpired() FwInputTimerExpired()

typedef struct _MACH_POINTER_STATE
{
    ULONG X;
    ULONG Y;
    LONG Wheel;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
    BOOLEAN Absolute;
} MACH_POINTER_STATE, *PMACH_POINTER_STATE;

typedef struct _UI_PROGRESS_BAR
{
    struct
    {
        ULONG Floor;
        ULONG Bias;
    } State;

    struct
    {
        ULONG Count;
        ULONG Expected;
        ULONG Percentage;
    } Indicator;

    ULONG Left;
    ULONG Top;
    ULONG Right;
    ULONG Bottom;
    BOOLEAN Show;
} UI_PROGRESS_BAR, *PUI_PROGRESS_BAR;

typedef enum
{
    UiMenuIconNone,
    UiMenuIconLiberNT,
    UiMenuIconDesktop
} UI_MENU_ICON;

typedef struct tagUI_MENU_INFO
{
    PCSTR MenuHeader;
    PCSTR MenuFooter;
    PCSTR* MenuItemList;
    const UI_MENU_ICON* MenuItemIcons;
    ULONG MenuItemCount;
    ULONG SelectedMenuItem;
    PVOID Context;
    ULONG Left;
    ULONG Top;
    ULONG Right;
    ULONG Bottom;
} UI_MENU_INFO, *PUI_MENU_INFO;

typedef
BOOLEAN
(*UiMenuKeyPressFilterCallback)(
    IN ULONG KeyPress,
    IN ULONG SelectedMenuItem,
    IN PVOID Context OPTIONAL);

typedef struct tagUIVTBL
{
    BOOLEAN (*Initialize)(VOID);
    VOID (*UnInitialize)(VOID);
    VOID (*DrawBackdrop)(ULONG DrawHeight);
    VOID (*FillArea)(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, CHAR FillChar, UCHAR Attr);
    VOID (*DrawShadow)(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom);
    VOID (*DrawBox)(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, UCHAR VertStyle, UCHAR HorzStyle, BOOLEAN Fill, BOOLEAN Shadow, UCHAR Attr);
    VOID (*DrawText)(ULONG X, ULONG Y, PCSTR Text, UCHAR Attr);
    VOID (*DrawText2)(ULONG X, ULONG Y, ULONG MaxNumChars, PCSTR Text, UCHAR Attr);
    VOID (*DrawCenteredText)(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, PCSTR TextString, UCHAR Attr);
    VOID (*DrawStatusText)(PCSTR StatusText);
    VOID (*UpdateDateTime)(VOID);
    VOID (*MessageBox)(PCSTR MessageText);
    VOID (*MessageBoxCritical)(PCSTR MessageText);
    VOID (*DrawProgressBarCenter)(PCSTR ProgressText);
    VOID (*DrawProgressBar)(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, PCSTR ProgressText);
    VOID (*SetProgressBarText)(PCSTR ProgressText);
    VOID (*TickProgressBar)(ULONG SubPercentTimes100);
    BOOLEAN (*EditBox)(PCSTR MessageText, PCHAR EditTextBuffer, ULONG Length);
    UCHAR (*TextToColor)(PCSTR ColorText);
    UCHAR (*TextToFillStyle)(PCSTR FillStyleText);
    VOID (*FadeInBackdrop)(VOID);
    VOID (*FadeOut)(VOID);
    BOOLEAN (*DisplayMenu)(
        IN PCSTR MenuHeader,
        IN PCSTR MenuFooter OPTIONAL,
        IN PCSTR MenuItemList[],
        IN ULONG MenuItemCount,
        IN ULONG DefaultMenuItem,
        IN LONG MenuTimeOut,
        OUT PULONG SelectedMenuItem,
        IN BOOLEAN CanEscape,
        IN const UI_MENU_ICON* MenuItemIcons OPTIONAL,
        IN UiMenuKeyPressFilterCallback KeyPressFilter OPTIONAL,
        IN PVOID Context OPTIONAL);
    VOID (*DrawMenu)(PUI_MENU_INFO MenuInfo);
} UIVTBL, *PUIVTBL;

extern const UIVTBL FbGuiVtbl;

extern ULONG UiScreenWidth;
extern ULONG UiScreenHeight;
extern BOOLEAN UiKeepFirmwareScreen;
extern UI_PROGRESS_BAR UiProgressBar;
extern BOOLEAN UiShowTime;
extern BOOLEAN UiMenuBox;
extern BOOLEAN UiCenterMenu;
extern BOOLEAN UiUseSpecialEffects;
extern UCHAR UiBackdropFgColor;
extern UCHAR UiBackdropBgColor;
extern UCHAR UiBackdropFillStyle;
extern UCHAR UiStatusBarFgColor;
extern UCHAR UiStatusBarBgColor;
extern UCHAR UiTitleBoxFgColor;
extern UCHAR UiTitleBoxBgColor;
extern UCHAR UiMessageBoxFgColor;
extern UCHAR UiMessageBoxBgColor;
extern UCHAR UiMenuFgColor;
extern UCHAR UiMenuBgColor;
extern UCHAR UiTextColor;
extern UCHAR UiSelectedTextColor;
extern UCHAR UiSelectedTextBgColor;
extern UCHAR UiEditBoxTextColor;
extern UCHAR UiEditBoxBgColor;

VOID
FwVideoSync(VOID);

VOID
FwIdle(VOID);

BOOLEAN
FwGetPointerState(
    ULONG Width,
    ULONG Height,
    PMACH_POINTER_STATE State);

BOOLEAN
FwSetInputTimer(
    ULONG Milliseconds);

BOOLEAN
FwInputTimerExpired(VOID);

VOID
VideoFreeOffScreenBuffer(VOID);

VOID
FbConsGetCellSize(
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight);

TIMEINFO*
ArcGetTime(VOID);

VOID
UiInitProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText);

BOOLEAN
GuiInitialize(VOID);

VOID
GuiShowFirmwareLogo(VOID);

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
    IN PVOID Context OPTIONAL);

VOID
Reboot(VOID);

BOOLEAN
FwFirmwareSetupSupported(VOID);

VOID
FwBootToFirmware(VOID);
