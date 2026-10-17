/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Time and language pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <algorithm>

#define W32TIME_KEY L"SYSTEM\\CurrentControlSet\\Services\\W32Time\\Parameters"
#define SERVERS_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DateTime\\Servers"
#define ZONES_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones"
#define PRELOAD_KEY L"Keyboard Layout\\Preload"
#define LAYOUTS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts"

typedef DWORD (WINAPI *PFN_W32TIMESYNCNOW)(LPCWSTR, UINT, UINT);
extern "C" BOOL WINAPI EnumTimeFormatsEx(TIMEFMT_ENUMPROCEX lpTimeFmtEnumProcEx, LPCWSTR lpLocaleName,
                                         DWORD dwFlags, LPARAM lParam);

static std::wstring
NowText(DWORD flags, bool time)
{
    SYSTEMTIME now;
    WCHAR text[128] = L"";

    GetLocalTime(&now);
    if (time)
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &now, NULL, text, _countof(text));
    else
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &now, NULL, text, _countof(text), NULL);
    return text;
}

static std::wstring
ZoneDisplay(const std::wstring &key)
{
    std::wstring display = ReadString(HKEY_LOCAL_MACHINE, (std::wstring(ZONES_KEY) + L"\\" + key).c_str(), L"Display");
    return display.empty() ? key : display;
}

static void
AddClockHero(PageBuilder &page)
{
    DYNAMIC_TIME_ZONE_INFORMATION zone;

    if (page.Indexing)
        return;
    GetDynamicTimeZoneInformation(&zone);
    page.Hero(NowText(0, true), NowText(DATE_LONGDATE, false) + L" \x00B7 " + ZoneDisplay(zone.TimeZoneKeyName),
              L"clock_20_regular");
    page.Host->SetTimerRefresh(1000);
}

void
BuildTime(PageBuilder &page)
{
    AddClockHero(page);
    page.Group();
    page.Navigate(Str(IDS_PAGE_DATETIME), Str(IDS_DATETIME_DESC), L"calendar_clock_20_regular", PageId::DateTime)
        .Keywords = Str(IDS_DATETIME_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_LANGUAGE), Str(IDS_LANGUAGE_DESC), L"local_language_20_regular", PageId::Language)
        .Keywords = Str(IDS_LANGUAGE_KEYWORDS);
}

static void
BroadcastTime()
{
    SendMessageTimeoutW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL);
}

static std::vector<DYNAMIC_TIME_ZONE_INFORMATION>
TimeZones()
{
    std::vector<DYNAMIC_TIME_ZONE_INFORMATION> zones;
    DYNAMIC_TIME_ZONE_INFORMATION zone;

    for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &zone) == ERROR_SUCCESS; i++)
        zones.push_back(zone);
    std::sort(zones.begin(), zones.end(), [](const DYNAMIC_TIME_ZONE_INFORMATION &a, const DYNAMIC_TIME_ZONE_INFORMATION &b)
              {
                  if (a.Bias != b.Bias)
                      return a.Bias > b.Bias;
                  return lstrcmpiW(ZoneDisplay(a.TimeZoneKeyName).c_str(), ZoneDisplay(b.TimeZoneKeyName).c_str()) < 0;
              });
    return zones;
}

static void
ApplyZone(HWND owner, DYNAMIC_TIME_ZONE_INFORMATION zone, bool automaticDst)
{
    if (!automaticDst)
    {
        zone.DynamicDaylightTimeDisabled = TRUE;
        ZeroMemory(&zone.DaylightDate, sizeof(zone.DaylightDate));
        ZeroMemory(&zone.StandardDate, sizeof(zone.StandardDate));
        zone.DaylightBias = 0;
    }
    EnablePrivilege(SE_TIME_ZONE_NAME);
    if (!SetDynamicTimeZoneInformation(&zone))
    {
        ShowError(owner, GetLastError());
        return;
    }
    BroadcastTime();
}

void
BuildDateTime(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<DYNAMIC_TIME_ZONE_INFORMATION> zones;
    std::vector<std::wstring> names, servers, serverValues;
    DYNAMIC_TIME_ZONE_INFORMATION current = { };
    std::wstring type, selectedServer;
    bool automatic, admin = IsAdmin(), hasDst = false, automaticDst;
    int zoneIndex = -1, serverIndex = -1;

    AddClockHero(page);
    type = ReadString(HKEY_LOCAL_MACHINE, W32TIME_KEY, L"Type");
    automatic = !type.empty() && lstrcmpiW(type.c_str(), L"NoSync") != 0;
    page.Group(Str(IDS_CLOCK));
    page.Toggle(Str(IDS_SET_TIME_AUTO), Str(IDS_SET_TIME_AUTO_DESC), L"arrow_sync_circle_20_regular", automatic,
                [host](int on)
                {
                    if (!WriteString(HKEY_LOCAL_MACHINE, W32TIME_KEY, L"Type", on ? L"NTP" : L"NoSync"))
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                }).Enabled = admin;
    if (!page.Indexing)
    {
        HKEY key;
        selectedServer = ReadString(HKEY_LOCAL_MACHINE, SERVERS_KEY, NULL);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SERVERS_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            for (DWORD i = 0; ; i++)
            {
                WCHAR name[64], value[256];
                DWORD nameLength = _countof(name), size = sizeof(value), kind;
                if (RegEnumValueW(key, i, name, &nameLength, NULL, &kind, (BYTE *)value, &size) != ERROR_SUCCESS)
                    break;
                if (!name[0] || kind != REG_SZ)
                    continue;
                if (selectedServer == name)
                    serverIndex = (int)servers.size();
                servers.push_back(value);
                serverValues.push_back(name);
            }
            RegCloseKey(key);
        }
    }
    page.Choice(Str(IDS_TIME_SERVER), Str(IDS_TIME_SERVER_DESC), L"globe_20_regular", servers, serverIndex,
                [host, serverValues](int index)
                {
                    if (!WriteString(HKEY_LOCAL_MACHINE, SERVERS_KEY, NULL, serverValues[index]))
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                }).Enabled = admin && !servers.empty();
    page.Button(Str(IDS_SYNC_NOW), Str(IDS_SYNC_NOW_DESC), L"arrow_sync_20_regular", Str(IDS_SYNC),
                [host](int)
                {
                    DWORD *result = new DWORD(ERROR_PROC_NOT_FOUND);
                    host->RunJob([result]()
                                 {
                                     HMODULE module = LoadLibraryW(L"w32time.dll");
                                     PFN_W32TIMESYNCNOW sync = module ? (PFN_W32TIMESYNCNOW)GetProcAddress(module,
                                                                                                   "W32TimeSyncNow") :
                                                                        NULL;
                                     if (sync)
                                         *result = sync(L"localhost", 0, 0);
                                     if (module)
                                         FreeLibrary(module);
                                 },
                                 [host, result]()
                                 {
                                     if (*result != ERROR_SUCCESS)
                                         ShowError(host->Window(), *result);
                                     delete result;
                                     BroadcastTime();
                                     host->Refresh();
                                 });
                }).Enabled = admin;
    page.Button(Str(IDS_SET_DATE_TIME), Str(IDS_SET_DATE_TIME_DESC), L"calendar_clock_20_regular", Str(IDS_CHANGE),
                [host](int)
                {
                    SYSTEMTIME time;
                    GetLocalTime(&time);
                    if (!PickDateTime(host->Window(), time))
                        return;
                    EnablePrivilege(SE_SYSTEMTIME_NAME);
                    if (!SetLocalTime(&time))
                        ShowError(host->Window(), GetLastError());
                    else
                        BroadcastTime();
                }).Enabled = admin && !automatic;

    page.Group(Str(IDS_TIME_ZONE_GROUP));
    if (!page.Indexing)
    {
        GetDynamicTimeZoneInformation(&current);
        zones = TimeZones();
        for (size_t i = 0; i < zones.size(); i++)
        {
            names.push_back(ZoneDisplay(zones[i].TimeZoneKeyName));
            if (!lstrcmpiW(zones[i].TimeZoneKeyName, current.TimeZoneKeyName))
            {
                zoneIndex = (int)i;
                hasDst = zones[i].DaylightDate.wMonth != 0;
            }
        }
    }
    automaticDst = !current.DynamicDaylightTimeDisabled || current.DaylightDate.wMonth != 0;
    page.Choice(Str(IDS_TIME_ZONE), std::wstring(), L"globe_clock_20_regular", names, zoneIndex,
                [host, zones](int index) { ApplyZone(host->Window(), zones[index], true); }).Keywords =
        Str(IDS_TIME_ZONE_KEYWORDS);
    page.Toggle(Str(IDS_AUTO_DST), Str(IDS_AUTO_DST_DESC), L"weather_moon_20_regular", hasDst && automaticDst,
                [host, zones, zoneIndex](int on)
                {
                    if (zoneIndex >= 0)
                        ApplyZone(host->Window(), zones[zoneIndex], on != 0);
                }).Enabled = hasDst;
}

static std::wstring
LocaleText(LCTYPE type)
{
    WCHAR text[256] = L"";

    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, text, _countof(text));
    return text;
}

static std::vector<std::wstring> *FormatList;

static BOOL CALLBACK
DateFormatProc(LPWSTR format, CALID calendar, LPARAM parameter)
{
    UNREFERENCED_PARAMETER(calendar);
    UNREFERENCED_PARAMETER(parameter);
    if (FormatList && std::find(FormatList->begin(), FormatList->end(), std::wstring(format)) == FormatList->end())
        FormatList->push_back(format);
    return TRUE;
}

static BOOL CALLBACK
TimeFormatProc(LPWSTR format, LPARAM parameter)
{
    UNREFERENCED_PARAMETER(parameter);
    if (FormatList && std::find(FormatList->begin(), FormatList->end(), std::wstring(format)) == FormatList->end())
        FormatList->push_back(format);
    return TRUE;
}

static void
SetFormat(LCTYPE type, const std::wstring &value)
{
    if (SetLocaleInfoW(LOCALE_USER_DEFAULT, type, value.c_str()))
        BroadcastSetting(L"intl");
}

static void
AddFormatChoice(PageBuilder &page, UINT title, LCTYPE type, bool time, DWORD flags)
{
    std::vector<std::wstring> formats, labels;
    std::wstring current;
    int selected = -1;

    if (!page.Indexing)
    {
        SYSTEMTIME now;
        current = LocaleText(type);
        FormatList = &formats;
        if (time)
            EnumTimeFormatsEx(TimeFormatProc, LOCALE_NAME_USER_DEFAULT, flags, 0);
        else
            EnumDateFormatsExEx(DateFormatProc, LOCALE_NAME_USER_DEFAULT, flags, 0);
        FormatList = NULL;
        if (std::find(formats.begin(), formats.end(), current) == formats.end() && !current.empty())
            formats.insert(formats.begin(), current);
        GetLocalTime(&now);
        for (size_t i = 0; i < formats.size(); i++)
        {
            WCHAR sample[128] = L"";
            if (time)
                GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, formats[i].c_str(), sample, _countof(sample));
            else
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, formats[i].c_str(), sample, _countof(sample), NULL);
            labels.push_back(std::wstring(sample) + L"  (" + formats[i] + L")");
            if (formats[i] == current)
                selected = (int)i;
        }
    }
    page.Choice(Str(title), std::wstring(), time ? L"clock_20_regular" : L"calendar_clock_20_regular", labels, selected,
                [type, formats](int index) { SetFormat(type, formats[index]); });
}

static std::wstring
LayoutName(const std::wstring &klid)
{
    WCHAR text[256];
    std::wstring key = std::wstring(LAYOUTS_KEY) + L"\\" + klid;
    HKEY handle;
    std::wstring name;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_QUERY_VALUE, &handle) == ERROR_SUCCESS)
    {
        if (RegLoadMUIStringW(handle, L"Layout Display Name", text, sizeof(text), NULL, 0, NULL) == ERROR_SUCCESS)
            name = text;
        RegCloseKey(handle);
    }
    if (name.empty())
        name = ReadString(HKEY_LOCAL_MACHINE, key.c_str(), L"Layout Text");
    return name.empty() ? klid : name;
}

static std::vector<std::wstring>
PreloadList()
{
    std::vector<std::wstring> layouts;

    for (int i = 1; i < 64; i++)
    {
        std::wstring klid = ReadString(HKEY_CURRENT_USER, PRELOAD_KEY, Printf(L"%d", i).c_str());
        if (klid.empty())
            break;
        layouts.push_back(klid);
    }
    return layouts;
}

static void
StorePreload(const std::vector<std::wstring> &layouts)
{
    HKEY key;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, PRELOAD_KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    for (int i = 1; i < 64; i++)
        RegDeleteValueW(key, Printf(L"%d", i).c_str());
    for (size_t i = 0; i < layouts.size(); i++)
    {
        std::wstring name = Printf(L"%u", (UINT)i + 1);
        RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE *)layouts[i].c_str(),
                       (DWORD)((layouts[i].length() + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(key);
}

static void
AddKeyboard(HWND owner)
{
    std::vector<std::pair<std::wstring, std::wstring>> layouts;
    std::vector<std::wstring> names, current = PreloadList();
    HKEY key;
    int chosen;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LAYOUTS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0; ; i++)
    {
        WCHAR klid[KL_NAMELENGTH + 8];
        DWORD length = _countof(klid);
        if (RegEnumKeyExW(key, i, klid, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (std::find(current.begin(), current.end(), std::wstring(klid)) == current.end())
            layouts.push_back(std::make_pair(LayoutName(klid), std::wstring(klid)));
    }
    RegCloseKey(key);
    std::sort(layouts.begin(), layouts.end());
    for (size_t i = 0; i < layouts.size(); i++)
        names.push_back(layouts[i].first);
    chosen = PickFromMenu(owner, names, -1);
    if (chosen < 0)
        return;
    if (!LoadKeyboardLayoutW(layouts[chosen].second.c_str(), KLF_NOTELLSHELL))
    {
        ShowError(owner, GetLastError());
        return;
    }
    current.push_back(layouts[chosen].second);
    StorePreload(current);
}

static std::vector<std::pair<std::wstring, GEOID>> *NationList;

static BOOL CALLBACK
NationProc(GEOID id)
{
    WCHAR name[128];

    if (NationList && GetGeoInfoW(id, GEO_FRIENDLYNAME, name, _countof(name), 0))
        NationList->push_back(std::make_pair(std::wstring(name), id));
    return TRUE;
}

void
BuildLanguage(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<std::wstring> layouts = page.Indexing ? std::vector<std::wstring>() : PreloadList();
    WCHAR uiName[LOCALE_NAME_MAX_LENGTH] = L"", display[256] = L"";
    std::vector<std::pair<std::wstring, GEOID>> nations;
    std::vector<std::wstring> nationNames;
    GEOID geo = GetUserGeoID(GEOCLASS_NATION);
    int geoIndex = -1;

    if (!page.Indexing && LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), uiName,
                                           _countof(uiName), 0))
        GetLocaleInfoEx(uiName, LOCALE_SLOCALIZEDDISPLAYNAME, display, _countof(display));
    page.Group(Str(IDS_LANGUAGE));
    page.Info(Str(IDS_DISPLAY_LANGUAGE), display, L"local_language_20_regular");

    page.Group(Str(IDS_REGION));
    if (!page.Indexing)
    {
        NationList = &nations;
        EnumSystemGeoID(GEOCLASS_NATION, 0, NationProc);
        NationList = NULL;
        std::sort(nations.begin(), nations.end());
        for (size_t i = 0; i < nations.size(); i++)
        {
            nationNames.push_back(nations[i].first);
            if (nations[i].second == geo)
                geoIndex = (int)i;
        }
    }
    page.Choice(Str(IDS_COUNTRY), Str(IDS_COUNTRY_DESC), L"globe_20_regular", nationNames, geoIndex,
                [nations](int index)
                {
                    SetUserGeoID(nations[index].second);
                    BroadcastSetting(L"intl");
                });
    page.Info(Str(IDS_REGIONAL_FORMAT), page.Indexing ? std::wstring() : LocaleText(LOCALE_SLOCALIZEDDISPLAYNAME),
              L"local_language_20_regular");

    page.Group(Str(IDS_FORMATS));
    AddFormatChoice(page, IDS_SHORT_DATE, LOCALE_SSHORTDATE, false, DATE_SHORTDATE);
    AddFormatChoice(page, IDS_LONG_DATE, LOCALE_SLONGDATE, false, DATE_LONGDATE);
    AddFormatChoice(page, IDS_SHORT_TIME, LOCALE_SSHORTTIME, true, TIME_NOSECONDS);
    AddFormatChoice(page, IDS_LONG_TIME, LOCALE_STIMEFORMAT, true, 0);
    {
        std::vector<std::wstring> days;
        int first = page.Indexing ? 0 : _wtoi(LocaleText(LOCALE_IFIRSTDAYOFWEEK).c_str());
        for (int i = 0; i < 7; i++)
            days.push_back(page.Indexing ? std::wstring() : LocaleText(LOCALE_SDAYNAME1 + i));
        page.Choice(Str(IDS_FIRST_DAY), std::wstring(), L"calendar_clock_20_regular", days, first,
                    [](int index) { SetFormat(LOCALE_IFIRSTDAYOFWEEK, Printf(L"%d", index)); });
    }
    {
        std::vector<std::wstring> systems;
        systems.push_back(Str(IDS_METRIC));
        systems.push_back(Str(IDS_US_MEASURE));
        page.Choice(Str(IDS_MEASUREMENT), std::wstring(), NULL, systems,
                    page.Indexing ? 0 : _wtoi(LocaleText(LOCALE_IMEASURE).c_str()),
                    [](int index) { SetFormat(LOCALE_IMEASURE, Printf(L"%d", index)); });
    }
    if (!page.Indexing)
    {
        WCHAR money[64] = L"";
        GetCurrencyFormatW(LOCALE_USER_DEFAULT, 0, L"1234.56", NULL, money, _countof(money));
        page.Info(Str(IDS_CURRENCY), money);
    }

    page.Group(Str(IDS_KEYBOARDS));
    page.Button(Str(IDS_ADD_KEYBOARD), Str(IDS_ADD_KEYBOARD_DESC), L"keyboard_20_regular", Str(IDS_ADD),
                [host](int) { AddKeyboard(host->Window()); }).Keywords = Str(IDS_KEYBOARD_KEYWORDS);
    for (size_t i = 0; i < layouts.size(); i++)
    {
        std::wstring klid = layouts[i];
        SettingRow &row = page.Button(LayoutName(klid), Str(i == 0 ? IDS_DEFAULT_KEYBOARD : IDS_INSTALLED),
                                      L"keyboard_20_regular", std::wstring(),
                                      [host, klid, i](int action)
                                      {
                                          std::vector<std::wstring> current = PreloadList();
                                          HKL layout = LoadKeyboardLayoutW(klid.c_str(), KLF_NOTELLSHELL);
                                          current.erase(std::remove(current.begin(), current.end(), klid), current.end());
                                          if (i > 0 && action == 0)
                                          {
                                              current.insert(current.begin(), klid);
                                              if (layout)
                                                  SystemParametersInfoW(SPI_SETDEFAULTINPUTLANG, 0, &layout,
                                                                        SPIF_SENDCHANGE);
                                          }
                                          else if (layout && !UnloadKeyboardLayout(layout))
                                          {
                                              ShowError(host->Window(), GetLastError());
                                          }
                                          StorePreload(current);
                                      });
        if (i > 0)
            row.Actions.push_back(Str(IDS_MAKE_DEFAULT));
        if (layouts.size() > 1)
            row.Actions.push_back(Str(IDS_REMOVE));
    }
}
