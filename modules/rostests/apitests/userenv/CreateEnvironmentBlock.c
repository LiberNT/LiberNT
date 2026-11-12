/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CreateEnvironmentBlock for a user whose profile is not loaded, once it is, and when it is refused
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <sddl.h>
#include <userenv.h>
#include <lm.h>

#define TEST_USER L"LntEnvBlockTest"
#define TEST_PASSWORD L"Lnt!EnvBlock#2026q"

static
PCWSTR
FindVariable(
    _In_ PCWSTR Environment,
    _In_ PCWSTR Name)
{
    SIZE_T Length = wcslen(Name);
    PCWSTR p;

    for (p = Environment; *p != UNICODE_NULL; p += wcslen(p) + 1)
    {
        if (_wcsnicmp(p, Name, Length) == 0 && p[Length] == L'=')
            return p + Length + 1;
    }

    return NULL;
}

static
VOID
TestEnvironment(
    _In_ HANDLE hToken)
{
    PROFILEINFOW Profile = {sizeof(Profile)};
    WCHAR Directory[MAX_PATH];
    DWORD Length = ARRAYSIZE(Directory);
    PVOID Environment = NULL;
    PCWSTR Value;

    /* User-specific variables come only with the profile, but the block is made without it (CreateEnvironmentBlock) */
    ok(CreateEnvironmentBlock(&Environment, hToken, FALSE), "CreateEnvironmentBlock failed %lu\n", GetLastError());
    if (Environment != NULL)
    {
        ok(FindVariable(Environment, L"SystemRoot") != NULL, "SystemRoot is missing\n");
        DestroyEnvironmentBlock(Environment);
        Environment = NULL;
    }

    Profile.lpUserName = TEST_USER;
    Profile.dwFlags = PI_NOUI;
    if (!LoadUserProfileW(hToken, &Profile))
    {
        ok(FALSE, "LoadUserProfileW failed %lu\n", GetLastError());
        return;
    }

    ok(GetUserProfileDirectoryW(hToken, Directory, &Length), "GetUserProfileDirectoryW failed %lu\n", GetLastError());
    ok(CreateEnvironmentBlock(&Environment, hToken, FALSE), "CreateEnvironmentBlock failed %lu\n", GetLastError());
    if (Environment != NULL)
    {
        Value = FindVariable(Environment, L"USERPROFILE");
        ok(Value != NULL && _wcsicmp(Value, Directory) == 0, "USERPROFILE is %ls, expected %ls\n",
           Value ? Value : L"(none)", Directory);
        DestroyEnvironmentBlock(Environment);
    }

    ok(UnloadUserProfile(hToken, Profile.hProfile), "UnloadUserProfile failed %lu\n", GetLastError());
}

static
VOID
DeleteTestProfile(
    _In_ HANDLE hToken)
{
    union
    {
        TOKEN_USER User;
        BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } Identity;
    LPWSTR SidString = NULL;
    DWORD Length;

    if (GetTokenInformation(hToken, TokenUser, &Identity, sizeof(Identity), &Length) &&
        ConvertSidToStringSidW(Identity.User.User.Sid, &SidString))
    {
        DeleteProfileW(SidString, NULL, NULL);
        LocalFree(SidString);
    }
}

/* A block that could not be made is not handed back */
static
VOID
TestRefused(VOID)
{
    PVOID Environment = NULL;
    DWORD Error;
    BOOL Result;

    if (!ImpersonateAnonymousToken(GetCurrentThread()))
    {
        skip("ImpersonateAnonymousToken failed %lu\n", GetLastError());
        return;
    }

    SetLastError(0xdeadbeef);
    Result = CreateEnvironmentBlock(&Environment, NULL, FALSE);
    Error = GetLastError();
    ok(RevertToSelf(), "RevertToSelf failed %lu\n", GetLastError());

    if (Result)
    {
        DestroyEnvironmentBlock(Environment);
        return;
    }

    ok(Environment == NULL, "the refused call left %p behind\n", Environment);
    ok(Error != 0xdeadbeef && Error != ERROR_SUCCESS, "the refused call set no error\n");
}

START_TEST(CreateEnvironmentBlock)
{
    USER_INFO_1 User = {0};
    HANDLE hToken = NULL;
    NET_API_STATUS Status;

    TestRefused();

    NetUserDel(NULL, TEST_USER);
    User.usri1_name = TEST_USER;
    User.usri1_password = TEST_PASSWORD;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    if (Status == ERROR_ACCESS_DENIED)
    {
        skip("Creating a user needs administrator rights\n");
        return;
    }
    ok(Status == NERR_Success, "NetUserAdd failed %lu\n", Status);
    if (Status != NERR_Success)
        return;

    if (LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        TestEnvironment(hToken);
        DeleteTestProfile(hToken);
        CloseHandle(hToken);
    }
    else
    {
        ok(FALSE, "LogonUserW failed %lu\n", GetLastError());
    }

    NetUserDel(NULL, TEST_USER);
}
