/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Gaming, accessibility, privacy and updates pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <mmsystem.h>
#include <setupapi.h>
#include <devguid.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <uxundoc.h>

#define WINDOWS_NT_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define ADVANCED_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define DATA_COLLECTION_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection"

static std::wstring
DriverValue(HKEY key, LPCWSTR name)
{
    WCHAR text[128] = L"";
    DWORD size = sizeof(text);

    if (RegGetValueW(key, NULL, name, RRF_RT_REG_SZ, NULL, text, &size) != ERROR_SUCCESS)
        text[0] = UNICODE_NULL;
    return text;
}

static void
AddDisplayAdapters(PageBuilder &page)
{
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
    SP_DEVINFO_DATA data = { sizeof(data) };

    if (set == INVALID_HANDLE_VALUE)
        return;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); i++, data.cbSize = sizeof(data))
    {
        WCHAR name[256] = L"";
        HKEY key;

        if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_FRIENDLYNAME, NULL, (PBYTE)name, sizeof(name), NULL) &&
            !SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_DEVICEDESC, NULL, (PBYTE)name, sizeof(name), NULL))
            continue;
        page.Info(Str(IDS_DISPLAY_ADAPTER), name, L"desktop_pulse_20_regular");
        key = SetupDiOpenDevRegKey(set, &data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
            continue;
        page.Info(Str(IDS_DRIVER_PROVIDER), DriverValue(key, L"ProviderName"));
        page.Info(Str(IDS_DRIVER_VERSION), DriverValue(key, L"DriverVersion"));
        page.Info(Str(IDS_DRIVER_DATE), DriverValue(key, L"DriverDate"));
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(set);
}

void
BuildGaming(PageBuilder &page)
{
    UINT count = page.Indexing ? 0 : joyGetNumDevs();
    bool found = false;

    page.Group(Str(IDS_CONTROLLERS));
    for (UINT i = 0; i < count; i++)
    {
        JOYCAPSW caps;
        JOYINFOEX info = { sizeof(info), JOY_RETURNALL };
        if (joyGetDevCapsW(i, &caps, sizeof(caps)) != JOYERR_NOERROR || joyGetPosEx(i, &info) != JOYERR_NOERROR)
            continue;
        page.Navigate(caps.szPname, Format(IDS_CONTROLLER_FORMAT, caps.wNumButtons, caps.wNumAxes), L"games_20_regular",
                      PageId::Controller, Printf(L"%u", i)).Keywords = Str(IDS_CONTROLLER_KEYWORDS);
        found = true;
    }
    if (!found && !page.Indexing)
        page.Text(Str(IDS_NO_CONTROLLERS));

    page.Group(Str(IDS_GRAPHICS));
    if (!page.Indexing)
        AddDisplayAdapters(page);
}

static void
AddAxis(PageBuilder &page, UINT title, DWORD value, UINT minimum, UINT maximum)
{
    SettingRow &row = page.Info(Str(title), std::wstring());

    row.Detail = Printf(L"%lu", value);
    row.Bar = true;
    row.Max = (int)max(1u, maximum - minimum);
    row.Position = (int)(value - minimum);
}

void
BuildController(PageBuilder &page)
{
    UINT index = (UINT)_wtoi(page.Param.c_str());
    JOYCAPSW caps;
    JOYINFOEX info = { sizeof(info), JOY_RETURNALL };
    std::wstring pressed;

    if (page.Indexing)
        return;
    if (joyGetDevCapsW(index, &caps, sizeof(caps)) != JOYERR_NOERROR || joyGetPosEx(index, &info) != JOYERR_NOERROR)
    {
        page.Group();
        page.Text(Str(IDS_CONTROLLER_GONE));
        return;
    }
    page.Title = caps.szPname;
    page.Group(Str(IDS_CONTROLLER_TEST));
    page.Text(Str(IDS_CONTROLLER_TEST_DESC));
    AddAxis(page, IDS_AXIS_X, info.dwXpos, caps.wXmin, caps.wXmax);
    AddAxis(page, IDS_AXIS_Y, info.dwYpos, caps.wYmin, caps.wYmax);
    if (caps.wNumAxes > 2)
        AddAxis(page, IDS_AXIS_Z, info.dwZpos, caps.wZmin, caps.wZmax);
    if (caps.wNumAxes > 3)
        AddAxis(page, IDS_AXIS_R, info.dwRpos, caps.wRmin, caps.wRmax);
    if (caps.wNumAxes > 4)
        AddAxis(page, IDS_AXIS_U, info.dwUpos, caps.wUmin, caps.wUmax);
    if (caps.wNumAxes > 5)
        AddAxis(page, IDS_AXIS_V, info.dwVpos, caps.wVmin, caps.wVmax);
    for (UINT i = 0; i < caps.wNumButtons && i < 32; i++)
    {
        if (info.dwButtons & (1u << i))
            pressed += (pressed.empty() ? L"" : L", ") + Printf(L"%u", i + 1);
    }
    page.Info(Str(IDS_BUTTONS_PRESSED), pressed.empty() ? Str(IDS_NONE) : pressed);
    if (caps.wCaps & JOYCAPS_HASPOV)
        page.Info(Str(IDS_POV), info.dwPOV == JOY_POVCENTERED ? Str(IDS_CENTERED) : Printf(L"%lu\x00B0", info.dwPOV / 100));
    page.Host->SetTimerRefresh(100);
}

static void
ApplySpi(UINT action, UINT uiParam, PVOID pvParam)
{
    SystemParametersInfoW(action, uiParam, pvParam, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

static void
AddBoolToggle(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, UINT get, UINT set)
{
    BOOL value = FALSE;
    bool supported = page.Indexing || SystemParametersInfoW(get, 0, &value, 0);

    page.Toggle(Str(title), detail ? Str(detail) : std::wstring(), icon, value != FALSE,
                [set](int on) { ApplySpi(set, 0, (PVOID)(INT_PTR)(on != 0)); }).Enabled = supported;
}

static DWORD
FindProcess(LPCWSTR image)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry = { sizeof(entry) };
    DWORD id = 0;

    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !id; more = Process32NextW(snapshot, &entry))
    {
        if (!lstrcmpiW(entry.szExeFile, image))
            id = entry.th32ProcessID;
    }
    CloseHandle(snapshot);
    return id;
}

static BOOL CALLBACK
CloseProcessWindow(HWND window, LPARAM parameter)
{
    DWORD id = 0;

    GetWindowThreadProcessId(window, &id);
    if (id == (DWORD)parameter && !GetWindow(window, GW_OWNER))
        PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

static void
AddToolToggle(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, LPCWSTR image)
{
    SettingsHost *host = page.Host;
    std::wstring name = image;

    page.Toggle(Str(title), Str(detail), icon, !page.Indexing && FindProcess(image) != 0,
                [host, name](int on)
                {
                    DWORD id = FindProcess(name.c_str());
                    if (on && !id)
                        Launch(name.c_str());
                    else if (!on && id)
                        EnumWindows(CloseProcessWindow, (LPARAM)id);
                    host->SetTimerRefresh(1200);
                });
}

template <typename T>
static T
GetAccess(UINT action)
{
    T value;

    ZeroMemory(&value, sizeof(value));
    value.cbSize = sizeof(value);
    SystemParametersInfoW(action, sizeof(value), &value, 0);
    return value;
}

template <typename T>
static void
SetAccess(UINT action, T value)
{
    value.cbSize = sizeof(value);
    SystemParametersInfoW(action, sizeof(value), &value, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

template <typename T>
static SettingRow &
AddAccessFlag(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, UINT get, UINT set, DWORD flag)
{
    T value = page.Indexing ? T() : GetAccess<T>(get);

    return page.Toggle(Str(title), detail ? Str(detail) : std::wstring(), icon, (value.dwFlags & flag) != 0,
                       [get, set, flag](int on)
                       {
                           T current = GetAccess<T>(get);
                           current.dwFlags = on ? (current.dwFlags | flag) : (current.dwFlags & ~flag);
                           SetAccess<T>(set, current);
                       });
}

static void
AddStickyKeys(PageBuilder &page)
{
    STICKYKEYS sticky = page.Indexing ? STICKYKEYS() : GetAccess<STICKYKEYS>(SPI_GETSTICKYKEYS);

    AddAccessFlag<STICKYKEYS>(page, IDS_STICKY_KEYS, IDS_STICKY_KEYS_DESC, L"keyboard_shift_20_regular",
                              SPI_GETSTICKYKEYS, SPI_SETSTICKYKEYS, SKF_STICKYKEYSON).Keywords = Str(IDS_STICKY_KEYWORDS);
    if (!(sticky.dwFlags & SKF_STICKYKEYSON))
        return;
    AddAccessFlag<STICKYKEYS>(page, IDS_STICKY_LOCK, 0, NULL, SPI_GETSTICKYKEYS, SPI_SETSTICKYKEYS, SKF_TRISTATE);
    AddAccessFlag<STICKYKEYS>(page, IDS_STICKY_TWOKEYS, 0, NULL, SPI_GETSTICKYKEYS, SPI_SETSTICKYKEYS, SKF_TWOKEYSOFF);
    AddAccessFlag<STICKYKEYS>(page, IDS_STICKY_SOUND, 0, NULL, SPI_GETSTICKYKEYS, SPI_SETSTICKYKEYS,
                              SKF_AUDIBLEFEEDBACK);
}

static const DWORD FilterTimes[] = { 300, 500, 1000, 1500, 2000 };

static void
AddFilterKeys(PageBuilder &page)
{
    FILTERKEYS filter = page.Indexing ? FILTERKEYS() : GetAccess<FILTERKEYS>(SPI_GETFILTERKEYS);
    std::vector<std::wstring> modes, times;
    DWORD current;
    int time = 0;

    AddAccessFlag<FILTERKEYS>(page, IDS_FILTER_KEYS, IDS_FILTER_KEYS_DESC, L"filter_20_regular", SPI_GETFILTERKEYS,
                              SPI_SETFILTERKEYS, FKF_FILTERKEYSON).Keywords = Str(IDS_FILTER_KEYWORDS);
    if (!(filter.dwFlags & FKF_FILTERKEYSON))
        return;
    modes.push_back(Str(IDS_FILTER_SLOW));
    modes.push_back(Str(IDS_FILTER_BOUNCE));
    current = filter.iBounceMSec ? filter.iBounceMSec : filter.iWaitMSec;
    for (UINT i = 0; i < _countof(FilterTimes); i++)
    {
        times.push_back(Printf(L"%u.%u s", FilterTimes[i] / 1000, (FilterTimes[i] % 1000) / 100));
        if (FilterTimes[i] <= current)
            time = (int)i;
    }
    page.Choice(Str(IDS_FILTER_MODE), std::wstring(), NULL, modes, filter.iBounceMSec ? 1 : 0,
                [](int index)
                {
                    FILTERKEYS value = GetAccess<FILTERKEYS>(SPI_GETFILTERKEYS);
                    DWORD time = value.iBounceMSec ? value.iBounceMSec : value.iWaitMSec;

                    if (!time)
                        time = 1000;
                    value.iBounceMSec = index == 1 ? time : 0;
                    value.iWaitMSec = index == 1 ? 0 : time;
                    value.iDelayMSec = index == 1 ? 0 : 1000;
                    value.iRepeatMSec = index == 1 ? 0 : 500;
                    SetAccess(SPI_SETFILTERKEYS, value);
                });
    page.Choice(Str(IDS_FILTER_TIME), Str(filter.iBounceMSec ? IDS_FILTER_TIME_BOUNCE : IDS_FILTER_TIME_SLOW), NULL,
                times, time,
                [](int index)
                {
                    FILTERKEYS value = GetAccess<FILTERKEYS>(SPI_GETFILTERKEYS);

                    if (value.iBounceMSec)
                        value.iBounceMSec = FilterTimes[index];
                    else
                        value.iWaitMSec = FilterTimes[index];
                    SetAccess(SPI_SETFILTERKEYS, value);
                });
}

static void
AddMouseKeys(PageBuilder &page)
{
    MOUSEKEYS mouse = page.Indexing ? MOUSEKEYS() : GetAccess<MOUSEKEYS>(SPI_GETMOUSEKEYS);

    AddAccessFlag<MOUSEKEYS>(page, IDS_MOUSE_KEYS, IDS_MOUSE_KEYS_DESC, L"cursor_20_regular", SPI_GETMOUSEKEYS,
                             SPI_SETMOUSEKEYS, MKF_MOUSEKEYSON).Keywords = Str(IDS_MOUSE_KEYWORDS);
    if (!(mouse.dwFlags & MKF_MOUSEKEYSON))
        return;
    AddAccessFlag<MOUSEKEYS>(page, IDS_MOUSE_KEYS_NUMLOCK, 0, NULL, SPI_GETMOUSEKEYS, SPI_SETMOUSEKEYS,
                             MKF_REPLACENUMBERS);
    AddAccessFlag<MOUSEKEYS>(page, IDS_MOUSE_KEYS_MODIFIERS, 0, NULL, SPI_GETMOUSEKEYS, SPI_SETMOUSEKEYS,
                             MKF_MODIFIERS);
    {
        SettingRow &row = page.Slider(Str(IDS_MOUSE_KEYS_SPEED), std::wstring(), NULL, 10, 360,
                                      (int)max(10, min(360, (int)mouse.iMaxSpeed)),
                                      [](int value)
                                      {
                                          MOUSEKEYS current = GetAccess<MOUSEKEYS>(SPI_GETMOUSEKEYS);
                                          current.iMaxSpeed = (DWORD)value;
                                          SetAccess(SPI_SETMOUSEKEYS, current);
                                      });
        row.Label = [](int value) { return Printf(L"%d", value); };
    }
}

#define APPEARANCE_SCHEMES_KEY L"Control Panel\\Appearance\\New Schemes"
#define LIBERNT_SETTINGS_KEY L"Software\\LiberNT\\Settings"
#define THEME_MANAGER_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager"

static const UINT ContrastSchemes[] = { 4, 5, 6, 7 };

static const LPCWSTR SystemColorNames[] =
{
    L"Scrollbar", L"Background", L"ActiveTitle", L"InactiveTitle", L"Menu", L"Window", L"WindowFrame",
    L"MenuText", L"WindowText", L"TitleText", L"ActiveBorder", L"InactiveBorder", L"AppWorkSpace", L"Hilight",
    L"HilightText", L"ButtonFace", L"ButtonShadow", L"GrayText", L"ButtonText", L"InactiveTitleText",
    L"ButtonHilight", L"ButtonDkShadow", L"ButtonLight", L"InfoText", L"InfoWindow", L"ButtonAlternateFace",
    L"HotTrackingColor", L"GradientActiveTitle", L"GradientInactiveTitle", L"MenuHilight", L"MenuBar",
};

static std::wstring
ContrastSchemeName(UINT scheme)
{
    return ReadString(HKEY_CURRENT_USER, Printf(APPEARANCE_SCHEMES_KEY L"\\%u", scheme).c_str(), L"LegacyName");
}

static void
SetHighContrast(DWORD flags, LPCWSTR scheme)
{
    HIGHCONTRASTW contrast = { sizeof(contrast) };

    contrast.dwFlags = flags;
    contrast.lpszDefaultScheme = (LPWSTR)scheme;
    SystemParametersInfoW(SPI_SETHIGHCONTRAST, sizeof(contrast), &contrast, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

static void
ApplyContrastScheme(int index)
{
    HIGHCONTRASTW contrast = { sizeof(contrast) };
    INT elements[_countof(SystemColorNames)];
    COLORREF colors[_countof(SystemColorNames)];
    std::wstring key, name;
    BOOL on;

    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
        return;
    if (contrast.lpszDefaultScheme)
        LocalFree(contrast.lpszDefaultScheme);
    on = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;

    if (index < 0)
    {
        std::wstring style, color, size;
        WCHAR windows[MAX_PATH];

        if (!on)
            return;
        SetHighContrast(contrast.dwFlags & ~HCF_HIGHCONTRASTON, NULL);
        style = ReadString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyle");
        color = ReadString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyleColor");
        size = ReadString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyleSize");
        if (style.empty() && GetWindowsDirectoryW(windows, _countof(windows)))
            style = std::wstring(windows) + L"\\Resources\\Themes\\Aero\\aero.msstyles";
        SetSystemVisualStyle(style.c_str(), color.c_str(), size.c_str(), 0);
        return;
    }

    if (!on)
    {
        if (ReadString(HKEY_CURRENT_USER, THEME_MANAGER_KEY, L"ThemeActive") == L"1")
        {
            WriteString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyle",
                        ReadString(HKEY_CURRENT_USER, THEME_MANAGER_KEY, L"DllName"));
            WriteString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyleColor",
                        ReadString(HKEY_CURRENT_USER, THEME_MANAGER_KEY, L"ColorName"));
            WriteString(HKEY_CURRENT_USER, LIBERNT_SETTINGS_KEY, L"VisualStyleSize",
                        ReadString(HKEY_CURRENT_USER, THEME_MANAGER_KEY, L"SizeName"));
        }
        SetSystemVisualStyle(NULL, NULL, NULL, 0);
    }

    key = Printf(APPEARANCE_SCHEMES_KEY L"\\%u\\Sizes\\0", ContrastSchemes[index]);
    for (UINT i = 0; i < _countof(SystemColorNames); i++)
    {
        DWORD value = ReadDword(HKEY_CURRENT_USER, key.c_str(), Printf(L"Color #%u", i).c_str(), GetSysColor(i));

        elements[i] = (INT)i;
        colors[i] = value & 0x00FFFFFF;
        WriteString(HKEY_CURRENT_USER, L"Control Panel\\Colors", SystemColorNames[i],
                    Printf(L"%u %u %u", GetRValue(colors[i]), GetGValue(colors[i]), GetBValue(colors[i])));
    }
    SetSysColors(_countof(SystemColorNames), elements, colors);
    name = ContrastSchemeName(ContrastSchemes[index]);
    SetHighContrast(contrast.dwFlags | HCF_HIGHCONTRASTON, name.c_str());
}

static void
AddContrastThemes(PageBuilder &page)
{
    HIGHCONTRASTW contrast = { sizeof(contrast) };
    std::vector<std::wstring> names;
    std::wstring current;
    int selected = 0;

    if (!page.Indexing && SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
    {
        if ((contrast.dwFlags & HCF_HIGHCONTRASTON) && contrast.lpszDefaultScheme)
            current = contrast.lpszDefaultScheme;
        if (contrast.lpszDefaultScheme)
            LocalFree(contrast.lpszDefaultScheme);
    }
    names.push_back(Str(IDS_NONE));
    for (UINT i = 0; i < _countof(ContrastSchemes); i++)
    {
        std::wstring name = page.Indexing ? std::wstring() : ContrastSchemeName(ContrastSchemes[i]);

        names.push_back(name);
        if (!current.empty() && !lstrcmpiW(name.c_str(), current.c_str()))
            selected = (int)i + 1;
    }
    page.Choice(Str(IDS_CONTRAST_THEMES), Str(IDS_CONTRAST_THEMES_DESC), L"dark_theme_20_regular", names, selected,
                [](int index) { ApplyContrastScheme(index - 1); }).Keywords = Str(IDS_CONTRAST_KEYWORDS);
}

void
BuildAccessibility(PageBuilder &page)
{
    DWORD caret = 1;
    bool caretSupported;

    caretSupported = page.Indexing || SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &caret, 0);

    page.Group(Str(IDS_VISION));
    {
        SettingRow &row = page.Slider(Str(IDS_CARET_WIDTH), Str(IDS_CARET_WIDTH_DESC), L"keyboard_20_regular", 1, 20,
                                      (int)caret, [](int value) { ApplySpi(SPI_SETCARETWIDTH, 0, (PVOID)(INT_PTR)value); });
        row.Enabled = caretSupported;
        row.Label = [](int value) { return Printf(L"%d", value); };
    }
    AddToolToggle(page, IDS_MAGNIFIER, IDS_MAGNIFIER_DESC, L"zoom_in_20_regular", L"magnify.exe");
    AddContrastThemes(page);

    page.Group(Str(IDS_VISUAL_EFFECTS));
    AddBoolToggle(page, IDS_CLIENT_ANIMATIONS, IDS_CLIENT_ANIMATIONS_DESC, L"slide_text_20_regular",
                  SPI_GETCLIENTAREAANIMATION, SPI_SETCLIENTAREAANIMATION);

    page.Group(Str(IDS_INTERACTION_KEYBOARD));
    AddBoolToggle(page, IDS_KEYBOARD_CUES, IDS_KEYBOARD_CUES_DESC, L"keyboard_20_regular", SPI_GETKEYBOARDCUES,
                  SPI_SETKEYBOARDCUES);
    AddToolToggle(page, IDS_OSK, IDS_OSK_DESC, L"keyboard_20_regular", L"osk.exe");
    AddStickyKeys(page);
    AddFilterKeys(page);
    AddAccessFlag<TOGGLEKEYS>(page, IDS_TOGGLE_KEYS, IDS_TOGGLE_KEYS_DESC, L"speaker_2_20_regular", SPI_GETTOGGLEKEYS,
                              SPI_SETTOGGLEKEYS, TKF_TOGGLEKEYSON);

    page.Group(Str(IDS_INTERACTION_MOUSE));
    AddMouseKeys(page);
}

void
BuildPrivacy(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    DWORD level = ReadDword(HKEY_LOCAL_MACHINE, DATA_COLLECTION_KEY, L"AllowTelemetry", 1);
    bool admin = IsAdmin();

    page.Group(Str(IDS_ACTIVITY));
    page.Toggle(Str(IDS_TRACK_DOCS), Str(IDS_TRACK_DOCS_DESC), L"history_20_regular",
                ReadDword(HKEY_CURRENT_USER, ADVANCED_KEY, L"Start_TrackDocs", 1) != 0,
                [](int on)
                {
                    WriteDword(HKEY_CURRENT_USER, ADVANCED_KEY, L"Start_TrackDocs", on != 0);
                    if (!on)
                        SHAddToRecentDocs(SHARD_PIDL, NULL);
                    BroadcastSetting(L"TraySettings");
                });
    page.Button(Str(IDS_CLEAR_RECENT), Str(IDS_CLEAR_RECENT_DESC), L"delete_20_regular", Str(IDS_CLEAR),
                [](int) { SHAddToRecentDocs(SHARD_PIDL, NULL); });

    page.Group(Str(IDS_DIAGNOSTICS));
    page.Toggle(Str(IDS_OPTIONAL_DIAGNOSTICS), Str(IDS_OPTIONAL_DIAGNOSTICS_DESC), L"shield_checkmark_20_regular",
                level >= 3,
                [host](int on)
                {
                    if (!WriteDword(HKEY_LOCAL_MACHINE, DATA_COLLECTION_KEY, L"AllowTelemetry", on ? 3 : 1))
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                }).Enabled = admin && level != 0;
    page.Text(Str(IDS_DIAGNOSTICS_NOTE));
}

static std::wstring
FileVersion(LPCWSTR path)
{
    DWORD handle = 0, size = GetFileVersionInfoSizeW(path, &handle);
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO *info = NULL;
    UINT length = 0;

    if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", (void **)&info, &length) || !info)
        return std::wstring();
    return Printf(L"%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                  HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

void
BuildUpdates(PageBuilder &page)
{
    std::wstring product = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"ProductName");
    std::wstring version = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"DisplayVersion");
    std::wstring build = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentBuild");
    DWORD major = ReadDword(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentMajorVersionNumber", 0);
    DWORD minor = ReadDword(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentMinorVersionNumber", 0);
    DWORD ubr = ReadDword(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"UBR", (DWORD)-1);
    std::wstring detail;
    std::wstring lab = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"BuildLabEx");
    WCHAR system[MAX_PATH];
    std::wstring kernel;

    if (lab.empty())
        lab = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"BuildLab");
    if (build.empty())
        build = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentBuildNumber");
    if (!build.empty() && ubr != (DWORD)-1)
        build += Printf(L".%lu", ubr);
    if (version.empty())
        version = major ? Printf(L"%lu.%lu", major, minor) : ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentVersion");
    if (!version.empty() && !build.empty())
        detail = Format(IDS_RUNNING_VERSION, version.c_str(), build.c_str());
    else
        detail = version.empty() ? build : version;
    if (!page.Indexing && GetSystemDirectoryW(system, _countof(system)))
    {
        kernel = FileVersion((std::wstring(system) + L"\\ntoskrnl.exe").c_str());
        if (kernel.empty())
            kernel = FileVersion((std::wstring(system) + L"\\ntkrnlmp.exe").c_str());
    }
    page.Hero(product.empty() ? Str(IDS_LIBERNT) : product, detail, L"arrow_sync_circle_20_regular");
    page.Group(Str(IDS_VERSION_INFO));
    if (!version.empty())
        page.Info(Str(IDS_VERSION), version);
    if (!build.empty())
        page.Info(Str(IDS_OS_BUILD), build);
    if (!lab.empty())
        page.Info(Str(IDS_BUILD_LAB), lab);
    if (!kernel.empty())
        page.Info(Str(IDS_KERNEL_VERSION), kernel);
    page.Group(Str(IDS_RELATED));
    page.Link(Str(IDS_LIBERNT_WEBSITE), Str(IDS_LIBERNT_WEBSITE_DESC), L"globe_20_regular",
              [](int) { Launch(L"https://libernt.com/"); });
    page.Navigate(Str(IDS_PAGE_ABOUT), Str(IDS_ABOUT_DESC), L"info_20_regular", PageId::About);
}
