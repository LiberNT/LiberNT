/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot manager screens and the firmware console they draw on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KEY_EXTENDED    0x00
#define KEY_BACKSPACE   0x08
#define KEY_ENTER       0x0D
#define KEY_ESC         0x1B
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_HOME        0x47
#define KEY_UP          0x48
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D
#define KEY_END         0x4F
#define KEY_DOWN        0x50
#define KEY_DELETE      0x53

#define BMUI_ATTR_NORMAL    0x07
#define BMUI_ATTR_BRIGHT    0x0F
#define BMUI_ATTR_INVERSE   0x70
#define BMUI_ATTR_SETTINGS  0x1F

typedef enum _BM_UI_ACTION
{
    BmUiActionStart,
    BmUiActionAdvanced,
    BmUiActionExit
} BM_UI_ACTION;

typedef enum _BM_UI_SETTING
{
    BmUiSettingNone,
    BmUiSettingStandard,
    BmUiSettingMore
} BM_UI_SETTING;

BOOLEAN
FwKeyAvailable(VOID);

int
FwReadKey(VOID);

VOID
VidFbGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth);

VOID
VidFbPresent(
    _In_ const ULONG* Pixels,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height);

VOID
FbConsClearScreen(
    _In_ UCHAR Attr);

VOID
FbConsPutChar(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row);

BOOLEAN
BmUiInitialize(VOID);

VOID
BmUiShutdown(VOID);

BM_UI_ACTION
BmUiChooseEntry(
    _In_ PCWSTR Title,
    _In_reads_(Count) PCWSTR* Names,
    _In_ ULONG Count,
    _Inout_ PULONG Selected,
    _In_ LONG Timeout);

BM_UI_SETTING
BmUiStartupSettings(
    _In_reads_(StandardCount) PCSTR* Standard,
    _In_ ULONG StandardCount,
    _In_reads_(MoreCount) PCSTR* More,
    _In_ ULONG MoreCount,
    _Out_ PULONG Choice);
