/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Page model shared by the Settings window and its pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <shellframe.h>
#include <lmcons.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "resource.h"

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef IID_PPV_ARG
#define IID_PPV_ARG(Itype, ppType) IID_##Itype, reinterpret_cast<void **>(static_cast<Itype **>(ppType))
#endif

#define WM_SETTINGS_REFRESH (WM_APP + 1)
#define WM_SETTINGS_JOB     (WM_APP + 2)
#define WM_SETTINGS_WIFI_FAILED (WM_APP + 3)

enum class PageId
{
    None,
    System,
    Display,
    Sound,
    Power,
    Storage,
    Multitasking,
    About,
    Environment,
    Recovery,
    BootLoader,
    BootEntry,
    Devices,
    Bluetooth,
    Mouse,
    Typing,
    Printers,
    Network,
    WiFi,
    WiFiKnown,
    Adapter,
    Proxy,
    NetworkAdvanced,
    Personalization,
    Background,
    Taskbar,
    Colors,
    LockScreen,
    Fonts,
    Apps,
    InstalledApps,
    DefaultApps,
    Startup,
    Accounts,
    SignIn,
    OtherUsers,
    User,
    Time,
    DateTime,
    Language,
    Gaming,
    Controller,
    Accessibility,
    Privacy,
    Updates,
    Search,
    Count
};

enum class RowKind
{
    Hero,
    Info,
    Navigate,
    Toggle,
    Choice,
    Slider,
    Button,
    Link,
    Text
};

struct SettingRow
{
    RowKind Kind = RowKind::Info;
    std::wstring Title;
    std::wstring Detail;
    std::wstring Value;
    LPCWSTR Icon = NULL;
    HICON Image = NULL;
    bool Enabled = true;
    bool Checked = false;
    bool Accent = false;
    bool Live = false;
    bool Bar = false;
    std::vector<std::wstring> Options;
    int Selected = -1;
    int Min = 0;
    int Max = 100;
    int Position = 0;
    std::wstring Action;
    std::vector<std::wstring> Actions;
    PageId Target = PageId::None;
    std::wstring Param;
    std::wstring Keywords;
    std::function<void(int)> Change;
    std::function<void(int)> Invoke;
    std::function<std::wstring(int)> Label;
};

struct SettingGroup
{
    std::wstring Title;
    std::wstring Footer;
    std::vector<SettingRow> Rows;
};

class SettingsHost
{
public:
    virtual ~SettingsHost() {}
    virtual HWND Window() const = 0;
    virtual void Refresh() = 0;
    virtual void Open(PageId page, const std::wstring &param) = 0;
    virtual void Back() = 0;
    virtual void RunJob(std::function<void()> work, std::function<void()> done) = 0;
    virtual void SetTimerRefresh(UINT milliseconds) = 0;
};

class PageBuilder
{
public:
    PageBuilder(SettingsHost *host, const std::wstring &param, bool indexing) :
        Host(host),
        Param(param),
        Indexing(indexing)
    {
    }

    SettingGroup &Group(const std::wstring &title = std::wstring());
    SettingRow &Add(RowKind kind, const std::wstring &title, const std::wstring &detail = std::wstring(),
                    LPCWSTR icon = NULL);
    SettingRow &Hero(const std::wstring &title, const std::wstring &detail, LPCWSTR icon);
    SettingRow &Info(const std::wstring &title, const std::wstring &value, LPCWSTR icon = NULL);
    SettingRow &Navigate(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, PageId target,
                         const std::wstring &param = std::wstring());
    SettingRow &Toggle(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, bool checked,
                       std::function<void(int)> change);
    SettingRow &Choice(const std::wstring &title, const std::wstring &detail, LPCWSTR icon,
                       const std::vector<std::wstring> &options, int selected, std::function<void(int)> change);
    SettingRow &Slider(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, int minimum,
                       int maximum, int position, std::function<void(int)> change);
    SettingRow &Button(const std::wstring &title, const std::wstring &detail, LPCWSTR icon,
                       const std::wstring &action, std::function<void(int)> invoke);
    SettingRow &Link(const std::wstring &title, const std::wstring &detail, LPCWSTR icon,
                     std::function<void(int)> invoke);
    SettingRow &Text(const std::wstring &text);

    SettingsHost *Host;
    std::wstring Param;
    bool Indexing;
    std::wstring Title;
    std::vector<SettingGroup> Groups;
};

typedef void (*PAGE_BUILD)(PageBuilder &page);

struct PageInfo
{
    PageId Id;
    UINT Title;
    LPCWSTR Icon;
    PageId Parent;
    PAGE_BUILD Build;
    LPCWSTR Uri;
};

const PageInfo *FindPage(PageId id);
const PageInfo *PageTable(UINT *count);

std::wstring Str(UINT id);
std::wstring Format(UINT id, ...);
std::wstring Printf(LPCWSTR format, ...);
std::wstring Bytes(ULONGLONG size);
std::wstring Win32Message(DWORD error);
DWORD ReadDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD fallback);
bool WriteDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD data);
std::wstring ReadString(HKEY root, LPCWSTR key, LPCWSTR value);
bool WriteString(HKEY root, LPCWSTR key, LPCWSTR value, const std::wstring &data);
void BroadcastSetting(LPCWSTR area);
void Launch(LPCWSTR program, LPCWSTR arguments = NULL);
void ShowError(HWND owner, DWORD error);
bool IsAdmin();
bool EnablePrivilege(LPCWSTR name);
void PromptRestart(HWND owner, const std::wstring &reason);
void CopyToClipboard(HWND owner, const std::wstring &text);

bool InputBox(HWND owner, const std::wstring &title, const std::wstring &prompt, std::wstring &value,
              bool password = false);
bool InputFields(HWND owner, const std::wstring &title, const std::vector<std::wstring> &labels,
                 std::vector<std::wstring> &values, const std::vector<bool> &secret);
bool Confirm(HWND owner, const std::wstring &title, const std::wstring &text, const std::wstring &action);
int PickFromMenu(HWND owner, const std::vector<std::wstring> &options, int selected);
bool PickDateTime(HWND owner, SYSTEMTIME &time);

enum class PointerStyle
{
    White,
    Black,
    Inverted,
    Custom
};

PointerStyle CurrentPointerStyle(COLORREF *color);
int CurrentPointerSize();
bool ApplyPointer(PointerStyle style, COLORREF color, int size);

void BuildSystem(PageBuilder &page);
void BuildDisplay(PageBuilder &page);
void BuildSound(PageBuilder &page);
void BuildPower(PageBuilder &page);
void BuildStorage(PageBuilder &page);
void BuildMultitasking(PageBuilder &page);
void BuildAbout(PageBuilder &page);
void BuildEnvironment(PageBuilder &page);
void BuildRecovery(PageBuilder &page);
void BuildBootLoader(PageBuilder &page);
void BuildBootEntry(PageBuilder &page);
int RunBootLoaderSelfTest(LPCWSTR mode, LPCWSTR log);
void BuildDevices(PageBuilder &page);
void BuildBluetooth(PageBuilder &page);
void BuildMouse(PageBuilder &page);
void BuildTyping(PageBuilder &page);
void BuildPrinters(PageBuilder &page);
void BuildNetwork(PageBuilder &page);
void BuildWiFi(PageBuilder &page);
void BuildWiFiKnown(PageBuilder &page);
void BuildAdapter(PageBuilder &page);
void BuildProxy(PageBuilder &page);
void BuildNetworkAdvanced(PageBuilder &page);
void BuildPersonalization(PageBuilder &page);
void BuildBackground(PageBuilder &page);
void BuildTaskbar(PageBuilder &page);
void BuildColors(PageBuilder &page);
void BuildLockScreen(PageBuilder &page);
void BuildFonts(PageBuilder &page);
void BuildApps(PageBuilder &page);
void BuildInstalledApps(PageBuilder &page);
void BuildDefaultApps(PageBuilder &page);
void BuildStartup(PageBuilder &page);
void BuildAccounts(PageBuilder &page);
void BuildSignIn(PageBuilder &page);
void BuildOtherUsers(PageBuilder &page);
void BuildUser(PageBuilder &page);
void BuildTime(PageBuilder &page);
void BuildDateTime(PageBuilder &page);
void BuildLanguage(PageBuilder &page);
void BuildGaming(PageBuilder &page);
void BuildController(PageBuilder &page);
void BuildAccessibility(PageBuilder &page);
void BuildPrivacy(PageBuilder &page);
void BuildUpdates(PageBuilder &page);

extern HINSTANCE Instance;
