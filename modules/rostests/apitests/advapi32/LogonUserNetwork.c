/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for network logons of local accounts through LogonUserW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winbase.h>
#include <lm.h>
#include <ntsecapi.h>

#define TEST_USER L"LntNetLogon"
#define TEST_PASSWORD L"Lnt!NetLogon#2026q"
#define TEST_BLANK_USER L"LntNetBlank"

typedef struct _LOGON_RESULT
{
    BOOL Result;
    DWORD Error;
    TOKEN_TYPE Type;
    SECURITY_IMPERSONATION_LEVEL Level;
} LOGON_RESULT;

static
NET_API_STATUS
AddUser(
    _In_ PCWSTR Name,
    _In_ PCWSTR Password)
{
    USER_INFO_1 Info = { 0 };

    NetUserDel(NULL, Name);
    Info.usri1_name = (PWSTR)Name;
    Info.usri1_password = (PWSTR)Password;
    Info.usri1_priv = USER_PRIV_USER;
    Info.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD | (Password[0] ? 0 : UF_PASSWD_NOTREQD);
    return NetUserAdd(NULL, 1, (LPBYTE)&Info, NULL);
}

static
BOOL
TokenHasGroup(
    _In_ HANDLE Token,
    _In_ WELL_KNOWN_SID_TYPE Type)
{
    BYTE Buffer[4096], Sid[SECURITY_MAX_SID_SIZE];
    DWORD Size = sizeof(Sid), Index;
    TOKEN_GROUPS *Groups = (TOKEN_GROUPS *)Buffer;

    if (!CreateWellKnownSid(Type, NULL, Sid, &Size))
        return FALSE;
    if (!GetTokenInformation(Token, TokenGroups, Buffer, sizeof(Buffer), &Size))
        return FALSE;
    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        if (EqualSid(Groups->Groups[Index].Sid, Sid))
            return TRUE;
    }
    return FALSE;
}

static
VOID
CheckNetworkToken(
    _In_ HANDLE Token)
{
    BYTE Buffer[256];
    WCHAR Name[64], Domain[64];
    DWORD Size, NameLength = ARRAYSIZE(Name), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;

    ok(TokenHasGroup(Token, WinNetworkSid), "the token has no NETWORK group\n");
    ok(!TokenHasGroup(Token, WinInteractiveSid), "the token has the INTERACTIVE group\n");
    ok(TokenHasGroup(Token, WinAuthenticatedUserSid), "the token has no Authenticated Users group\n");
    ok(GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Size), "TokenUser failed %lu\n", GetLastError());
    ok(LookupAccountSidW(NULL, ((TOKEN_USER *)Buffer)->User.Sid, Name, &NameLength, Domain, &DomainLength, &Use) &&
       !_wcsicmp(Name, TEST_USER), "token user %ls\n", Name);
}

static
VOID
TestLogonUserEx(VOID)
{
    HANDLE Token = NULL;
    PVOID Profile = NULL;
    DWORD ProfileLength = 0;
    PSID LogonSid = NULL;
    QUOTA_LIMITS Quota;
    BOOL Result;

    Result = LogonUserExW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &Token,
                          &LogonSid, &Profile, &ProfileLength, &Quota);
    ok(Result, "LogonUserExW failed %lu\n", GetLastError());
    if (!Result)
        return;

    ok(LogonSid != NULL, "no logon SID\n");
    ok(Profile != NULL && ProfileLength >= sizeof(ULONG) && *(ULONG *)Profile == MsV1_0Lm20LogonProfile,
       "profile %p length %lu\n", Profile, ProfileLength);
    trace("profile %p length %lu type %lu\n", Profile, ProfileLength,
          (Profile && ProfileLength >= sizeof(ULONG)) ? *(ULONG *)Profile : 0xffffffff);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    if (LogonSid)
        FreeSid(LogonSid);
    CloseHandle(Token);
}

static
LOGON_RESULT
NetworkLogon(
    _In_ PCWSTR Name,
    _In_opt_ PCWSTR Domain,
    _In_ PCWSTR Password)
{
    LOGON_RESULT Logon = { FALSE, 0, 0, 0 };
    HANDLE Token = NULL;
    DWORD Length;

    SetLastError(0xdeadbeef);
    Logon.Result = LogonUserW(Name, Domain, Password, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &Token);
    Logon.Error = GetLastError();
    if (Logon.Result && Token)
    {
        GetTokenInformation(Token, TokenType, &Logon.Type, sizeof(Logon.Type), &Length);
        if (Logon.Type == TokenImpersonation)
            GetTokenInformation(Token, TokenImpersonationLevel, &Logon.Level, sizeof(Logon.Level), &Length);
        if (!lstrcmpiW(Name, TEST_USER))
            CheckNetworkToken(Token);
        CloseHandle(Token);
    }
    trace("%ls: result %d error %lu type %d level %d\n", Name, Logon.Result, Logon.Error, Logon.Type, Logon.Level);
    return Logon;
}

START_TEST(LogonUserNetwork)
{
    LOGON_RESULT Logon;
    NET_API_STATUS Status;

    Status = AddUser(TEST_USER, TEST_PASSWORD);
    if (Status == ERROR_ACCESS_DENIED)
    {
        skip("Creating a user needs administrator rights\n");
        return;
    }
    ok(Status == NERR_Success, "NetUserAdd failed %lu\n", Status);
    Status = AddUser(TEST_BLANK_USER, L"");
    ok(Status == NERR_Success, "NetUserAdd for the blank password failed %lu\n", Status);

    Logon = NetworkLogon(TEST_USER, L".", TEST_PASSWORD);
    ok(Logon.Result, "network logon with the password failed %lu\n", Logon.Error);
    ok(Logon.Type == TokenImpersonation, "token type %d\n", Logon.Type);
    ok(Logon.Level == SecurityImpersonation, "impersonation level %d\n", Logon.Level);

    Logon = NetworkLogon(TEST_USER, NULL, TEST_PASSWORD);
    ok(Logon.Result, "network logon with a NULL domain failed %lu\n", Logon.Error);

    TestLogonUserEx();

    Logon = NetworkLogon(TEST_USER, L".", L"wrong");
    ok(!Logon.Result, "network logon with a wrong password succeeded\n");
    ok(Logon.Error == ERROR_LOGON_FAILURE, "error %lu\n", Logon.Error);

    Logon = NetworkLogon(TEST_BLANK_USER, L".", L"");
    ok(!Logon.Result, "network logon with a blank password succeeded\n");
    ok(Logon.Error == ERROR_ACCOUNT_RESTRICTION, "error %lu\n", Logon.Error);

    NetUserDel(NULL, TEST_BLANK_USER);
    NetUserDel(NULL, TEST_USER);
}
