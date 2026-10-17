/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accounts pages: your info, sign-in options and other users
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <lm.h>
#include <algorithm>

static std::wstring
CurrentUser()
{
    WCHAR name[UNLEN + 1];
    DWORD size = _countof(name);

    if (!GetUserNameW(name, &size))
        name[0] = UNICODE_NULL;
    return name;
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
                    std::vector<bool> secret(3, true);
                    NET_API_STATUS status;
                    std::wstring user = CurrentUser();

                    labels.push_back(Str(IDS_CURRENT_PASSWORD));
                    labels.push_back(Str(IDS_NEW_PASSWORD));
                    labels.push_back(Str(IDS_CONFIRM_PASSWORD));
                    if (!InputFields(host->Window(), Str(IDS_CHANGE_PASSWORD), labels, values, secret))
                        return;
                    if (values[1] != values[2])
                    {
                        ShowError(host->Window(), ERROR_INVALID_PASSWORD);
                        return;
                    }
                    status = NetUserChangePassword(NULL, user.c_str(), values[0].c_str(), values[1].c_str());
                    for (size_t i = 0; i < values.size(); i++)
                        SecureZeroMemory(&values[i][0], values[i].length() * sizeof(WCHAR));
                    if (status != NERR_Success)
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
    secret.push_back(false);
    secret.push_back(true);
    secret.push_back(true);
    if (!InputFields(owner, Str(IDS_ADD_ACCOUNT), labels, values, secret) || values[0].empty())
        return;
    if (values[1] != values[2])
    {
        ShowError(owner, ERROR_INVALID_PASSWORD);
        return;
    }
    info.usri1_name = (LPWSTR)values[0].c_str();
    info.usri1_password = (LPWSTR)values[1].c_str();
    info.usri1_priv = USER_PRIV_USER;
    info.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD;
    status = NetUserAdd(NULL, 1, (LPBYTE)&info, &error);
    SecureZeroMemory(&values[1][0], values[1].length() * sizeof(WCHAR));
    SecureZeroMemory(&values[2][0], values[2].length() * sizeof(WCHAR));
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
                    std::vector<bool> secret(2, true);
                    USER_INFO_1003 info;
                    DWORD error = 0;
                    NET_API_STATUS status;

                    labels.push_back(Str(IDS_NEW_PASSWORD));
                    labels.push_back(Str(IDS_CONFIRM_PASSWORD));
                    if (!InputFields(host->Window(), Str(IDS_RESET_PASSWORD), labels, values, secret))
                        return;
                    if (values[0] != values[1])
                    {
                        ShowError(host->Window(), ERROR_INVALID_PASSWORD);
                        return;
                    }
                    info.usri1003_password = (LPWSTR)values[0].c_str();
                    status = NetUserSetInfo(NULL, user.c_str(), 1003, (LPBYTE)&info, &error);
                    SecureZeroMemory(&values[0][0], values[0].length() * sizeof(WCHAR));
                    SecureZeroMemory(&values[1][0], values[1].length() * sizeof(WCHAR));
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
