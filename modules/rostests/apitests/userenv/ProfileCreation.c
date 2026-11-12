/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Two new profiles made from the default profile at the same time both come out whole
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <sddl.h>
#include <userenv.h>
#include <lm.h>
#include <strsafe.h>

#define TEST_PASSWORD L"Lnt!ProfRace#2026q"

typedef struct _PROFILE_THREAD
{
    PCWSTR UserName;
    HANDLE hToken;
    HANDLE hStart;
    PROFILEINFOW Profile;
    DWORD Error;
} PROFILE_THREAD, *PPROFILE_THREAD;

static
DWORD
WINAPI
LoadProfileThread(
    _In_ PVOID Parameter)
{
    PPROFILE_THREAD Thread = Parameter;

    WaitForSingleObject(Thread->hStart, INFINITE);

    Thread->Profile.dwSize = sizeof(Thread->Profile);
    Thread->Profile.lpUserName = (PWSTR)Thread->UserName;
    Thread->Profile.dwFlags = PI_NOUI;
    Thread->Error = LoadUserProfileW(Thread->hToken, &Thread->Profile) ? ERROR_SUCCESS : GetLastError();

    return 0;
}

static
BOOL
AddTestUser(
    _In_ PCWSTR UserName,
    _Out_ PHANDLE phToken)
{
    USER_INFO_1 User = {0};
    NET_API_STATUS Status;

    *phToken = NULL;
    NetUserDel(NULL, UserName);
    User.usri1_name = (PWSTR)UserName;
    User.usri1_password = TEST_PASSWORD;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    if (Status != NERR_Success)
    {
        SetLastError(Status);
        return FALSE;
    }

    if (!LogonUserW(UserName, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, phToken))
    {
        NetUserDel(NULL, UserName);
        return FALSE;
    }

    return TRUE;
}

static
VOID
RemoveTestUser(
    _In_ PCWSTR UserName,
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

    CloseHandle(hToken);
    NetUserDel(NULL, UserName);
}

START_TEST(ProfileCreation)
{
    static const PCWSTR UserNames[] = {L"LntProfRaceA", L"LntProfRaceB"};
    PROFILE_THREAD Threads[ARRAYSIZE(UserNames)] = {{0}};
    HANDLE hThreads[ARRAYSIZE(UserNames)] = {NULL};
    WCHAR Directory[MAX_PATH], HivePath[MAX_PATH];
    HANDLE hStart;
    DWORD Length;
    ULONG Index;

    hStart = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(hStart != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (hStart == NULL)
        return;

    for (Index = 0; Index < ARRAYSIZE(UserNames); Index++)
    {
        if (!AddTestUser(UserNames[Index], &Threads[Index].hToken))
        {
            skip("Making the test user %ls needs administrator rights: %lu\n", UserNames[Index], GetLastError());
            break;
        }

        Threads[Index].UserName = UserNames[Index];
        Threads[Index].hStart = hStart;
        hThreads[Index] = CreateThread(NULL, 0, LoadProfileThread, &Threads[Index], 0, NULL);
        ok(hThreads[Index] != NULL, "CreateThread failed %lu\n", GetLastError());
    }

    /* Both new profiles are made from the default profile at once */
    SetEvent(hStart);

    for (Index = 0; Index < ARRAYSIZE(UserNames); Index++)
    {
        if (hThreads[Index] == NULL)
            continue;

        ok(WaitForSingleObject(hThreads[Index], 30000) == WAIT_OBJECT_0, "profile thread %lu did not end\n", Index);
        CloseHandle(hThreads[Index]);

        ok(Threads[Index].Error == ERROR_SUCCESS, "LoadUserProfileW(%ls) failed %lu\n",
           UserNames[Index], Threads[Index].Error);
        if (Threads[Index].Error != ERROR_SUCCESS)
            continue;

        Length = ARRAYSIZE(Directory);
        ok(GetUserProfileDirectoryW(Threads[Index].hToken, Directory, &Length),
           "GetUserProfileDirectoryW(%ls) failed %lu\n", UserNames[Index], GetLastError());
        StringCchPrintfW(HivePath, ARRAYSIZE(HivePath), L"%s\\ntuser.dat", Directory);
        ok(GetFileAttributesW(HivePath) != INVALID_FILE_ATTRIBUTES, "%ls has no hive\n", Directory);

        ok(UnloadUserProfile(Threads[Index].hToken, Threads[Index].Profile.hProfile),
           "UnloadUserProfile(%ls) failed %lu\n", UserNames[Index], GetLastError());
    }

    for (Index = 0; Index < ARRAYSIZE(UserNames); Index++)
    {
        if (Threads[Index].hToken != NULL)
            RemoveTestUser(UserNames[Index], Threads[Index].hToken);
    }

    CloseHandle(hStart);
}
