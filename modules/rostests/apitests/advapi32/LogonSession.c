/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for logon tokens, their profiles and impersonation across logon sessions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <sddl.h>
#include <lm.h>
#include <userenv.h>

#define TEST_USER L"LntLogonTest"
#define TEST_PASSWORD L"Lnt!Logon#2026q"

static void GetDataPath(LPWSTR psz, SIZE_T cch, LPCWSTR pszName)
{
    LPWSTR p;

    GetModuleFileNameW(NULL, psz, (DWORD)cch);
    p = wcsrchr(psz, L'\\');
    if (p)
        p[1] = UNICODE_NULL;
    StringCchCatW(psz, cch, pszName);
}

static SECURITY_IMPERSONATION_LEVEL ImpersonationLevel(HANDLE hImpersonation)
{
    SECURITY_IMPERSONATION_LEVEL level = 0xdead;
    HANDLE hThreadToken = NULL;
    DWORD cb;

    if (!SetThreadToken(NULL, hImpersonation))
        return 0xbad;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hThreadToken))
    {
        GetTokenInformation(hThreadToken, TokenImpersonationLevel, &level, sizeof(level), &cb);
        CloseHandle(hThreadToken);
    }
    RevertToSelf();
    return level;
}

static void RunChild(void)
{
    WCHAR szIn[MAX_PATH], szOut[MAX_PATH];
    char sz[128] = "";
    void *pOwn = NULL, *pOther = NULL;
    HANDLE hFile;
    DWORD cb = 0;

    GetDataPath(szIn, _countof(szIn), L"LogonSession.in");
    GetDataPath(szOut, _countof(szOut), L"LogonSession.out");
    hFile = CreateFileW(szIn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ReadFile(hFile, sz, sizeof(sz) - 1, &cb, NULL);
        CloseHandle(hFile);
    }
    sz[cb] = 0;
    if (sscanf(sz, "%p %p", &pOwn, &pOther) != 2)
        return;
    StringCchPrintfA(sz, _countof(sz), "%d %d", ImpersonationLevel((HANDLE)pOwn), ImpersonationLevel((HANDLE)pOther));
    hFile = CreateFileW(szOut, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, sz, (DWORD)strlen(sz), &cb, NULL);
        CloseHandle(hFile);
    }
}

static PSID GetTokenLogonSid(HANDLE hToken)
{
    TOKEN_GROUPS *pGroups;
    DWORD cb = 0, i;
    PSID pSid = NULL;

    GetTokenInformation(hToken, TokenGroups, NULL, 0, &cb);
    pGroups = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!pGroups)
        return NULL;
    if (GetTokenInformation(hToken, TokenGroups, pGroups, cb, &cb))
    {
        for (i = 0; i < pGroups->GroupCount; i++)
        {
            if ((pGroups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            {
                DWORD cbSid = GetLengthSid(pGroups->Groups[i].Sid);
                pSid = HeapAlloc(GetProcessHeap(), 0, cbSid);
                if (pSid)
                    CopySid(cbSid, pSid, pGroups->Groups[i].Sid);
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, pGroups);
    return pSid;
}

static void TestLogonDefaultDacl(HANDLE hToken)
{
    TOKEN_DEFAULT_DACL *pDacl;
    TOKEN_USER *pUser;
    PSID pLogonSid;
    DWORD cb = 0, i;
    ACCESS_MASK maskUser = 0, maskSystem = 0, maskLogon = 0;

    pLogonSid = GetTokenLogonSid(hToken);
    ok(pLogonSid != NULL, "the logon token has no logon SID\n");
    GetTokenInformation(hToken, TokenUser, NULL, 0, &cb);
    pUser = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!pUser || !GetTokenInformation(hToken, TokenUser, pUser, cb, &cb))
    {
        ok(0, "TokenUser failed %lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, pUser);
        HeapFree(GetProcessHeap(), 0, pLogonSid);
        return;
    }
    cb = 0;
    GetTokenInformation(hToken, TokenDefaultDacl, NULL, 0, &cb);
    pDacl = HeapAlloc(GetProcessHeap(), 0, cb);
    if (pDacl && GetTokenInformation(hToken, TokenDefaultDacl, pDacl, cb, &cb) && pDacl->DefaultDacl)
    {
        for (i = 0; i < pDacl->DefaultDacl->AceCount; i++)
        {
            ACCESS_ALLOWED_ACE *pAce;
            LPWSTR pszSid = NULL;
            PSID pSid;
            if (!GetAce(pDacl->DefaultDacl, i, (void **)&pAce) || pAce->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
                continue;
            pSid = (PSID)&pAce->SidStart;
            ConvertSidToStringSidW(pSid, &pszSid);
            trace("logon token default DACL ACE %lu: %ls mask 0x%lx\n", i, pszSid, pAce->Mask);
            if (EqualSid(pSid, pUser->User.Sid))
                maskUser = pAce->Mask;
            else if (pszSid && !wcscmp(pszSid, L"S-1-5-18"))
                maskSystem = pAce->Mask;
            else if (pLogonSid && EqualSid(pSid, pLogonSid))
                maskLogon = pAce->Mask;
            LocalFree(pszSid);
        }
    }
    else
    {
        ok(0, "TokenDefaultDacl failed %lu\n", GetLastError());
    }
    ok_long(maskUser, GENERIC_ALL);
    ok_long(maskSystem, GENERIC_ALL);
    ok_long(maskLogon, GENERIC_READ | GENERIC_EXECUTE);
    HeapFree(GetProcessHeap(), 0, pDacl);
    HeapFree(GetProcessHeap(), 0, pUser);
    HeapFree(GetProcessHeap(), 0, pLogonSid);
}

static void TestProfile(HANDLE hToken)
{
    PROFILEINFOW profile = { sizeof(profile) };
    WCHAR szDir[MAX_PATH], sz[256];
    DWORD cch = _countof(szDir);
    PSECURITY_DESCRIPTOR pSD = NULL;
    LPWSTR pszSddl = NULL;
    DWORD error;

    profile.lpUserName = TEST_USER;
    profile.dwFlags = PI_NOUI;
    ok(LoadUserProfileW(hToken, &profile), "LoadUserProfileW failed %lu\n", GetLastError());
    if (!profile.hProfile)
        return;
    ok(GetUserProfileDirectoryW(hToken, szDir, &cch), "GetUserProfileDirectoryW failed %lu\n", GetLastError());
    StringCchCatW(szDir, _countof(szDir), L"\\AppData\\LocalLow");
    ok(GetFileAttributesW(szDir) != INVALID_FILE_ATTRIBUTES, "%ls is missing\n", szDir);
    sz[0] = UNICODE_NULL;
    error = GetNamedSecurityInfoW(szDir, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    if (!error && ConvertSecurityDescriptorToStringSecurityDescriptorW(pSD, SDDL_REVISION_1, LABEL_SECURITY_INFORMATION,
                                                                       &pszSddl, NULL))
    {
        StringCchCopyW(sz, _countof(sz), pszSddl);
        LocalFree(pszSddl);
    }
    LocalFree(pSD);
    ok(!wcscmp(sz, L"S:AI(ML;OICI;NW;;;LW)"), "LocalLow label of a new profile is %ls (error %lu)\n", sz, error);
    ok(UnloadUserProfile(hToken, profile.hProfile), "UnloadUserProfile failed %lu\n", GetLastError());
}

static BOOL GrantObject(HANDLE hObject, PSID pSid, PACL *ppOldDacl, PSECURITY_DESCRIPTOR *ppSD)
{
    EXPLICIT_ACCESSW ea = { 0 };
    PACL pNewDacl = NULL;
    DWORD error;

    error = GetSecurityInfo(hObject, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, ppOldDacl, NULL, ppSD);
    if (error)
        return FALSE;
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.ptstrName = (LPWSTR)pSid;
    error = SetEntriesInAclW(1, &ea, *ppOldDacl, &pNewDacl);
    if (!error)
        error = SetSecurityInfo(hObject, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDacl, NULL);
    LocalFree(pNewDacl);
    return !error;
}

static BOOL WriteData(LPCWSTR pszPath, const char *psz)
{
    HANDLE hFile = CreateFileW(pszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD cb;
    BOOL ret;

    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    ret = WriteFile(hFile, psz, (DWORD)strlen(psz), &cb, NULL);
    CloseHandle(hFile);
    return ret;
}

static void TestOtherLogonSession(HANDLE hTokenA, HANDLE hTokenB)
{
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH * 2], szIn[MAX_PATH], szOut[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HWINSTA hWinSta = GetProcessWindowStation();
    HDESK hDesk = GetThreadDesktop(GetCurrentThreadId());
    PSECURITY_DESCRIPTOR pWinStaSD = NULL, pDeskSD = NULL;
    PACL pWinStaDacl = NULL, pDeskDacl = NULL;
    HANDLE hOwn = NULL, hOther = NULL, hChildOwn = NULL, hChildOther = NULL, hFile;
    PSID pLogonSid;
    char sz[64] = "";
    DWORD cb = 0, dwExit = 0xdeadbeef;
    int own = -1, other = -1;

    GetDataPath(szIn, _countof(szIn), L"LogonSession.in");
    GetDataPath(szOut, _countof(szOut), L"LogonSession.out");
    DeleteFileW(szOut);
    pLogonSid = GetTokenLogonSid(hTokenA);
    if (!pLogonSid || !GrantObject(hWinSta, pLogonSid, &pWinStaDacl, &pWinStaSD) ||
        !GrantObject(hDesk, pLogonSid, &pDeskDacl, &pDeskSD))
    {
        ok(0, "cannot grant the window station and desktop to the test user\n");
        goto cleanup;
    }

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" LogonSession child", szExe);
    if (!CreateProcessWithTokenW(hTokenA, 0, NULL, szCmd, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcessWithTokenW failed %lu\n", GetLastError());
        goto cleanup;
    }
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL);
    DuplicateTokenEx(hTokenA, TOKEN_ALL_ACCESS, &sa, SecurityImpersonation, TokenImpersonation, &hOwn);
    DuplicateTokenEx(hTokenB, TOKEN_ALL_ACCESS, &sa, SecurityImpersonation, TokenImpersonation, &hOther);
    LocalFree(sa.lpSecurityDescriptor);
    DuplicateHandle(GetCurrentProcess(), hOwn, pi.hProcess, &hChildOwn, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DuplicateHandle(GetCurrentProcess(), hOther, pi.hProcess, &hChildOther, 0, FALSE, DUPLICATE_SAME_ACCESS);
    StringCchPrintfA(sz, _countof(sz), "%p %p", hChildOwn, hChildOther);
    ok(WriteData(szIn, sz), "cannot write %ls\n", szIn);
    ResumeThread(pi.hThread);
    ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "the child did not exit\n");
    GetExitCodeProcess(pi.hProcess, &dwExit);
    ok_long(dwExit, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    hFile = CreateFileW(szOut, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    sz[0] = 0;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ReadFile(hFile, sz, sizeof(sz) - 1, &cb, NULL);
        CloseHandle(hFile);
        sz[cb] = 0;
    }
    ok(sscanf(sz, "%d %d", &own, &other) == 2, "no result from the child: \"%s\"\n", sz);
    ok(own == SecurityImpersonation, "own logon session: level %d\n", own);
    ok(other == SecurityImpersonation, "other logon session of the same user: level %d\n", other);

cleanup:
    if (pWinStaSD)
    {
        SetSecurityInfo(hWinSta, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pWinStaDacl, NULL);
        LocalFree(pWinStaSD);
    }
    if (pDeskSD)
    {
        SetSecurityInfo(hDesk, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pDeskDacl, NULL);
        LocalFree(pDeskSD);
    }
    if (hOwn)
        CloseHandle(hOwn);
    if (hOther)
        CloseHandle(hOther);
    DeleteFileW(szIn);
    DeleteFileW(szOut);
    HeapFree(GetProcessHeap(), 0, pLogonSid);
}

START_TEST(LogonSession)
{
    USER_INFO_1 ui = { 0 };
    HANDLE hTokenA = NULL, hTokenB = NULL;
    TOKEN_STATISTICS statA, statB;
    NET_API_STATUS status;
    DWORD cb;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        RunChild();
        return;
    }

    NetUserDel(NULL, TEST_USER);
    ui.usri1_name = TEST_USER;
    ui.usri1_password = TEST_PASSWORD;
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    status = NetUserAdd(NULL, 1, (LPBYTE)&ui, NULL);
    if (status == ERROR_ACCESS_DENIED)
    {
        skip("Creating a user needs administrator rights\n");
        return;
    }
    ok(status == NERR_Success, "NetUserAdd failed %lu\n", status);
    if (status != NERR_Success)
        return;

    ok(LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hTokenA),
       "LogonUserW failed %lu\n", GetLastError());
    ok(LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hTokenB),
       "LogonUserW failed %lu\n", GetLastError());
    if (hTokenA && hTokenB)
    {
        GetTokenInformation(hTokenA, TokenStatistics, &statA, sizeof(statA), &cb);
        GetTokenInformation(hTokenB, TokenStatistics, &statB, sizeof(statB), &cb);
        ok(memcmp(&statA.AuthenticationId, &statB.AuthenticationId, sizeof(LUID)) != 0,
           "the two logons share a logon session\n");
        TestLogonDefaultDacl(hTokenA);
        TestProfile(hTokenA);
        TestOtherLogonSession(hTokenA, hTokenB);
    }

    if (hTokenB)
        CloseHandle(hTokenB);
    if (hTokenA)
    {
        TOKEN_USER *pUser;
        LPWSTR pszSid = NULL;
        cb = 0;
        GetTokenInformation(hTokenA, TokenUser, NULL, 0, &cb);
        pUser = HeapAlloc(GetProcessHeap(), 0, cb);
        if (pUser && GetTokenInformation(hTokenA, TokenUser, pUser, cb, &cb) &&
            ConvertSidToStringSidW(pUser->User.Sid, &pszSid))
        {
            DeleteProfileW(pszSid, NULL, NULL);
            LocalFree(pszSid);
        }
        HeapFree(GetProcessHeap(), 0, pUser);
        CloseHandle(hTokenA);
    }
    NetUserDel(NULL, TEST_USER);
}
