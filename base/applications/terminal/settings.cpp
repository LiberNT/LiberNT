/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminal settings: profiles, color schemes, key bindings and the settings file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include "json.h"

#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

namespace
{

std::basic_string<char32_t> U32(const std::wstring &text)
{
    return Vt::DecodeUtf16(reinterpret_cast<const char16_t *>(text.c_str()), text.size());
}

std::wstring W(const std::basic_string<char32_t> &text)
{
    std::basic_string<char16_t> utf16 = Vt::EncodeUtf16(text);
    return std::wstring(reinterpret_cast<const wchar_t *>(utf16.c_str()), utf16.size());
}

Json::Value Str(const std::wstring &text)
{
    return Json::Value(U32(text));
}

std::wstring GetString(const Json::Value &object, const char32_t *key, const std::wstring &fallback)
{
    const Json::Value *value = object.Find(key);
    return value && value->IsString() ? W(value->AsString()) : fallback;
}

int GetInt(const Json::Value &object, const char32_t *key, int fallback, int low, int high)
{
    const Json::Value *value = object.Find(key);
    int result;

    if (!value || !value->IsNumber())
        return fallback;
    result = (int)value->AsNumber();
    return result < low ? low : result > high ? high : result;
}

bool GetBool(const Json::Value &object, const char32_t *key, bool fallback)
{
    const Json::Value *value = object.Find(key);
    return value && value->IsBool() ? value->AsBool() : fallback;
}

struct SchemeSource
{
    const wchar_t *Name;
    COLORREF Foreground;
    COLORREF Background;
    COLORREF Cursor;
    COLORREF Selection;
    COLORREF Table[16];
};

/* The LiberNT schemes are this project's own palettes; Solarized and Tango keep the
   published values of those palettes. */
const SchemeSource BuiltInSchemes[] =
{
    { L"LiberNT Dark", RGB(0xD4, 0xD8, 0xDF), RGB(0x14, 0x17, 0x1D), RGB(0xE8, 0xEA, 0xEE), RGB(0x34, 0x45, 0x60),
      { RGB(0x14, 0x17, 0x1D), RGB(0xE0, 0x5A, 0x55), RGB(0x5F, 0xB3, 0x6A), RGB(0xD2, 0xA3, 0x3C),
        RGB(0x4F, 0x8F, 0xE6), RGB(0xB4, 0x7E, 0xE0), RGB(0x3F, 0xB8, 0xC4), RGB(0xC3, 0xC8, 0xD0),
        RGB(0x5C, 0x66, 0x73), RGB(0xF2, 0x80, 0x7A), RGB(0x84, 0xD0, 0x8C), RGB(0xEB, 0xC3, 0x63),
        RGB(0x7D, 0xB3, 0xF5), RGB(0xD0, 0xA6, 0xF2), RGB(0x6E, 0xD6, 0xDE), RGB(0xF2, 0xF4, 0xF7) } },
    { L"LiberNT Light", RGB(0x2A, 0x2E, 0x35), RGB(0xFB, 0xFB, 0xFC), RGB(0x1F, 0x23, 0x29), RGB(0xBD, 0xD5, 0xF5),
      { RGB(0x2A, 0x2E, 0x35), RGB(0xC2, 0x36, 0x30), RGB(0x2F, 0x86, 0x3B), RGB(0x9A, 0x6B, 0x00),
        RGB(0x1F, 0x5F, 0xC2), RGB(0x8A, 0x45, 0xB8), RGB(0x16, 0x83, 0x8F), RGB(0xC8, 0xCC, 0xD3),
        RGB(0x6A, 0x72, 0x7E), RGB(0xE0, 0x4B, 0x44), RGB(0x3F, 0xA0, 0x4D), RGB(0xB8, 0x86, 0x14),
        RGB(0x3B, 0x7B, 0xE0), RGB(0xA8, 0x62, 0xD6), RGB(0x24, 0xA2, 0xAF), RGB(0xFF, 0xFF, 0xFF) } },
    { L"Classic Console", RGB(0xC0, 0xC0, 0xC0), RGB(0x00, 0x00, 0x00), RGB(0xC0, 0xC0, 0xC0), RGB(0x80, 0x80, 0x80),
      { RGB(0x00, 0x00, 0x00), RGB(0x80, 0x00, 0x00), RGB(0x00, 0x80, 0x00), RGB(0x80, 0x80, 0x00),
        RGB(0x00, 0x00, 0x80), RGB(0x80, 0x00, 0x80), RGB(0x00, 0x80, 0x80), RGB(0xC0, 0xC0, 0xC0),
        RGB(0x80, 0x80, 0x80), RGB(0xFF, 0x00, 0x00), RGB(0x00, 0xFF, 0x00), RGB(0xFF, 0xFF, 0x00),
        RGB(0x00, 0x00, 0xFF), RGB(0xFF, 0x00, 0xFF), RGB(0x00, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF) } },
    { L"Solarized Dark", RGB(0x83, 0x94, 0x96), RGB(0x00, 0x2B, 0x36), RGB(0x93, 0xA1, 0xA1), RGB(0x07, 0x36, 0x42),
      { RGB(0x07, 0x36, 0x42), RGB(0xDC, 0x32, 0x2F), RGB(0x85, 0x99, 0x00), RGB(0xB5, 0x89, 0x00),
        RGB(0x26, 0x8B, 0xD2), RGB(0xD3, 0x36, 0x82), RGB(0x2A, 0xA1, 0x98), RGB(0xEE, 0xE8, 0xD5),
        RGB(0x00, 0x2B, 0x36), RGB(0xCB, 0x4B, 0x16), RGB(0x58, 0x6E, 0x75), RGB(0x65, 0x7B, 0x83),
        RGB(0x83, 0x94, 0x96), RGB(0x6C, 0x71, 0xC4), RGB(0x93, 0xA1, 0xA1), RGB(0xFD, 0xF6, 0xE3) } },
    { L"Solarized Light", RGB(0x65, 0x7B, 0x83), RGB(0xFD, 0xF6, 0xE3), RGB(0x58, 0x6E, 0x75), RGB(0xEE, 0xE8, 0xD5),
      { RGB(0x07, 0x36, 0x42), RGB(0xDC, 0x32, 0x2F), RGB(0x85, 0x99, 0x00), RGB(0xB5, 0x89, 0x00),
        RGB(0x26, 0x8B, 0xD2), RGB(0xD3, 0x36, 0x82), RGB(0x2A, 0xA1, 0x98), RGB(0xEE, 0xE8, 0xD5),
        RGB(0x00, 0x2B, 0x36), RGB(0xCB, 0x4B, 0x16), RGB(0x58, 0x6E, 0x75), RGB(0x65, 0x7B, 0x83),
        RGB(0x83, 0x94, 0x96), RGB(0x6C, 0x71, 0xC4), RGB(0x93, 0xA1, 0xA1), RGB(0xFD, 0xF6, 0xE3) } },
    { L"Tango Dark", RGB(0xD3, 0xD7, 0xCF), RGB(0x00, 0x00, 0x00), RGB(0xEE, 0xEE, 0xEC), RGB(0x55, 0x57, 0x53),
      { RGB(0x00, 0x00, 0x00), RGB(0xCC, 0x00, 0x00), RGB(0x4E, 0x9A, 0x06), RGB(0xC4, 0xA0, 0x00),
        RGB(0x34, 0x65, 0xA4), RGB(0x75, 0x50, 0x7B), RGB(0x06, 0x98, 0x9A), RGB(0xD3, 0xD7, 0xCF),
        RGB(0x55, 0x57, 0x53), RGB(0xEF, 0x29, 0x29), RGB(0x8A, 0xE2, 0x34), RGB(0xFC, 0xE9, 0x4F),
        RGB(0x72, 0x9F, 0xCF), RGB(0xAD, 0x7F, 0xA8), RGB(0x34, 0xE2, 0xE2), RGB(0xEE, 0xEE, 0xEC) } },
    { L"Tango Light", RGB(0x55, 0x57, 0x53), RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0x00, 0x00), RGB(0xD3, 0xD7, 0xCF),
      { RGB(0x00, 0x00, 0x00), RGB(0xCC, 0x00, 0x00), RGB(0x4E, 0x9A, 0x06), RGB(0xC4, 0xA0, 0x00),
        RGB(0x34, 0x65, 0xA4), RGB(0x75, 0x50, 0x7B), RGB(0x06, 0x98, 0x9A), RGB(0xD3, 0xD7, 0xCF),
        RGB(0x55, 0x57, 0x53), RGB(0xEF, 0x29, 0x29), RGB(0x8A, 0xE2, 0x34), RGB(0xFC, 0xE9, 0x4F),
        RGB(0x72, 0x9F, 0xCF), RGB(0xAD, 0x7F, 0xA8), RGB(0x34, 0xE2, 0xE2), RGB(0xEE, 0xEE, 0xEC) } },
};

const wchar_t *const ColorNames[16] =
{
    L"black", L"red", L"green", L"yellow", L"blue", L"purple", L"cyan", L"white",
    L"brightBlack", L"brightRed", L"brightGreen", L"brightYellow", L"brightBlue", L"brightPurple",
    L"brightCyan", L"brightWhite"
};

struct ActionName
{
    Action Command;
    const wchar_t *Name;
};

const ActionName ActionNames[] =
{
    { Action::NewTab, L"newTab" },
    { Action::NewTabProfile, L"newTabProfile" },
    { Action::DuplicateTab, L"duplicateTab" },
    { Action::CloseTab, L"closeTab" },
    { Action::CloseWindow, L"closeWindow" },
    { Action::NextTab, L"nextTab" },
    { Action::PrevTab, L"prevTab" },
    { Action::SwitchToTab, L"switchToTab" },
    { Action::Copy, L"copy" },
    { Action::Paste, L"paste" },
    { Action::SelectAll, L"selectAll" },
    { Action::Find, L"find" },
    { Action::OpenSettings, L"openSettings" },
    { Action::OpenSettingsFile, L"openSettingsFile" },
    { Action::ZoomIn, L"zoomIn" },
    { Action::ZoomOut, L"zoomOut" },
    { Action::ZoomReset, L"zoomReset" },
    { Action::ToggleFullscreen, L"toggleFullscreen" },
    { Action::ScrollUp, L"scrollUp" },
    { Action::ScrollDown, L"scrollDown" },
    { Action::ScrollPageUp, L"scrollPageUp" },
    { Action::ScrollPageDown, L"scrollPageDown" },
    { Action::ScrollToTop, L"scrollToTop" },
    { Action::ScrollToBottom, L"scrollToBottom" },
    { Action::ClearBuffer, L"clearBuffer" },
    { Action::RenameTab, L"renameTab" },
    { Action::About, L"about" },
};

struct KeyName
{
    UINT Key;
    const wchar_t *Name;
};

const KeyName KeyNames[] =
{
    { VK_TAB, L"tab" }, { VK_RETURN, L"enter" }, { VK_ESCAPE, L"esc" }, { VK_SPACE, L"space" },
    { VK_BACK, L"backspace" }, { VK_PRIOR, L"pgup" }, { VK_NEXT, L"pgdn" }, { VK_HOME, L"home" },
    { VK_END, L"end" }, { VK_INSERT, L"insert" }, { VK_DELETE, L"delete" }, { VK_UP, L"up" },
    { VK_DOWN, L"down" }, { VK_LEFT, L"left" }, { VK_RIGHT, L"right" }, { VK_OEM_PLUS, L"plus" },
    { VK_OEM_MINUS, L"minus" }, { VK_OEM_COMMA, L"," }, { VK_OEM_PERIOD, L"." }, { VK_OEM_2, L"/" },
    { VK_OEM_PLUS, L"=" }, { VK_ADD, L"numpad_plus" }, { VK_SUBTRACT, L"numpad_minus" },
};

void AddBinding(Settings &settings, Action command, int argument, const wchar_t *keys)
{
    KeyBinding binding;

    if (SettingsStore::ParseKeys(keys, binding))
    {
        binding.Command = command;
        binding.Argument = argument;
        settings.Bindings.push_back(binding);
    }
}

ColorScheme SchemeFromSource(const SchemeSource &source)
{
    ColorScheme scheme;

    scheme.Name = source.Name;
    scheme.Foreground = source.Foreground;
    scheme.Background = source.Background;
    scheme.Cursor = source.Cursor;
    scheme.Selection = source.Selection;
    memcpy(scheme.Table, source.Table, sizeof(scheme.Table));
    return scheme;
}

bool FileExistsInSystem(const wchar_t *name)
{
    wchar_t path[MAX_PATH];

    if (!GetSystemDirectoryW(path, MAX_PATH))
        return false;
    wcscat_s(path, MAX_PATH, L"\\");
    wcscat_s(path, MAX_PATH, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

const wchar_t *CursorName(Vt::CursorShape shape)
{
    switch (shape)
    {
        case Vt::CursorShape::Block: return L"filledBox";
        case Vt::CursorShape::Underline: return L"underscore";
        default: return L"bar";
    }
}

Vt::CursorShape CursorFromName(const std::wstring &name, Vt::CursorShape fallback)
{
    if (name == L"filledBox" || name == L"block")
        return Vt::CursorShape::Block;
    if (name == L"underscore" || name == L"underline")
        return Vt::CursorShape::Underline;
    if (name == L"bar" || name == L"vintage")
        return Vt::CursorShape::Bar;
    return fallback;
}

void ReadProfile(const Json::Value &object, Profile &profile)
{
    std::wstring closeOnExit;

    profile.Name = GetString(object, U"name", profile.Name);
    profile.CommandLine = GetString(object, U"commandline", profile.CommandLine);
    profile.StartingDirectory = GetString(object, U"startingDirectory", profile.StartingDirectory);
    profile.TabTitle = GetString(object, U"tabTitle", profile.TabTitle);
    profile.ColorScheme = GetString(object, U"colorScheme", profile.ColorScheme);
    profile.FontFace = GetString(object, U"fontFace", profile.FontFace);
    profile.FontSize = GetInt(object, U"fontSize", profile.FontSize, 6, 72);
    profile.FontBold = GetBool(object, U"fontBold", profile.FontBold);
    profile.Cursor = CursorFromName(GetString(object, U"cursorShape", L""), profile.Cursor);
    profile.CursorBlink = GetBool(object, U"cursorBlink", profile.CursorBlink);
    profile.ScrollbackLines = GetInt(object, U"historySize", profile.ScrollbackLines, 0, 1000000);
    profile.Padding = GetInt(object, U"padding", profile.Padding, 0, 64);
    closeOnExit = GetString(object, U"closeOnExit", L"");
    if (closeOnExit == L"always")
        profile.OnExit = CloseOnExit::Always;
    else if (closeOnExit == L"never")
        profile.OnExit = CloseOnExit::Never;
    else if (closeOnExit == L"graceful")
        profile.OnExit = CloseOnExit::Graceful;
    profile.SuppressApplicationTitle = GetBool(object, U"suppressApplicationTitle", profile.SuppressApplicationTitle);
    profile.Hidden = GetBool(object, U"hidden", profile.Hidden);
    profile.BellSound = GetBool(object, U"bellSound", profile.BellSound);
}

Json::Value WriteProfile(const Profile &profile)
{
    Json::Value object = Json::Value::MakeObject();

    object.Set(U"name", Str(profile.Name));
    object.Set(U"commandline", Str(profile.CommandLine));
    object.Set(U"startingDirectory", Str(profile.StartingDirectory));
    if (!profile.TabTitle.empty())
        object.Set(U"tabTitle", Str(profile.TabTitle));
    object.Set(U"colorScheme", Str(profile.ColorScheme));
    object.Set(U"fontFace", Str(profile.FontFace));
    object.Set(U"fontSize", Json::Value((double)profile.FontSize));
    object.Set(U"fontBold", Json::Value(profile.FontBold));
    object.Set(U"cursorShape", Str(CursorName(profile.Cursor)));
    object.Set(U"cursorBlink", Json::Value(profile.CursorBlink));
    object.Set(U"historySize", Json::Value((double)profile.ScrollbackLines));
    object.Set(U"padding", Json::Value((double)profile.Padding));
    object.Set(U"closeOnExit", Str(profile.OnExit == CloseOnExit::Always ? L"always" :
                                   profile.OnExit == CloseOnExit::Never ? L"never" : L"graceful"));
    object.Set(U"suppressApplicationTitle", Json::Value(profile.SuppressApplicationTitle));
    object.Set(U"bellSound", Json::Value(profile.BellSound));
    object.Set(U"hidden", Json::Value(profile.Hidden));
    return object;
}

}

const Profile *
Settings::FindProfile(const std::wstring &name) const
{
    for (const Profile &profile : Profiles)
    {
        if (_wcsicmp(profile.Name.c_str(), name.c_str()) == 0)
            return &profile;
    }
    return NULL;
}

const Profile &
Settings::Default() const
{
    const Profile *profile = FindProfile(DefaultProfile);

    if (profile)
        return *profile;
    for (const Profile &candidate : Profiles)
    {
        if (!candidate.Hidden)
            return candidate;
    }
    return Profiles.front();
}

const ColorScheme &
Settings::SchemeFor(const Profile &profile) const
{
    for (const ColorScheme &scheme : Schemes)
    {
        if (_wcsicmp(scheme.Name.c_str(), profile.ColorScheme.c_str()) == 0)
            return scheme;
    }
    return Schemes.front();
}

const KeyBinding *
Settings::FindBinding(UINT key, bool ctrl, bool shift, bool alt) const
{
    for (const KeyBinding &binding : Bindings)
    {
        if (binding.Key == key && binding.Ctrl == ctrl && binding.Shift == shift && binding.Alt == alt)
            return &binding;
    }
    return NULL;
}

std::wstring
Settings::KeysFor(Action command, int argument) const
{
    for (const KeyBinding &binding : Bindings)
    {
        if (binding.Command == command && binding.Argument == argument)
            return SettingsStore::FormatKeys(binding);
    }
    return L"";
}

namespace SettingsStore
{

std::wstring
FilePath()
{
    wchar_t *base = NULL;
    std::wstring path;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)) && base)
    {
        path = base;
        CoTaskMemFree(base);
    }
    else
    {
        wchar_t buffer[MAX_PATH];
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        path = length && length < MAX_PATH ? buffer : L".";
    }
    return path + L"\\LiberNT\\Terminal\\settings.json";
}

Settings
Defaults()
{
    Settings settings;
    Profile cmd;

    cmd.Name = L"Command Prompt";
    cmd.CommandLine = L"%SystemRoot%\\System32\\cmd.exe";
    cmd.StartingDirectory = L"%USERPROFILE%";
    cmd.ColorScheme = L"LiberNT Dark";
    cmd.FontFace = L"Lucida Console";
    settings.Profiles.push_back(cmd);

    if (FileExistsInSystem(L"WindowsPowerShell\\v1.0\\powershell.exe"))
    {
        Profile powershell = cmd;
        powershell.Name = L"Windows PowerShell";
        powershell.CommandLine = L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        settings.Profiles.push_back(powershell);
    }
    if (FileExistsInSystem(L"lxterm.exe"))
    {
        Profile ubuntu = cmd;
        ubuntu.Name = L"Ubuntu";
        ubuntu.CommandLine = L"%SystemRoot%\\System32\\lxterm.exe";
        ubuntu.StartingDirectory = L"";
        settings.Profiles.push_back(ubuntu);
    }

    for (const SchemeSource &source : BuiltInSchemes)
        settings.Schemes.push_back(SchemeFromSource(source));

    settings.DefaultProfile = cmd.Name;

    AddBinding(settings, Action::NewTab, 0, L"ctrl+shift+t");
    AddBinding(settings, Action::DuplicateTab, 0, L"ctrl+shift+d");
    AddBinding(settings, Action::CloseTab, 0, L"ctrl+shift+w");
    AddBinding(settings, Action::CloseWindow, 0, L"alt+f4");
    AddBinding(settings, Action::NextTab, 0, L"ctrl+tab");
    AddBinding(settings, Action::PrevTab, 0, L"ctrl+shift+tab");
    for (int i = 1; i <= 9; i++)
    {
        wchar_t keys[32];

        swprintf(keys, 32, L"ctrl+alt+%d", i);
        AddBinding(settings, Action::SwitchToTab, i, keys);
        swprintf(keys, 32, L"ctrl+shift+%d", i);
        AddBinding(settings, Action::NewTabProfile, i, keys);
    }
    AddBinding(settings, Action::Copy, 0, L"ctrl+shift+c");
    AddBinding(settings, Action::Copy, 0, L"ctrl+insert");
    AddBinding(settings, Action::Paste, 0, L"ctrl+shift+v");
    AddBinding(settings, Action::Paste, 0, L"shift+insert");
    AddBinding(settings, Action::SelectAll, 0, L"ctrl+shift+a");
    AddBinding(settings, Action::Find, 0, L"ctrl+shift+f");
    AddBinding(settings, Action::OpenSettings, 0, L"ctrl+,");
    AddBinding(settings, Action::OpenSettingsFile, 0, L"ctrl+shift+,");
    AddBinding(settings, Action::ZoomIn, 0, L"ctrl+=");
    AddBinding(settings, Action::ZoomIn, 0, L"ctrl+numpad_plus");
    AddBinding(settings, Action::ZoomOut, 0, L"ctrl+minus");
    AddBinding(settings, Action::ZoomOut, 0, L"ctrl+numpad_minus");
    AddBinding(settings, Action::ZoomReset, 0, L"ctrl+0");
    AddBinding(settings, Action::ToggleFullscreen, 0, L"f11");
    AddBinding(settings, Action::ToggleFullscreen, 0, L"alt+enter");
    AddBinding(settings, Action::ScrollUp, 0, L"ctrl+shift+up");
    AddBinding(settings, Action::ScrollDown, 0, L"ctrl+shift+down");
    AddBinding(settings, Action::ScrollPageUp, 0, L"ctrl+shift+pgup");
    AddBinding(settings, Action::ScrollPageDown, 0, L"ctrl+shift+pgdn");
    AddBinding(settings, Action::ScrollToTop, 0, L"ctrl+shift+home");
    AddBinding(settings, Action::ScrollToBottom, 0, L"ctrl+shift+end");
    AddBinding(settings, Action::ClearBuffer, 0, L"ctrl+shift+k");
    AddBinding(settings, Action::RenameTab, 0, L"ctrl+shift+r");
    return settings;
}

FILETIME
LastWrite()
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    FILETIME none = { 0, 0 };

    if (!GetFileAttributesExW(FilePath().c_str(), GetFileExInfoStandard, &data))
        return none;
    return data.ftLastWriteTime;
}

static bool ReadFileText(const std::wstring &path, std::string &text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    DWORD read = 0;

    if (file == INVALID_HANDLE_VALUE)
        return false;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 16 * 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }
    text.resize((size_t)size.QuadPart);
    if (!text.empty() && !ReadFile(file, &text[0], (DWORD)text.size(), &read, NULL))
        read = 0;
    CloseHandle(file);
    text.resize(read);
    return true;
}

static void CreateParentDirectories(const std::wstring &path)
{
    for (size_t i = 3; i < path.size(); i++)
    {
        if (path[i] == L'\\')
            CreateDirectoryW(path.substr(0, i).c_str(), NULL);
    }
}

Settings
Load(std::wstring *error)
{
    Settings defaults = Defaults(), settings;
    std::wstring path = FilePath();
    std::string text;
    Json::Value root;
    int line = 0;

    if (error)
        error->clear();
    if (!ReadFileText(path, text))
    {
        Save(defaults, NULL);
        return defaults;
    }
    if (!Json::Parse(text, root, &line) || !root.IsObject())
    {
        if (error)
        {
            wchar_t message[128];
            swprintf(message, 128, L"settings.json is not valid JSON (line %d); the defaults are in use.", line);
            *error = message;
        }
        return defaults;
    }

    settings = defaults;
    settings.DefaultProfile = GetString(root, U"defaultProfile", defaults.DefaultProfile);
    {
        std::wstring theme = GetString(root, U"theme", L"system");
        settings.Theme = theme == L"light" ? ThemeMode::Light : theme == L"dark" ? ThemeMode::Dark : ThemeMode::System;
    }
    settings.CopyOnSelect = GetBool(root, U"copyOnSelect", defaults.CopyOnSelect);
    settings.ConfirmCloseAllTabs = GetBool(root, U"confirmCloseAllTabs", defaults.ConfirmCloseAllTabs);
    settings.ShowBellIndicator = GetBool(root, U"showBellIndicator", defaults.ShowBellIndicator);
    settings.InitialCols = GetInt(root, U"initialCols", defaults.InitialCols, 20, 1000);
    settings.InitialRows = GetInt(root, U"initialRows", defaults.InitialRows, 5, 500);
    settings.WordDelimiters = GetString(root, U"wordDelimiters", defaults.WordDelimiters);

    if (const Json::Value *profiles = root.Find(U"profiles"))
    {
        if (profiles->IsArray() && profiles->Size() > 0)
        {
            Profile base = defaults.Profiles.front();

            settings.Profiles.clear();
            for (size_t i = 0; i < profiles->Size(); i++)
            {
                Profile profile = base;

                if (!(*profiles)[i].IsObject())
                    continue;
                profile.Name.clear();
                profile.CommandLine.clear();
                ReadProfile((*profiles)[i], profile);
                if (!profile.Name.empty() && !profile.CommandLine.empty())
                    settings.Profiles.push_back(profile);
            }
            if (settings.Profiles.empty())
                settings.Profiles = defaults.Profiles;
        }
    }

    if (const Json::Value *schemes = root.Find(U"schemes"))
    {
        for (size_t i = 0; schemes->IsArray() && i < schemes->Size(); i++)
        {
            const Json::Value &object = (*schemes)[i];
            ColorScheme scheme = defaults.Schemes.front();
            bool replaced = false;

            if (!object.IsObject())
                continue;
            scheme.Name = GetString(object, U"name", L"");
            if (scheme.Name.empty())
                continue;
            ParseColor(GetString(object, U"foreground", L""), &scheme.Foreground);
            ParseColor(GetString(object, U"background", L""), &scheme.Background);
            ParseColor(GetString(object, U"cursorColor", L""), &scheme.Cursor);
            ParseColor(GetString(object, U"selectionBackground", L""), &scheme.Selection);
            for (int c = 0; c < 16; c++)
                ParseColor(GetString(object, U32(ColorNames[c]).c_str(), L""), &scheme.Table[c]);
            for (ColorScheme &existing : settings.Schemes)
            {
                if (_wcsicmp(existing.Name.c_str(), scheme.Name.c_str()) == 0)
                {
                    existing = scheme;
                    replaced = true;
                }
            }
            if (!replaced)
                settings.Schemes.push_back(scheme);
        }
    }

    if (const Json::Value *actions = root.Find(U"actions"))
    {
        if (actions->IsArray())
        {
            settings.Bindings.clear();
            for (size_t i = 0; i < actions->Size(); i++)
            {
                const Json::Value &object = (*actions)[i];
                KeyBinding binding;

                if (!object.IsObject() || !ParseKeys(GetString(object, U"keys", L""), binding))
                    continue;
                binding.Command = ActionFromName(GetString(object, U"command", L""));
                binding.Argument = GetInt(object, U"index", 0, 0, 64);
                if (binding.Command != Action::None)
                    settings.Bindings.push_back(binding);
            }
        }
    }
    return settings;
}

bool
Save(const Settings &settings, std::wstring *error)
{
    Json::Value root = Json::Value::MakeObject();
    Json::Value profiles = Json::Value::MakeArray();
    Json::Value schemes = Json::Value::MakeArray();
    Json::Value actions = Json::Value::MakeArray();
    std::wstring path = FilePath(), temporary;
    std::string text;
    HANDLE file;
    DWORD written = 0;
    BOOL ok;

    root.Set(U"defaultProfile", Str(settings.DefaultProfile));
    root.Set(U"theme", Str(settings.Theme == ThemeMode::Light ? L"light" : settings.Theme == ThemeMode::Dark ? L"dark" : L"system"));
    root.Set(U"copyOnSelect", Json::Value(settings.CopyOnSelect));
    root.Set(U"confirmCloseAllTabs", Json::Value(settings.ConfirmCloseAllTabs));
    root.Set(U"showBellIndicator", Json::Value(settings.ShowBellIndicator));
    root.Set(U"initialCols", Json::Value((double)settings.InitialCols));
    root.Set(U"initialRows", Json::Value((double)settings.InitialRows));
    root.Set(U"wordDelimiters", Str(settings.WordDelimiters));

    for (const Profile &profile : settings.Profiles)
        profiles.Append(WriteProfile(profile));
    root.Set(U"profiles", profiles);

    for (const ColorScheme &scheme : settings.Schemes)
    {
        Json::Value object = Json::Value::MakeObject();

        object.Set(U"name", Str(scheme.Name));
        object.Set(U"foreground", Str(FormatColor(scheme.Foreground)));
        object.Set(U"background", Str(FormatColor(scheme.Background)));
        object.Set(U"cursorColor", Str(FormatColor(scheme.Cursor)));
        object.Set(U"selectionBackground", Str(FormatColor(scheme.Selection)));
        for (int c = 0; c < 16; c++)
            object.Set(U32(ColorNames[c]), Str(FormatColor(scheme.Table[c])));
        schemes.Append(object);
    }
    root.Set(U"schemes", schemes);

    for (const KeyBinding &binding : settings.Bindings)
    {
        Json::Value object = Json::Value::MakeObject();

        object.Set(U"command", Str(ActionName(binding.Command)));
        if (binding.Argument)
            object.Set(U"index", Json::Value((double)binding.Argument));
        object.Set(U"keys", Str(FormatKeys(binding)));
        actions.Append(object);
    }
    root.Set(U"actions", actions);

    text = Json::Write(root);
    CreateParentDirectories(path);
    temporary = path + L".tmp";
    file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (error)
            *error = L"The settings file cannot be written.";
        return false;
    }
    ok = WriteFile(file, text.data(), (DWORD)text.size(), &written, NULL) && written == text.size();
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(temporary.c_str());
        if (error)
            *error = L"The settings file cannot be written.";
        return false;
    }
    return true;
}

std::wstring
ActionName(Action command)
{
    for (const auto &entry : ActionNames)
    {
        if (entry.Command == command)
            return entry.Name;
    }
    return L"";
}

Action
ActionFromName(const std::wstring &name)
{
    for (const auto &entry : ActionNames)
    {
        if (name == entry.Name)
            return entry.Command;
    }
    return Action::None;
}

std::wstring
FormatKeys(const KeyBinding &binding)
{
    std::wstring text;
    wchar_t number[16];

    if (binding.Ctrl)
        text += L"ctrl+";
    if (binding.Alt)
        text += L"alt+";
    if (binding.Shift)
        text += L"shift+";
    for (const KeyName &name : KeyNames)
    {
        if (name.Key == binding.Key)
            return text + name.Name;
    }
    if (binding.Key >= VK_F1 && binding.Key <= VK_F24)
    {
        swprintf(number, ARRAYSIZE(number), L"f%u", binding.Key - VK_F1 + 1);
        return text + number;
    }
    if ((binding.Key >= L'A' && binding.Key <= L'Z') || (binding.Key >= L'0' && binding.Key <= L'9'))
        return text + (wchar_t)towlower((wint_t)binding.Key);
    swprintf(number, ARRAYSIZE(number), L"vk%u", binding.Key);
    return text + number;
}

bool
ParseKeys(const std::wstring &text, KeyBinding &binding)
{
    std::wstring rest = text;

    binding = KeyBinding();
    for (;;)
    {
        size_t plus = rest.find(L'+');
        std::wstring part;

        if (plus == std::wstring::npos || plus == rest.size() - 1)
        {
            part = rest;
            rest.clear();
        }
        else
        {
            part = rest.substr(0, plus);
            rest = rest.substr(plus + 1);
        }
        for (wchar_t &ch : part)
            ch = (wchar_t)towlower((wint_t)ch);

        if (part == L"ctrl")
            binding.Ctrl = true;
        else if (part == L"shift")
            binding.Shift = true;
        else if (part == L"alt")
            binding.Alt = true;
        else
        {
            bool found = false;

            if (!rest.empty())
                return false;
            for (const KeyName &name : KeyNames)
            {
                if (part == name.Name)
                {
                    binding.Key = name.Key;
                    found = true;
                    break;
                }
            }
            if (!found && part.size() == 1 && ((part[0] >= L'a' && part[0] <= L'z') || (part[0] >= L'0' && part[0] <= L'9')))
            {
                binding.Key = (UINT)towupper(part[0]);
                found = true;
            }
            if (!found && part.size() >= 2 && part[0] == L'f')
            {
                int number = _wtoi(part.c_str() + 1);
                if (number >= 1 && number <= 24)
                {
                    binding.Key = VK_F1 + number - 1;
                    found = true;
                }
            }
            if (!found && part.compare(0, 2, L"vk") == 0)
            {
                binding.Key = (UINT)_wtoi(part.c_str() + 2);
                found = binding.Key != 0;
            }
            return found;
        }
        if (rest.empty())
            return false;
    }
}

std::wstring
FormatColor(COLORREF color)
{
    wchar_t text[16];

    swprintf(text, 16, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return text;
}

bool
ParseColor(const std::wstring &text, COLORREF *color)
{
    unsigned value = 0;

    if (text.size() != 7 || text[0] != L'#')
        return false;
    for (size_t i = 1; i < 7; i++)
    {
        wchar_t ch = text[i];
        unsigned digit;

        if (ch >= L'0' && ch <= L'9')
            digit = ch - L'0';
        else if (ch >= L'a' && ch <= L'f')
            digit = ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F')
            digit = ch - L'A' + 10;
        else
            return false;
        value = value * 16 + digit;
    }
    *color = RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return true;
}

}
