/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Light or dark color scheme and shell material shared by every themed component
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <winreg.h>

#define REACTOS_DWM_SETTINGS_KEY L"Software\\ReactOS\\DWM"
#define REACTOS_PERSONALIZE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

#define REACTOS_DWM_COLOR_SCHEME L"ColorScheme"

#define DWM_COLOR_SCHEME_LIGHT 0u
#define DWM_COLOR_SCHEME_DARK  1u

#define DWM_SHELL_MATERIAL_APP(Scheme) \
    ((Scheme) == DWM_COLOR_SCHEME_LIGHT ? L"TaskbarLight" : L"Taskbar")
#define DWM_SHELL_MATERIAL(Scheme) \
    ((Scheme) == DWM_COLOR_SCHEME_LIGHT ? L"TaskbarLight::Liquid" : L"Taskbar::Liquid")

static __inline BOOL
DwmSettingsQueryDword(LPCWSTR KeyName, LPCWSTR ValueName, DWORD *Value)
{
    HKEY Key;
    DWORD Type, Size = sizeof(*Value);
    LONG Error;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, KeyName, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, ValueName, NULL, &Type, (BYTE *)Value, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_DWORD && Size == sizeof(*Value);
}

static __inline DWORD
DwmSettingsReadColorScheme(void)
{
    DWORD Value;

    if (DwmSettingsQueryDword(REACTOS_PERSONALIZE_KEY, L"SystemUsesLightTheme", &Value))
        return Value ? DWM_COLOR_SCHEME_LIGHT : DWM_COLOR_SCHEME_DARK;
    if (DwmSettingsQueryDword(REACTOS_DWM_SETTINGS_KEY, REACTOS_DWM_COLOR_SCHEME, &Value) &&
        Value <= DWM_COLOR_SCHEME_DARK)
        return Value;
    return DWM_COLOR_SCHEME_DARK;
}

static __inline LONG
DwmSettingsSetDword(LPCWSTR KeyName, LPCWSTR ValueName, DWORD Value)
{
    HKEY Key;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, KeyName, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(Key, ValueName, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
    return Error;
}

static __inline LONG
DwmSettingsWriteColorScheme(DWORD Scheme)
{
    DWORD Light = Scheme == DWM_COLOR_SCHEME_LIGHT;
    LONG Error;

    if (Scheme > DWM_COLOR_SCHEME_DARK)
        return ERROR_INVALID_PARAMETER;
    Error = DwmSettingsSetDword(REACTOS_PERSONALIZE_KEY, L"AppsUseLightTheme", Light);
    if (Error == ERROR_SUCCESS)
        Error = DwmSettingsSetDword(REACTOS_PERSONALIZE_KEY, L"SystemUsesLightTheme", Light);
    if (Error == ERROR_SUCCESS)
        Error = DwmSettingsSetDword(REACTOS_DWM_SETTINGS_KEY, REACTOS_DWM_COLOR_SCHEME, Scheme);
    return Error;
}

static __inline void
DwmSettingsBroadcastColorScheme(void)
{
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet",
                        SMTO_NORMAL, 2000, NULL);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)REACTOS_DWM_SETTINGS_KEY,
                        SMTO_NORMAL, 2000, NULL);
}
