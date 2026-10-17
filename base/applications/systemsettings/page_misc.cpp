/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Gaming, accessibility, privacy and updates pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <mmsystem.h>

#define WINDOWS_NT_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define ADVANCED_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define DATA_COLLECTION_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection"

void
BuildGaming(PageBuilder &page)
{
    UINT count = page.Indexing ? 0 : joyGetNumDevs();
    DISPLAY_DEVICEW display = { sizeof(display) };
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
    if (!page.Indexing && EnumDisplayDevicesW(NULL, 0, &display, 0))
        page.Info(Str(IDS_DISPLAY_ADAPTER), display.DeviceString, L"desktop_pulse_20_regular");
    page.Link(Str(IDS_DXDIAG), Str(IDS_DXDIAG_DESC), L"desktop_pulse_20_regular", [](int) { Launch(L"dxdiag.exe"); });
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
    page.Link(Str(IDS_MAGNIFIER), Str(IDS_MAGNIFIER_DESC), L"zoom_in_20_regular", [](int) { Launch(L"magnify.exe"); });

    page.Group(Str(IDS_VISUAL_EFFECTS));
    AddBoolToggle(page, IDS_CLIENT_ANIMATIONS, IDS_CLIENT_ANIMATIONS_DESC, L"slide_text_20_regular",
                  SPI_GETCLIENTAREAANIMATION, SPI_SETCLIENTAREAANIMATION);

    page.Group(Str(IDS_INTERACTION_KEYBOARD));
    AddBoolToggle(page, IDS_KEYBOARD_CUES, IDS_KEYBOARD_CUES_DESC, L"keyboard_20_regular", SPI_GETKEYBOARDCUES,
                  SPI_SETKEYBOARDCUES);
    page.Link(Str(IDS_OSK), Str(IDS_OSK_DESC), L"keyboard_20_regular", [](int) { Launch(L"osk.exe"); });
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
