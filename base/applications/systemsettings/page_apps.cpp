/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apps pages: installed apps, default apps and startup apps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <algorithm>

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APPROVED_RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define APPROVED_FOLDER_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"

struct InstalledApp
{
    std::wstring Name;
    std::wstring Publisher;
    std::wstring Version;
    std::wstring Icon;
    std::wstring Uninstall;
    std::wstring Modify;
    DWORD SizeKb;
    DWORD Date;
};

static int SortOrder;

static std::wstring
KeyString(HKEY key, LPCWSTR value)
{
    WCHAR data[2048];
    DWORD size = sizeof(data), type;

    if (RegQueryValueExW(key, value, NULL, &type, (BYTE *)data, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
        return std::wstring();
    data[min(size / sizeof(WCHAR), (DWORD)_countof(data) - 1)] = UNICODE_NULL;
    return data;
}

static DWORD
KeyDword(HKEY key, LPCWSTR value, DWORD fallback)
{
    DWORD data, size = sizeof(data), type;

    if (RegQueryValueExW(key, value, NULL, &type, (BYTE *)&data, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return fallback;
    return data;
}

static void
CollectApps(HKEY root, REGSAM view, std::vector<InstalledApp> &apps)
{
    HKEY uninstall;
    WCHAR name[256];

    if (RegOpenKeyExW(root, UNINSTALL_KEY, 0, KEY_READ | view, &uninstall) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0; ; i++)
    {
        DWORD length = _countof(name);
        HKEY key;
        InstalledApp app;
        std::wstring release, date;

        if (RegEnumKeyExW(uninstall, i, name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(uninstall, name, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
            continue;
        app.Name = KeyString(key, L"DisplayName");
        release = KeyString(key, L"ReleaseType");
        if (app.Name.empty() || KeyDword(key, L"SystemComponent", 0) || !KeyString(key, L"ParentKeyName").empty() ||
            !lstrcmpiW(release.c_str(), L"Update") || !lstrcmpiW(release.c_str(), L"Hotfix") ||
            !lstrcmpiW(release.c_str(), L"Security Update"))
        {
            RegCloseKey(key);
            continue;
        }
        app.Publisher = KeyString(key, L"Publisher");
        app.Version = KeyString(key, L"DisplayVersion");
        app.Icon = KeyString(key, L"DisplayIcon");
        app.Uninstall = KeyDword(key, L"NoRemove", 0) ? std::wstring() : KeyString(key, L"UninstallString");
        app.Modify = KeyString(key, L"ModifyPath");
        if (KeyDword(key, L"NoModify", 0))
            app.Modify.clear();
        app.SizeKb = KeyDword(key, L"EstimatedSize", 0);
        date = KeyString(key, L"InstallDate");
        app.Date = date.length() == 8 ? wcstoul(date.c_str(), NULL, 10) : 0;
        RegCloseKey(key);
        bool duplicate = false;
        for (size_t j = 0; j < apps.size() && !duplicate; j++)
            duplicate = apps[j].Name == app.Name && apps[j].Version == app.Version;
        if (!duplicate)
            apps.push_back(app);
    }
    RegCloseKey(uninstall);
}

static HICON
AppIcon(const std::wstring &source)
{
    WCHAR path[MAX_PATH * 2];
    HICON icon = NULL;
    int index;

    if (source.empty())
        return NULL;
    ExpandEnvironmentStringsW(source.c_str(), path, _countof(path));
    PathUnquoteSpacesW(path);
    index = PathParseIconLocationW(path);
    PathUnquoteSpacesW(path);
    if (ExtractIconExW(path, index, &icon, NULL, 1) != 1)
        icon = NULL;
    return icon;
}

static std::wstring
DateText(DWORD yyyymmdd)
{
    SYSTEMTIME time = { };
    WCHAR text[64];

    if (!yyyymmdd)
        return std::wstring();
    time.wYear = (WORD)(yyyymmdd / 10000);
    time.wMonth = (WORD)(yyyymmdd / 100 % 100);
    time.wDay = (WORD)(yyyymmdd % 100);
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, text, _countof(text), NULL))
        return std::wstring();
    return text;
}

static void
RunCommand(HWND owner, const std::wstring &command)
{
    WCHAR expanded[4096];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;

    ExpandEnvironmentStringsW(command.c_str(), expanded, _countof(expanded));
    if (!CreateProcessW(NULL, expanded, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
    {
        ShowError(owner, GetLastError());
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void
BuildApps(PageBuilder &page)
{
    page.Group();
    page.Navigate(Str(IDS_PAGE_INSTALLED), Str(IDS_INSTALLED_DESC), L"apps_list_20_regular", PageId::InstalledApps)
        .Keywords = Str(IDS_INSTALLED_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_DEFAULT_APPS), Str(IDS_DEFAULT_APPS_DESC), L"apps_add_in_20_regular", PageId::DefaultApps);
    page.Navigate(Str(IDS_PAGE_STARTUP), Str(IDS_STARTUP_DESC), L"rocket_20_regular", PageId::Startup);
    page.Group(Str(IDS_RELATED));
    page.Link(Str(IDS_APP_STORE), Str(IDS_APP_STORE_DESC), L"arrow_download_20_regular", [](int) { Launch(L"rapps.exe"); });
}

void
BuildInstalledApps(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<InstalledApp> apps;
    std::vector<std::wstring> orders;
    ULONGLONG total = 0;

    orders.push_back(Str(IDS_SORT_NAME));
    orders.push_back(Str(IDS_SORT_SIZE));
    orders.push_back(Str(IDS_SORT_DATE));
    page.Group();
    page.Choice(Str(IDS_SORT_BY), std::wstring(), L"arrow_sort_20_regular", orders, SortOrder,
                [](int index) { SortOrder = index; });
    if (page.Indexing)
        return;
    CollectApps(HKEY_LOCAL_MACHINE, 0, apps);
    CollectApps(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, apps);
    CollectApps(HKEY_CURRENT_USER, 0, apps);
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b)
              {
                  if (SortOrder == 1 && a.SizeKb != b.SizeKb)
                      return a.SizeKb > b.SizeKb;
                  if (SortOrder == 2 && a.Date != b.Date)
                      return a.Date > b.Date;
                  return lstrcmpiW(a.Name.c_str(), b.Name.c_str()) < 0;
              });
    for (size_t i = 0; i < apps.size(); i++)
        total += (ULONGLONG)apps[i].SizeKb * 1024;
    page.Group(apps.size() == 1 ? Str(IDS_APP_FOUND) : Format(IDS_APPS_FOUND, (UINT)apps.size()));
    if (total)
        page.Groups.back().Title += L" \x00B7 " + Bytes(total);
    if (apps.empty())
        page.Text(Str(IDS_NO_APPS));
    for (size_t i = 0; i < apps.size(); i++)
    {
        InstalledApp app = apps[i];
        std::wstring detail = app.Publisher;
        if (!app.Version.empty())
            detail += (detail.empty() ? L"" : L" \x00B7 ") + app.Version;
        if (app.Date)
            detail += (detail.empty() ? L"" : L" \x00B7 ") + DateText(app.Date);
        if (app.SizeKb)
            detail += (detail.empty() ? L"" : L" \x00B7 ") + Bytes((ULONGLONG)app.SizeKb * 1024);
        SettingRow &row = page.Button(app.Name, detail, L"apps_20_regular", std::wstring(),
                                      [host, app](int action)
                                      {
                                          if (!app.Modify.empty() && action == 0)
                                          {
                                              RunCommand(host->Window(), app.Modify);
                                              return;
                                          }
                                          if (Confirm(host->Window(), Format(IDS_UNINSTALL_TITLE, app.Name.c_str()),
                                                      Str(IDS_UNINSTALL_TEXT), Str(IDS_UNINSTALL)))
                                              RunCommand(host->Window(), app.Uninstall);
                                      });
        if (!app.Modify.empty())
            row.Actions.push_back(Str(IDS_MODIFY));
        if (!app.Uninstall.empty())
            row.Actions.push_back(Str(IDS_UNINSTALL));
        row.Image = AppIcon(app.Icon);
        row.Keywords = app.Publisher;
    }
}

static const LPCWSTR DefaultTypes[] =
{
    L".htm", L".html", L".pdf", L".txt", L".jpg", L".png", L".gif", L".bmp", L".mp3", L".wav", L".mp4", L".avi",
    L".zip", L".7z", L".xml", L".json",
};

static const LPCWSTR DefaultProtocols[] = { L"http", L"https", L"mailto", L"ftp" };

static std::wstring
Association(LPCWSTR item, ASSOCSTR what, ASSOCF flags)
{
    WCHAR text[512];
    DWORD size = _countof(text);

    if (FAILED(AssocQueryStringW(flags, what, item, NULL, text, &size)))
        return std::wstring();
    return text;
}

static void
ChooseDefault(HWND owner, const std::wstring &extension)
{
    std::wstring file = L"file" + extension;
    OPENASINFO info = { file.c_str(), NULL, OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_FORCE_REGISTRATION };

    SHOpenWithDialog(owner, &info);
}

void
BuildDefaultApps(PageBuilder &page)
{
    SettingsHost *host = page.Host;

    page.Group();
    page.Button(Str(IDS_DEFAULT_BY_TYPE), Str(IDS_DEFAULT_BY_TYPE_DESC), L"apps_add_in_20_regular", Str(IDS_CHOOSE),
                [host](int)
                {
                    std::wstring extension;
                    if (!InputBox(host->Window(), Str(IDS_DEFAULT_BY_TYPE), Str(IDS_ENTER_EXTENSION), extension) ||
                        extension.empty())
                        return;
                    if (extension[0] != L'.')
                        extension.insert(extension.begin(), L'.');
                    ChooseDefault(host->Window(), extension);
                });
    page.Group(Str(IDS_FILE_TYPES));
    for (UINT i = 0; i < _countof(DefaultTypes); i++)
    {
        std::wstring extension = DefaultTypes[i];
        std::wstring app = page.Indexing ? std::wstring() : Association(DefaultTypes[i], ASSOCSTR_FRIENDLYAPPNAME, 0);
        std::wstring type = page.Indexing ? std::wstring() : Association(DefaultTypes[i], ASSOCSTR_FRIENDLYDOCNAME, 0);
        SettingRow &row = page.Navigate(extension, type, L"document_20_regular", PageId::None);
        row.Value = app.empty() ? Str(IDS_CHOOSE_DEFAULT) : app;
        row.Invoke = [host, extension](int) { ChooseDefault(host->Window(), extension); };
    }
    page.Group(Str(IDS_LINK_TYPES));
    for (UINT i = 0; i < _countof(DefaultProtocols); i++)
    {
        std::wstring app = page.Indexing ? std::wstring() :
                           Association(DefaultProtocols[i], ASSOCSTR_FRIENDLYAPPNAME, ASSOCF_IS_PROTOCOL);
        page.Info(DefaultProtocols[i], app.empty() ? Str(IDS_NONE) : app, L"globe_20_regular");
    }
}

struct StartupItem
{
    std::wstring Name;
    std::wstring Command;
    HKEY Root;
    bool Folder;
    bool Enabled;
};

static bool
StartupApproved(HKEY root, LPCWSTR key, const std::wstring &name)
{
    BYTE data[32];
    DWORD size = sizeof(data);

    if (RegGetValueW(root, key, name.c_str(), RRF_RT_REG_BINARY, NULL, data, &size) != ERROR_SUCCESS || size < 1)
        return true;
    return !(data[0] & 1);
}

static void
SetStartupApproved(HWND owner, const StartupItem &item, bool enabled)
{
    BYTE data[12] = { enabled ? (BYTE)2 : (BYTE)3 };
    FILETIME now;
    LSTATUS result;

    if (!enabled)
    {
        GetSystemTimeAsFileTime(&now);
        CopyMemory(&data[4], &now, sizeof(now));
    }
    result = RegSetKeyValueW(item.Root, item.Folder ? APPROVED_FOLDER_KEY : APPROVED_RUN_KEY, item.Name.c_str(),
                             REG_BINARY, data, sizeof(data));
    if (result != ERROR_SUCCESS)
        ShowError(owner, (DWORD)result);
}

static void
CollectRun(HKEY root, REGSAM view, std::vector<StartupItem> &items)
{
    HKEY key;
    WCHAR name[256], command[2048];

    if (RegOpenKeyExW(root, RUN_KEY, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0; ; i++)
    {
        DWORD nameLength = _countof(name), size = sizeof(command), type;
        StartupItem item;

        if (RegEnumValueW(key, i, name, &nameLength, NULL, &type, (BYTE *)command, &size) != ERROR_SUCCESS)
            break;
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            continue;
        command[min(size / sizeof(WCHAR), (DWORD)_countof(command) - 1)] = UNICODE_NULL;
        item.Name = name;
        item.Command = command;
        item.Root = root;
        item.Folder = false;
        item.Enabled = StartupApproved(root, APPROVED_RUN_KEY, item.Name);
        items.push_back(item);
    }
    RegCloseKey(key);
}

static void
CollectFolder(int csidl, HKEY root, std::vector<StartupItem> &items)
{
    WCHAR folder[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (FAILED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, folder)))
        return;
    find = FindFirstFileW((std::wstring(folder) + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        StartupItem item;
        if (data.cFileName[0] == L'.' || (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)))
            continue;
        item.Name = data.cFileName;
        item.Command = std::wstring(folder) + L"\\" + data.cFileName;
        item.Root = root;
        item.Folder = true;
        item.Enabled = StartupApproved(root, APPROVED_FOLDER_KEY, item.Name);
        items.push_back(item);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

void
BuildStartup(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<StartupItem> items;

    page.Group();
    page.Text(Str(IDS_STARTUP_INTRO));
    if (page.Indexing)
        return;
    CollectRun(HKEY_CURRENT_USER, 0, items);
    CollectRun(HKEY_LOCAL_MACHINE, 0, items);
    CollectFolder(CSIDL_STARTUP, HKEY_CURRENT_USER, items);
    CollectFolder(CSIDL_COMMON_STARTUP, HKEY_LOCAL_MACHINE, items);
    std::sort(items.begin(), items.end(), [](const StartupItem &a, const StartupItem &b)
              { return lstrcmpiW(a.Name.c_str(), b.Name.c_str()) < 0; });
    page.Group(Str(IDS_PAGE_STARTUP));
    if (items.empty())
        page.Text(Str(IDS_NO_STARTUP));
    for (size_t i = 0; i < items.size(); i++)
    {
        StartupItem item = items[i];
        SettingRow &row = page.Toggle(item.Folder ? std::wstring(PathFindFileNameW(item.Name.c_str())) : item.Name,
                                      item.Command, L"rocket_20_regular", item.Enabled,
                                      [host, item](int on) { SetStartupApproved(host->Window(), item, on != 0); });
        row.Enabled = item.Root == HKEY_CURRENT_USER || IsAdmin();
    }
}
