/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LogonUserExExW and the NETWORK_CLEARTEXT logon type
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <lm.h>

typedef BOOL (WINAPI *PLOGONUSEREXEXW)(PCWSTR, PCWSTR, PCWSTR, DWORD, DWORD, PTOKEN_GROUPS, PHANDLE, PSID *,
                                       PVOID *, LPDWORD, PQUOTA_LIMITS);

#define TEST_USER L"LntLogonExEx"
#define TEST_PASSWORD L"Lnt!LogonExEx#2026q"

START_TEST(LogonUserExEx)
{
    PLOGONUSEREXEXW pLogonUserExExW;
    USER_INFO_1 User = { 0 };
    HANDLE hToken;
    DWORD ExError, ExExError;
    BOOL Result;

    pLogonUserExExW = (PLOGONUSEREXEXW)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "LogonUserExExW");
    ok(pLogonUserExExW != NULL, "LogonUserExExW is not exported\n");

    hToken = NULL;
    SetLastError(0xdeadbeef);
    Result = LogonUserExW(L"NoSuchUserLnt", L".", L"x", LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                          &hToken, NULL, NULL, NULL, NULL);
    ExError = GetLastError();
    ok(!Result && ExError == ERROR_LOGON_FAILURE, "LogonUserExW(unknown user) returned %d, error %lu\n", Result, ExError);
    if (pLogonUserExExW)
    {
        SetLastError(0xdeadbeef);
        Result = pLogonUserExExW(L"NoSuchUserLnt", L".", L"x", LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                                 NULL, &hToken, NULL, NULL, NULL, NULL);
        ExExError = GetLastError();
        ok(!Result && ExExError == ExError, "LogonUserExExW(unknown user) returned %d, error %lu\n", Result, ExExError);
    }

    NetUserDel(NULL, TEST_USER);
    User.usri1_name = TEST_USER;
    User.usri1_password = TEST_PASSWORD;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    if (NetUserAdd(NULL, 1, (LPBYTE)&User, NULL) != NERR_Success)
    {
        skip("Creating the test user needs administrator rights\n");
        return;
    }

    if (pLogonUserExExW)
    {
        hToken = NULL;
        Result = pLogonUserExExW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                                 NULL, &hToken, NULL, NULL, NULL, NULL);
        ok(Result && hToken != NULL, "LogonUserExExW without groups failed %lu\n", GetLastError());
        if (hToken)
            CloseHandle(hToken);
    }

    hToken = NULL;
    SetLastError(0xdeadbeef);
    Result = LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_NETWORK_CLEARTEXT, LOGON32_PROVIDER_DEFAULT, &hToken);
    ok(Result && hToken != NULL, "NETWORK_CLEARTEXT logon failed %lu\n", GetLastError());
    if (hToken)
        CloseHandle(hToken);

    hToken = NULL;
    SetLastError(0xdeadbeef);
    Result = LogonUserW(TEST_USER, L".", L"wrong", LOGON32_LOGON_NETWORK_CLEARTEXT, LOGON32_PROVIDER_DEFAULT, &hToken);
    ok(!Result && GetLastError() == ERROR_LOGON_FAILURE, "NETWORK_CLEARTEXT with a wrong password returned %d, error %lu\n",
       Result, GetLastError());
    if (hToken)
        CloseHandle(hToken);

    NetUserDel(NULL, TEST_USER);
}
