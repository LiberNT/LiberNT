/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that NET USER changes the password of an existing account
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <lm.h>

#define TEST_USER L"netpwtest"

static DWORD RunNet(const WCHAR *arguments)
{
    WCHAR command[MAX_PATH * 2];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    DWORD code = (DWORD)-1;
    if (!GetSystemDirectoryW(command, MAX_PATH)) return code;
    lstrcatW(command, L"\\net.exe ");
    lstrcatW(command, arguments);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return code;
    WaitForSingleObject(process.hProcess, 30000);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

static BOOL CanLogOn(const WCHAR *password)
{
    HANDLE token;
    if (!LogonUserW(TEST_USER, L".", password, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token)) return FALSE;
    CloseHandle(token);
    return TRUE;
}

START_TEST(NetUserCommand)
{
    USER_INFO_1 info = {0};
    NET_API_STATUS status;

    NetUserDel(NULL, TEST_USER);
    info.usri1_name = TEST_USER;
    info.usri1_password = L"Old-Pass1";
    info.usri1_priv = USER_PRIV_USER;
    info.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT;
    status = NetUserAdd(NULL, 1, (BYTE *)&info, NULL);
    if (status == ERROR_ACCESS_DENIED)
    {
        skip("Adding an account needs an administrator\n");
        return;
    }
    ok(status == NERR_Success, "NetUserAdd failed with %lu\n", status);
    if (status != NERR_Success) return;
    ok(CanLogOn(L"Old-Pass1"), "the new account cannot log on, error %lu\n", GetLastError());

    ok(RunNet(L"user " TEST_USER L" New-Pass2") == 0, "net user with a password failed\n");
    ok(CanLogOn(L"New-Pass2"), "the changed password is refused, error %lu\n", GetLastError());
    ok(!CanLogOn(L"Old-Pass1"), "the old password still logs on\n");
    ok(GetLastError() == ERROR_LOGON_FAILURE, "the old password failed with %lu\n", GetLastError());

    ok(RunNet(L"user " TEST_USER L" /comment:changed") == 0, "net user with an option failed\n");
    ok(CanLogOn(L"New-Pass2"), "an option without a password changed the password, error %lu\n", GetLastError());

    ok(NetUserDel(NULL, TEST_USER) == NERR_Success, "NetUserDel failed\n");
}
