/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DeleteProfileW removes the profile directory and its ProfileList entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <sddl.h>
#include <userenv.h>
#include <lm.h>
#include <strsafe.h>

#define TEST_USER L"LntDeleteProf"
#define TEST_PASSWORD L"Lnt!DelProf#2026q"
#define PROFILE_LIST L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"

static
BOOL
MakeProfile(
    _Out_ PWSTR SidString,
    _In_ SIZE_T SidLength,
    _Out_ PWSTR Directory,
    _In_ DWORD DirectoryLength)
{
    union
    {
        TOKEN_USER User;
        BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } Identity;
    USER_INFO_1 User = {0};
    NET_API_STATUS Status;
    PROFILEINFOW Profile = {0};
    LPWSTR Sid = NULL;
    HANDLE hToken;
    DWORD Length;
    BOOL Result = FALSE;

    NetUserDel(NULL, TEST_USER);
    User.usri1_name = TEST_USER;
    User.usri1_password = TEST_PASSWORD;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    if (Status != NERR_Success)
    {
        SetLastError(Status);
        return FALSE;
    }

    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        ok(0, "LogonUserW failed %lu\n", GetLastError());
        return FALSE;
    }

    Profile.dwSize = sizeof(Profile);
    Profile.lpUserName = TEST_USER;
    Profile.dwFlags = PI_NOUI;
    if (!LoadUserProfileW(hToken, &Profile))
    {
        ok(0, "LoadUserProfileW failed %lu\n", GetLastError());
        CloseHandle(hToken);
        return FALSE;
    }

    if (GetTokenInformation(hToken, TokenUser, &Identity, sizeof(Identity), &Length) &&
        ConvertSidToStringSidW(Identity.User.User.Sid, &Sid))
    {
        StringCchCopyW(SidString, SidLength, Sid);
        LocalFree(Sid);
        Result = GetUserProfileDirectoryW(hToken, Directory, &DirectoryLength);
        ok(Result, "GetUserProfileDirectoryW failed %lu\n", GetLastError());
    }

    ok(UnloadUserProfile(hToken, Profile.hProfile), "UnloadUserProfile failed %lu\n", GetLastError());
    CloseHandle(hToken);
    return Result;
}

static
LONG
OpenProfileListEntry(
    _In_ PCWSTR SidString)
{
    WCHAR KeyName[MAX_PATH];
    HKEY hKey;
    LONG Error;

    StringCchPrintfW(KeyName, ARRAYSIZE(KeyName), L"%s\\%s", PROFILE_LIST, SidString);
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE, &hKey);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);
    return Error;
}

START_TEST(DeleteProfile)
{
    WCHAR SidString[256], Directory[MAX_PATH];
    ULONG Pass;
    BOOL Result;

    SetLastError(0xdeadbeef);
    ok(!DeleteProfileW(NULL, NULL, NULL), "DeleteProfileW(NULL) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Error %lu\n", GetLastError());

    for (Pass = 0; Pass < 2; Pass++)
    {
        if (!MakeProfile(SidString, ARRAYSIZE(SidString), Directory, ARRAYSIZE(Directory)))
        {
            skip("Making a test profile needs administrator rights: %lu\n", GetLastError());
            NetUserDel(NULL, TEST_USER);
            return;
        }

        ok(GetFileAttributesW(Directory) != INVALID_FILE_ATTRIBUTES, "%ls does not exist\n", Directory);
        ok(OpenProfileListEntry(SidString) == ERROR_SUCCESS, "%ls has no ProfileList entry\n", SidString);

        SetLastError(0xdeadbeef);
        Result = DeleteProfileW(SidString, Pass ? Directory : NULL, NULL);
        ok(Result, "DeleteProfileW(%ls, %ls) failed %lu\n", SidString, Pass ? Directory : L"NULL", GetLastError());

        ok(GetFileAttributesW(Directory) == INVALID_FILE_ATTRIBUTES, "%ls still exists\n", Directory);
        ok(OpenProfileListEntry(SidString) == ERROR_FILE_NOT_FOUND, "%ls still has a ProfileList entry\n", SidString);

        NetUserDel(NULL, TEST_USER);
    }
}
