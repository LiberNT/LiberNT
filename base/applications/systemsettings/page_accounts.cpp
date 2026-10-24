/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accounts pages: your info, sign-in options and other users
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <lm.h>
#include <winternl.h>
#include <ntsam.h>
#include <algorithm>

#define WINLOGON_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"
#define PERSONALIZATION_POLICY_KEY L"SOFTWARE\\Policies\\Microsoft\\Windows\\Personalization"

static std::wstring
CurrentUser()
{
    WCHAR name[UNLEN + 1];
    DWORD size = _countof(name);

    if (!GetUserNameW(name, &size))
        name[0] = UNICODE_NULL;
    return name;
}

template <class Action>
static bool
WithLocalUser(const std::wstring &user, ACCESS_MASK access, const Action &action)
{
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD length = _countof(computer);
    UNICODE_STRING domainName, name;
    SAM_HANDLE server = NULL, domain = NULL, handle = NULL;
    PSID domainSid = NULL;
    PULONG rids = NULL;
    PSID_NAME_USE use = NULL;
    bool result = false;

    if (!GetComputerNameW(computer, &length))
        return false;
    domainName.Buffer = computer;
    domainName.Length = (USHORT)(length * sizeof(WCHAR));
    domainName.MaximumLength = domainName.Length;
    name.Buffer = (PWSTR)user.c_str();
    name.Length = (USHORT)(user.length() * sizeof(WCHAR));
    name.MaximumLength = name.Length;
    if (SamConnect(NULL, &server, SAM_SERVER_CONNECT | SAM_SERVER_LOOKUP_DOMAIN, NULL) >= 0 &&
        SamLookupDomainInSamServer(server, &domainName, &domainSid) >= 0 &&
        SamOpenDomain(server, DOMAIN_LOOKUP, domainSid, &domain) >= 0 &&
        SamLookupNamesInDomain(domain, 1, &name, &rids, &use) >= 0 && use[0] == SidTypeUser &&
        SamOpenUser(domain, access, rids[0], &handle) >= 0)
        result = action(handle);
    if (handle)
        SamCloseHandle(handle);
    if (domain)
        SamCloseHandle(domain);
    if (server)
        SamCloseHandle(server);
    if (domainSid)
        SamFreeMemory(domainSid);
    if (rids)
        SamFreeMemory(rids);
    if (use)
        SamFreeMemory(use);
    return result;
}

static bool
IsPasswordBlank(const std::wstring &user)
{
    bool blank = false;

    WithLocalUser(user, USER_READ_ACCOUNT,
                  [&blank](SAM_HANDLE handle)
                  {
                      PUSER_LOGON_UI_INFORMATION info = NULL;

                      if (SamQueryInformationUser(handle, UserLogonUIInformation, (PVOID *)&info) < 0 || !info)
                          return false;
                      blank = info->PasswordIsBlank != FALSE;
                      SamFreeMemory(info);
                      return true;
                  });
    return blank;
}

static bool
SetPasswordHint(const std::wstring &user, const std::wstring &hint)
{
    return WithLocalUser(user, USER_WRITE_PREFERENCES,
                         [&hint](SAM_HANDLE handle)
                         {
                             USER_EXTENDED_INFORMATION info = { };

                             info.ExtendedWhichFields = USER_EXTENDED_FIELD_PASSWORD_HINT;
                             info.PasswordHint.Buffer = (PWSTR)hint.c_str();
                             info.PasswordHint.Length = (USHORT)(hint.length() * sizeof(WCHAR));
                             info.PasswordHint.MaximumLength = info.PasswordHint.Length;
                             return SamSetInformationUser(handle, UserExtendedInformation, &info) >= 0;
                         });
}

static void
ShowPasswordProblem(HWND owner, UINT id)
{
    TASKDIALOGCONFIG config = { sizeof(config) };
    std::wstring title = Str(IDS_APP_TITLE), heading = Str(IDS_ERROR_HEADING), text = Str(id);

    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = heading.c_str();
    config.pszContent = text.c_str();
    TaskDialogIndirect(&config, NULL, NULL, NULL);
}

static bool
CheckNewPassword(HWND owner, const std::wstring &password, const std::wstring &confirm, const std::wstring &hint)
{
    std::wstring lowerPassword = password, lowerHint = hint;

    if (password != confirm)
    {
        ShowPasswordProblem(owner, IDS_PASSWORD_MISMATCH);
        return false;
    }
    if (password.empty())
        return true;
    if (hint.empty())
    {
        ShowPasswordProblem(owner, IDS_PASSWORD_HINT_NEEDED);
        return false;
    }
    CharLowerBuffW(&lowerPassword[0], (DWORD)lowerPassword.length());
    CharLowerBuffW(&lowerHint[0], (DWORD)lowerHint.length());
    if (lowerHint.find(lowerPassword) != std::wstring::npos)
    {
        SecureZeroMemory(&lowerPassword[0], lowerPassword.length() * sizeof(WCHAR));
        ShowPasswordProblem(owner, IDS_PASSWORD_HINT_CONTAINS);
        return false;
    }
    SecureZeroMemory(&lowerPassword[0], lowerPassword.length() * sizeof(WCHAR));
    return true;
}

static void
DisableBlankAutoLogon(const std::wstring &user)
{
    HKEY key;
    WCHAR password[2] = L"";
    DWORD type, size = sizeof(password);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, WINLOGON_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (ReadString(HKEY_LOCAL_MACHINE, WINLOGON_KEY, L"AutoAdminLogon") == L"1" &&
        !lstrcmpiW(ReadString(HKEY_LOCAL_MACHINE, WINLOGON_KEY, L"DefaultUserName").c_str(), user.c_str()) &&
        RegQueryValueExW(key, L"DefaultPassword", NULL, &type, (PBYTE)password, &size) == ERROR_SUCCESS &&
        type == REG_SZ && !password[0])
    {
        RegSetValueExW(key, L"AutoAdminLogon", 0, REG_SZ, (const BYTE *)L"0", 2 * sizeof(WCHAR));
        RegDeleteValueW(key, L"DefaultPassword");
    }
    RegCloseKey(key);
}

static void
ClearSecrets(std::vector<std::wstring> &values, size_t count)
{
    for (size_t i = 0; i < count && i < values.size(); i++)
        if (!values[i].empty())
            SecureZeroMemory(&values[i][0], values[i].length() * sizeof(WCHAR));
}

static void
SignInScreenToggle(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, LPCWSTR key, LPCWSTR value,
                   DWORD fallback, DWORD whenOn)
{
    SettingsHost *host = page.Host;
    DWORD current = page.Indexing ? fallback : ReadDword(HKEY_LOCAL_MACHINE, key, value, fallback);

    page.Toggle(Str(title), Str(detail), icon, (current != 0) == (whenOn != 0),
                [host, key, value, whenOn](int on)
                {
                    if (!WriteDword(HKEY_LOCAL_MACHINE, key, value, on ? whenOn : !whenOn))
                        ShowError(host->Window(), ERROR_ACCESS_DENIED);
                }).Enabled = IsAdmin();
}

static std::wstring
WellKnownGroup(WELL_KNOWN_SID_TYPE type)
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid), nameSize = 256, domainSize = 256;
    WCHAR name[256], domain[256];
    SID_NAME_USE use;

    if (!CreateWellKnownSid(type, NULL, sid, &sidSize) ||
        !LookupAccountSidW(NULL, sid, name, &nameSize, domain, &domainSize, &use))
        return std::wstring();
    return name;
}

static bool
IsUserAdmin(const std::wstring &user)
{
    LOCALGROUP_USERS_INFO_0 *groups = NULL;
    DWORD read = 0, total = 0;
    std::wstring admins = WellKnownGroup(WinBuiltinAdministratorsSid);
    bool member = false;

    if (NetUserGetLocalGroups(NULL, user.c_str(), 0, LG_INCLUDE_INDIRECT, (LPBYTE *)&groups, MAX_PREFERRED_LENGTH,
                              &read, &total) != NERR_Success)
        return false;
    for (DWORD i = 0; i < read && !member; i++)
        member = !lstrcmpiW(groups[i].lgrui0_name, admins.c_str());
    NetApiBufferFree(groups);
    return member;
}

static DWORD
SetGroupMembership(const std::wstring &user, WELL_KNOWN_SID_TYPE group, bool add)
{
    LOCALGROUP_MEMBERS_INFO_3 member;
    std::wstring name = WellKnownGroup(group);
    NET_API_STATUS status;

    if (name.empty())
        return ERROR_NO_SUCH_GROUP;
    member.lgrmi3_domainandname = (LPWSTR)user.c_str();
    status = add ? NetLocalGroupAddMembers(NULL, name.c_str(), 3, (LPBYTE)&member, 1) :
                   NetLocalGroupDelMembers(NULL, name.c_str(), 3, (LPBYTE)&member, 1);
    if (status == ERROR_MEMBER_IN_ALIAS || status == ERROR_MEMBER_NOT_IN_ALIAS)
        return NERR_Success;
    return status;
}

static std::wstring
UserFullName(const std::wstring &user)
{
    USER_INFO_2 *info = NULL;
    std::wstring name;

    if (NetUserGetInfo(NULL, user.c_str(), 2, (LPBYTE *)&info) == NERR_Success && info)
    {
        if (info->usri2_full_name && info->usri2_full_name[0])
            name = info->usri2_full_name;
        NetApiBufferFree(info);
    }
    return name;
}

void
BuildAccounts(PageBuilder &page)
{
    std::wstring user = CurrentUser(), full = page.Indexing ? std::wstring() : UserFullName(user);
    bool admin = IsAdmin();

    page.Hero(full.empty() ? user : full,
              Str(admin ? IDS_ACCOUNT_ADMIN_LOCAL : IDS_ACCOUNT_STANDARD_LOCAL) + (full.empty() ? L"" : L" \x00B7 " + user),
              L"person_20_regular");
    page.Group(Str(IDS_ACCOUNT_SETTINGS));
    page.Navigate(Str(IDS_PAGE_SIGNIN), Str(IDS_SIGNIN_DESC), L"key_20_regular", PageId::SignIn).Keywords =
        Str(IDS_SIGNIN_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_OTHER_USERS), Str(IDS_OTHER_USERS_DESC), L"people_20_regular", PageId::OtherUsers);
    page.Link(Str(IDS_LOCK_NOW), Str(IDS_LOCK_NOW_DESC), L"lock_closed_20_regular", [](int) { LockWorkStation(); });
}

void
BuildSignIn(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    DWORD timeout = 0;
    BOOL secure = FALSE;

    page.Group(Str(IDS_PASSWORD));
    page.Button(Str(IDS_CHANGE_PASSWORD), Str(IDS_CHANGE_PASSWORD_DESC), L"key_20_regular", Str(IDS_CHANGE),
                [host](int)
                {
                    std::vector<std::wstring> labels, values;
                    std::vector<bool> secret;
                    NET_API_STATUS status;
                    std::wstring user = CurrentUser(), current;
                    bool blank = IsPasswordBlank(user);
                    size_t first = blank ? 0 : 1;

                    if (!blank)
                    {
                        labels.push_back(Str(IDS_CURRENT_PASSWORD));
                        secret.push_back(true);
                    }
                    labels.push_back(Str(IDS_NEW_PASSWORD));
                    labels.push_back(Str(IDS_CONFIRM_PASSWORD));
                    labels.push_back(Str(IDS_PASSWORD_HINT));
                    secret.push_back(true);
                    secret.push_back(true);
                    secret.push_back(false);
                    if (!InputFields(host->Window(), Str(blank ? IDS_CREATE_PASSWORD : IDS_CHANGE_PASSWORD), labels,
                                     values, secret))
                        return;
                    if (!CheckNewPassword(host->Window(), values[first], values[first + 1], values[first + 2]))
                    {
                        ClearSecrets(values, first + 2);
                        return;
                    }
                    if (!blank)
                        current = values[0];
                    status = NetUserChangePassword(NULL, user.c_str(), current.c_str(), values[first].c_str());
                    if (status == NERR_Success)
                    {
                        SetPasswordHint(user, values[first].empty() ? std::wstring() : values[first + 2]);
                        if (!values[first].empty())
                            DisableBlankAutoLogon(user);
                    }
                    if (!current.empty())
                        SecureZeroMemory(&current[0], current.length() * sizeof(WCHAR));
                    ClearSecrets(values, first + 2);
                    if (status == ERROR_INVALID_PASSWORD)
                        ShowPasswordProblem(host->Window(), IDS_PASSWORD_INCORRECT);
                    else if (status != NERR_Success)
                        ShowError(host->Window(), status);
                });
    page.Group(Str(IDS_SIGNIN_EXTRA));
    if (!page.Indexing)
    {
        SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT, 0, &timeout, 0);
        SystemParametersInfoW(SPI_GETSCREENSAVESECURE, 0, &secure, 0);
    }
    page.Toggle(Str(IDS_REQUIRE_SIGNIN), Str(IDS_REQUIRE_SIGNIN_DESC), L"lock_closed_20_regular", secure != FALSE,
                [](int on)
                {
                    SystemParametersInfoW(SPI_SETSCREENSAVESECURE, on != 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                });
    page.Link(Str(IDS_LOCK_NOW), Str(IDS_LOCK_NOW_DESC), L"lock_closed_20_regular", [](int) { LockWorkStation(); });

    page.Group(Str(IDS_SIGNIN_SCREEN));
    SignInScreenToggle(page, IDS_SHOW_LOCK_SCREEN, IDS_SHOW_LOCK_SCREEN_DESC, L"lock_closed_20_regular",
                       PERSONALIZATION_POLICY_KEY, L"NoLockScreen", 0, 0);
    SignInScreenToggle(page, IDS_REQUIRE_CAD, IDS_REQUIRE_CAD_DESC, L"keyboard_20_regular", WINLOGON_KEY,
                       L"DisableCAD", 1, 0);
    SignInScreenToggle(page, IDS_SHOW_LAST_USER, IDS_SHOW_LAST_USER_DESC, L"person_20_regular", WINLOGON_KEY,
                       L"DontDisplayLastUserName", 0, 0);
    SignInScreenToggle(page, IDS_SIGNIN_POWER, IDS_SIGNIN_POWER_DESC, L"power_20_regular", WINLOGON_KEY,
                       L"ShutdownWithoutLogon", 1, 1);
}

static void
AddAccount(HWND owner)
{
    std::vector<std::wstring> labels, values;
    std::vector<bool> secret;
    USER_INFO_1 info = { };
    DWORD error = 0;
    NET_API_STATUS status;

    labels.push_back(Str(IDS_USER_NAME));
    labels.push_back(Str(IDS_NEW_PASSWORD));
    labels.push_back(Str(IDS_CONFIRM_PASSWORD));
    labels.push_back(Str(IDS_PASSWORD_HINT));
    secret.push_back(false);
    secret.push_back(true);
    secret.push_back(true);
    secret.push_back(false);
    if (!InputFields(owner, Str(IDS_ADD_ACCOUNT), labels, values, secret) || values[0].empty())
        return;
    if (!CheckNewPassword(owner, values[1], values[2], values[3]))
    {
        ClearSecrets(values, 3);
        return;
    }
    info.usri1_name = (LPWSTR)values[0].c_str();
    info.usri1_password = (LPWSTR)values[1].c_str();
    info.usri1_priv = USER_PRIV_USER;
    info.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD | (values[1].empty() ? UF_PASSWD_NOTREQD : 0);
    status = NetUserAdd(NULL, 1, (LPBYTE)&info, &error);
    if (status == NERR_Success && !values[1].empty())
        SetPasswordHint(values[0], values[3]);
    ClearSecrets(values, 3);
    if (status != NERR_Success)
    {
        ShowError(owner, status);
        return;
    }
    status = SetGroupMembership(values[0], WinBuiltinUsersSid, true);
    if (status != NERR_Success)
        ShowError(owner, status);
}

void
BuildOtherUsers(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    USER_INFO_20 *users = NULL;
    DWORD read = 0, total = 0, resume = 0;
    std::wstring self = CurrentUser();
    bool admin = IsAdmin();
    std::vector<std::pair<std::wstring, std::wstring>> list;

    page.Group();
    page.Button(Str(IDS_ADD_ACCOUNT), Str(IDS_ADD_ACCOUNT_DESC), L"person_add_20_regular", Str(IDS_ADD),
                [host](int) { AddAccount(host->Window()); }).Enabled = admin;
    if (!admin && !page.Indexing)
        page.Text(Str(IDS_NEED_ADMIN_USERS));
    if (page.Indexing)
        return;
    if (NetUserEnum(NULL, 20, FILTER_NORMAL_ACCOUNT, (LPBYTE *)&users, MAX_PREFERRED_LENGTH, &read, &total, &resume) ==
            NERR_Success && users)
    {
        for (DWORD i = 0; i < read; i++)
        {
            if ((users[i].usri20_flags & UF_ACCOUNTDISABLE) || !lstrcmpiW(users[i].usri20_name, self.c_str()))
                continue;
            list.push_back(std::make_pair(std::wstring(users[i].usri20_name),
                                          users[i].usri20_full_name ? std::wstring(users[i].usri20_full_name) :
                                                                       std::wstring()));
        }
        NetApiBufferFree(users);
    }
    std::sort(list.begin(), list.end());
    page.Group(Str(IDS_OTHER_USERS));
    if (list.empty())
        page.Text(Str(IDS_NO_OTHER_USERS));
    for (size_t i = 0; i < list.size(); i++)
    {
        std::wstring detail = Str(IsUserAdmin(list[i].first) ? IDS_ADMINISTRATOR : IDS_STANDARD_USER);
        if (!list[i].second.empty())
            detail = list[i].second + L" \x00B7 " + detail;
        page.Navigate(list[i].first, detail, L"person_20_regular", PageId::User, list[i].first);
    }
}

void
BuildUser(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::wstring user = page.Param;
    std::vector<std::wstring> types;
    bool admin = IsAdmin(), userAdmin;

    if (page.Indexing || user.empty())
        return;
    page.Title = user;
    userAdmin = IsUserAdmin(user);
    page.Hero(user, Str(userAdmin ? IDS_ADMINISTRATOR : IDS_STANDARD_USER), L"person_20_regular");
    types.push_back(Str(IDS_STANDARD_USER));
    types.push_back(Str(IDS_ADMINISTRATOR));
    page.Group(Str(IDS_ACCOUNT_OPTIONS));
    page.Choice(Str(IDS_ACCOUNT_TYPE), Str(IDS_ACCOUNT_TYPE_DESC), L"shield_20_regular", types, userAdmin ? 1 : 0,
                [host, user](int index)
                {
                    DWORD status = SetGroupMembership(user, WinBuiltinAdministratorsSid, index == 1);
                    if (status == NERR_Success && index == 1)
                        status = SetGroupMembership(user, WinBuiltinUsersSid, true);
                    if (status != NERR_Success)
                        ShowError(host->Window(), status);
                }).Enabled = admin;
    page.Button(Str(IDS_RESET_PASSWORD), Str(IDS_RESET_PASSWORD_DESC), L"key_20_regular", Str(IDS_RESET),
                [host, user](int)
                {
                    std::vector<std::wstring> labels, values;
                    std::vector<bool> secret;
                    USER_INFO_1003 info;
                    DWORD error = 0;
                    NET_API_STATUS status;

                    labels.push_back(Str(IDS_NEW_PASSWORD));
                    labels.push_back(Str(IDS_CONFIRM_PASSWORD));
                    labels.push_back(Str(IDS_PASSWORD_HINT));
                    secret.push_back(true);
                    secret.push_back(true);
                    secret.push_back(false);
                    if (!InputFields(host->Window(), Str(IDS_RESET_PASSWORD), labels, values, secret))
                        return;
                    if (!CheckNewPassword(host->Window(), values[0], values[1], values[2]))
                    {
                        ClearSecrets(values, 2);
                        return;
                    }
                    info.usri1003_password = (LPWSTR)values[0].c_str();
                    status = NetUserSetInfo(NULL, user.c_str(), 1003, (LPBYTE)&info, &error);
                    if (status == NERR_Success)
                    {
                        SetPasswordHint(user, values[0].empty() ? std::wstring() : values[2]);
                        if (!values[0].empty())
                            DisableBlankAutoLogon(user);
                    }
                    ClearSecrets(values, 2);
                    if (status != NERR_Success)
                        ShowError(host->Window(), status);
                }).Enabled = admin;
    page.Group();
    page.Button(Str(IDS_REMOVE_ACCOUNT), Str(IDS_REMOVE_ACCOUNT_DESC), L"delete_20_regular", Str(IDS_REMOVE),
                [host, user](int)
                {
                    NET_API_STATUS status;
                    if (!Confirm(host->Window(), Format(IDS_REMOVE_ACCOUNT_TITLE, user.c_str()),
                                 Str(IDS_REMOVE_ACCOUNT_TEXT), Str(IDS_REMOVE)))
                        return;
                    status = NetUserDel(NULL, user.c_str());
                    if (status != NERR_Success)
                        ShowError(host->Window(), status);
                    else
                        host->Back();
                }).Enabled = admin;
}
