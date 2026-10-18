/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System pages: display, sound, power, storage, multitasking and about
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <powrprof.h>
#include <algorithm>

#define WINDOWS_NT_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

extern "C" BOOL WINAPI GetPhysicallyInstalledSystemMemory(PULONGLONG TotalMemoryInKilobytes);

static const GUID VideoSubgroupGuid =
    { 0x7516b95f, 0xf776, 0x4464, { 0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99 } };
static const GUID VideoPowerdownTimeoutGuid =
    { 0x3c0bc021, 0xc8a8, 0x4e07, { 0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e } };
static const GUID SleepSubgroupGuid =
    { 0x238c9fa8, 0x0aad, 0x41ed, { 0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20 } };
static const GUID StandbyTimeoutGuid =
    { 0x29f6c1db, 0x86da, 0x48c5, { 0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda } };
static const GUID SystemButtonSubgroupGuid =
    { 0x4f971e89, 0xeebd, 0x4455, { 0xa8, 0xde, 0x9e, 0x59, 0x04, 0x0e, 0x73, 0x47 } };
static const GUID PowerButtonActionGuid =
    { 0x7648efa3, 0xdd9c, 0x4e3e, { 0xb5, 0x66, 0x50, 0xf9, 0x29, 0x38, 0x62, 0x80 } };
static const GUID LidCloseActionGuid =
    { 0x5ca83367, 0x6e45, 0x459f, { 0xa2, 0x7b, 0x47, 0x6b, 0x1d, 0x01, 0xc9, 0x36 } };

static std::wstring
ComputerName()
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 64];
    DWORD size = _countof(name);

    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, name, &size))
    {
        size = _countof(name);
        if (!GetComputerNameW(name, &size))
            name[0] = UNICODE_NULL;
    }
    return name;
}

static std::wstring
ProductName()
{
    std::wstring name = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"ProductName");
    return name.empty() ? Str(IDS_LIBERNT) : name;
}

static void
RenameComputer(HWND owner)
{
    std::wstring name = ComputerName();

    if (!InputBox(owner, Str(IDS_RENAME_PC), Str(IDS_RENAME_PC_PROMPT), name) || name.empty())
        return;
    if (!SetComputerNameExW(ComputerNamePhysicalDnsHostname, name.c_str()))
    {
        ShowError(owner, GetLastError());
        return;
    }
    PromptRestart(owner, Str(IDS_RESTART_RENAME));
}

void
BuildSystem(PageBuilder &page)
{
    SettingsHost *host = page.Host;

    page.Hero(ComputerName(), ProductName(), L"desktop_20_regular").Action = Str(IDS_RENAME);
    page.Groups.back().Rows.back().Invoke = [host](int) { RenameComputer(host->Window()); };
    page.Group();
    page.Navigate(Str(IDS_PAGE_DISPLAY), Str(IDS_DISPLAY_DESC), L"desktop_20_regular", PageId::Display).Keywords =
        Str(IDS_DISPLAY_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_SOUND), Str(IDS_SOUND_DESC), L"speaker_2_20_regular", PageId::Sound).Keywords =
        Str(IDS_SOUND_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_POWER), Str(IDS_POWER_DESC), L"power_20_regular", PageId::Power);
    page.Navigate(Str(IDS_PAGE_STORAGE), Str(IDS_STORAGE_DESC), L"storage_20_regular", PageId::Storage);
    page.Navigate(Str(IDS_PAGE_MULTITASKING), Str(IDS_MULTITASKING_DESC), L"window_multiple_20_regular",
                  PageId::Multitasking);
    page.Navigate(Str(IDS_PAGE_BOOTLOADER), Str(IDS_BOOTLOADER_DESC), L"power_20_regular", PageId::BootLoader).Keywords =
        Str(IDS_BOOTLOADER_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_ABOUT), Str(IDS_ABOUT_DESC), L"info_20_regular", PageId::About);
}

struct DisplayMode
{
    DWORD Width;
    DWORD Height;
    DWORD Bits;
    DWORD Frequency;
};

static std::vector<std::wstring>
DisplayDevices(std::vector<std::wstring> &names)
{
    std::vector<std::wstring> labels;
    DISPLAY_DEVICEW device = { sizeof(device) };

    for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &device, 0); i++, device.cb = sizeof(device))
    {
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        names.push_back(device.DeviceName);
        labels.push_back(Printf(L"%u. %s", (UINT)labels.size() + 1, device.DeviceString));
    }
    return labels;
}

struct KeepState
{
    int Remaining;
    std::wstring Text;
};

static HRESULT CALLBACK
KeepDisplayCallback(HWND dialog, UINT notification, WPARAM wParam, LPARAM lParam, LONG_PTR data)
{
    KeepState *state = (KeepState *)data;

    UNREFERENCED_PARAMETER(lParam);
    if (notification == TDN_TIMER)
    {
        int remaining = 15 - (int)(wParam / 1000);
        if (remaining != state->Remaining)
        {
            state->Remaining = remaining;
            if (remaining <= 0)
            {
                SendMessageW(dialog, TDM_CLICK_BUTTON, IDNO, 0);
                return S_OK;
            }
            state->Text = Format(IDS_KEEP_DISPLAY_TEXT, remaining);
            SendMessageW(dialog, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)state->Text.c_str());
        }
    }
    return S_OK;
}

static bool
KeepDisplayChanges(HWND owner)
{
    KeepState state = { 15, std::wstring() };
    TASKDIALOGCONFIG config = { sizeof(config) };
    TASKDIALOG_BUTTON buttons[] = { { IDYES, NULL }, { IDNO, NULL } };
    std::wstring keep = Str(IDS_KEEP_CHANGES), revert = Str(IDS_REVERT), title = Str(IDS_APP_TITLE);
    std::wstring heading = Str(IDS_KEEP_DISPLAY);
    int pressed = IDNO;

    buttons[0].pszButtonText = keep.c_str();
    buttons[1].pszButtonText = revert.c_str();
    state.Text = Format(IDS_KEEP_DISPLAY_TEXT, state.Remaining);
    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_CALLBACK_TIMER;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = heading.c_str();
    config.pszContent = state.Text.c_str();
    config.cButtons = _countof(buttons);
    config.pButtons = buttons;
    config.nDefaultButton = IDNO;
    config.lpCallbackData = (LONG_PTR)&state;
    config.pfCallback = KeepDisplayCallback;
    return SUCCEEDED(TaskDialogIndirect(&config, &pressed, NULL, NULL)) && pressed == IDYES;
}

static void
ApplyDisplayMode(HWND owner, const std::wstring &device, DEVMODEW mode)
{
    DEVMODEW previous = { };
    LONG result;

    previous.dmSize = sizeof(previous);
    if (!EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &previous, 0))
        return;
    result = ChangeDisplaySettingsExW(device.c_str(), &mode, NULL, CDS_UPDATEREGISTRY, NULL);
    if (result != DISP_CHANGE_SUCCESSFUL)
    {
        ShowError(owner, result == DISP_CHANGE_RESTART ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_INVALID_PARAMETER);
        return;
    }
    if (!KeepDisplayChanges(owner))
    {
        previous.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY |
                            DM_DISPLAYORIENTATION;
        ChangeDisplaySettingsExW(device.c_str(), &previous, NULL, CDS_UPDATEREGISTRY, NULL);
    }
}

static const UINT ScaleSteps[] = { 100, 125, 150, 175, 200, 225, 250 };

void
BuildDisplay(PageBuilder &page)
{
    std::vector<std::wstring> names, labels, options;
    std::vector<DisplayMode> modes, sizes;
    std::wstring device;
    DEVMODEW current = { };
    DISPLAY_DEVICEW adapter = { sizeof(adapter) };
    SettingsHost *host = page.Host;
    int selected = 0, sizeIndex = -1, frequencyIndex = -1;
    DWORD logPixels;

    if (page.Indexing)
    {
        page.Group(Str(IDS_SCALE_LAYOUT));
        page.Choice(Str(IDS_SCALE), Str(IDS_SCALE_DESC), L"zoom_in_20_regular", options, -1, nullptr);
        page.Choice(Str(IDS_RESOLUTION), std::wstring(), L"desktop_20_regular", options, -1, nullptr);
        page.Choice(Str(IDS_ORIENTATION), std::wstring(), L"desktop_20_regular", options, -1, nullptr);
        page.Choice(Str(IDS_REFRESH_RATE), std::wstring(), L"desktop_pulse_20_regular", options, -1, nullptr);
        return;
    }
    labels = DisplayDevices(names);
    for (size_t i = 0; i < names.size(); i++)
    {
        if (names[i] == page.Param)
            selected = (int)i;
    }
    if (names.empty())
    {
        page.Group();
        page.Text(Str(IDS_NO_DISPLAY));
        return;
    }
    device = names[selected];
    current.dmSize = sizeof(current);
    EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &current, 0);
    for (DWORD i = 0; ; i++)
    {
        DEVMODEW mode = { };
        DisplayMode entry;
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(device.c_str(), i, &mode, 0))
            break;
        entry.Width = mode.dmPelsWidth;
        entry.Height = mode.dmPelsHeight;
        entry.Bits = mode.dmBitsPerPel;
        entry.Frequency = mode.dmDisplayFrequency;
        modes.push_back(entry);
    }
    for (size_t i = 0; i < modes.size(); i++)
    {
        bool known = false;
        if (modes[i].Bits != current.dmBitsPerPel)
            continue;
        for (size_t j = 0; j < sizes.size() && !known; j++)
            known = sizes[j].Width == modes[i].Width && sizes[j].Height == modes[i].Height;
        if (!known)
            sizes.push_back(modes[i]);
    }
    std::sort(sizes.begin(), sizes.end(), [](const DisplayMode &a, const DisplayMode &b)
              { return a.Width * a.Height > b.Width * b.Height; });

    if (labels.size() > 1)
    {
        page.Group();
        page.Choice(Str(IDS_SELECT_DISPLAY), Str(IDS_SELECT_DISPLAY_DESC), L"desktop_20_regular", labels, selected,
                    [host, names](int index) { host->Open(PageId::Display, names[index]); });
    }

    page.Group(Str(IDS_SCALE_LAYOUT));
    logPixels = GetDpiForSystem();
    options.clear();
    for (UINT i = 0; i < _countof(ScaleSteps); i++)
    {
        options.push_back(Printf(L"%u%%", ScaleSteps[i]));
        if (MulDiv(96, ScaleSteps[i], 100) == (int)logPixels)
            sizeIndex = (int)i;
    }
    page.Choice(Str(IDS_SCALE), Str(IDS_SCALE_DESC), L"zoom_in_20_regular", options, sizeIndex,
                [host](int index)
                {
                    DWORD dpi = (DWORD)MulDiv(96, ScaleSteps[index], 100);
                    if (!WriteDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDPI",
                                    L"LogPixels", dpi) ||
                        !WriteDword(HKEY_LOCAL_MACHINE,
                                    L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts",
                                    L"LogPixels", dpi))
                    {
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                        return;
                    }
                    WriteDword(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", L"AppliedDPI", dpi);
                    PromptRestart(host->Window(), Str(IDS_RESTART_SCALE));
                }).Enabled = IsAdmin();

    options.clear();
    sizeIndex = -1;
    for (size_t i = 0; i < sizes.size(); i++)
    {
        options.push_back(Printf(L"%lu \x00D7 %lu", sizes[i].Width, sizes[i].Height));
        if (sizes[i].Width == current.dmPelsWidth && sizes[i].Height == current.dmPelsHeight)
            sizeIndex = (int)i;
    }
    page.Choice(Str(IDS_RESOLUTION), Str(IDS_RESOLUTION_DESC), L"desktop_20_regular", options, sizeIndex,
                [host, device, sizes, current](int index)
                {
                    DEVMODEW mode = current;
                    mode.dmPelsWidth = sizes[index].Width;
                    mode.dmPelsHeight = sizes[index].Height;
                    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
                    ApplyDisplayMode(host->Window(), device, mode);
                });

    options.clear();
    options.push_back(Str(IDS_LANDSCAPE));
    options.push_back(Str(IDS_PORTRAIT));
    options.push_back(Str(IDS_LANDSCAPE_FLIPPED));
    options.push_back(Str(IDS_PORTRAIT_FLIPPED));
    page.Choice(Str(IDS_ORIENTATION), std::wstring(), L"desktop_20_regular", options,
                (current.dmFields & DM_DISPLAYORIENTATION) ? (int)current.dmDisplayOrientation : 0,
                [host, device, current](int index)
                {
                    DEVMODEW mode = current;
                    BOOL swap = ((int)current.dmDisplayOrientation % 2) != (index % 2);
                    mode.dmDisplayOrientation = (DWORD)index;
                    if (swap)
                    {
                        mode.dmPelsWidth = current.dmPelsHeight;
                        mode.dmPelsHeight = current.dmPelsWidth;
                    }
                    mode.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;
                    ApplyDisplayMode(host->Window(), device, mode);
                });

    page.Group(Str(IDS_ADVANCED_DISPLAY));
    {
        std::vector<DWORD> frequencies;
        options.clear();
        for (size_t i = 0; i < modes.size(); i++)
        {
            if (modes[i].Width != current.dmPelsWidth || modes[i].Height != current.dmPelsHeight ||
                modes[i].Bits != current.dmBitsPerPel ||
                std::find(frequencies.begin(), frequencies.end(), modes[i].Frequency) != frequencies.end())
                continue;
            frequencies.push_back(modes[i].Frequency);
        }
        std::sort(frequencies.begin(), frequencies.end());
        for (size_t i = 0; i < frequencies.size(); i++)
        {
            options.push_back(frequencies[i] <= 1 ? Str(IDS_HARDWARE_DEFAULT) : Printf(L"%lu Hz", frequencies[i]));
            if (frequencies[i] == current.dmDisplayFrequency)
                frequencyIndex = (int)i;
        }
        page.Choice(Str(IDS_REFRESH_RATE), Str(IDS_REFRESH_RATE_DESC), L"desktop_pulse_20_regular", options,
                    frequencyIndex,
                    [host, device, current, frequencies](int index)
                    {
                        DEVMODEW mode = current;
                        mode.dmDisplayFrequency = frequencies[index];
                        mode.dmFields = DM_DISPLAYFREQUENCY;
                        ApplyDisplayMode(host->Window(), device, mode);
                    }).Enabled = options.size() > 1;
    }
    for (DWORD i = 0; ; i++)
    {
        DISPLAY_DEVICEW candidate = { sizeof(candidate) };
        if (!EnumDisplayDevicesW(NULL, i, &candidate, 0))
            break;
        if (device == candidate.DeviceName)
        {
            adapter = candidate;
            break;
        }
    }
    page.Info(Str(IDS_DISPLAY_ADAPTER), adapter.DeviceString, L"desktop_pulse_20_regular");
    page.Info(Str(IDS_DESKTOP_MODE),
              Printf(L"%lu \x00D7 %lu, %lu Hz", current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency));
    page.Info(Str(IDS_COLOR_DEPTH), Format(IDS_BITS_FORMAT, current.dmBitsPerPel));
}

static const DWORD PowerTimes[] = { 60, 120, 180, 300, 600, 900, 1200, 1500, 1800, 2700, 3600, 7200, 10800,
                                    14400, 18000, 0 };

static std::wstring
PowerTimeText(DWORD seconds)
{
    if (!seconds)
        return Str(IDS_NEVER);
    if (seconds < 3600)
        return Format(seconds == 60 ? IDS_MINUTE : IDS_MINUTES, seconds / 60);
    return Format(seconds == 3600 ? IDS_HOUR : IDS_HOURS, seconds / 3600);
}

static void
AddPowerTime(PageBuilder &page, const GUID &scheme, const GUID &subgroup, const GUID &setting, bool ac, UINT title,
             LPCWSTR icon)
{
    std::vector<std::wstring> options;
    DWORD value = 0;
    int selected = -1;
    GUID schemeCopy = scheme, subgroupCopy = subgroup, settingCopy = setting;

    if (!page.Indexing)
    {
        if (ac)
            PowerReadACValueIndex(NULL, &scheme, &subgroup, &setting, &value);
        else
            PowerReadDCValueIndex(NULL, &scheme, &subgroup, &setting, &value);
    }
    for (UINT i = 0; i < _countof(PowerTimes); i++)
    {
        options.push_back(PowerTimeText(PowerTimes[i]));
        if (PowerTimes[i] == value)
            selected = (int)i;
    }
    if (selected < 0 && !page.Indexing)
    {
        options.push_back(PowerTimeText(value));
        selected = (int)options.size() - 1;
    }
    page.Choice(Str(title), std::wstring(), icon, options, selected,
                [schemeCopy, subgroupCopy, settingCopy, ac](int index)
                {
                    if (index >= (int)_countof(PowerTimes))
                        return;
                    if (ac)
                        PowerWriteACValueIndex(NULL, &schemeCopy, &subgroupCopy, &settingCopy, PowerTimes[index]);
                    else
                        PowerWriteDCValueIndex(NULL, &schemeCopy, &subgroupCopy, &settingCopy, PowerTimes[index]);
                    PowerSetActiveScheme(NULL, &schemeCopy);
                });
}

void
BuildPower(PageBuilder &page)
{
    SYSTEM_POWER_STATUS status = { };
    GUID *active = NULL, scheme = { };
    std::vector<std::wstring> names, actions;
    std::vector<GUID> schemes;
    bool battery;
    int selected = -1;
    DWORD action = 0;

    battery = !page.Indexing && GetSystemPowerStatus(&status) && status.BatteryFlag != 128 &&
              status.BatteryFlag != 255;
    if (battery)
    {
        std::wstring detail = Str(status.ACLineStatus == 1 ? IDS_PLUGGED_IN : IDS_ON_BATTERY);
        page.Hero(Format(IDS_BATTERY_PERCENT, status.BatteryLifePercent), detail, L"battery_saver_20_regular");
    }
    if (!page.Indexing && PowerGetActiveScheme(NULL, &active) == ERROR_SUCCESS && active)
    {
        scheme = *active;
        LocalFree(active);
    }
    if (!page.Indexing)
    {
        for (ULONG i = 0; ; i++)
        {
            GUID guid;
            DWORD size = sizeof(guid);
            WCHAR name[256];
            DWORD nameSize = sizeof(name);

            if (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, i, (UCHAR *)&guid, &size) != ERROR_SUCCESS)
                break;
            if (PowerReadFriendlyName(NULL, &guid, NULL, NULL, (UCHAR *)name, &nameSize) != ERROR_SUCCESS)
                StringCchPrintfW(name, _countof(name), L"%lu", i + 1);
            if (IsEqualGUID(guid, scheme))
                selected = (int)schemes.size();
            schemes.push_back(guid);
            names.push_back(name);
        }
    }
    page.Group(Str(IDS_POWER));
    page.Choice(Str(IDS_POWER_MODE), Str(IDS_POWER_MODE_DESC), L"battery_saver_20_regular", names, selected,
                [schemes](int index) { PowerSetActiveScheme(NULL, &schemes[index]); }).Enabled = !schemes.empty();

    page.Group(Str(IDS_SCREEN_SLEEP));
    AddPowerTime(page, scheme, VideoSubgroupGuid, VideoPowerdownTimeoutGuid, true, IDS_SCREEN_OFF_AC,
                 L"desktop_20_regular");
    AddPowerTime(page, scheme, SleepSubgroupGuid, StandbyTimeoutGuid, true, IDS_SLEEP_AC,
                 L"weather_moon_20_regular");
    if (battery)
    {
        AddPowerTime(page, scheme, VideoSubgroupGuid, VideoPowerdownTimeoutGuid, false, IDS_SCREEN_OFF_DC,
                     L"desktop_20_regular");
        AddPowerTime(page, scheme, SleepSubgroupGuid, StandbyTimeoutGuid, false, IDS_SLEEP_DC,
                     L"weather_moon_20_regular");
    }

    page.Group(Str(IDS_POWER_BUTTONS));
    actions.push_back(Str(IDS_DO_NOTHING));
    actions.push_back(Str(IDS_SLEEP));
    actions.push_back(Str(IDS_HIBERNATE));
    actions.push_back(Str(IDS_SHUT_DOWN));
    if (!page.Indexing)
        PowerReadACValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &PowerButtonActionGuid, &action);
    page.Choice(Str(IDS_POWER_BUTTON_ACTION), std::wstring(), L"power_20_regular", actions,
                action < actions.size() ? (int)action : -1,
                [scheme](int index)
                {
                    PowerWriteACValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &PowerButtonActionGuid,
                                           (DWORD)index);
                    PowerWriteDCValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &PowerButtonActionGuid,
                                           (DWORD)index);
                    PowerSetActiveScheme(NULL, &scheme);
                });
    if (battery)
    {
        action = 0;
        PowerReadACValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &LidCloseActionGuid, &action);
        page.Choice(Str(IDS_LID_ACTION), std::wstring(), L"laptop_20_regular", actions,
                    action < actions.size() ? (int)action : -1,
                    [scheme](int index)
                    {
                        PowerWriteACValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &LidCloseActionGuid,
                                               (DWORD)index);
                        PowerWriteDCValueIndex(NULL, &scheme, &SystemButtonSubgroupGuid, &LidCloseActionGuid,
                                               (DWORD)index);
                        PowerSetActiveScheme(NULL, &scheme);
                    });
    }
}

static ULONGLONG TempBytes;
static bool TempMeasured;
static bool TempBusy;

static std::vector<std::wstring>
TempFolders()
{
    std::vector<std::wstring> folders;
    WCHAR path[MAX_PATH];

    if (GetTempPathW(_countof(path), path))
    {
        PathRemoveBackslashW(path);
        folders.push_back(path);
    }
    if (IsAdmin() && GetWindowsDirectoryW(path, _countof(path)))
        folders.push_back(std::wstring(path) + L"\\Temp");
    return folders;
}

static ULONGLONG
FolderBytes(const std::wstring &folder, bool remove)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
    ULONGLONG total = 0;

    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        std::wstring path = folder + L"\\" + data.cFileName;
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            total += FolderBytes(path, remove);
            if (remove)
                RemoveDirectoryW(path.c_str());
            continue;
        }
        if (remove)
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(path.c_str(), data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (!DeleteFileW(path.c_str()))
                total += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        }
        else
        {
            total += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return total;
}

static void
ScanTemp(SettingsHost *host, bool remove)
{
    ULONGLONG *result = new ULONGLONG(0);

    TempBusy = true;
    host->RunJob([result, remove]()
                 {
                     std::vector<std::wstring> folders = TempFolders();
                     for (size_t i = 0; i < folders.size(); i++)
                         *result += FolderBytes(folders[i], remove);
                 },
                 [host, result]()
                 {
                     TempBytes = *result;
                     TempMeasured = true;
                     TempBusy = false;
                     delete result;
                     host->Refresh();
                 });
}

static void
AddCleanup(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    SHQUERYRBINFO bin = { sizeof(bin) };

    page.Group(Str(IDS_CLEANUP));
    if (!page.Indexing && FAILED(SHQueryRecycleBinW(NULL, &bin)))
        bin.i64Size = bin.i64NumItems = 0;
    {
        SettingRow &row = page.Button(Str(IDS_RECYCLE_BIN),
                                      page.Indexing ? std::wstring() :
                                      Format(IDS_RECYCLE_BIN_DESC, (UINT)bin.i64NumItems, Bytes(bin.i64Size).c_str()),
                                      L"delete_20_regular", Str(IDS_EMPTY),
                                      [host](int)
                                      {
                                          HRESULT hr;
                                          if (!Confirm(host->Window(), Str(IDS_EMPTY_BIN_TITLE), Str(IDS_EMPTY_BIN_TEXT),
                                                       Str(IDS_EMPTY)))
                                              return;
                                          hr = SHEmptyRecycleBinW(host->Window(), NULL,
                                                                  SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                                          if (FAILED(hr) && hr != E_UNEXPECTED)
                                              ShowError(host->Window(), HRESULT_CODE(hr));
                                      });
        row.Enabled = page.Indexing || bin.i64NumItems != 0;
        row.Keywords = Str(IDS_CLEANUP_KEYWORDS);
    }
    if (!page.Indexing && !TempMeasured && !TempBusy)
        ScanTemp(host, false);
    {
        SettingRow &row = page.Button(Str(IDS_TEMP_FILES),
                                      TempBusy ? Str(IDS_TEMP_SCANNING) :
                                      (TempMeasured ? Format(IDS_TEMP_FILES_DESC, Bytes(TempBytes).c_str()) : std::wstring()),
                                      L"document_20_regular", Str(IDS_CLEAN),
                                      [host](int)
                                      {
                                          if (Confirm(host->Window(), Str(IDS_CLEAN_TEMP_TITLE), Str(IDS_CLEAN_TEMP_TEXT),
                                                      Str(IDS_CLEAN)))
                                              ScanTemp(host, true);
                                      });
        row.Enabled = page.Indexing || (!TempBusy && TempBytes != 0);
        row.Keywords = Str(IDS_CLEANUP_KEYWORDS);
    }
}

void
BuildStorage(PageBuilder &page)
{
    WCHAR drives[256];

    page.Group(Str(IDS_DRIVES));
    if (!page.Indexing && GetLogicalDriveStringsW(_countof(drives), drives))
    {
        for (LPCWSTR drive = drives; *drive; drive += wcslen(drive) + 1)
        {
            ULARGE_INTEGER available, total, freeBytes;
            WCHAR label[MAX_PATH] = L"", fileSystem[64] = L"";
            UINT type = GetDriveTypeW(drive);
            std::wstring root = drive, title;

            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
                continue;
            if (!GetDiskFreeSpaceExW(drive, &available, &total, &freeBytes) || !total.QuadPart)
                continue;
            GetVolumeInformationW(drive, label, _countof(label), NULL, NULL, NULL, fileSystem, _countof(fileSystem));
            title = Printf(L"%s (%c:)", label[0] ? label : Str(IDS_LOCAL_DISK).c_str(), drive[0]);
            {
                SettingRow &row = page.Link(title,
                                            Format(IDS_DRIVE_USAGE, Bytes(total.QuadPart - freeBytes.QuadPart).c_str(),
                                                   Bytes(total.QuadPart).c_str(), Bytes(freeBytes.QuadPart).c_str(),
                                                   fileSystem),
                                            L"hard_drive_20_regular",
                                            [root](int) { ShellExecuteW(NULL, L"open", root.c_str(), NULL, NULL,
                                                                        SW_SHOWNORMAL); });
                row.Bar = true;
                row.Max = 1000;
                row.Position = (int)((total.QuadPart - freeBytes.QuadPart) * 1000 / total.QuadPart);
            }
        }
    }
    AddCleanup(page);
    page.Group(Str(IDS_STORAGE_MANAGEMENT));
    page.Link(Str(IDS_DISK_MANAGEMENT), Str(IDS_DISK_MANAGEMENT_DESC), L"hard_drive_20_regular",
              [](int) { Launch(L"diskmgmt.exe"); });
}

static bool
SpiBool(UINT get, BOOL *value)
{
    return SystemParametersInfoW(get, 0, value, 0) != FALSE;
}

static void
AddSpiToggle(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, UINT get, UINT set, bool pointerParam)
{
    BOOL value = FALSE;
    bool supported = page.Indexing || SpiBool(get, &value);

    page.Toggle(Str(title), detail ? Str(detail) : std::wstring(), icon, value != FALSE,
                [set, pointerParam](int on)
                {
                    BOOL flag = on != 0;
                    if (pointerParam)
                        SystemParametersInfoW(set, 0, &flag, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                    else
                        SystemParametersInfoW(set, flag, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                }).Enabled = supported;
}

void
BuildMultitasking(PageBuilder &page)
{
    page.Group(Str(IDS_WINDOWS));
    AddSpiToggle(page, IDS_SNAP_WINDOWS, IDS_SNAP_WINDOWS_DESC, L"window_multiple_20_regular", SPI_GETWINARRANGING,
                 SPI_SETWINARRANGING, false);
    AddSpiToggle(page, IDS_DRAG_FULL, IDS_DRAG_FULL_DESC, L"window_apps_20_regular", SPI_GETDRAGFULLWINDOWS,
                 SPI_SETDRAGFULLWINDOWS, false);
}

static std::wstring
ProcessorName()
{
    std::wstring name = ReadString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                   L"ProcessorNameString");
    size_t start = name.find_first_not_of(L' ');

    return start == std::wstring::npos ? std::wstring() : name.substr(start);
}

static std::wstring
SystemType()
{
    SYSTEM_INFO info;
    UINT arch;

    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_ARM64: arch = IDS_ARCH_ARM64; break;
        case PROCESSOR_ARCHITECTURE_AMD64: arch = IDS_ARCH_X64; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = IDS_ARCH_X86; break;
        default: arch = IDS_ARCH_OTHER; break;
    }
    return Format(IDS_SYSTEM_TYPE_FORMAT, (UINT)(sizeof(void *) * 8), Str(arch).c_str());
}

static std::wstring
InstallDate()
{
    DWORD seconds = ReadDword(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"InstallDate", 0);
    ULARGE_INTEGER time;
    FILETIME utc, local;
    SYSTEMTIME system;
    WCHAR text[64];

    if (!seconds)
        return std::wstring();
    time.QuadPart = ((ULONGLONG)seconds + 11644473600ULL) * 10000000ULL;
    utc.dwLowDateTime = time.LowPart;
    utc.dwHighDateTime = time.HighPart;
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &system);
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &system, NULL, text, _countof(text), NULL))
        return std::wstring();
    return text;
}

static std::wstring
BuildNumber()
{
    std::wstring build = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentBuild");
    DWORD ubr = ReadDword(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"UBR", (DWORD)-1);

    if (build.empty())
        build = ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"CurrentBuildNumber");
    if (ubr != (DWORD)-1)
        build += Printf(L".%lu", ubr);
    return build;
}

void
BuildAbout(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<std::pair<std::wstring, std::wstring>> device, system;
    ULONGLONG installed = 0;
    MEMORYSTATUSEX memory = { sizeof(memory) };
    SYSTEM_INFO info;
    std::wstring manufacturer, model;

    if (!page.Indexing)
    {
        manufacturer = ReadString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer");
        model = ReadString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
        GetNativeSystemInfo(&info);
        GlobalMemoryStatusEx(&memory);
        if (!GetPhysicallyInstalledSystemMemory(&installed))
            installed = memory.ullTotalPhys / 1024;
        device.push_back(std::make_pair(Str(IDS_DEVICE_NAME), ComputerName()));
        device.push_back(std::make_pair(Str(IDS_PROCESSOR),
                                        Format(IDS_PROCESSOR_FORMAT, ProcessorName().c_str(),
                                               (UINT)info.dwNumberOfProcessors)));
        device.push_back(std::make_pair(Str(IDS_INSTALLED_RAM),
                                        Format(IDS_RAM_FORMAT, Bytes(installed * 1024).c_str(),
                                               Bytes(memory.ullTotalPhys).c_str())));
        device.push_back(std::make_pair(Str(IDS_SYSTEM_TYPE), SystemType()));
        if (!manufacturer.empty() || !model.empty())
            device.push_back(std::make_pair(Str(IDS_MODEL), manufacturer + (manufacturer.empty() ? L"" : L" ") + model));
        device.push_back(std::make_pair(Str(IDS_PRODUCT_ID), ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"ProductId")));
        system.push_back(std::make_pair(Str(IDS_EDITION), ProductName()));
        system.push_back(std::make_pair(Str(IDS_VERSION), ReadString(HKEY_LOCAL_MACHINE, WINDOWS_NT_KEY, L"DisplayVersion")));
        system.push_back(std::make_pair(Str(IDS_INSTALLED_ON), InstallDate()));
        system.push_back(std::make_pair(Str(IDS_OS_BUILD), BuildNumber()));
    }
    page.Hero(ComputerName(), model.empty() ? ProductName() : model, L"laptop_20_regular").Action = Str(IDS_RENAME_PC);
    page.Groups.back().Rows.back().Invoke = [host](int) { RenameComputer(host->Window()); };

    page.Group(Str(IDS_DEVICE_SPECS));
    for (size_t i = 0; i < device.size(); i++)
    {
        if (!device[i].second.empty())
            page.Info(device[i].first, device[i].second);
    }
    page.Button(Str(IDS_COPY_SPECS), std::wstring(), L"copy_20_regular", Str(IDS_COPY),
                [host, device](int)
                {
                    std::wstring text;
                    for (size_t i = 0; i < device.size(); i++)
                        text += device[i].first + L"\t" + device[i].second + L"\r\n";
                    CopyToClipboard(host->Window(), text);
                });

    page.Group(Str(IDS_SYSTEM_SPECS));
    for (size_t i = 0; i < system.size(); i++)
    {
        if (!system[i].second.empty())
            page.Info(system[i].first, system[i].second);
    }
    page.Button(Str(IDS_COPY_SPECS), std::wstring(), L"copy_20_regular", Str(IDS_COPY),
                [host, system](int)
                {
                    std::wstring text;
                    for (size_t i = 0; i < system.size(); i++)
                        text += system[i].first + L"\t" + system[i].second + L"\r\n";
                    CopyToClipboard(host->Window(), text);
                });

    page.Group(Str(IDS_RELATED));
    page.Navigate(Str(IDS_PAGE_ENVIRONMENT), Str(IDS_ENVIRONMENT_DESC), L"code_20_regular", PageId::Environment)
        .Keywords = Str(IDS_ENVIRONMENT_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_RECOVERY), Str(IDS_RECOVERY_DESC), L"storage_20_regular", PageId::Recovery)
        .Keywords = Str(IDS_RECOVERY_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_DEVICES), Str(IDS_DEVICES_DESC), L"device_meeting_room_20_regular", PageId::Devices);
}

#define SYSTEM_ENVIRONMENT_KEY L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"

static std::vector<std::pair<std::wstring, std::wstring>>
EnvironmentValues(HKEY root, LPCWSTR key)
{
    std::vector<std::pair<std::wstring, std::wstring>> values;
    HKEY handle;

    if (RegOpenKeyExW(root, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
        return values;
    for (DWORD i = 0; ; i++)
    {
        WCHAR name[256];
        DWORD nameLength = _countof(name), size = 0, type;
        std::vector<BYTE> data;

        if (RegEnumValueW(handle, i, name, &nameLength, NULL, &type, NULL, &size) != ERROR_SUCCESS)
            break;
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            continue;
        data.resize(size + sizeof(WCHAR));
        nameLength = _countof(name);
        if (RegEnumValueW(handle, i, name, &nameLength, NULL, &type, data.data(), &size) != ERROR_SUCCESS)
            continue;
        values.push_back(std::make_pair(std::wstring(name), std::wstring((LPCWSTR)data.data())));
    }
    RegCloseKey(handle);
    std::sort(values.begin(), values.end(), [](const std::pair<std::wstring, std::wstring> &a,
                                               const std::pair<std::wstring, std::wstring> &b)
              { return lstrcmpiW(a.first.c_str(), b.first.c_str()) < 0; });
    return values;
}

static void
StoreEnvironment(HWND owner, HKEY root, LPCWSTR key, const std::wstring &oldName, const std::wstring &name,
                 const std::wstring *value)
{
    HKEY handle;
    LSTATUS result = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &handle, NULL);

    if (result == ERROR_SUCCESS)
    {
        if (!oldName.empty() && (!value || lstrcmpiW(oldName.c_str(), name.c_str())))
            result = RegDeleteValueW(handle, oldName.c_str());
        if (value && result == ERROR_SUCCESS)
            result = RegSetValueExW(handle, name.c_str(), 0,
                                    value->find(L'%') != std::wstring::npos ? REG_EXPAND_SZ : REG_SZ,
                                    (const BYTE *)value->c_str(), (DWORD)((value->length() + 1) * sizeof(WCHAR)));
        RegCloseKey(handle);
    }
    if (result != ERROR_SUCCESS)
    {
        ShowError(owner, (DWORD)result);
        return;
    }
    BroadcastSetting(L"Environment");
}

static void
EditEnvironment(HWND owner, HKEY root, LPCWSTR key, const std::wstring &oldName, const std::wstring &oldValue)
{
    std::vector<std::wstring> labels, values;
    std::vector<bool> secret(2, false);

    labels.push_back(Str(IDS_VARIABLE_NAME));
    labels.push_back(Str(IDS_VARIABLE_VALUE));
    values.push_back(oldName);
    values.push_back(oldValue);
    if (!InputFields(owner, Str(oldName.empty() ? IDS_NEW_VARIABLE : IDS_EDIT_VARIABLE), labels, values, secret) ||
        values[0].empty())
        return;
    StoreEnvironment(owner, root, key, oldName, values[0], &values[1]);
}

static void
AddEnvironmentGroup(PageBuilder &page, UINT title, HKEY root, LPCWSTR key, bool enabled)
{
    SettingsHost *host = page.Host;
    std::vector<std::pair<std::wstring, std::wstring>> values;

    page.Group(Str(title));
    page.Button(Str(IDS_NEW_VARIABLE), std::wstring(), L"add_circle_20_regular", Str(IDS_ADD),
                [host, root, key](int) { EditEnvironment(host->Window(), root, key, std::wstring(), std::wstring()); })
        .Enabled = enabled;
    if (page.Indexing)
        return;
    values = EnvironmentValues(root, key);
    for (size_t i = 0; i < values.size(); i++)
    {
        std::wstring name = values[i].first, value = values[i].second;
        SettingRow &row = page.Button(name, value, NULL, std::wstring(),
                                      [host, root, key, name, value](int action)
                                      {
                                          if (action == 0)
                                              EditEnvironment(host->Window(), root, key, name, value);
                                          else if (Confirm(host->Window(), Format(IDS_DELETE_VARIABLE_TITLE, name.c_str()),
                                                           value, Str(IDS_DELETE)))
                                              StoreEnvironment(host->Window(), root, key, name, name, NULL);
                                      });
        row.Actions.push_back(Str(IDS_EDIT));
        row.Actions.push_back(Str(IDS_DELETE));
        row.Enabled = enabled;
    }
}

void
BuildEnvironment(PageBuilder &page)
{
    page.Group();
    page.Text(Str(IDS_ENVIRONMENT_INTRO));
    AddEnvironmentGroup(page, IDS_USER_VARIABLES, HKEY_CURRENT_USER, L"Environment", true);
    AddEnvironmentGroup(page, IDS_SYSTEM_VARIABLES, HKEY_LOCAL_MACHINE, SYSTEM_ENVIRONMENT_KEY, IsAdmin());
}

#define MEMORY_MANAGEMENT_KEY L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management"
#define CRASH_CONTROL_KEY L"SYSTEM\\CurrentControlSet\\Control\\CrashControl"

static std::wstring
ReadFirstPagingFile(bool *present)
{
    WCHAR data[512];
    DWORD size = sizeof(data);

    *present = RegGetValueW(HKEY_LOCAL_MACHINE, MEMORY_MANAGEMENT_KEY, L"PagingFiles", RRF_RT_REG_MULTI_SZ, NULL, data,
                            &size) == ERROR_SUCCESS;
    if (!*present || size < sizeof(WCHAR))
        return std::wstring();
    data[_countof(data) - 1] = UNICODE_NULL;
    return data;
}

static bool
WritePagingFile(const std::wstring &entry)
{
    std::vector<WCHAR> data(entry.begin(), entry.end());
    HKEY key;
    LONG error;

    data.push_back(UNICODE_NULL);
    if (!entry.empty())
        data.push_back(UNICODE_NULL);
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, MEMORY_MANAGEMENT_KEY, 0, KEY_SET_VALUE, &key);
    if (error == ERROR_SUCCESS)
    {
        error = RegSetValueExW(key, L"PagingFiles", 0, REG_MULTI_SZ, (const BYTE *)data.data(),
                               (DWORD)(data.size() * sizeof(WCHAR)));
        RegCloseKey(key);
    }
    return error == ERROR_SUCCESS;
}

static std::wstring
SystemPagingFile()
{
    WCHAR windows[MAX_PATH];

    if (!GetWindowsDirectoryW(windows, _countof(windows)))
        return L"C:\\pagefile.sys";
    return std::wstring(windows, 2) + L"\\pagefile.sys";
}

struct DumpType
{
    UINT Name;
    DWORD Enabled;
    DWORD Filter;
};

static const DumpType DumpTypes[] =
{
    { IDS_NONE, 0, 0 },
    { IDS_DUMP_AUTOMATIC, 7, 0 },
    { IDS_DUMP_KERNEL, 2, 0 },
    { IDS_DUMP_COMPLETE, 1, 0 },
    { IDS_DUMP_ACTIVE, 1, 1 },
};

void
BuildRecovery(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<std::wstring> paging, dumps;
    std::wstring entry, args;
    ULONG minimum = 0, maximum = 0;
    bool present = false;
    int pagingIndex = 0, dumpIndex = 1;
    DWORD enabled = 7, filter = 0, reboot = 1;

    if (!page.Indexing)
    {
        entry = ReadFirstPagingFile(&present);
        enabled = ReadDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"CrashDumpEnabled", 7);
        filter = ReadDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"FilterPages", 0);
        reboot = ReadDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"AutoReboot", 1);
    }
    if (entry.empty())
        pagingIndex = present ? 3 : 0;
    else if (entry[0] == L'?')
        pagingIndex = 0;
    else
    {
        size_t space = entry.find(L' ');

        pagingIndex = 1;
        if (space != std::wstring::npos)
        {
            args = entry.substr(space + 1);
            if (swscanf(args.c_str(), L"%lu %lu", &minimum, &maximum) >= 1 && minimum)
                pagingIndex = 2;
        }
    }
    paging.push_back(Str(IDS_PAGING_AUTO));
    paging.push_back(Format(IDS_PAGING_SYSTEM, SystemPagingFile().substr(0, 2).c_str()));
    paging.push_back(pagingIndex == 2 ? Format(IDS_PAGING_CUSTOM_FORMAT, minimum, max(minimum, maximum))
                                      : Str(IDS_PAGING_CUSTOM));
    paging.push_back(Str(IDS_PAGING_NONE));
    for (UINT i = 0; i < _countof(DumpTypes); i++)
    {
        dumps.push_back(Str(DumpTypes[i].Name));
        if (DumpTypes[i].Enabled == enabled && DumpTypes[i].Filter == (enabled == 1 && filter ? 1u : 0u))
            dumpIndex = (int)i;
    }

    page.Group(Str(IDS_VIRTUAL_MEMORY));
    page.Choice(Str(IDS_PAGING_FILE), Str(IDS_PAGING_FILE_DESC), L"storage_20_regular", paging, pagingIndex,
                [host, minimum, maximum](int index)
                {
                    std::wstring value;

                    if (index == 0)
                        value = L"?:\\pagefile.sys";
                    else if (index == 1)
                        value = SystemPagingFile();
                    else if (index == 2)
                    {
                        std::vector<std::wstring> labels, values;
                        std::vector<bool> secret(2, false);
                        ULONG low, high;

                        labels.push_back(Str(IDS_INITIAL_SIZE));
                        labels.push_back(Str(IDS_MAXIMUM_SIZE));
                        values.push_back(minimum ? Printf(L"%lu", minimum) : std::wstring());
                        values.push_back(maximum ? Printf(L"%lu", maximum) : std::wstring());
                        if (!InputFields(host->Window(), Str(IDS_PAGING_CUSTOM), labels, values, secret))
                            return;
                        low = wcstoul(values[0].c_str(), NULL, 10);
                        high = wcstoul(values[1].c_str(), NULL, 10);
                        if (!low || high < low)
                        {
                            ShowError(host->Window(), ERROR_INVALID_PARAMETER);
                            return;
                        }
                        value = SystemPagingFile() + Printf(L" %lu %lu", low, high);
                    }
                    if (!WritePagingFile(value))
                    {
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                        return;
                    }
                    PromptRestart(host->Window(), Str(IDS_RESTART_RECOVERY));
                }).Enabled = IsAdmin();

    page.Group(Str(IDS_STARTUP_RECOVERY));
    page.Choice(Str(IDS_DUMP_TYPE), Str(IDS_DUMP_TYPE_DESC), L"document_20_regular", dumps, dumpIndex,
                [host](int index)
                {
                    if (!WriteDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"CrashDumpEnabled", DumpTypes[index].Enabled) ||
                        !WriteDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"FilterPages", DumpTypes[index].Filter))
                    {
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                        return;
                    }
                    PromptRestart(host->Window(), Str(IDS_RESTART_RECOVERY));
                }).Enabled = IsAdmin();
    page.Toggle(Str(IDS_AUTO_RESTART), Str(IDS_AUTO_RESTART_DESC), L"arrow_counterclockwise_20_regular", reboot != 0,
                [host](int on)
                {
                    if (!WriteDword(HKEY_LOCAL_MACHINE, CRASH_CONTROL_KEY, L"AutoReboot", on != 0))
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                }).Enabled = IsAdmin();
}
