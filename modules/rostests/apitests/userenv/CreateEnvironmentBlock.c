/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for CreateEnvironmentBlock with a user whose profile is not loaded
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <userenv.h>
#include <lm.h>

#define TEST_USER L"LntEnvTest"
#define TEST_PASSWORD L"Lnt!Env#2026q"

typedef NET_API_STATUS (WINAPI *FN_NetUserAdd)(LPCWSTR, DWORD, LPBYTE, LPDWORD);
typedef NET_API_STATUS (WINAPI *FN_NetUserDel)(LPCWSTR, LPCWSTR);

static PCWSTR FindVariable(PCWSTR pszEnvironment, PCWSTR pszName)
{
    SIZE_T cchName = wcslen(pszName);
    PCWSTR p;

    for (p = pszEnvironment; *p; p += wcslen(p) + 1)
    {
        if (!_wcsnicmp(p, pszName, cchName) && p[cchName] == L'=')
            return p + cchName + 1;
    }
    return NULL;
}

START_TEST(CreateEnvironmentBlock)
{
    FN_NetUserAdd pNetUserAdd;
    FN_NetUserDel pNetUserDel;
    HMODULE hNetapi;
    USER_INFO_1 ui;
    HANDLE hToken;
    PVOID pEnvironment = NULL;
    WCHAR szDefault[MAX_PATH];
    DWORD cchDefault = _countof(szDefault);
    PCWSTR pszValue;
    BOOL fOk;

    hNetapi = LoadLibraryW(L"netapi32.dll");
    pNetUserAdd = (FN_NetUserAdd)GetProcAddress(hNetapi, "NetUserAdd");
    pNetUserDel = (FN_NetUserDel)GetProcAddress(hNetapi, "NetUserDel");
    if (!pNetUserAdd || !pNetUserDel)
    {
        skip("netapi32 is missing\n");
        return;
    }
    ZeroMemory(&ui, sizeof(ui));
    ui.usri1_name = (LPWSTR)TEST_USER;
    ui.usri1_password = (LPWSTR)TEST_PASSWORD;
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    pNetUserDel(NULL, TEST_USER);
    if (pNetUserAdd(NULL, 1, (LPBYTE)&ui, NULL) != NERR_Success)
    {
        skip("Cannot create the test user; run as administrator\n");
        return;
    }
    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        ok(0, "LogonUserW failed %lu\n", GetLastError());
        pNetUserDel(NULL, TEST_USER);
        return;
    }

    fOk = CreateEnvironmentBlock(&pEnvironment, hToken, FALSE);
    ok(fOk, "CreateEnvironmentBlock failed %lu\n", GetLastError());
    if (fOk)
    {
        pszValue = FindVariable(pEnvironment, L"USERNAME");
        ok(pszValue && !lstrcmpiW(pszValue, TEST_USER), "USERNAME is %ls\n", pszValue);
        ok(GetDefaultUserProfileDirectoryW(szDefault, &cchDefault), "GetDefaultUserProfileDirectoryW failed\n");
        pszValue = FindVariable(pEnvironment, L"USERPROFILE");
        ok(pszValue && !lstrcmpiW(pszValue, szDefault), "USERPROFILE is %ls, not %ls\n", pszValue, szDefault);
        DestroyEnvironmentBlock(pEnvironment);
    }

    CloseHandle(hToken);
    pNetUserDel(NULL, TEST_USER);
}
