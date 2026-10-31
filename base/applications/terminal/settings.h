/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminal settings: profiles, color schemes, key bindings and the settings file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "vt.h"

enum class ThemeMode
{
    System,
    Light,
    Dark
};

enum class CloseOnExit
{
    Graceful,
    Always,
    Never
};

enum class Action
{
    None,
    NewTab,
    NewTabProfile,
    DuplicateTab,
    CloseTab,
    CloseWindow,
    NextTab,
    PrevTab,
    SwitchToTab,
    Copy,
    Paste,
    SelectAll,
    Find,
    OpenSettings,
    OpenSettingsFile,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    ToggleFullscreen,
    ScrollUp,
    ScrollDown,
    ScrollPageUp,
    ScrollPageDown,
    ScrollToTop,
    ScrollToBottom,
    ClearBuffer,
    RenameTab,
    About
};

struct ColorScheme
{
    std::wstring Name;
    COLORREF Foreground;
    COLORREF Background;
    COLORREF Cursor;
    COLORREF Selection;
    COLORREF Table[16];
};

struct Profile
{
    std::wstring Name;
    std::wstring CommandLine;
    std::wstring StartingDirectory;
    std::wstring TabTitle;
    std::wstring ColorScheme;
    std::wstring FontFace;
    int FontSize = 10;
    bool FontBold = false;
    Vt::CursorShape Cursor = Vt::CursorShape::Bar;
    bool CursorBlink = true;
    int ScrollbackLines = 9001;
    int Padding = 2;
    CloseOnExit OnExit = CloseOnExit::Graceful;
    bool SuppressApplicationTitle = false;
    bool Hidden = false;
    bool BellSound = false;
};

struct KeyBinding
{
    Action Command = Action::None;
    int Argument = 0;
    UINT Key = 0;
    bool Ctrl = false;
    bool Shift = false;
    bool Alt = false;
};

struct Settings
{
    std::wstring DefaultProfile;
    ThemeMode Theme = ThemeMode::System;
    bool CopyOnSelect = false;
    bool ConfirmCloseAllTabs = true;
    bool ShowBellIndicator = true;
    int InitialCols = 80;
    int InitialRows = 25;
    std::wstring WordDelimiters = L" ()[]{}'\",;:|<>`";
    std::vector<Profile> Profiles;
    std::vector<ColorScheme> Schemes;
    std::vector<KeyBinding> Bindings;

    const Profile *FindProfile(const std::wstring &name) const;
    const Profile &Default() const;
    const ColorScheme &SchemeFor(const Profile &profile) const;
    const KeyBinding *FindBinding(UINT key, bool ctrl, bool shift, bool alt) const;
    std::wstring KeysFor(Action command, int argument) const;
};

namespace SettingsStore
{

std::wstring FilePath();
Settings Defaults();
/* Loads the settings file, creating it with the defaults when it does not exist. */
Settings Load(std::wstring *error);
bool Save(const Settings &settings, std::wstring *error);
FILETIME LastWrite();

std::wstring ActionName(Action command);
Action ActionFromName(const std::wstring &name);
std::wstring FormatKeys(const KeyBinding &binding);
bool ParseKeys(const std::wstring &text, KeyBinding &binding);
std::wstring FormatColor(COLORREF color);
bool ParseColor(const std::wstring &text, COLORREF *color);

}
