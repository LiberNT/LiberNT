/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for network logons of local accounts through LogonUserW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winbase.h>
#include <lm.h>

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
LOGON_RESULT
NetworkLogon(
    _In_ PCWSTR Name,
    _In_ PCWSTR Password)
{
    LOGON_RESULT Logon = { FALSE, 0, 0, 0 };
    HANDLE Token = NULL;
    DWORD Length;

    SetLastError(0xdeadbeef);
    Logon.Result = LogonUserW(Name, L".", Password, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &Token);
    Logon.Error = GetLastError();
    if (Logon.Result && Token)
    {
        GetTokenInformation(Token, TokenType, &Logon.Type, sizeof(Logon.Type), &Length);
        if (Logon.Type == TokenImpersonation)
            GetTokenInformation(Token, TokenImpersonationLevel, &Logon.Level, sizeof(Logon.Level), &Length);
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

    Logon = NetworkLogon(TEST_USER, TEST_PASSWORD);
    ok(Logon.Result, "network logon with the password failed %lu\n", Logon.Error);
    ok(Logon.Type == TokenImpersonation, "token type %d\n", Logon.Type);
    ok(Logon.Level == SecurityImpersonation, "impersonation level %d\n", Logon.Level);

    Logon = NetworkLogon(TEST_USER, L"wrong");
    ok(!Logon.Result, "network logon with a wrong password succeeded\n");
    ok(Logon.Error == ERROR_LOGON_FAILURE, "error %lu\n", Logon.Error);

    Logon = NetworkLogon(TEST_BLANK_USER, L"");
    ok(!Logon.Result, "network logon with a blank password succeeded\n");
    ok(Logon.Error == ERROR_ACCOUNT_RESTRICTION, "error %lu\n", Logon.Error);

    NetUserDel(NULL, TEST_BLANK_USER);
    NetUserDel(NULL, TEST_USER);
}
