/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Page table, page builder, dialogs and helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <stdarg.h>

static const PageInfo Pages[] =
{
    { PageId::System, IDS_PAGE_SYSTEM, L"desktop_20_regular", PageId::None, BuildSystem, L"system" },
    { PageId::Devices, IDS_PAGE_DEVICES, L"bluetooth_20_regular", PageId::None, BuildDevices, L"devices;connecteddevices" },
    { PageId::Network, IDS_PAGE_NETWORK, L"wifi_1_20_regular", PageId::None, BuildNetwork, L"network;network-status" },
    { PageId::Personalization, IDS_PAGE_PERSONALIZE, L"paint_brush_20_regular", PageId::None, BuildPersonalization,
      L"personalization;themes" },
    { PageId::Apps, IDS_PAGE_APPS, L"apps_20_regular", PageId::None, BuildApps, L"apps" },
    { PageId::Accounts, IDS_PAGE_ACCOUNTS, L"person_20_regular", PageId::None, BuildAccounts, L"accounts;yourinfo" },
    { PageId::Time, IDS_PAGE_TIME, L"globe_clock_20_regular", PageId::None, BuildTime, L"time-language" },
    { PageId::Gaming, IDS_PAGE_GAMING, L"games_20_regular", PageId::None, BuildGaming, L"gaming;gaming-gamebar" },
    { PageId::Accessibility, IDS_PAGE_ACCESSIBILITY, L"accessibility_20_regular", PageId::None, BuildAccessibility,
      L"easeofaccess;accessibility;easeofaccess-keyboard;easeofaccess-mouse;easeofaccess-highcontrast" },
    { PageId::Privacy, IDS_PAGE_PRIVACY, L"shield_20_regular", PageId::None, BuildPrivacy, L"privacy;windowsdefender" },
    { PageId::Updates, IDS_PAGE_UPDATE, L"arrow_sync_20_regular", PageId::None, BuildUpdates, L"windowsupdate" },
    { PageId::Display, IDS_PAGE_DISPLAY, L"desktop_20_regular", PageId::System, BuildDisplay,
      L"display;screenrotation;display-advanced" },
    { PageId::Sound, IDS_PAGE_SOUND, L"speaker_2_20_regular", PageId::System, BuildSound,
      L"sound;sound-devices;apps-volume" },
    { PageId::Power, IDS_PAGE_POWER, L"power_20_regular", PageId::System, BuildPower, L"powersleep;batterysaver" },
    { PageId::Storage, IDS_PAGE_STORAGE, L"storage_20_regular", PageId::System, BuildStorage, L"storagesense" },
    { PageId::Multitasking, IDS_PAGE_MULTITASKING, L"window_multiple_20_regular", PageId::System, BuildMultitasking,
      L"multitasking" },
    { PageId::About, IDS_PAGE_ABOUT, L"info_20_regular", PageId::System, BuildAbout, L"about" },
    { PageId::Bluetooth, IDS_PAGE_BLUETOOTH, L"bluetooth_20_regular", PageId::Devices, BuildBluetooth,
      L"bluetooth" },
    { PageId::Mouse, IDS_PAGE_MOUSE, L"cursor_20_regular", PageId::Devices, BuildMouse, L"mousetouchpad" },
    { PageId::Typing, IDS_PAGE_TYPING, L"keyboard_20_regular", PageId::Devices, BuildTyping, L"typing" },
    { PageId::Printers, IDS_PAGE_PRINTERS, L"print_20_regular", PageId::Devices, BuildPrinters, L"printers" },
    { PageId::WiFi, IDS_PAGE_WIFI, L"wifi_1_20_regular", PageId::Network, BuildWiFi,
      L"network-wifi;network-wifisettings" },
    { PageId::WiFiKnown, IDS_PAGE_WIFI_KNOWN, L"wifi_1_20_regular", PageId::WiFi, BuildWiFiKnown, NULL },
    { PageId::Adapter, IDS_PAGE_ADAPTER, L"plug_connected_20_regular", PageId::Network, BuildAdapter,
      L"network-ethernet" },
    { PageId::Proxy, IDS_PAGE_PROXY, L"globe_20_regular", PageId::Network, BuildProxy, L"network-proxy" },
    { PageId::NetworkAdvanced, IDS_ADVANCED_NETWORK, L"network_check_20_regular", PageId::Network, BuildNetworkAdvanced,
      L"network-advancedsettings" },
    { PageId::Background, IDS_PAGE_BACKGROUND, L"image_20_regular", PageId::Personalization, BuildBackground,
      L"personalization-background" },
    { PageId::Taskbar, IDS_PAGE_TASKBAR, L"panel_bottom_20_regular", PageId::Personalization, BuildTaskbar,
      L"taskbar" },
    { PageId::Colors, IDS_PAGE_COLORS, L"color_20_regular", PageId::Personalization, BuildColors,
      L"colors;personalization-colors" },
    { PageId::LockScreen, IDS_PAGE_LOCKSCREEN, L"lock_closed_20_regular", PageId::Personalization, BuildLockScreen,
      L"lockscreen;screensaver" },
    { PageId::Fonts, IDS_PAGE_FONTS, L"text_font_20_regular", PageId::Personalization, BuildFonts, L"fonts" },
    { PageId::InstalledApps, IDS_PAGE_INSTALLED, L"apps_list_20_regular", PageId::Apps, BuildInstalledApps,
      L"appsfeatures;appsfeatures-app" },
    { PageId::DefaultApps, IDS_PAGE_DEFAULT_APPS, L"apps_add_in_20_regular", PageId::Apps, BuildDefaultApps,
      L"defaultapps" },
    { PageId::Startup, IDS_PAGE_STARTUP, L"rocket_20_regular", PageId::Apps, BuildStartup, L"startupapps" },
    { PageId::SignIn, IDS_PAGE_SIGNIN, L"key_20_regular", PageId::Accounts, BuildSignIn, L"signinoptions" },
    { PageId::OtherUsers, IDS_PAGE_OTHER_USERS, L"people_20_regular", PageId::Accounts, BuildOtherUsers,
      L"otherusers;family-group" },
    { PageId::User, IDS_PAGE_USER, L"person_20_regular", PageId::OtherUsers, BuildUser, NULL },
    { PageId::Environment, IDS_PAGE_ENVIRONMENT, L"code_20_regular", PageId::About, BuildEnvironment, NULL },
    { PageId::Recovery, IDS_PAGE_RECOVERY, L"storage_20_regular", PageId::About, BuildRecovery, NULL },
    { PageId::BootLoader, IDS_PAGE_BOOTLOADER, L"power_20_regular", PageId::System, BuildBootLoader, L"bootloader" },
    { PageId::BootEntry, IDS_PAGE_BOOT_ENTRY, L"desktop_20_regular", PageId::BootLoader, BuildBootEntry, NULL },
    { PageId::Controller, IDS_PAGE_CONTROLLER, L"games_20_regular", PageId::Gaming, BuildController, NULL },
    { PageId::DateTime, IDS_PAGE_DATETIME, L"calendar_clock_20_regular", PageId::Time, BuildDateTime,
      L"dateandtime" },
    { PageId::Language, IDS_PAGE_LANGUAGE, L"local_language_20_regular", PageId::Time, BuildLanguage,
      L"regionlanguage;regionformatting;keyboard" },
};

const PageInfo *
PageTable(UINT *count)
{
    *count = _countof(Pages);
    return Pages;
}

const PageInfo *
FindPage(PageId id)
{
    for (UINT i = 0; i < _countof(Pages); i++)
    {
        if (Pages[i].Id == id)
            return &Pages[i];
    }
    return NULL;
}

SettingGroup &
PageBuilder::Group(const std::wstring &title)
{
    Groups.push_back(SettingGroup());
    Groups.back().Title = title;
    return Groups.back();
}

SettingRow &
PageBuilder::Add(RowKind kind, const std::wstring &title, const std::wstring &detail, LPCWSTR icon)
{
    if (Groups.empty())
        Group();
    Groups.back().Rows.push_back(SettingRow());
    SettingRow &row = Groups.back().Rows.back();
    row.Kind = kind;
    row.Title = title;
    row.Detail = detail;
    row.Icon = icon;
    return row;
}

SettingRow &
PageBuilder::Hero(const std::wstring &title, const std::wstring &detail, LPCWSTR icon)
{
    return Add(RowKind::Hero, title, detail, icon);
}

SettingRow &
PageBuilder::Info(const std::wstring &title, const std::wstring &value, LPCWSTR icon)
{
    SettingRow &row = Add(RowKind::Info, title, std::wstring(), icon);
    row.Value = value;
    return row;
}

SettingRow &
PageBuilder::Navigate(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, PageId target,
                      const std::wstring &param)
{
    SettingRow &row = Add(RowKind::Navigate, title, detail, icon);
    row.Target = target;
    row.Param = param;
    return row;
}

SettingRow &
PageBuilder::Toggle(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, bool checked,
                    std::function<void(int)> change)
{
    SettingRow &row = Add(RowKind::Toggle, title, detail, icon);
    row.Checked = checked;
    row.Change = change;
    return row;
}

SettingRow &
PageBuilder::Choice(const std::wstring &title, const std::wstring &detail, LPCWSTR icon,
                    const std::vector<std::wstring> &options, int selected, std::function<void(int)> change)
{
    SettingRow &row = Add(RowKind::Choice, title, detail, icon);
    row.Options = options;
    row.Selected = selected;
    row.Change = change;
    return row;
}

SettingRow &
PageBuilder::Slider(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, int minimum, int maximum,
                    int position, std::function<void(int)> change)
{
    SettingRow &row = Add(RowKind::Slider, title, detail, icon);
    row.Min = minimum;
    row.Max = maximum;
    row.Position = position;
    row.Change = change;
    return row;
}

SettingRow &
PageBuilder::Button(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, const std::wstring &action,
                    std::function<void(int)> invoke)
{
    SettingRow &row = Add(RowKind::Button, title, detail, icon);
    row.Action = action;
    row.Invoke = invoke;
    return row;
}

SettingRow &
PageBuilder::Link(const std::wstring &title, const std::wstring &detail, LPCWSTR icon, std::function<void(int)> invoke)
{
    SettingRow &row = Add(RowKind::Link, title, detail, icon);
    row.Invoke = invoke;
    return row;
}

SettingRow &
PageBuilder::Text(const std::wstring &text)
{
    return Add(RowKind::Text, text);
}

std::wstring
Str(UINT id)
{
    LPCWSTR text = NULL;
    int length = LoadStringW(Instance, id, (LPWSTR)&text, 0);

    return length > 0 && text ? std::wstring(text, length) : std::wstring();
}

static std::wstring
VFormat(LPCWSTR format, va_list args)
{
    WCHAR buffer[1024];

    if (FAILED(StringCchVPrintfW(buffer, _countof(buffer), format, args)))
        buffer[_countof(buffer) - 1] = UNICODE_NULL;
    return buffer;
}

std::wstring
Format(UINT id, ...)
{
    std::wstring format = Str(id), result;
    va_list args;

    va_start(args, id);
    result = VFormat(format.c_str(), args);
    va_end(args);
    return result;
}

std::wstring
Printf(LPCWSTR format, ...)
{
    std::wstring result;
    va_list args;

    va_start(args, format);
    result = VFormat(format, args);
    va_end(args);
    return result;
}

std::wstring
Bytes(ULONGLONG size)
{
    WCHAR text[64];

    if (!StrFormatByteSizeW((LONGLONG)size, text, _countof(text)))
        text[0] = UNICODE_NULL;
    return text;
}

std::wstring
Win32Message(DWORD error)
{
    LPWSTR text = NULL;
    std::wstring result;

    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error, 0, (LPWSTR)&text, 0, NULL) && text)
    {
        result = text;
        while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r' || result.back() == L' '))
            result.pop_back();
        LocalFree(text);
    }
    if (result.empty())
        result = Printf(L"0x%08lX", error);
    return result;
}

DWORD
ReadDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD fallback)
{
    DWORD data, size = sizeof(data);

    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, NULL, &data, &size) != ERROR_SUCCESS)
        return fallback;
    return data;
}

bool
WriteDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD data)
{
    return RegSetKeyValueW(root, key, value, REG_DWORD, &data, sizeof(data)) == ERROR_SUCCESS;
}

std::wstring
ReadString(HKEY root, LPCWSTR key, LPCWSTR value)
{
    WCHAR data[1024];
    DWORD size = sizeof(data);

    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, data, &size) !=
        ERROR_SUCCESS)
        return std::wstring();
    return data;
}

bool
WriteString(HKEY root, LPCWSTR key, LPCWSTR value, const std::wstring &data)
{
    return RegSetKeyValueW(root, key, value, REG_SZ, data.c_str(), (DWORD)((data.length() + 1) * sizeof(WCHAR))) ==
           ERROR_SUCCESS;
}

static DWORD WINAPI
BroadcastThread(LPVOID parameter)
{
    LPWSTR area = (LPWSTR)parameter;

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)area, SMTO_ABORTIFHUNG, 2000, NULL);
    HeapFree(GetProcessHeap(), 0, area);
    return 0;
}

void
BroadcastSetting(LPCWSTR area)
{
    SIZE_T size = ((area ? wcslen(area) : 0) + 1) * sizeof(WCHAR);
    LPWSTR copy = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, size);
    HANDLE thread;

    if (!copy)
        return;
    CopyMemory(copy, area ? area : L"", size);
    thread = CreateThread(NULL, 0, BroadcastThread, copy, 0, NULL);
    if (thread)
        CloseHandle(thread);
    else
        BroadcastThread(copy);
}

void
Launch(LPCWSTR program, LPCWSTR arguments)
{
    ShellExecuteW(NULL, L"open", program, arguments, NULL, SW_SHOWNORMAL);
}

void
ShowError(HWND owner, DWORD error)
{
    TASKDIALOGCONFIG config = { sizeof(config) };
    std::wstring title = Str(IDS_APP_TITLE), heading = Str(IDS_ERROR_HEADING), text = Win32Message(error);

    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = heading.c_str();
    config.pszContent = text.c_str();
    TaskDialogIndirect(&config, NULL, NULL, NULL);
}

bool
IsAdmin()
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    BOOL member = FALSE;

    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &admins))
    {
        if (!CheckTokenMembership(NULL, admins, &member))
            member = FALSE;
        FreeSid(admins);
    }
    return member != FALSE;
}

bool
Confirm(HWND owner, const std::wstring &title, const std::wstring &text, const std::wstring &action)
{
    TASKDIALOGCONFIG config = { sizeof(config) };
    TASKDIALOG_BUTTON button = { IDOK, action.c_str() };
    std::wstring caption = Str(IDS_APP_TITLE);
    int pressed = IDCANCEL;

    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = caption.c_str();
    config.pszMainInstruction = title.c_str();
    config.pszContent = text.c_str();
    config.cButtons = 1;
    config.pButtons = &button;
    config.nDefaultButton = IDCANCEL;
    return SUCCEEDED(TaskDialogIndirect(&config, &pressed, NULL, NULL)) && pressed == IDOK;
}

int
PickFromMenu(HWND owner, const std::vector<std::wstring> &options, int selected)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    int chosen;

    for (size_t i = 0; i < options.size(); i++)
        AppendMenuW(menu, MF_STRING | ((int)i == selected ? MF_CHECKED : 0), i + 1, options[i].c_str());
    GetCursorPos(&pt);
    chosen = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, owner, NULL);
    DestroyMenu(menu);
    return chosen - 1;
}

class TemplateWriter
{
public:
    void Word(WORD value)
    {
        Align(2);
        m_Data.push_back(LOBYTE(value));
        m_Data.push_back(HIBYTE(value));
    }

    void Dword(DWORD value)
    {
        Word(LOWORD(value));
        Word(HIWORD(value));
    }

    void String(LPCWSTR text)
    {
        do
            Word(*text);
        while (*text++);
    }

    void Align(size_t boundary)
    {
        while (m_Data.size() % boundary)
            m_Data.push_back(0);
    }

    void Item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, LPCWSTR text)
    {
        Align(4);
        Dword(style | WS_CHILD | WS_VISIBLE);
        Dword(0);
        Word(x);
        Word(y);
        Word(cx);
        Word(cy);
        Word(id);
        Word(0xFFFF);
        Word(cls);
        String(text);
        Word(0);
    }

    void Named(DWORD style, short x, short y, short cx, short cy, WORD id, LPCWSTR cls, LPCWSTR text)
    {
        Align(4);
        Dword(style | WS_CHILD | WS_VISIBLE);
        Dword(0);
        Word(x);
        Word(y);
        Word(cx);
        Word(cy);
        Word(id);
        String(cls);
        String(text);
        Word(0);
    }

    const BYTE *Data() const { return m_Data.data(); }

private:
    std::vector<BYTE> m_Data;
};

struct InputState
{
    std::vector<std::wstring> *Values;
    size_t Count;
};

static INT_PTR CALLBACK
InputProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam)
{
    InputState *state = (InputState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (msg)
    {
        case WM_INITDIALOG:
            state = (InputState *)lParam;
            SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
            for (size_t i = 0; i < state->Count; i++)
                SetDlgItemTextW(dialog, 200 + (int)i, (*state->Values)[i].c_str());
            SendDlgItemMessageW(dialog, 200, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, 200));
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                for (size_t i = 0; i < state->Count; i++)
                {
                    int length = GetWindowTextLengthW(GetDlgItem(dialog, 200 + (int)i));
                    std::wstring text(length + 1, L'\0');
                    GetDlgItemTextW(dialog, 200 + (int)i, &text[0], length + 1);
                    text.resize(length);
                    (*state->Values)[i] = text;
                }
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

bool
InputFields(HWND owner, const std::wstring &title, const std::vector<std::wstring> &labels,
            std::vector<std::wstring> &values, const std::vector<bool> &secret)
{
    TemplateWriter writer;
    InputState state;
    std::wstring ok = Str(IDS_OK), cancel = Str(IDS_CANCEL);
    short y = 10, height;
    WORD count = (WORD)(labels.size() * 2 + 2);

    values.resize(labels.size());
    height = (short)(labels.size() * 34 + 44);
    writer.Dword(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    writer.Dword(0);
    writer.Word(count);
    writer.Word(0);
    writer.Word(0);
    writer.Word(260);
    writer.Word(height);
    writer.Word(0);
    writer.Word(0);
    writer.String(title.c_str());
    writer.Word(9);
    writer.String(L"Segoe UI");
    for (size_t i = 0; i < labels.size(); i++)
    {
        writer.Item(SS_LEFT, 10, y, 240, 10, (WORD)(100 + i), 0x0082, labels[i].c_str());
        writer.Item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | (i < secret.size() && secret[i] ? ES_PASSWORD : 0), 10,
                    (short)(y + 12), 240, 14, (WORD)(200 + i), 0x0081, L"");
        y += 34;
    }
    writer.Item(BS_DEFPUSHBUTTON | WS_TABSTOP, 146, (short)(height - 24), 50, 14, IDOK, 0x0080, ok.c_str());
    writer.Item(BS_PUSHBUTTON | WS_TABSTOP, 200, (short)(height - 24), 50, 14, IDCANCEL, 0x0080, cancel.c_str());
    state.Values = &values;
    state.Count = labels.size();
    return DialogBoxIndirectParamW(Instance, (LPCDLGTEMPLATEW)writer.Data(), owner, InputProc, (LPARAM)&state) == IDOK;
}

bool
InputBox(HWND owner, const std::wstring &title, const std::wstring &prompt, std::wstring &value, bool password)
{
    std::vector<std::wstring> labels(1, prompt), values(1, value);
    std::vector<bool> secret(1, password);

    if (!InputFields(owner, title, labels, values, secret))
        return false;
    value = values[0];
    return true;
}

bool
EnablePrivilege(LPCWSTR name)
{
    HANDLE token;
    TOKEN_PRIVILEGES privileges = { 1 };
    BOOL result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    result = LookupPrivilegeValueW(NULL, name, &privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return result != FALSE;
}

static bool
AskNow(HWND owner, const std::wstring &reason, UINT now, UINT later)
{
    TASKDIALOGCONFIG config = { sizeof(config) };
    std::wstring nowText = Str(now), laterText = Str(later), title = Str(IDS_APP_TITLE);
    TASKDIALOG_BUTTON buttons[] = { { IDYES, nowText.c_str() }, { IDNO, laterText.c_str() } };
    int pressed = IDNO;

    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = reason.c_str();
    config.cButtons = _countof(buttons);
    config.pButtons = buttons;
    config.nDefaultButton = IDNO;
    return SUCCEEDED(TaskDialogIndirect(&config, &pressed, NULL, NULL)) && pressed == IDYES;
}

void
PromptRestart(HWND owner, const std::wstring &reason)
{
    if (!AskNow(owner, reason, IDS_RESTART_NOW, IDS_RESTART_LATER))
        return;
    EnablePrivilege(SE_SHUTDOWN_NAME);
    if (!ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG |
                                   SHTDN_REASON_FLAG_PLANNED))
        ShowError(owner, GetLastError());
}

void
CopyToClipboard(HWND owner, const std::wstring &text)
{
    SIZE_T size = (text.length() + 1) * sizeof(WCHAR);
    HGLOBAL memory;

    if (!OpenClipboard(owner))
        return;
    EmptyClipboard();
    memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory)
    {
        CopyMemory(GlobalLock(memory), text.c_str(), size);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory))
            GlobalFree(memory);
    }
    CloseClipboard();
}

static INT_PTR CALLBACK
DateTimeProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SYSTEMTIME *time = (SYSTEMTIME *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (msg)
    {
        case WM_INITDIALOG:
            time = (SYSTEMTIME *)lParam;
            SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)time);
            SendDlgItemMessageW(dialog, 200, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)time);
            SendDlgItemMessageW(dialog, 201, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)time);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                SYSTEMTIME date, clock;
                SendDlgItemMessageW(dialog, 200, DTM_GETSYSTEMTIME, 0, (LPARAM)&date);
                SendDlgItemMessageW(dialog, 201, DTM_GETSYSTEMTIME, 0, (LPARAM)&clock);
                *time = date;
                time->wHour = clock.wHour;
                time->wMinute = clock.wMinute;
                time->wSecond = clock.wSecond;
                time->wMilliseconds = 0;
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

bool
PickDateTime(HWND owner, SYSTEMTIME &time)
{
    TemplateWriter writer;
    std::wstring title = Str(IDS_SET_DATE_TIME), date = Str(IDS_DATE), clock = Str(IDS_TIME);
    std::wstring ok = Str(IDS_OK), cancel = Str(IDS_CANCEL);

    writer.Dword(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    writer.Dword(0);
    writer.Word(6);
    writer.Word(0);
    writer.Word(0);
    writer.Word(220);
    writer.Word(112);
    writer.Word(0);
    writer.Word(0);
    writer.String(title.c_str());
    writer.Word(9);
    writer.String(L"Segoe UI");
    writer.Item(SS_LEFT, 10, 10, 200, 10, 100, 0x0082, date.c_str());
    writer.Named(WS_TABSTOP | DTS_LONGDATEFORMAT, 10, 22, 200, 14, 200, DATETIMEPICK_CLASSW, L"");
    writer.Item(SS_LEFT, 10, 44, 200, 10, 101, 0x0082, clock.c_str());
    writer.Named(WS_TABSTOP | DTS_TIMEFORMAT, 10, 56, 200, 14, 201, DATETIMEPICK_CLASSW, L"");
    writer.Item(BS_DEFPUSHBUTTON | WS_TABSTOP, 106, 88, 50, 14, IDOK, 0x0080, ok.c_str());
    writer.Item(BS_PUSHBUTTON | WS_TABSTOP, 160, 88, 50, 14, IDCANCEL, 0x0080, cancel.c_str());
    return DialogBoxIndirectParamW(Instance, (LPCDLGTEMPLATEW)writer.Data(), owner, DateTimeProc, (LPARAM)&time) == IDOK;
}
